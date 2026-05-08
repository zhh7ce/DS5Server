#include "ds5server.h"
#include <iostream>
#include <unistd.h>

DS5Server::DS5Server(const std::string& deviceId)
    : m_deviceId(deviceId)
{
    std::cout << "[DS5Server] Created with device ID: " << m_deviceId << std::endl;
}

DS5Server::~DS5Server()
{
    stop();
    std::cout << "[DS5Server] Destroyed" << std::endl;
}

bool DS5Server::initialize()
{
    // 创建 AudioSink 对象，传入相同的设备标识符
    m_audioSink = std::make_unique<AudioSink>(m_deviceId);
    
    if (!m_audioSink) {
        std::cerr << "[DS5Server] Failed to create AudioSink object" << std::endl;
        return false;
    }

    // 设置 AudioSink 的停止回调，转发到 DS5Server 的回调
    m_audioSink->setStopCallback([this]() {
        if (m_stopCallback) {
            m_stopCallback();
        }
    });

    // 设置 AudioSink 的音频数据回调为 test 方法
    m_audioSink->setAudioSinkDataCallback([this](const float* data, uint32_t frames, uint32_t channels) {
        this->test(data, frames, channels);
    });

    std::cout << "[DS5Server] Initialized successfully with AudioSink object" << std::endl;
    
    return true;
}

bool DS5Server::start()
{
    if (!m_audioSink) {
        std::cerr << "[DS5Server] AudioSink object is null" << std::endl;
        return false;
    }

    bool result = m_audioSink->start();
    if (result) {
        std::cout << "[DS5Server] AudioSink service started" << std::endl;
    } else {
        std::cerr << "[DS5Server] Failed to start audio service" << std::endl;
    }
    
    return result;
}

void DS5Server::stop()
{
    std::cout << "[DS5Server::stop] Starting stop process..." << std::endl;
    
    if (m_audioSink) {
        std::cout << "[DS5Server::stop] Calling AudioSink::stop()..." << std::endl;
        m_audioSink->stop();
        std::cout << "[DS5Server::stop] AudioSink service stopped" << std::endl;
    } else {
        std::cout << "[DS5Server::stop] AudioSink object is null, skipping" << std::endl;
    }
    
    std::cout << "[DS5Server::stop] Stop process completed" << std::endl;
}

void DS5Server::setStopCallback(StopCallback callback)
{
    m_stopCallback = std::move(callback);
}

void DS5Server::test(const float* data, uint32_t frames, uint32_t channels)
{    
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

const std::string& DS5Server::getDeviceId() const
{
    return m_deviceId;
}
