# Cara Penggunaan SEWU Audio S3

Dokumen ini menjelaskan kontrol fisik firmware SEWU Audio S3 saat ini. UI LCD masih mode stress test, jadi beberapa teks di layar memang masih berupa status/debug.

## Kontrol Utama

### Rotary Encoder

Putar rotary:
- Mengubah nilai parameter yang sedang aktif.
- Parameter aktif ditampilkan di LCD sebagai label kontrol, misalnya `VOL`, `BASS`, `LMID`, `MID`, `HMID`, `TREB`, `MGAIN`, `BKL`, `DIMT`, `STBYT`, `PROF`, `VISM`, atau `BAL`.

Tekan rotary sebentar:
- Pindah ke parameter berikutnya.
- Urutan parameter:
  `VOL -> BASS -> LMID -> MID -> HMID -> TREB -> MGAIN -> BKL -> DIMT -> STBYT -> PROF -> VISM -> BAL`

Tahan rotary sekitar 900 ms:
- Pindah preset EQ berikutnya.
- Preset berjalan dari preset bawaan ke slot user.

Tahan rotary sekitar 2.5 detik:
- Simpan setting EQ saat ini ke preset user aktif.

## Parameter Rotary

`VOL`
- Volume utama, rentang 0 sampai 100%.

`BASS`
- Gain bass, rentang -12 sampai +12 dB.

`LMID`
- Gain low-mid, rentang -12 sampai +12 dB.

`MID`
- Gain mid, rentang -12 sampai +12 dB.

`HMID`
- Gain high-mid, rentang -12 sampai +12 dB.

`TREB`
- Gain treble, rentang -12 sampai +12 dB.

`MGAIN`
- Master gain, rentang 50 sampai 150%.

`BKL`
- Backlight LCD, rentang 5 sampai 100%.

`DIMT`
- Waktu auto-dim LCD, rentang 5 sampai 120 detik.

`STBYT`
- Waktu standby saat USB tidak streaming, rentang 5 sampai 300 detik.

`PROF`
- Performance profile:
  `STABLE`, `BAL`, `MAX`.

`VISM`
- Mode visualizer:
  `BAR`, `DOT`, `PEAK`.

`BAL`
- Balance kiri/kanan, rentang -100 sampai +100.

## Tombol BTN2

Tekan BTN2 sebentar:
- Toggle limiter ON/OFF.

Tahan BTN2 sekitar 700 ms:
- Pindah halaman LCD.
- Halaman saat ini:
  `HOME` dan `USB DEBUG`.

## Tombol K0

Tekan K0 sebentar:
- Ganti source mode.
- Urutan source:
  `AUTO -> USB -> TONE`

Tahan K0 sekitar 1 detik:
- Reset statistik USB audio.
- Berguna saat stress test untuk mengulang hitungan underrun/overrun dari nol.

Tahan K0 sekitar 2.5 detik:
- Simpan setting EQ saat ini ke preset user aktif.

## Source Mode

`AUTO`
- Firmware otomatis memakai USB jika stream stabil dan buffer cukup.
- Jika USB belum siap, sistem fallback ke tone.

`USB`
- Paksa output dari USB audio.
- Jika USB kosong, output akan ditahan/fade agar tidak pop/click.

`TONE`
- Paksa test tone internal.
- Berguna untuk cek jalur I2S/amplifier tanpa host USB.

## Halaman LCD Saat Stress Test

`HOME`
- Ringkasan source, volume, EQ, USB health, latency, VU, limiter, dan backlight.

`USB DEBUG`
- Detail USB driver/streaming, buffer, target fill, health, latency, underrun, overrun, frames in, host volume, dan visualizer kecil.

## Indikator Penting Saat Stress Test

`UNDERRUN`
- Harus idealnya tidak naik saat USB stream stabil.
- Kalau naik sesekali saat start/stop stream masih wajar.

`OVERRUN`
- Harus idealnya tidak naik terus.
- Kalau naik terus, host mengirim data lebih cepat daripada pipeline konsumsi atau buffer terlalu penuh.

`HLT` / health
- `OK`: kondisi bagus.
- `BUSY`: masih jalan, tapi buffer/health mulai tidak ideal.
- `RISK`: perlu diperhatikan, biasanya ada underrun/overrun atau stream tidak stabil.

`LAT`
- Perkiraan latency USB berdasarkan jumlah frame di ringbuffer.

`awe`
- Audio write error I2S.
- Saat stabil harus tetap `0`.

## Rekomendasi Stress Test

1. Set source ke `USB` atau `AUTO`.
2. Putar audio dari host.
3. Biarkan 10 sampai 30 menit dulu.
4. Pantau LCD atau serial log:
   `UNDERRUN`, `OVERRUN`, `HLT`, `LAT`, dan `awe`.
5. Kalau aman, lanjut test lebih lama 2 sampai 4 jam.

Target kondisi aman:
- `awe = 0`
- `UNDERRUN` tidak naik terus saat stream sudah stabil
- `OVERRUN` tidak naik terus
- health mayoritas `OK` atau sesekali `BUSY`, tidak sering `RISK`

