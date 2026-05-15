#include "audio_encoder.h"
#include <opus.h>
#include <samplerate.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

const size_t MAX_AUDIO_SIZE = 200;
const size_t MAX_HAPTIC_SIZE = 64;

const size_t MAX_INPUT_SIZE = 48 * 2 * 3300; // 48 samples * 2 channels * 22 frames
const size_t MAX_OUTPUT_AUDIO_SIZE = MAX_AUDIO_SIZE * 300;
const size_t MAX_OUTPUT_HAPTIC_SIZE = MAX_HAPTIC_SIZE * 300;

const float AUDIO_RESAMPLE_RATIO = 480.0f / 512.0f;
const float HAPTIC_RESAMPLE_RATIO = 3.0f / 48.0f;

AudioEncoder::AudioEncoder() = default;

AudioEncoder::~AudioEncoder() {
    stop();
}

bool AudioEncoder::init(uint32_t sampleRate,
                        uint32_t totalChannels,
                        uint32_t audioChannels,
                        uint32_t audioSampleRate,
                        uint32_t bitrate,
                        uint32_t hapticsSampleRate,
                        uint32_t frameSize) {
    // 保存配置
    m_config.sampleRate = sampleRate;
    m_config.totalChannels = totalChannels;
    m_config.audioChannels = audioChannels;
    m_config.audioSampleRate = audioSampleRate;
    m_config.bitrate = bitrate;
    m_config.hapticsSampleRate = hapticsSampleRate;
    m_config.frameSize = frameSize;

    // 初始化缓存控件
    m_inputBuffer.set_capacity(MAX_INPUT_SIZE);
    m_outputAudioBuffer.set_capacity(MAX_OUTPUT_AUDIO_SIZE);
    m_outputHapticsBuffer.set_capacity(MAX_OUTPUT_HAPTIC_SIZE);

    // 初始化 Opus 编码器
    int error = 0;
    m_opusEncoder = opus_encoder_create(
        audioSampleRate,
        audioChannels,
        OPUS_APPLICATION_RESTRICTED_LOWDELAY,
        &error
    );

    if (error != OPUS_OK || !m_opusEncoder) {
        std::cerr << "[AudioEncoder] Failed to create Opus encoder: "
                  << opus_strerror(error) << std::endl;
        return false;
    }

    // 配置 Opus 编码器参数（仿照 sample/audio.cpp）
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_VBR(false));
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_COMPLEXITY(0));  // 最低复杂度

    // 初始化 libsamplerate - 音频重采样（512帧 -> 480帧）
    int audioSrcError = 0;
    m_audioSrcState = src_new(SRC_LINEAR, audioChannels, &audioSrcError);
    if (audioSrcError != 0) {
        std::cerr << "[AudioEncoder] Failed to create audio SRC state: "
                  << src_strerror(audioSrcError) << std::endl;
        return false;
    }

    // 初始化 libsamplerate - 触觉重采样（48帧 @48kHz -> 3帧 @3kHz）
    int hapticsSrcError = 0;
    m_hapticsSrcState = src_new(SRC_LINEAR, audioChannels, &hapticsSrcError);
    if (hapticsSrcError != 0) {
        std::cerr << "[AudioEncoder] Failed to create haptics SRC state: "
                  << src_strerror(hapticsSrcError) << std::endl;
        return false;
    }

    // 启动编码线程
    m_running = true;
    m_encodeThread = std::thread(&AudioEncoder::encodeLoop, this);

    std::cout << "[AudioEncoder] Initialized successfully" << std::endl;
    std::cout << "  Audio sample rate: " << audioSampleRate << " Hz" << std::endl;
    std::cout << "  Audio channels: " << audioChannels << std::endl;
    std::cout << "  Haptics sample rate: " << hapticsSampleRate << " Hz" << std::endl;
    std::cout << "  Bitrate: " << bitrate << " bps" << std::endl;

    return true;
}

void AudioEncoder::processFrame(const float* input, uint32_t frames) {
    if (!m_running || !input || frames == 0) {
        std::cerr << "[AudioEncoder] processFrame: invalid input" << std::endl;
        return;
    }

    // 将输入数据放入队列
    {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_inputBuffer.insert(m_inputBuffer.end(), input, input + frames * m_config.totalChannels);
    }

    // 通知编码线程
    m_queueCV.notify_one();
}

void AudioEncoder::setEncodeCallback(EncodeCallback callback) {
    m_encodeCallback = std::move(callback);
}

