#pragma once

#include "audio.h"
#include <string>
#include <memory>
#include <functional>
#include <fstream>
#include <vector>

class DS5 {
public:
    // 状态变化回调
    using StopCallback = std::function<void()>;

    /**
     * @brief 构造函数
     * @param deviceId 设备标识符，用于创建 Audio 对象
     */
    explicit DS5(const std::string& deviceId);
    
    ~DS5();

    /**
     * @brief 初始化 DS5 设备（创建 Audio 对象）
     * @return 成功返回 true，失败返回 false
     */
    bool initialize();

    /**
     * @brief 启动音频服务
     * @return 成功返回 true，失败返回 false
     */
    bool start();

    /**
     * @brief 停止音频服务
     */
    void stop();

    /**
     * @brief 获取设备标识符
     * @return 设备标识符字符串
     */
    const std::string& getDeviceId() const;

    /**
     * @brief 设置状态变化回调
     * @param callback 当设备状态变化时调用
     */
    void setStopCallback(StopCallback callback);

    /**
     * @brief 测试方法：作为音频数据回调使用
     * @param data 音频数据指针
     * @param frames 帧数
     * @param channels 通道数
     */
    void test(const float* data, uint32_t frames, uint32_t channels);

private:
    std::string m_deviceId;
    std::unique_ptr<Audio> m_audio;  // Audio 对象
    StopCallback m_stopCallback;    // 状态回调
};
