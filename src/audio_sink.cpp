#include "audio_sink.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <iostream>
#include <cstring>
#include <cmath>

AudioSink::AudioSink(const std::string& deviceId)
{
    m_config.alsaComponents = deviceId;
    m_config.nodeName += deviceId;
    m_config.nodeNick += deviceId;
    m_config.nodeDescription += deviceId;
}

AudioSink::~AudioSink()
{
    stop();
}

bool AudioSink::start()
{
    // 初始化 PipeWire
    pw_init(nullptr, nullptr);

    // 创建主循环
    m_loop = pw_main_loop_new(nullptr);
    if (!m_loop) {
        std::cerr << "Failed to create main loop" << std::endl;
        return false;
    }

    // 获取上下文
    struct pw_context* context = pw_context_new(
        pw_main_loop_get_loop(m_loop),
        nullptr,
        0
    );
    if (!context) {
        std::cerr << "Failed to create context" << std::endl;
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
        return false;
    }

    // 创建核心对象
    m_core = pw_context_connect(context, nullptr, 0);
    if (!m_core) {
        std::cerr << "Failed to connect to PipeWire" << std::endl;
        pw_context_destroy(context);
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
        return false;
    }

    // 创建流属性
    struct pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_CLASS, m_config.mediaClass.c_str(),
        PW_KEY_NODE_NAME, m_config.nodeName.c_str(),
        PW_KEY_NODE_NICK, m_config.nodeNick.c_str(),
        PW_KEY_NODE_DESCRIPTION, m_config.nodeDescription.c_str(),
        PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_DEVICE_ICON_NAME, "audio-card",

        "audio.channels", "4",                  // 4 声道（环绕声）
        "audio.position", "[ FL, FR, RL, RR ]", // 声道布局
        
        "node.latency", "48/48000",             // 设置最小延迟：48帧 @ 48kHz = 1ms
        "clock.quantum", "48",                  // 最小量子大小
        "clock.min-quantum", "48",              // 最小量子
        "clock.max-quantum", "1024",            // 最大量子
        nullptr
    );

    if (!props) {
        std::cerr << "Failed to create properties" << std::endl;
        pw_core_disconnect(m_core);
        pw_context_destroy(context);
        pw_main_loop_destroy(m_loop);
        m_core = nullptr;
        m_loop = nullptr;
        return false;
    }

    // 创建音频流
    static const struct pw_stream_events streamEvents = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = onStreamStateChanged,
        .process = onStreamProcess,
    };

    m_stream = pw_stream_new_simple(
        pw_main_loop_get_loop(m_loop),
        m_config.nodeName.c_str(),
        props,
        &streamEvents,
        this
    );

    if (!m_stream) {
        std::cerr << "Failed to create stream" << std::endl;
        pw_properties_free(props);
        pw_core_disconnect(m_core);
        pw_context_destroy(context);
        pw_main_loop_destroy(m_loop);
        m_core = nullptr;
        m_loop = nullptr;
        return false;
    }

    // 构建音频格式参数
    uint8_t buffer[1024];
    struct spa_pod_builder podBuilder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    
    struct spa_audio_info_raw audioInfo = {};
    audioInfo.format = SPA_AUDIO_FORMAT_F32_LE;
    audioInfo.rate = m_config.sampleRate;
    audioInfo.channels = m_config.channels;
    
    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(
        &podBuilder,
        SPA_PARAM_EnumFormat,
        &audioInfo
    );

    // 连接流（Sink 使用 INPUT 方向，因为它是接收音频数据）
    // 添加低延迟标志：RT_PROCESS 启用实时处理
    int ret = pw_stream_connect(
        m_stream,
        PW_DIRECTION_INPUT,  // Sink 是输入方向（接收音频）
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | 
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS  // 启用实时处理
        ),
        params,
        1
    );

    if (ret < 0) {
        std::cerr << "Failed to connect stream: " << strerror(-ret) << std::endl;
        pw_stream_destroy(m_stream);
        pw_properties_free(props);
        pw_core_disconnect(m_core);
        pw_context_destroy(context);
        pw_main_loop_destroy(m_loop);
        m_stream = nullptr;
        m_core = nullptr;
        m_loop = nullptr;
        return false;
    }

    std::cout << "Virtual audio source started successfully" << std::endl;
    std::cout << "Node name: " << m_config.nodeName << std::endl;
    std::cout << "Sample rate: " << m_config.sampleRate << std::endl;
    std::cout << "Channels: " << m_config.channels << std::endl;

    // 启动后台线程运行 PipeWire 主循环
    m_stopRequested = false;  // 重置停止标志
    m_loopThread = std::thread([this]() {
        std::cout << "[AudioSink] Background loop started (using pw_main_loop_run)" << std::endl;
        // 阻塞运行，直到 pw_main_loop_quit() 被调用
        pw_main_loop_run(m_loop);
        std::cout << "[AudioSink] Background loop exited" << std::endl;
        
        // 只有在非主动停止时才触发回调（表示异常断开）
        if (!m_stopRequested && m_stopCallback) {
            std::cout << "[AudioSink] Triggering stop callback (unexpected disconnect)" << std::endl;
            m_stopCallback();
        }
    });

    return true;
}