bool AudioEncoder::getEncodedAudioData(std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_outputAudioMutex);
    if (m_outputAudioBuffer.size() < MAX_AUDIO_SIZE) {
        return false;
    }

    if (m_outputHapticsBuffer.size() < MAX_HAPTIC_SIZE) {
        static uint8_t zeroData[MAX_HAPTIC_SIZE];
        data.insert(data.end(), zeroData, zeroData + MAX_HAPTIC_SIZE);
    } else {
        data.insert(data.end(), m_outputHapticsBuffer.begin(), m_outputHapticsBuffer.begin() + MAX_HAPTIC_SIZE);
        m_outputHapticsBuffer.erase_begin(MAX_HAPTIC_SIZE);
    }

    data.insert(data.end(), m_outputAudioBuffer.begin(), m_outputAudioBuffer.begin() + MAX_AUDIO_SIZE);
    m_outputAudioBuffer.erase_begin(MAX_AUDIO_SIZE);

    return true;
}

bool AudioEncoder::getEncodedHapticsData(std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_outputHapticsMutex);
    if (m_outputHapticsBuffer.size() < MAX_HAPTIC_SIZE) {
        return false;
    }

    size_t outCount = m_outputHapticsBuffer.size() / MAX_HAPTIC_SIZE * MAX_HAPTIC_SIZE;
    data.insert(data.end(), m_outputHapticsBuffer.begin(), m_outputHapticsBuffer.begin() + outCount);
    m_outputHapticsBuffer.erase_begin(outCount);

    return true;
}

void AudioEncoder::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    m_queueCV.notify_all();

    if (m_encodeThread.joinable()) {
        m_encodeThread.join();
    }

    // 清理 Opus 编码器
    if (m_opusEncoder) {
        opus_encoder_destroy(m_opusEncoder);
        m_opusEncoder = nullptr;
    }

    // 清理音频重采样 state
    if (m_audioSrcState) {
        src_delete(static_cast<SRC_STATE*>(m_audioSrcState));
        m_audioSrcState = nullptr;
    }

    // 清理触觉重采样 state
    if (m_hapticsSrcState) {
        src_delete(static_cast<SRC_STATE*>(m_hapticsSrcState));
        m_hapticsSrcState = nullptr;
    }

    std::cout << "[AudioEncoder] Stopped" << std::endl;
}

void AudioEncoder::encodeLoop() {
    std::cout << "[AudioEncoder] Encode thread started" << std::endl;

    //原始数据
    std::vector<float> audioBuffer;
    std::vector<float> hapticsBuffer;

    //采样数据512->480 48->3
    std::vector<float> sampledAudioBuffer;
    std::vector<float> sampledHapticsBuffer;

    //编码数据
    std::vector<uint8_t> encodeAudioBuffer(MAX_AUDIO_SIZE);
    std::vector<int8_t> encodeHapticsBuffer(MAX_HAPTIC_SIZE);

    while (m_running) {
        // 从队列获取数据
        {
            std::unique_lock<std::mutex> lock(m_inputMutex);
            m_queueCV.wait(lock, [this] {
                return !m_inputBuffer.empty() || !m_running;
            });

            if (!m_running && m_inputBuffer.empty()) {
                break;
            }

            size_t frameCount = m_inputBuffer.size() / m_config.totalChannels;
            
            for (uint32_t i = 0; i < frameCount; i++) {
                audioBuffer.push_back(m_inputBuffer[i * m_config.totalChannels]);         // Left (ch0)
                audioBuffer.push_back(m_inputBuffer[i * m_config.totalChannels + 1]);     // Right (ch1)
                hapticsBuffer.push_back(m_inputBuffer[i * m_config.totalChannels + 2]);     // Haptics Left (ch2)
                hapticsBuffer.push_back(m_inputBuffer[i * m_config.totalChannels + 3]);     // Haptics Right (ch3)
            }

            m_inputBuffer.erase_begin(frameCount * m_config.totalChannels);
        }

        // Step 3: Opus 编码
        while (audioBuffer.size() >= 512 * 2) {
            // 重采样音频数据：512帧 -> 480帧
            size_t resampledBatch = resampleAudio(audioBuffer, sampledAudioBuffer);
            
            for (size_t i = 0; i < resampledBatch; i++) {
                // 从累积缓冲区取出足够的数据进行编码
                int encodedBytes = opus_encode_float(
                    m_opusEncoder,
                    sampledAudioBuffer.data() + i * 480 * 2,
                    480,
                    encodeAudioBuffer.data(),
                    encodeAudioBuffer.size()
                );

                if (encodedBytes > 0) {
                    // 将编码后的数据放入队列
                    {
                        std::lock_guard<std::mutex> lock(m_outputAudioMutex);
                        m_outputAudioBuffer.insert(m_outputAudioBuffer.end(), encodeAudioBuffer.begin(), encodeAudioBuffer.end());
                    }

                    // 触发回调
                    // if (m_encodeCallback) {
                    //     m_encodeCallback();
                    // }
                } else {
                    std::cerr << "[AudioEncoder] Opus encode failed: "
                            << opus_strerror(encodedBytes) << std::endl;
                }
            }
            sampledAudioBuffer.clear();
        }
        
        // 重采样触觉数据（只有当缓冲区有足够数据时）
        // 48 samples @ 48kHz = 1ms，重采样到 3kHz 后约为 3 samples
        if (hapticsBuffer.size() >= 48 * 2) {
            resampleHaptics(hapticsBuffer, sampledHapticsBuffer);
            convertToInt8(sampledHapticsBuffer, encodeHapticsBuffer);
            
            if (!encodeHapticsBuffer.empty()) {
                // 将触觉数据放入队列
                {
                    std::lock_guard<std::mutex> lock(m_outputHapticsMutex);
                    m_outputHapticsBuffer.insert(m_outputHapticsBuffer.end(), encodeHapticsBuffer.begin(), encodeHapticsBuffer.end());
                }
                encodeHapticsBuffer.clear();

                // 触发回调（直接传递刚生成的数据，而不是从队列读取）
                if (m_encodeCallback) {
                    m_encodeCallback();
                }
            }
        }
    }

    std::cout << "[AudioEncoder] Encode thread exited" << std::endl;
}

