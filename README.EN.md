# DS5Server

[中文](README.md) | [English](README.EN.md)

Audio server for DS5Dongle. Receives audio data from a PipeWire virtual audio device, encodes it to Opus format, and sends it to DS5Dongle via USB.

## Dependencies

- libpipewire
- libspa
- libopus
- libsamplerate
- libboost
- libusb

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Run

```bash
./ds5server
```

The program creates a virtual audio device named "DualSense Wireless Controller". The system outputs audio to this device, and DS5Server receives and encodes the audio data before sending it to DS5Dongle.

## Auto-start

You can copy the compiled binary to the appropriate directory and enable auto-start via systemd. Alternatively, use udev rules to automatically run the program when the controller is connected.

```bash
# Enable service (auto-start on boot)
systemctl --user enable ds5server.service

# Start service immediately
systemctl --user start ds5server.service
```

## Companion Client

This project requires the `client` branch of [DS5Dongle](https://github.com/zhh7ce/DS5Dongle).

## Architecture

- **AudioSink**: Creates a PipeWire virtual audio input node to receive system audio
- **AudioEncoder**: Separates 4-channel audio into haptic feedback and stereo, encoded with Opus
- **USBSink**: Sends encoded audio data to the DualSense controller via libusb
