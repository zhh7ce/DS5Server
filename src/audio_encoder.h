#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <string>

#include <boost/circular_buffer.hpp>

#include "opus_output.h"

// Opus 前置声明
struct OpusEncoder;
struct OpusDecoder;

class AudioEncoder {
public:
    // 回调类型定义
    using EncodeCallback = std::function<void()>;

    AudioEncoder();
    ~AudioEncoder();

    /**
     * @brief 初始化编码器
     * @param audioSampleRate 音频采样率（默认 48000）
     * @param audioChannels 音频通道数（默认 2）
     * @param hapticsSampleRate 触觉数据采样率（默认 3000）
     * @param bitrate 编码码率（bps，默认 16000）
     * @return 成功返回 true，失败返回 false
     */
    bool init(uint32_t sampleRate = 48000,
              uint32_t totalChannels = 4,
              uint32_t audioChannels = 2,
              uint32_t audioSampleRate = 48000,
              uint32_t bitrate = 16000,
              uint32_t hapticsSampleRate = 3000,
              uint32_t frameSize = 480);

    /**
     * @brief 处理一帧4通道音频数据
     * @param input 输入音频数据（float32，interleaved，4通道）
     *               - ch0, ch1: 用于 Opus 编码输出到 audio.opus
     *               - ch2, ch3: 用于触觉数据生成（重采样到 3kHz）
     * @param frames 帧数
     */
    void processFrame(const float* input, uint32_t frames);

    /**
     * @brief 设置编码完成回调
     * @param callback 当音频数据编码完成后触发
     */
    void setEncodeCallback(EncodeCallback callback);

    /**
     * @brief 获取已编码的音频数据包
     * @return 编码后的音频数据队列
     */
    std::vector<uint8_t> getEncodedAudioData();

    /**
     * @brief 获取已编码的触觉数据包
     * @return 编码后的触觉数据队列
     */
    std::vector<uint8_t> getEncodedHapticsData();

    /**
     * @brief 停止编码器并清理资源
     */
    void stop();

private:
    // 编码器配置
    struct Config {
        uint32_t sampleRate = 48000;
        uint32_t totalChannels = 4;
        uint32_t audioChannels = 2;
        uint32_t audioSampleRate = 48000;
        uint32_t bitrate = 16000;
        uint32_t hapticsSampleRate = 3000;
        uint32_t frameSize = 48;
    } m_config;

    // Opus 编码器
    OpusEncoder* m_opusEncoder = nullptr;

    // libsamplerate 状态（用于触觉数据重采样）
    void* m_srcState = nullptr;  // SRC_STATE*

    // 线程和队列
    std::thread m_encodeThread;
    boost::circular_buffer<float> m_inputBuffer;
    boost::circular_buffer<uint8_t> m_outputAudioBuffer;
    boost::circular_buffer<uint8_t> m_outputHapticsBuffer;

    // 同步原语 
    std::mutex m_inputMutex;
    std::mutex m_outputAudioMutex;
    std::mutex m_outputHapticsMutex;
    std::condition_variable m_queueCV;
    std::atomic<bool> m_running{false};

    // 回调
    EncodeCallback m_encodeCallback;

    // Ogg Opus 文件输出
    OpusOutput m_opusOutput;

    // 内部方法
    void encodeLoop();
    void resample(std::vector<float>& input, std::vector<float>& output, float ratio);
    void convertToInt8(std::vector<float>& input, std::vector<int8_t>& output);
};
