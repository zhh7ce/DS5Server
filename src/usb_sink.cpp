#include "usb_sink.h"
#include <libusb-1.0/libusb.h>
#include <iostream>
#include <cstring>

USBSink::USBSink()
{
}

USBSink::~USBSink()
{
    stop();
}

bool USBSink::init(uint16_t vendorId, uint16_t productId, uint8_t interfaceNum)
{
    // 保存配置
    m_config.vendorId = vendorId;
    m_config.productId = productId;
    m_config.interfaceNum = interfaceNum;

    // 初始化 libusb
    int rc = libusb_init(&m_ctx);
    if (rc < 0) {
        std::cerr << "Failed to initialize libusb: " << libusb_error_name(rc) << std::endl;
        return false;
    }

    // 打开设备
    m_handle = libusb_open_device_with_vid_pid(m_ctx, vendorId, productId);
    if (!m_handle) {
        std::cerr << "Device not found (VID:0x" << std::hex << vendorId 
                  << ", PID:0x" << productId << ")" << std::dec << std::endl;
        libusb_exit(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    std::cout << "Device opened successfully" << std::endl;

    // 声明接口
    rc = libusb_claim_interface(m_handle, interfaceNum);
    if (rc < 0) {
        std::cerr << "Failed to claim interface " << interfaceNum << ": " 
                  << libusb_error_name(rc) << std::endl;
        libusb_close(m_handle);
        libusb_exit(m_ctx);
        m_handle = nullptr;
        m_ctx = nullptr;
        return false;
    }

    std::cout << "Interface " << static_cast<int>(interfaceNum) << " claimed" << std::endl;

    // 查找 OUT 端点
    if (!findOutEndpoint()) {
        std::cerr << "OUT endpoint not found on interface " << static_cast<int>(interfaceNum) << std::endl;
        libusb_release_interface(m_handle, interfaceNum);
        libusb_close(m_handle);
        libusb_exit(m_ctx);
        m_handle = nullptr;
        m_ctx = nullptr;
        return false;
    }

    m_initialized = true;
    std::cout << "USBSink initialized successfully" << std::endl;
    return true;
}

void USBSink::stop()
{
    if (!m_initialized) {
        return;
    }

    std::cout << "[USBSink::stop] Starting cleanup..." << std::endl;

    if (m_handle) {
        std::cout << "[USBSink::stop] Releasing interface..." << std::endl;
        libusb_release_interface(m_handle, m_config.interfaceNum);
        
        std::cout << "[USBSink::stop] Closing device..." << std::endl;
        libusb_close(m_handle);
        m_handle = nullptr;
    }

    if (m_ctx) {
        std::cout << "[USBSink::stop] Exiting libusb..." << std::endl;
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }

    m_initialized = false;
    m_config.outEndpointAddr = 0;
    
    std::cout << "[USBSink::stop] Cleanup completed" << std::endl;
}

void USBSink::setStopCallback(StopCallback callback)
{
    m_stopCallback = std::move(callback);
}

bool USBSink::write(const uint8_t* data, uint32_t size)
{
    if (!m_initialized || !m_handle) {
        std::cerr << "[USBSink::write] Not initialized or handle is null" << std::endl;
        return false;
    }

    if (!data || size == 0) {
        std::cerr << "[USBSink::write] Invalid data or size" << std::endl;
        return false;
    }

    if (m_config.outEndpointAddr == 0) {
        std::cerr << "[USBSink::write] OUT endpoint not configured" << std::endl;
        return false;
    }

    // 执行批量传输
    int transferred = 0;
    int rc = libusb_bulk_transfer(
        m_handle,
        m_config.outEndpointAddr,
        const_cast<uint8_t*>(data),
        size,
        &transferred,
        100  // 超时
    );

    if (rc != 0) {
        std::cerr << "[USBSink::write] Transfer failed: " << libusb_error_name(rc) 
                  << " (transferred: " << transferred << "/" << size << ")" << std::endl;
        if (m_stopCallback) m_stopCallback();
        return false;
    }

    if (static_cast<uint32_t>(transferred) != size) {
        std::cerr << "[USBSink::write] Incomplete transfer: " << transferred 
                  << "/" << size << " bytes" << std::endl;
        if (m_stopCallback) m_stopCallback();
        return false;
    }

    return true;
}

bool USBSink::findOutEndpoint()
{
    if (!m_handle) {
        return false;
    }

    libusb_config_descriptor* config = nullptr;
    int rc = libusb_get_active_config_descriptor(
        libusb_get_device(m_handle), 
        &config
    );
    
    if (rc != 0) {
        std::cerr << "Failed to get config descriptor: " << libusb_error_name(rc) << std::endl;
        return false;
    }

    bool found = false;
    
    // 遍历所有接口
    for (int i = 0; i < config->bNumInterfaces && !found; i++) {
        const libusb_interface& iface = config->interface[i];
        
        // 遍历所有 alternate settings
        for (int j = 0; j < iface.num_altsetting && !found; j++) {
            const libusb_interface_descriptor& altsetting = iface.altsetting[j];
            
            // 检查是否是目标接口
            if (altsetting.bInterfaceNumber == m_config.interfaceNum) {
                // 遍历所有端点
                for (int k = 0; k < altsetting.bNumEndpoints; k++) {
                    const libusb_endpoint_descriptor& ep = altsetting.endpoint[k];
                    
                    // 检查是否是 OUT 端点
                    if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
                        m_config.outEndpointAddr = ep.bEndpointAddress;
                        found = true;
                        std::cout << "Found OUT endpoint: 0x" << std::hex 
                                  << static_cast<int>(m_config.outEndpointAddr) 
                                  << std::dec << std::endl;
                        break;
                    }
                }
            }
        }
    }

    libusb_free_config_descriptor(config);
    return found;
}
