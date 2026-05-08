#include "ds5.h"
#include <iostream>
#include <unistd.h>

DS5::DS5(const std::string& deviceId)
    : m_deviceId(deviceId)
{
    std::cout << "[DS5] Created with device ID: " << m_deviceId << std::endl;
}

DS5::~DS5()
{
    stop();
    std::cout << "[DS5] Destroyed" << std::endl;
}

bool DS5::initialize()
{
    // 创建 Audio 对象，传入相同的设备标识符
    m_audio = std::make_unique<Audio>(m_deviceId);
    
    if (!m_audio) {
        std::cerr << "[DS5] Failed to create Audio object" << std::endl;
        return false;
    }

    // 设置 Audio 的停止回调，转发到 DS5 的回调
    m_audio->setStopCallback([this]() {
        if (m_stopCallback) {
            m_stopCallback();
        }
    });

    // 设置 Audio 的音频数据回调为 test 方法
    m_audio->setAudioDataCallback([this](const float* data, uint32_t frames, uint32_t channels) {
        this->test(data, frames, channels);
    });

    std::cout << "[DS5] Initialized successfully with Audio object" << std::endl;
    
    return true;
}

bool DS5::start()
{
    if (!m_audio) {
        std::cerr << "[DS5] Audio object is null" << std::endl;
        return false;
    }

    bool result = m_audio->start();
    if (result) {
        std::cout << "[DS5] Audio service started" << std::endl;
    } else {
        std::cerr << "[DS5] Failed to start audio service" << std::endl;
    }
    
    return result;
}

void DS5::stop()
{
    std::cout << "[DS5::stop] Starting stop process..." << std::endl;
    
    if (m_audio) {
        std::cout << "[DS5::stop] Calling Audio::stop()..." << std::endl;
        m_audio->stop();
        std::cout << "[DS5::stop] Audio service stopped" << std::endl;
    } else {
        std::cout << "[DS5::stop] Audio object is null, skipping" << std::endl;
    }
    
    std::cout << "[DS5::stop] Stop process completed" << std::endl;
}

void DS5::setStopCallback(StopCallback callback)
{
    m_stopCallback = std::move(callback);
}

void DS5::test(const float* data, uint32_t frames, uint32_t channels)
{
    // 测试方法：处理接收到的音频数据
    // std::cout << "[DS5::test] Received audio data: " 
    //           << frames << " frames, " 
    //           << channels << " channels" << std::endl;
    
    // // 打印前几个样本值作为示例
    // if (frames > 0 && channels > 0) {
    //     std::cout << "  First sample: " << data[0] << std::endl;
    //     if (channels > 1) {
    //         std::cout << "  Second sample: " << data[1] << std::endl;
    //     }
    // }
    
    // 直接输出前两个声道的原始PCM数据到stdout
    // 只写入前两个声道（左声道和右声道）
    for (uint32_t i = 0; i < frames; i++) {
        // 写入左声道 (channel 0)
        std::cout.write(reinterpret_cast<const char*>(&data[i * channels + 0]), sizeof(float));
        // 写入右声道 (channel 1)
        std::cout.write(reinterpret_cast<const char*>(&data[i * channels + 1]), sizeof(float));
    }
    
    // 确保数据立即输出
    std::cout.flush(); 
}

const std::string& DS5::getDeviceId() const
{
    return m_deviceId;
}
