# SEWU Audio S3

ESP32-S3 based USB Audio Class (UAC) audio device with DSP processing, OLED display, rotary encoder input, and WiFi connectivity.

## Features

- **USB UAC 2.0** - High-quality audio interface (48kHz, stereo)
- **DSP Processing** - Real-time audio effects (EQ, filters, etc.)
- **OLED Display** - 128x64 pixel display for UI
- **Rotary Encoder** - Physical control interface
- **WiFi Control** - Wireless configuration via web interface
- **Expandable** - SPIFFS storage, SD card support

## Hardware Requirements

- ESP32-S3 DevKit Board
- OLED Display 128x64 (I2C)
- Rotary Encoder
- I2S DAC/Amp for audio output
- USB-C cable for power + data

## Installation

### 1. Prerequisites

Install ESP-IDF v5.3.3:
- [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.3/esp32s3/get-started/)

### 2. Clone & Setup

```bash
git clone https://github.com/hobysampingan/sewu_audio_s3_idf
cd sewu_audio_s3_idf
```

### 3. Configure

```bash
idf.py reconfigure
```

This will download all required managed components:
- espressif/cmake_utilities
- espressif/esp-dsp
- leeebo/tinyusb_src

### 4. Build

```bash
idf.py build
```

### 5. Flash

```bash
idf.py -p COMx flash
```

Replace `COMx` with your ESP32-S3 serial port (e.g., COM6 on Windows).

### 6. Monitor

```bash
idf.py -p COMx monitor
```

Press `Ctrl+]` to exit monitor.

## Project Structure

```
sewu_audio_s3_idf/
├── main/                    # Main application
│   ├── CMakeLists.txt
│   └── sewu_main.c
├── components/              # Custom components
│   ├── sewu_audio/          # Audio engine
│   ├── sewu_core/           # Core system/state
│   ├── sewu_dsp/            # DSP processing
│   ├── sewu_gfx/            # Graphics/display
│   ├── sewu_input/          # Input (encoder)
│   ├── sewu_settings/       # Settings management
│   ├── sewu_ui/             # User interface
│   ├── sewu_usb/            # USB audio handling
│   ├── sewu_wifi/           # WiFi connectivity
│   └── espressif__usb_device_uac/  # USB UAC driver
├── doc/                     # Documentation
├── CMakeLists.txt           # Build config
├── sdkconfig.defaults       # Default configuration
└── run_idf.ps1              # Build helper (Windows)
```

## USB Info

When connected to a PC, the device appears as:
- **Manufacturer:** SEWU AUDIO
- **Product:** SEWU AUDIO
- **Serial:** SEWU-S3

## License

MIT
