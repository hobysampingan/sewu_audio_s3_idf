# SEWU AUDIO NEXT GEN (ESP32-S3) - Full Blueprint

Dokumen ini adalah blueprint project 
- USB Audio dari komputer
- Wi-Fi streaming (DLNA/HTTP) opsional
- Output line ke mixer/speaker + output headset terpisah

---

## 1) Tujuan Project

Membuat audio interface / network audio receiver berbasis:
- `ESP32-S3 N16R8`
- `TFT 2.4" ST7789`
- `PCM5102` (I2S DAC line-out)
- `Rotary Encoder`
- `2 Push Button`
- `TDA1308` (headphone amp, opsional tapi direkomendasikan)

Use case:
- PC/Notebook -> USB -> device -> line out ke Yamaha / mixer
- HP/PC -> Wi-Fi stream (opsional) -> device
- Monitoring headset langsung dari output headphone amp

---

## 2) Konsep Arsitektur

### Audio Path
1. `USB Audio` (utama) atau `Wi-Fi Stream` (opsional)
2. DSP ringan di ESP32-S3 (EQ/limiter/gain/balance/clarity)
3. I2S out ke `PCM5102`
4. Cabang output:
   - `Line Out` (langsung dari PCM5102) ke mixer/speaker aktif
   - `Headphone Out` via `TDA1308`

### UI Path
- ST7789 untuk HOME / SETTINGS / INFO
- Rotary + 2 tombol untuk kontrol cepat

---

## 3) Kenapa ESP32-S3

- Native USB device (penting untuk USB Audio)
- PSRAM besar (bagus untuk buffer/UI)
- Wi-Fi kuat untuk mode streaming
- Cukup headroom untuk DSP menengah + UI smooth

Catatan:
- ESP32-S3 cocok untuk USB/Wi-Fi audio.
- ESP32-S3 bukan chip "khusus audio", tapi sangat capable untuk audio digital embedded.
- Untuk kualitas final, faktor analog (power/ground/layout) tetap paling menentukan.

---

## 4) Fitur Target

## Fitur v1 (MVP)
- USB Audio playback dari PC
- Volume master
- EQ 3-band (Bass/Mid/Treble)
- Master gain
- Balance L/R
- Soft limiter
- VU meter + limiter meter
- Save/load setting (NVS)

## Fitur v1.1
- Clarity control
- Preset audio (Indoor/Outdoor/Headset/Amplifier + preset lama)
- Backlight control + auto-dim
- Standby / lock state saat tidak ada stream

## Fitur v1.2 (Pro Audio Start)
- EQ upgrade ke `5-band` (IIR parametric fixed-center)
- Preset slot user (save 2-4 preset custom)
- Stereo width (opsional, ringan)
- Peak hold indicator pada VU

## Fitur v2 (Pro Visual + Tuning)
- EQ `7/10-band` (pilih berdasarkan load test)
- Visualizer multi-band (`16-band`) per frekuensi
- Spectrum style: bar + peak decay + limiter activity overlay
- Latency monitor sederhana (buffer health)

## Fitur v3 (opsional)
- Wi-Fi renderer (DLNA/HTTP)
- Web UI sederhana untuk monitor + update setting
- Source switch: USB <-> Wi-Fi
- Visualizer advanced (`24/32-band`) jika resource cukup

---

## 4b) Profil DSP Maksimal (Target Bertahap)

Jangan langsung loncat ke paling berat. Naikkan bertahap sambil ukur stabilitas.

## Level A (aman)
- 3-band EQ + limiter + clarity + balance
- VU L/R + limiter bar

## Level B (menengah)
- 5-band EQ
- Visualizer pseudo-band (mapping energi)

## Level C (pro)
- 7/10-band EQ
- FFT visualizer 16-band
- Peak hold + smooth decay

## Level D (eksperimental)
- 10-band EQ + FFT 24/32-band + efek UI lebih kompleks
- Hanya dipakai jika audio tetap drop-free pada stress test panjang

---

## 5) Pinout Rekomendasi (ESP32-S3) - Final v2

Mapping ini diprioritaskan untuk modul gabungan `EasyWare EP003954` (TFT + EC11).

