#include "ds5server.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <ostream>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <unistd.h>

std::atomic<bool> g_running(true);

void signalHandler(int signum)
{
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

int main()
{
    // 注册信号处理器
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 将 stderr 重定向到 /dev/null，避免干扰 stdout 的音频数据
    freopen("/dev/null", "w", stderr);

    std::cout << "=== PipeWire Virtual Audio Sink Demo ===" << std::endl;

    // 创建设备标识符
    std::string deviceId = "USB054c:0ce6";
    
    // 创建 DS5Server 对象，传入设备标识符
    auto ds5 = std::make_unique<DS5Server>(deviceId);

    // 设置状态回调：当设备断开时，将 g_running 设为 false
    ds5->setStopCallback([&ds5]() {
        std::cout << "[Main] Device disconnected, signaling shutdown..." << std::endl;
        g_running = false;  // 触发退出
    });

    // 初始化 DS5Server（会创建相同标识符的 Audio 对象）
    if (!ds5->initialize()) {
        std::cerr << "Failed to initialize DS5Server" << std::endl;
        return 1;
    }

    // 启动音频服务
    if (!ds5->start()) {
        std::cerr << "Failed to start audio sink" << std::endl;
        return 1;
    }

    std::cout << "Virtual audio sink is running. Press Ctrl+C to stop." << std::endl;
    std::cout << "The program will exit when the device is disconnected or Ctrl+C is pressed." << std::endl;

    // 等待直到 g_running 变为 false（由信号或回调触发）
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // 停止音频服务
    ds5->stop();
    ds5.reset();  // 显式析构

    std::cout << "Program exiting." << std::endl;

    return 0;
}
