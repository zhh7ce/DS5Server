#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <functional>

// libusb 前置声明
struct libusb_context;
struct libusb_device_handle;

class USBSink {
public:
    // 停止回调
    using StopCallback = std::function<void()>;

    /**
     * @brief 构造函数
     */
    USBSink();
    
    ~USBSink();

    /**
     * @brief 初始化并连接 USB 设备
     * @param vendorId 厂商 ID (默认 0x054C - Sony)
     * @param productId 产品 ID (默认 0x0CE6 - DualSense)
     * @param interfaceNum 接口号 (默认 4)
     * @return 成功返回 true，失败返回 false
     */
    bool init(uint16_t vendorId = 0x054C, 
              uint16_t productId = 0x0CE6, 
              uint8_t interfaceNum = 1);

    /**
     * @brief 停止并断开 USB 设备
     */
    void stop();

    /**
     * @brief 设置停止回调
     * @param callback 当需要停止时调用
     */
    void setStopCallback(StopCallback callback);

    /**
     * @brief 写入数据到 USB 设备
     * @param data 数据指针
     * @param size 数据大小
     * @return 成功返回 true，失败返回 false
     */
    bool write(const uint8_t* data, uint32_t size);

private:
    // libusb 相关成员
    libusb_context* m_ctx = nullptr;
    libusb_device_handle* m_handle = nullptr;
    
    // 配置参数
    struct Config {
        uint16_t vendorId = 0x054C;
        uint16_t productId = 0x0CE6;
        uint8_t interfaceNum = 4;
        uint8_t outEndpointAddr = 0;  // OUT 端点地址
    } m_config;

    // 内部状态
    bool m_initialized = false;

    // 停止回调
    StopCallback m_stopCallback;

    // 内部方法
    bool findOutEndpoint();
    
    // 禁止拷贝
    USBSink(const USBSink&) = delete;
    USBSink& operator=(const USBSink&) = delete;
};