## 5a) Pin yang di-reserve
- `GPIO19 (USB D-)` dan `GPIO20 (USB D+)` di-reserve untuk native USB.
- Jangan dipakai untuk tombol/LCD/audio supaya jalur USB audio aman.

## 5b) I2S ke PCM5102
| Fungsi | GPIO |
|---|---|
| I2S BCK -> PCM5102 BCK | GPIO42 |
| I2S WS/LRCK -> PCM5102 LCK | GPIO41 |
| I2S DOUT -> PCM5102 DIN | GPIO40 | 

## 5c) EP003954 (TFT + Rotary)
Label pin modul umumnya: `GND VCC SCL SDA RES DC CS BLK A B PUSH K0`

| Pin Modul EP003954 | GPIO ESP32-S3 |
|---|---|
| SCL (SPI SCK) | GPIO12 |
| SDA (SPI MOSI) | GPIO11 |
| RES | GPIO14 |
| DC | GPIO9 |
| CS | GPIO10 |
| BLK (PWM dimmer) | GPIO13 |
| A (Encoder CLK) | GPIO4 |
| B (Encoder DT) | GPIO5 |
| PUSH (Encoder SW) | GPIO6 |
| K0 (extra key, opsional) | GPIO7 |

## 5d) Tombol tambahan dan indikator
| Fungsi | GPIO |
|---|---|
| BTN2 (Next/Down) | GPIO8 |
| LED Status (opsional) | GPIO2 |

Catatan:
- Jika tidak pakai `K0`, bisa pakai `GPIO7` untuk fungsi lain.
- Hindari pin strapping untuk fungsi penting boot (tergantung devboard).
- Kalau orientasi layar terbalik, cukup ubah `rotation` di firmware.

Power:
- ESP32-S3: 5V dari adaptor USB/step-down berkualitas
- PCM5102: 5V (VIN) + GND
- ST7789: 3V3 + GND
- TDA1308: 5V (atau sesuai modul) + GND

---

## 6) Wiring Audio Output

## A) Line Out ke Mixer/Speaker Aktif
- PCM5102 `LOUT/LROUT` -> Line L
- PCM5102 `ROUT` -> Line R
- PCM5102 `AGND` -> Audio GND

## B) Headphone Out (via TDA1308)
- PCM5102 L -> TDA1308 IN L
- PCM5102 R -> TDA1308 IN R
- AGND/GND disatukan dengan grounding star
- Output TDA1308 -> jack headset stereo

Catatan:
- Jangan pakai PAM8302 untuk headset.
- TDA1308 lebih cocok, murah, dan lebih forgiving untuk prototype.

---

## 7) Grounding & Decoupling (Wajib)

- Gunakan `star ground` dari titik ground utama power.
- Pisahkan jalur analog audio dari jalur digital/SPI/I2S.
- Tambah decoupling dekat modul:
  - Tiap modul: `100nF` + `10uF`
  - Rail utama: `470uF` s/d `1000uF` (>= 10V, boleh 25V/50V)
- Kabel sinyal audio analog dipendekkan.

---

## 8) Software Stack Rekomendasi

## Opsi A (Direkomendasikan untuk USB Audio matang)
- Framework: `ESP-IDF`
- USB: `TinyUSB` (USB Audio Class)
- I2S: driver ESP-IDF
- UI: ST7789 library (port sesuai framework)

## Opsi B (Migrasi cepat dari project lama)
- Framework: `Arduino-ESP32` (UI + kontrol dulu)
- USB audio bisa menyusul setelah core/library dipastikan stabil

Saran eksekusi:
- Start dari Arduino style modular (seperti project sekarang),
- lalu migrate audio engine ke IDF saat USB UAC final.

---

## 9) Struktur File yang Disarankan

```text
sewu_audio_s3/
  sewu_audio_s3.ino
  audio_engine.ino
  usb_audio.ino
  wifi_stream.ino
  dsp_core.ino
  ui_render.ino
  input_ctrl.ino
  settings_nvs.ino
  hw_pins.h
  README.md
```

---

## 10) Daftar Library (Target)

