#include "audio_encoder.h"
#include <opus.h>
#include <samplerate.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

const size_t MAX_AUDIO_SIZE = 200;
const size_t MAX_HAPTIC_SIZE = 64;

const size_t MAX_INPUT_SIZE = 48 * 2 * 33; // 48 samples * 2 channels * 22 frames
const size_t MAX_OUTPUT_AUDIO_SIZE = MAX_AUDIO_SIZE * 3;
const size_t MAX_OUTPUT_HAPTIC_SIZE = MAX_HAPTIC_SIZE * 3;

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
        OPUS_APPLICATION_AUDIO,
        &error
    );

    if (error != OPUS_OK || !m_opusEncoder) {
        std::cerr << "[AudioEncoder] Failed to create Opus encoder: "
                  << opus_strerror(error) << std::endl;
        return false;
    }

    // 配置 Opus 编码器参数（仿照 sample/audio.cpp）
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_BITRATE(static_cast<opus_int32>(bitrate)));
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_VBR(false));
    opus_encoder_ctl(m_opusEncoder, OPUS_SET_COMPLEXITY(0));  // 最低复杂度

    // 初始化 libsamplerate（使用最快的线性插值）
    int srcError = 0;
    m_srcState = src_new(SRC_LINEAR, audioChannels, &srcError);
    if (srcError != 0) {
        std::cerr << "[AudioEncoder] Failed to create SRC state: "
                  << src_strerror(srcError) << std::endl;
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

    m_opusOutput.init("audio.opus", audioSampleRate, audioChannels);

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

std::vector<uint8_t> AudioEncoder::getEncodedAudioData() {
    std::lock_guard<std::mutex> lock(m_outputAudioMutex);
    if (m_outputAudioBuffer.size() < MAX_AUDIO_SIZE) {
        return {};
    }

    size_t outCount = m_outputAudioBuffer.size() / MAX_AUDIO_SIZE * MAX_AUDIO_SIZE;
    std::vector<uint8_t> result(m_outputAudioBuffer.begin(), m_outputAudioBuffer.begin() + outCount);
    m_outputAudioBuffer.erase_begin(outCount);

    return result;
}

std::vector<uint8_t> AudioEncoder::getEncodedHapticsData() {
    std::lock_guard<std::mutex> lock(m_outputHapticsMutex);
    if (m_outputHapticsBuffer.size() < MAX_HAPTIC_SIZE) {
        return {};
    }

    size_t outCount = m_outputHapticsBuffer.size() / MAX_HAPTIC_SIZE * MAX_HAPTIC_SIZE;
    std::vector<uint8_t> result(m_outputHapticsBuffer.begin(), m_outputHapticsBuffer.begin() + outCount);
    m_outputHapticsBuffer.erase_begin(outCount);

    return result;
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

    // 停止文件输出
    m_opusOutput.close();

    // 清理 Opus 编码器
    if (m_opusEncoder) {
        opus_encoder_destroy(m_opusEncoder);
        m_opusEncoder = nullptr;
    }

    // 清理 libsamplerate 资源
    if (m_srcState) {
        src_delete(static_cast<SRC_STATE*>(m_srcState));
        m_srcState = nullptr;
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
    std::vector<uint8_t> encodeAudioBuffer;
    std::vector<int8_t> encodeHapticsBuffer;

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
            
            audioBuffer.resize(audioBuffer.size() + frameCount * 2);
            hapticsBuffer.resize(hapticsBuffer.size() + frameCount * 2);
            for (uint32_t i = 0; i < frameCount; i++) {
                audioBuffer.push_back(m_inputBuffer[i * m_config.totalChannels]);         // Left (ch0)
                audioBuffer.push_back(m_inputBuffer[i * m_config.totalChannels + 1]);     // Right (ch1)
                hapticsBuffer.push_back(m_inputBuffer[i * m_config.totalChannels + 2]);     // Haptics Left (ch2)
                hapticsBuffer.push_back(m_inputBuffer[i * m_config.totalChannels + 3]);     // Haptics Right (ch3)
            }

            m_inputBuffer.erase_begin(frameCount * m_config.totalChannels);
        }

        // Step 3: Opus 编码 每32/3s处理一次        
        while (audioBuffer.size() >= 512 * 2) {

            //将音频数据重采样为480 samples
            resample(audioBuffer, sampledAudioBuffer, AUDIO_RESAMPLE_RATIO);
            
            // 从累积缓冲区取出足够的数据进行编码
            int encodedBytes = opus_encode_float(
                m_opusEncoder,
                sampledAudioBuffer.data(),
                480,
                encodeAudioBuffer.data(),
                MAX_OUTPUT_AUDIO_SIZE
            );

            if (encodedBytes > 0) {
                // 将编码后的数据放入队列
                {
                    std::lock_guard<std::mutex> lock(m_outputAudioMutex);
                    m_outputAudioBuffer.insert(m_outputAudioBuffer.end(), encodeAudioBuffer.begin(), encodeAudioBuffer.end());
                }

                // 如果启用了文件输出，使用 OpusOutput 写入数据包
                if (m_opusOutput.isOpen()) {
                    m_opusOutput.writePacket(encodeAudioBuffer.data(), encodedBytes, m_config.frameSize);
                }

                encodeAudioBuffer.clear();

                // 触发回调
                if (m_encodeCallback) {
                    m_encodeCallback();
                }
            } else {
                std::cerr << "[AudioEncoder] Opus encode failed: "
                          << opus_strerror(encodedBytes) << std::endl;
            }
        }
        
        // 重采样触觉数据（只有当缓冲区有足够数据时）
        // 48 samples @ 48kHz = 1ms，重采样到 3kHz 后约为 3 samples
        if (hapticsBuffer.size() >= 48 * 2) {
            resample(hapticsBuffer, sampledHapticsBuffer, HAPTIC_RESAMPLE_RATIO);
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

void AudioEncoder::resample(std::vector<float>& input, std::vector<float>& output, float ratio) {
    if (input.empty() || ratio == 0) {
        return;
    }

    const size_t channels = m_config.audioChannels;
    const size_t inputFrames = input.size() / channels;
    const size_t outputFrames = static_cast<size_t>(std::ceil(inputFrames * ratio));
    output.resize(outputFrames * channels);

    // 设置重采样参数
    SRC_DATA srcData;
    srcData.data_in = input.data();
    srcData.data_out = output.data();
    srcData.input_frames = inputFrames;
    srcData.output_frames = outputFrames;
    srcData.src_ratio = ratio;
    srcData.end_of_input = 0;  // 还有更多数据

    // 执行重采样
    int error = src_process(static_cast<SRC_STATE*>(m_srcState), &srcData);
    if (error != 0) {
        std::cerr << "[AudioEncoder] SRC process failed: "
                  << src_strerror(error) << std::endl;
    }

    input.erase(input.begin(), input.begin() + inputFrames * channels);
}

void AudioEncoder::convertToInt8(std::vector<float>& input, std::vector<int8_t>& output) {
    for (size_t i = 0; i < input.size(); i++) {
        // 应用增益并限制范围 [-128, 127]
        float val = input[i] * 127.0f;
        val = std::clamp(val, -128.0f, 127.0f);
        output[i] = static_cast<int8_t>(val);
    }

    input.clear();
}