void AudioSink::stop()
{
    std::cout << "[AudioSink::stop] Starting cleanup..." << std::endl;
    
    // 标记为主动请求停止
    m_stopRequested = true;
    
    // 停止后台线程
    if (m_loop) {
        std::cout << "[AudioSink::stop] Calling pw_main_loop_quit()..." << std::endl;
        pw_main_loop_quit(m_loop);  // 这会让 pw_main_loop_run() 返回
    }
    
    if (m_loopThread.joinable()) {
        std::cout << "[AudioSink::stop] Waiting for background thread to join..." << std::endl;
        m_loopThread.join();
        std::cout << "[AudioSink::stop] Background thread joined" << std::endl;
    } else {
        std::cout << "[AudioSink::stop] Background thread not running" << std::endl;
    }

    if (m_stream) {
        std::cout << "[AudioSink::stop] Destroying stream..." << std::endl;
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }

    if (m_core) {
        std::cout << "[AudioSink::stop] Disconnecting core..." << std::endl;
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }

    if (m_loop) {
        std::cout << "[AudioSink::stop] Destroying main loop..." << std::endl;
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
    }

    std::cout << "[AudioSink::stop] Deinitializing PipeWire..." << std::endl;
    pw_deinit();
    
    std::cout << "[AudioSink::stop] Cleanup completed" << std::endl;
}

void AudioSink::setStopCallback(StopCallback callback)
{
    m_stopCallback = std::move(callback);
}

void AudioSink::setAudioSinkDataCallback(AudioSinkDataCallback callback)
{
    m_audioSinkDataCallback = std::move(callback);
}

void AudioSink::onStreamProcess(void* userdata)
{
    AudioSink* self = static_cast<AudioSink*>(userdata);
    if (!self || !self->m_stream) {
        return;
    }

    // 获取输出缓冲区
    struct pw_buffer* buffer = pw_stream_dequeue_buffer(self->m_stream);
    if (!buffer) {
        return;
    }

    struct spa_buffer* spaBuffer = buffer->buffer;
    if (!spaBuffer || !spaBuffer->datas[0].data) {
        pw_stream_queue_buffer(self->m_stream, buffer);
        return;
    }

    // 获取缓冲区信息
    uint32_t nFrames = spaBuffer->datas[0].chunk->size / 
                      (sizeof(float) * self->m_config.channels);
    
    float* data = static_cast<float*>(spaBuffer->datas[0].data);

    // 通过回调传递音频数据
    if (self->m_audioSinkDataCallback) {
        self->m_audioSinkDataCallback(data, nFrames, self->m_config.channels);
    }
    
    pw_stream_queue_buffer(self->m_stream, buffer);
}

void AudioSink::onStreamParamChanged(void* userdata, uint32_t id, const struct spa_pod* param)
{
    // 参数变化处理（可选）
    (void)userdata;
    (void)id;
    (void)param;
}

void AudioSink::onStreamStateChanged(void* userdata, enum pw_stream_state old,
                                 enum pw_stream_state state, const char* error)
{
    AudioSink* self = static_cast<AudioSink*>(userdata);
    if (!self) {
        return;
    }

    std::cout << "[AudioSink] Stream state changed: ";
    
    switch (state) {
        case PW_STREAM_STATE_UNCONNECTED:
            std::cout << "UNCONNECTED";
            break;
        case PW_STREAM_STATE_CONNECTING:
            std::cout << "CONNECTING";
            break;
        case PW_STREAM_STATE_PAUSED:
            std::cout << "PAUSED";
            break;
        case PW_STREAM_STATE_STREAMING:
            std::cout << "STREAMING";
            break;
        default:
            std::cout << "UNKNOWN(" << state << ")";
            break;
    }
    std::cout << std::endl;

    // 如果状态变为未连接或错误，触发停止
    if (state == PW_STREAM_STATE_UNCONNECTED || 
        state == PW_STREAM_STATE_ERROR) {
        std::cerr << "[AudioSink] Stream disconnected or error: " 
                  << (error ? error : "unknown") << std::endl;
        
        if (self->m_stopCallback) {
            self->m_stopCallback();
        }
    }
}
