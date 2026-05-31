# SEWU Audio S3 IDF - Pinout dan Fitur Implementasi

Dokumen ini khusus untuk project IDF di folder `sewu_audio_s3_idf`.
Tujuan: jadi referensi cepat untuk wiring hardware, pin mapping, fitur yang sudah aktif, dan parameter penting saat build/flash/testing.

## 1) Ringkasan Sistem

- MCU: ESP32-S3 N16R8
- Framework: ESP-IDF v5.3.x
- Fungsi utama: USB Audio Interface (playback) + DSP + UI TFT + kontrol encoder/button
- Output audio: I2S ke DAC eksternal (contoh PCM5102)
- UI: TFT ST7789 via SPI + PWM backlight

## 2) Pinout Hardware (Aktif di Firmware)

Semua pin di bawah diambil dari `components/sewu_core/include/sewu_app_state.h`.

| Fungsi | GPIO ESP32-S3 | Arah | Tujuan/Wiring |
|---|---:|---|---|
| I2S BCLK | 42 | Output | Ke `BCK` DAC (PCM5102) |
| I2S LRCK/WS | 41 | Output | Ke `LCK/WS` DAC |
| I2S DOUT | 40 | Output | Ke `DIN` DAC |
| TFT MOSI | 11 | Output | Ke `SDA/MOSI` ST7789 |
| TFT SCLK | 12 | Output | Ke `SCL/SCLK` ST7789 |
| TFT CS | 10 | Output | Ke `CS` ST7789 |
| TFT DC | 9 | Output | Ke `DC` ST7789 |
| TFT RST | 14 | Output | Ke `RST` ST7789 |
| TFT BLK | 13 | Output PWM | Ke `BL/LED` ST7789 (dimming) |
| Encoder A | 4 | Input pull-up | Rotary encoder channel A |
| Encoder B | 5 | Input pull-up | Rotary encoder channel B |
| Encoder SW | 6 | Input pull-up | Tombol tekan encoder |
| Key K0 | 7 | Input pull-up | Tombol fungsi source/reset |
| BTN2 | 8 | Input pull-up | Tombol limiter/page |
| Status LED | 2 | Output | LED status sistem |

## 3) Pin Native USB (Wajib Board ESP32-S3)

USB Audio lewat USB native ESP32-S3 (TinyUSB), bukan USB-serial converter eksternal.

| Fungsi USB | GPIO ESP32-S3 |
|---|---:|
| USB D- | 19 |
| USB D+ | 20 |

Catatan:
- Jalur D+/D- mengikuti hardware native USB ESP32-S3.
- Pastikan konektor USB board memang terhubung ke USB native SoC.

## 4) Wiring Minimum yang Disarankan

### 4.1 I2S DAC (PCM5102)
- `ESP32 GPIO42` -> `PCM5102 BCK`
- `ESP32 GPIO41` -> `PCM5102 LCK/WS`
- `ESP32 GPIO40` -> `PCM5102 DIN`
- `ESP32 3V3` -> `PCM5102 VCC`
- `ESP32 GND` -> `PCM5102 GND`
- Output analog PCM5102 -> amplifier/headphone stage sesuai desain kamu

### 4.2 TFT ST7789 (SPI)
- `GPIO11 MOSI` -> `SDA`
- `GPIO12 SCLK` -> `SCL`
- `GPIO10 CS` -> `CS`
- `GPIO9 DC` -> `DC`
- `GPIO14 RST` -> `RST`
- `GPIO13` -> `BL/LED` (PWM backlight)
- `3V3` dan `GND` sesuai modul TFT

### 4.3 Input Kontrol
- Encoder A/B/SW ke GPIO `4/5/6` dengan wiring tombol ke GND (aktif-low, pull-up internal aktif)
- BTN2 ke GPIO `8` (aktif-low)
- K0 ke GPIO `7` (aktif-low)

## 5) Fitur yang Sudah Diimplementasikan

### 5.1 USB Audio (IDF-first)
- `espressif/usb_device_uac` aktif
- Mode playback stereo:
  - `CONFIG_UAC_SPEAKER_CHANNEL_NUM=2`
  - `CONFIG_UAC_MIC_CHANNEL_NUM=0`
  - `CONFIG_UAC_SAMPLE_RATE=48000`
- Device identity:
  - `CONFIG_TUSB_MANUFACTURER="SEWU AUDIO"`
  - `CONFIG_TUSB_PRODUCT="SEWU Audio S3"`
  - `CONFIG_TUSB_SERIAL_NUM="SEWU-S3"`
- Host control callback aktif:
  - Mute dari host OS -> diterapkan ke output USB
  - Volume dari host OS -> diterapkan ke gain output USB

### 5.2 Pipeline Audio
- Driver audio output: `i2s_std` (bukan legacy)
- Format output: stereo 16-bit
- Sample rate: 48 kHz
- USB ring buffer: 8192 frame stereo (`freertos/ringbuf`)
- DSP realtime:
  - EQ 5-band
  - Limiter
  - VU/Peak meter
  - Data visualizer adaptif

### 5.3 UI dan Kontrol
- UI ST7789 custom renderer via `esp_lcd`
- PWM backlight (`LEDC`)
- Halaman dashboard + USB debug
- Serial telemetry:
  - `[HOME]`, `[USB]`, `[STRESS]`, `[ALERT]`, `[HOST]`

## 6) Arsitektur Task dan Core

Di `main/sewu_main.c`:

- `sewu_audio_task`:
  - Core: `1`
  - Priority: `23`
  - Fokus: render audio/I2S loop realtime
- `sewu_service_task`:
  - Core: `0`
  - Priority: `6`
  - Fokus: input, DSP service, USB health/update, UI, settings, wifi placeholder

Konfigurasi ini dipilih untuk menjaga audio tetap halus saat UI/input berjalan.

## 7) Konfigurasi Build Penting (Saat Ini)

Di `sdkconfig.defaults`:

- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`
- `CONFIG_SPIRAM=y`
- `CONFIG_COMPILER_OPTIMIZATION_PERF=y`
- UAC task core/prio sudah disetel ke core 0 (`CONFIG_UAC_TINYUSB_TASK_CORE=0`, `CONFIG_UAC_SPK_TASK_CORE=0`)

## 8) Build, Flash, Monitor

Contoh cepat:

```powershell
cd .\sewu_audio_s3_idf
.\run_idf.ps1 build
.\run_idf.ps1 -p COM7 flash monitor
```

Ganti `COM7` sesuai port board kamu.

## 9) Checklist Verifikasi Hardware

- USB device terbaca sebagai `SEWU Audio S3` di host
- Audio keluar dari DAC tanpa glitch saat playback panjang
- Log `[STRESS]` stabil (underrun/overrun tidak naik cepat)
- Log `[HOST]` berubah saat volume/mute host OS diubah
- UI tetap responsif saat audio berjalan