## Jika Arduino-first:
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `ESP32Encoder`
- `Preferences` (built-in)
- (USB audio sesuai dukungan core/library terbaru)

## Jika IDF-first:
- `ESP-IDF TinyUSB`
- `ESP-IDF I2S`
- `NVS`
- UI stack pilihan (SPI LCD driver)

---

## 11) UI/UX Plan

Halaman utama:
- Nama device + source aktif (`USB` / `Wi-Fi`)
- Volume bar
- Preset + parameter ringkas (MG, CLR, BEX, LIM)
- VU L/R + limiter meter
- Indikator load ringan (opsional): `AUDIO OK` / `BUSY`

Settings section:
- AUDIO: EQ, Preset, Master Gain, Balance, Clarity, Bass Enhance, Limiter, (opsional) Stereo Width
- DISPLAY: Backlight, Auto Dim
- SYSTEM: Source select, Reset, Info

Info page:
- Sample rate
- Bit depth (jika tersedia)
- Stream status
- Device connected status
- DSP mode aktif (3/5/7/10 band)
- Visualizer mode aktif (VU / 16-band / 24-band)

---

## 12) Roadmap Implementasi

## Phase 1 - Bring-up hardware
1. ESP32-S3 + ST7789 boot + UI basic
2. I2S out ke PCM5102, tes tone/audio
3. Input rotary + tombol stabil

## Phase 2 - Audio core
1. Volume + EQ + limiter
2. VU meter smooth
3. Save/load NVS

## Phase 3 - USB Audio stable
1. Enumerasi USB ke PC
2. Stream playback USB -> I2S
3. Latency & stability test

## Phase 4 - DSP upgrade
1. Upgrade EQ 5-band
2. Uji headroom + anti-clipping
3. Simpan preset custom

## Phase 5 - Visualizer pro
1. 16-band visualizer
2. Peak hold + decay tuning
3. Render optimization (partial redraw)

## Phase 6 - Headphone stage
1. Integrasi TDA1308
2. Noise floor tuning
3. Gain matching line/headphone

## Phase 7 - Wi-Fi mode (opsional)
1. Stream receiver
2. Source switching USB/Wi-Fi
3. UI status lengkap

## Phase 8 - Max mode (opsional)
1. Coba 7/10-band + 24-band visualizer
2. Stress test minimum 2-4 jam
3. Final mode lock berdasarkan hasil uji

---

## 13) Risiko & Mitigasi

- Noise/hiss analog:
  - Perbaiki power filtering, grounding, kabel audio.
- Flicker layar:
  - Batasi redraw parsial, hindari full repaint berulang.
- Audio pop saat play/pause:
  - Fade in/out + mute gate.
- USB drop:
  - Besarkan buffer + prioritaskan task audio.
- DSP/visual terlalu berat:
  - Turunkan band count, jaga audio callback tetap ringan.
- Jitter UI saat audio sibuk:
  - Batasi FPS UI, update parsial, hindari operasi float berat di render loop.

---

## 13b) Strategi Performa (Kunci Sukses)

- Prioritas utama: `audio task` selalu nomor 1.
- UI tidak boleh blok audio; UI cukup 20-30 FPS.
- FFT/visualizer update di rate terpisah (mis. 15-25 Hz), tidak setiap sample callback.
- Hindari log serial berlebihan saat mode release.
- Pakai fixed-size buffer dan hindari alokasi dinamis di jalur audio real-time.

---

## 14) Definition of Done (DoD)

Project dianggap siap rilis jika:
- Playback USB stabil >= 2 jam tanpa drop
- UI responsif tanpa ganggu audio
- Tidak ada pop besar saat transport
- Output line bersih ke mixer
- Output headset nyaman via TDA1308
- Semua setting tersimpan dan restore saat reboot
- Mode DSP/visualizer yang dipilih lolos stress test tanpa glitch

---

## 15) Catatan Session Berikutnya

Saat mulai next session, referensi dokumen ini dulu:
- Pilih stack: Arduino-first atau IDF-first
- Fix pin mapping final sesuai board ESP32-S3 yang dipakai
- Start dari Phase 1 dan cek per milestone
