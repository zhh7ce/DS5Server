# DS5Server

[中文](README.md) | [English](README.EN.md)

DS5Dongle音频服务器。从 PipeWire 虚拟音频设备接收音频数据，编码为 Opus 格式并通过 USB 发送到 DS5Dongle。

## 依赖

- libpipewire
- libspa
- libopus
- libsamplerate
- libboost
- libusb

## 编译

```bash
mkdir build && cd build
cmake ..
make
```

## 运行

```bash
./ds5server
```

程序会创建一个名为 "DualSense Wireless Controller" 的虚拟音频设备，系统会将音频输出到此设备，DS5Server 接收音频数据编码后发送到DS5Dongle。

## 配套客户端

本项目需要配合 [DS5Dongle](https://github.com/zhh7ce/DS5Dongle) 的 `client` 分支使用。

## 架构

- **AudioSink**: 创建 PipeWire 虚拟音频输入节点，接收系统音频
- **AudioEncoder**: 将 4 声道音频分离为触觉反馈和立体声，使用 Opus 编码
- **USBSink**: 通过 libusb 将编码后的音频数据发送到 DualSense 控制器
