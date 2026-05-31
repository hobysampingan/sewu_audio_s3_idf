# SEWU Audio S3 IDF - Progress & Planning

Terakhir diperbarui: **May 29, 2026**  
Scope: status implementasi stack IDF, optimasi performa terakhir, fase roadmap, dan next-step setelah stress test.

## 1) Current State (Ringkas)

- Stack: **Full ESP-IDF** (bukan Arduino runtime).
- Build status: **PASS** (binary berhasil dihasilkan).
- USB Audio playback: **jalan**, dengan ringbuffer + health logger.
- UI TFT + input + DSP + settings NVS: **jalan**.
- Optimasi bottleneck audio/UI/DSP: **masuk dan build PASS**.
- Stress test: **siap dijalankan** (target awal 10-30 menit, lalu 2-4 jam).
- Baseline audio state: **freeze sebelum lanjut Phase 6/7**.
- Catatan production: source `TONE` masih dipakai sebagai mode test/debug. Untuk production, fallback AUTO akan dibuat silent/mute halus, bukan tone.

## 2) Progress per Phase (Acuan `BLUEPRINT.md`)

## Phase 1 - Bring-up hardware
- Status: **DONE**
- Catatan:
  - UI ST7789 boot dan render aktif.
  - I2S out ke DAC aktif.
  - Encoder + tombol terbaca stabil.

## Phase 2 - Audio core
- Status: **DONE**
- Catatan:
  - Volume + EQ + limiter aktif.
  - VU meter/peak aktif.
  - Save/load NVS aktif.

## Phase 3 - USB Audio stable
- Status: **IN VALIDATION**
- Sudah:
  - Enumerasi USB dan stream USB -> I2S aktif.
  - Callback host mute/volume aktif.
  - Logging stress (`[STRESS]`, `[ALERT]`, `[HOST]`) aktif.
  - USB ringbuffer read sudah batch, bukan frame-by-frame.
  - AUTO mode sudah diperbaiki agar tidak menguras ringbuffer sebelum threshold masuk USB tercapai.
  - Statistik underrun USB sudah dirapikan agar tidak double-count dari batch reader dan audio engine.
- Gate selesai:
  - Lulus stress test minimal 2 jam tanpa drop mayor.

## Phase 4 - DSP upgrade
- Status: **DONE**
- Catatan:
  - EQ 5-band aktif.
  - Limiter + anti-pop gate aktif.
  - Preset custom (user slot) tersimpan.
  - EQ gain sudah di-cache agar `powf()` tidak jalan tiap service tick.
  - Mapping visualizer FFT sudah precomputed agar tidak menghitung `powf()` tiap update visualizer.

## Phase 5 - Visualizer pro
- Status: **DONE (STRESS TEST UI)**
- Catatan:
  - 16/24 band visualizer aktif.
  - Peak/decay aktif.
  - Render di-throttle sesuai health/performance profile.
  - Spectrum panel sudah dirender ke buffer kecil lalu dikirim sekali via bitmap untuk mengurangi transaksi SPI.
  - UI production-ready belum dimulai; layar masih sengaja menampilkan log/status teknikal untuk stress test.

## Phase 6 - Headphone stage
- Status: **IN PROGRESS / HW VALIDATION**
- Catatan:
  - Integrasi hardware amp headphone (TDA1308) sedang diuji.
  - Noise floor/gain matching line vs headphone sedang divalidasi.
  - Tetap pertahankan jalur USB/line baseline saat headphone testing.

## Phase 7 - Wi-Fi mode (opsional)
- Status: **IN PROGRESS**
- Catatan:
  - Komponen placeholder sudah ada, pengembangan stream receiver dimulai.
  - Target pertama: Wi-Fi mode skeleton + source status, tanpa mengganggu USB audio.

## Phase 8 - Max mode (opsional)
- Status: **IN PROGRESS**
- Catatan:
  - Mode performa tinggi sudah disiapkan.
  - Menunggu hasil stress test sebagai dasar lock final mode.

## Phase 9 - Production UI/UX
- Status: **PENDING AFTER AUDIO VALIDATION**
- Catatan:
  - Akan dimulai setelah audio dan log LCD aman selama stress test.
  - Target: UI lebih clean, visualizer lebih smooth, navigasi lebih gampang, tanpa log/debug teknikal di layar utama.
  - Mode `TONE` tetap bisa dipertahankan sebagai menu/service test, tetapi fallback AUTO production diarahkan ke silence/mute halus.

## 3) DoD Tracker (Acuan `BLUEPRINT.md` Section 14)

- [ ] Playback USB stabil >= 2 jam tanpa drop
- [x] UI responsif tanpa ganggu audio (berdasarkan uji internal awal)
- [x] Tidak ada pop besar saat transport (anti-pop gate aktif, perlu konfirmasi jangka panjang)
- [ ] Output line bersih ke mixer (butuh verifikasi final di setup target)
- [ ] Output headset nyaman via TDA1308 (Phase 6 belum final)
- [x] Semua setting tersimpan dan restore saat reboot
- [ ] Mode DSP/visualizer stress-test lolos tanpa glitch audio
- [ ] Production UI clean tanpa log/debug di layar utama
- [ ] AUTO fallback production silent/mute halus, bukan test tone

Kesimpulan saat ini:
- **DoD release penuh: BELUM**
- **DoD core software IDF audio pipeline: HAMPIR, menunggu hasil stress test**

## 4) Optimasi Terakhir (May 28, 2026)

- Audio engine:
  - USB batch prefetch hanya dilakukan saat data benar-benar akan dikonsumsi.
  - AUTO mode tidak lagi menarik data USB sebelum buffer mencapai enter threshold.
  - Double-count underrun pada mode USB dikurangi.
- USB audio:
  - Batch reader `sewu_usb_audio_read_batch()` tersedia untuk mengurangi lock contention ringbuffer.
- UI TFT:
  - `fill_rect()` sudah mengirim chunk beberapa baris per transaksi.
  - Spectrum panel memakai buffer lokal dan satu `draw_bitmap()`.
- DSP:
  - EQ gain cache aktif.
  - Visualizer FFT bin map precomputed.
- Build:
  - `.\run_idf.ps1 build` PASS setelah optimasi.

## 5) Immediate Next Step (Setelah Stress Test Selesai)

Jika hasil stress test **AMAN**:
1. Freeze baseline (tag internal `rc1`).
2. Tandai Phase 3 = DONE.
3. Mulai Phase 9 production UI/UX:
   - sederhanakan label/menu,
   - hilangkan noise teknikal di UI release,
   - buat visualizer lebih smooth,
   - buat navigasi lebih mudah,
   - ubah AUTO fallback production ke silent/mute halus.

Jika hasil stress test **BELUM AMAN**:
1. Ambil log window masalah (timestamp + counter underrun/overrun + awe).
2. Tuning buffer/threshold/task priority.
3. Ulang stress test sampai stabil.

## 6) File Referensi Utama

- Blueprint roadmap: `BLUEPRINT.md`
- Pinout + wiring + fitur aktif: `IDF_PINOUT_FEATURES.md`
- Build/run guide: `README.md`
- Cara penggunaan kontrol: `cara penggunaan.md`
