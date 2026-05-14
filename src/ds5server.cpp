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
    try {
        // 创建 AudioSink 对象，传入相同的设备标识符
        m_audioSink = std::make_unique<AudioSink>(m_deviceId);
        
        if (!m_audioSink) {
            std::cerr << "[DS5Server] Failed to create AudioSink object" << std::endl;
            return false;
        }

        // 创建 AudioEncoder 对象
        m_audioEncoder = std::make_unique<AudioEncoder>();
        
        if (!m_audioEncoder) {
            std::cerr << "[DS5Server] Failed to create AudioEncoder object" << std::endl;
            return false;
        }

        // 初始化 AudioEncoder（48kHz, 2通道, 3kHz触觉, 160kbps码率）
        if (!m_audioEncoder->init()) {
            std::cerr << "[DS5Server] Failed to initialize AudioEncoder" << std::endl;
            // 清理已分配的资源
            m_audioEncoder.reset();
            m_audioSink.reset();
            return false;
        }

        // 设置 AudioSink 的停止回调，转发到 DS5Server 的回调
        m_audioSink->setStopCallback([this]() {
            if (m_stopCallback) {
                m_stopCallback();
            }
        });

        // 设置 AudioSink 的音频数据回调
        m_audioSink->setAudioSinkDataCallback([this](const float* data, uint32_t frames, uint32_t channels) {
            this->onAudioSinkData(data, frames, channels);
        });

        std::cout << "[DS5Server] Initialized successfully with AudioSink and AudioEncoder" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DS5Server] Exception during initialization: " << e.what() << std::endl;
        // 确保资源被清理
        m_audioEncoder.reset();
        m_audioSink.reset();
        return false;
    }
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
    
    // 停止 AudioEncoder
    if (m_audioEncoder) {
        std::cout << "[DS5Server::stop] Stopping AudioEncoder..." << std::endl;
        m_audioEncoder->stop();
        std::cout << "[DS5Server::stop] AudioEncoder stopped" << std::endl;
    }
    
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

void DS5Server::onAudioSinkData(const float* data, uint32_t frames, uint32_t channels)
{
    if (!m_audioEncoder || !data || frames == 0) {
        return;
    }

    // 传递4通道数据给编码器，内部会自动分离处理
    m_audioEncoder->processFrame(data, frames);
}

const std::string& DS5Server::getDeviceId() const
{
    return m_deviceId;
}
