#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEWU_VIS_BAND_COUNT 24
#define SEWU_AUDIO_SAMPLE_RATE 48000
#define SEWU_AUDIO_BITS_PER_SAMPLE 16
#define SEWU_AUDIO_CHANNELS 2
#define SEWU_AUDIO_BUFFER_SAMPLES 256

#define SEWU_PIN_I2S_BCK 42
#define SEWU_PIN_I2S_WS 41
#define SEWU_PIN_I2S_DOUT 40
#define SEWU_PIN_TFT_MOSI 11
#define SEWU_PIN_TFT_SCLK 12
#define SEWU_PIN_TFT_CS 10
#define SEWU_PIN_TFT_DC 9
#define SEWU_PIN_TFT_RST 14
#define SEWU_PIN_TFT_BLK 13
#define SEWU_PIN_ENC_A 4
#define SEWU_PIN_ENC_B 5
#define SEWU_PIN_ENC_SW 6
#define SEWU_PIN_KEY_K0 7
#define SEWU_PIN_BTN2 8
#define SEWU_PIN_LED_STATUS 2

typedef struct {
  int bass_db;
  int low_mid_db;
  int mid_db;
  int high_mid_db;
  int treble_db;
  int master_gain_percent;
  int balance_percent;
  bool limiter_enabled;
} sewu_eq_preset_t;

typedef struct {
  int volume_percent;
  int balance_percent;
  int master_gain_percent;
  int bass_db;
  int low_mid_db;
  int mid_db;
  int high_mid_db;
  int treble_db;
  float tone_hz;
  bool tone_enabled;

  bool i2s_ready;
  bool usb_ready;
  bool usb_driver_ready;
  bool wifi_ready;
  bool usb_streaming;
  bool limiter_enabled;

  int source_mode;
  int active_source;
  int active_control_index;
  int preset_index;
  int performance_profile;
  int visualizer_mode;
  int vis_active_bands;
  int ui_page;

  int backlight_percent;
  int backlight_dim_percent;
  int backlight_standby_percent;
  uint32_t auto_dim_timeout_ms;
  uint32_t standby_timeout_ms;

  bool standby_active;

  int usb_fill_percent;
  int usb_target_fill_percent;
  int usb_health_percent;
  int usb_health_state;
  int usb_latency_ms;
  int usb_host_volume_percent;
  bool usb_host_muted;
  uint32_t usb_frames_in;
  uint32_t usb_underruns;
  uint32_t usb_overruns;
  uint32_t usb_last_reset_ms;
  uint32_t audio_write_errors;

  int vu_left_percent;
  int vu_right_percent;
  int vu_left_peak_percent;
  int vu_right_peak_percent;
  int limiter_percent;
  int limiter_peak_percent;

  int vis_band_percent[SEWU_VIS_BAND_COUNT];
  int vis_band_peak_percent[SEWU_VIS_BAND_COUNT];

  uint32_t last_input_ms;
  uint32_t boot_ms;

  int  ui_settings_cursor;
  bool ui_settings_editing;
} sewu_app_state_t;

extern sewu_app_state_t g_sewu_state;

void sewu_app_state_init_defaults(void);

#ifdef __cplusplus
}
#endif