size_t AudioEncoder::resampleAudio(std::vector<float>& input, std::vector<float>& output) {
    const size_t channels = m_config.audioChannels;
    const size_t inputBatchNum = 512;
    const size_t outputBatchNum = 480;
    const float ratio = AUDIO_RESAMPLE_RATIO;  // 480/512
    
    size_t batch = input.size() / channels / inputBatchNum;

    if (input.empty() || batch == 0 || ratio == 0) {
        return 0;
    }

    const size_t inputFrames = batch * inputBatchNum;
    const size_t outputFrames = batch * outputBatchNum;
    const size_t outputOffset = output.size();
    output.resize(outputOffset + outputFrames * channels);

    // 设置重采样参数
    SRC_DATA srcData;
    srcData.data_in = input.data();
    srcData.data_out = output.data() + outputOffset;
    srcData.input_frames = inputFrames;
    srcData.output_frames = outputFrames;
    srcData.src_ratio = ratio;
    srcData.end_of_input = 0;  // 还有更多数据

    // 执行重采样（使用音频专用的 state）
    int error = src_process(static_cast<SRC_STATE*>(m_audioSrcState), &srcData);
    if (error != 0) {
        std::cerr << "[AudioEncoder] Audio SRC process failed: "
                  << src_strerror(error) << std::endl;
    }
    
    // 根据实际使用的输入帧数来erase
    size_t actualInputFramesUsed = srcData.input_frames_used;
    if (actualInputFramesUsed > 0 && actualInputFramesUsed <= inputFrames) {
        input.erase(input.begin(), input.begin() + actualInputFramesUsed * channels);
    } else {
        input.erase(input.begin(), input.begin() + inputFrames * channels);
    }
    
    return batch;
}

size_t AudioEncoder::resampleHaptics(std::vector<float>& input, std::vector<float>& output) {
    const size_t channels = m_config.audioChannels;
    const size_t inputBatchNum = 48;
    const size_t outputBatchNum = 3;
    const float ratio = HAPTIC_RESAMPLE_RATIO;  // 3/48
    
    size_t batch = input.size() / channels / inputBatchNum;

    if (input.empty() || batch == 0 || ratio == 0) {
        return 0;
    }

    const size_t inputFrames = batch * inputBatchNum;
    const size_t outputFrames = batch * outputBatchNum;
    const size_t outputOffset = output.size();
    output.resize(outputOffset + outputFrames * channels);

    // 设置重采样参数
    SRC_DATA srcData;
    srcData.data_in = input.data();
    srcData.data_out = output.data() + outputOffset;
    srcData.input_frames = inputFrames;
    srcData.output_frames = outputFrames;
    srcData.src_ratio = ratio;
    srcData.end_of_input = 0;  // 还有更多数据

    // 执行重采样（使用触觉专用的 state）
    int error = src_process(static_cast<SRC_STATE*>(m_hapticsSrcState), &srcData);
    if (error != 0) {
        std::cerr << "[AudioEncoder] Haptics SRC process failed: "
                  << src_strerror(error) << std::endl;
    }
    
    // 根据实际使用的输入帧数来erase
    size_t actualInputFramesUsed = srcData.input_frames_used;
    if (actualInputFramesUsed > 0 && actualInputFramesUsed <= inputFrames) {
        input.erase(input.begin(), input.begin() + actualInputFramesUsed * channels);
    } else {
        input.erase(input.begin(), input.begin() + inputFrames * channels);
    }
    
    return batch;
}

void AudioEncoder::convertToInt8(std::vector<float>& input, std::vector<int8_t>& output) {
    output.resize(output.size() + input.size());
    for (size_t i = 0; i < input.size(); i++) {
        // 应用增益并限制范围 [-128, 127]
        float val = input[i] * 127.0f;
        output[i] = std::clamp(val, -128.0f, 127.0f);
    }

    input.clear();
}
