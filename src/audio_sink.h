#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>

// PipeWire 前置声明
#include <pipewire/pipewire.h>

class AudioSink {
public:
    // 停止回调
    using StopCallback = std::function<void()>;

    /**
     * @brief 构造函数
     * @param deviceId 设备标识符，用于替换 alsa.components 和追加到节点名称
     */
    explicit AudioSink(const std::string& deviceId);
    
    ~AudioSink();

    /**
     * @brief 初始化并启动虚拟音频输出设备（自动启动后台线程）
     * @return 成功返回 true，失败返回 false
     */
    bool start();

    /**
     * @brief 停止虚拟音频输出设备
     */
    void stop();

    /**
     * @brief 设置停止回调
     * @param callback 当需要停止时调用
     */
    void setStopCallback(StopCallback callback);

    /**
     * @brief 设置音频数据回调
     * @param callback 当接收到音频数据时调用
     */
    using AudioSinkDataCallback = std::function<void(const float* data, uint32_t frames, uint32_t channels)>;
    void setAudioSinkDataCallback(AudioSinkDataCallback callback);

private:
    // PipeWire 相关成员
    struct pw_main_loop* m_loop = nullptr;
    struct pw_core* m_core = nullptr;
    struct pw_stream* m_stream = nullptr;

    // 配置参数（基于 ds5.txt）
    struct Config {
        std::string alsaComponents = "USB054c:0ce6";
        std::string mediaClass = "Audio/Sink";  // 修改为 Sink（输出设备）
        std::string nodeDescription = "DualSense Wireless Controller (PS5) ";
        std::string nodeName = "alsa_output.Sony_Interactive_Entertainment_DualSense_Wireless_Controller_";
        std::string nodeNick = "DualSense Wireless Controller ";
        uint32_t sampleRate = 48000;
        uint32_t channels = 4;
    } m_config;

    // 后台线程相关
    std::thread m_loopThread;            // 主循环线程
    
    // 停止回调
    StopCallback m_stopCallback;         // 停止回调
    AudioSinkDataCallback m_audioSinkDataCallback; // 音频数据回调
    
    // 内部状态
    std::atomic<bool> m_stopRequested{false};  // 是否请求停止（用于区分主动/被动停止）

    // 内部方法
    static void onStreamStateChanged(void* userdata, enum pw_stream_state old, 
                                     enum pw_stream_state state, const char* error);
    static void onStreamProcess(void* userdata);
    static void onStreamParamChanged(void* userdata, uint32_t id, const struct spa_pod* param);
    
    // 禁止拷贝
    AudioSink(const AudioSink&) = delete;
    AudioSink& operator=(const AudioSink&) = delete;
};
