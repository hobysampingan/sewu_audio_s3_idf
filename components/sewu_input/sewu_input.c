/* sewu_input.c — encoder + button input for Sewu Audio S3 (redesigned) */

#include "sewu_input.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sewu_app_state.h"
#include "sewu_settings.h"
#include "sewu_usb_audio.h"

static const char *TAG = "sewu_input";

/* ---------- Settings item enumeration ---------- */
enum {
    SET_VOLUME = 0,
    SET_BASS,
    SET_LOW_MID,
    SET_MID,
    SET_HIGH_MID,
    SET_TREBLE,
    SET_MASTER_GAIN,
    SET_BALANCE,
    SET_LIMITER,
    SET_PRESET,
    SET_VIS_MODE,
    SET_PERFORMANCE,
    SET_BACKLIGHT,
    SET_DIM_TIMEOUT,
    SET_STANDBY_TIMEOUT,
    SET_SAVE_USER1,
    SET_SAVE_USER2,
    SET_RESET_STATS,
    SET_COUNT,
};

/* ---------- Preset data ---------- */
typedef struct {
    int bass;
    int low_mid;
    int mid;
    int high_mid;
    int treble;
    int mgain;
    int bal;
    bool limiter;
} eq_preset_fixed_t;

static const eq_preset_fixed_t PRESETS[] = {
    /*  0: Default  */  {2, 1, 0, 1, 2, 102, 0, true},
    /*  1: Bass Boost*/  {5, 3, 0, 1, 2, 108, 0, true},
    /*  2: Flat      */  {0, 0, 0, 0, 0, 100, 0, true},
    /*  3: V-Shape   */  {3, 1, 0, 1, 3, 105, 0, true},
    /*  4: Podcast   */  {0, 2, 4, 3, 1, 100, 0, true},
    /*  5: Clarity   */  {1, 1, 2, 3, 4, 100, 0, true},
    /*  6: Classic   */  {2, 1, 1, 1, 2, 102, 0, true},
    /*  7: Neural    */  {1, 2, 3, 2, 1, 102, 0, true},
    /*  8: Rock      */  {3, 1, -1, 2, 4, 108, 0, true},
    /*  9: Jazz      */  {2, 2, 2, 1, 1, 100, 0, true},
    /* 10: Acoustic  */  {0, 1, 1, 2, 3, 100, 0, true},
    /* 11: Loudness  */  {4, 2, -1, 2, 4, 112, 0, true},
};

static const int PRESET_FIXED_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);
static const int PRESET_USER1 = 12;
static const int PRESET_USER2 = 13;
static const int PRESET_COUNT = 14;

/* ---------- Utility ---------- */
static int clamp_int(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* ---------- Hardware state ---------- */
static bool s_last_sw = true;
static bool s_last_btn2 = true;
static bool s_last_k0 = true;
static uint32_t s_sw_down_ms;
static bool s_sw_long_consumed;
static uint32_t s_btn2_down_ms;
static bool s_btn2_long_consumed;
static uint32_t s_k0_down_ms;
static bool s_k0_long_consumed;

// Double-press state: timer-based, no "awaiting" flags
static uint32_t s_btn2_last_release_ms;
static int     s_btn2_release_count;    // 0 = idle, 1 = first release waiting, 2 = second release (double done)

static uint32_t s_k0_last_release_ms;
static int     s_k0_release_count;

static uint8_t s_last_ab;
static int s_enc_accum;
static portMUX_TYPE s_enc_mux = portMUX_INITIALIZER_UNLOCKED;

#define ENC_LONG_MS    900U
#define BTN2_LONG_MS   700U
#define BTN2_DOUBLE_MS 400U
#define K0_LONG_MS     1000U
#define K0_DOUBLE_MS   400U

static const int8_t ENC_TRANSITION[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
};

/* ---------- Encoder ISR Handler ---------- */
static void IRAM_ATTR encoder_isr_handler(void* arg) {
    portENTER_CRITICAL_ISR(&s_enc_mux);
    int a = gpio_get_level(SEWU_PIN_ENC_A) ? 1 : 0;
    int b = gpio_get_level(SEWU_PIN_ENC_B) ? 1 : 0;
    uint8_t ab = (uint8_t)((a << 1) | b);
    uint8_t idx = (uint8_t)((s_last_ab << 2) | ab);
    s_last_ab = ab;
    s_enc_accum += ENC_TRANSITION[idx & 0x0F];
    portEXIT_CRITICAL_ISR(&s_enc_mux);
}

/* ---------- Encoder step reader ---------- */
static int read_encoder_step(void) {
    portENTER_CRITICAL(&s_enc_mux);
    int accum = s_enc_accum;
    int step = accum / 4;
    s_enc_accum = accum % 4;
    portEXIT_CRITICAL(&s_enc_mux);
    return step;
}

/* ---------- Preset management ---------- */
static void mark_preset_dirty(void) {
    if (g_sewu_state.preset_index < PRESET_USER1 || g_sewu_state.preset_index >= PRESET_COUNT) {
        g_sewu_state.preset_index = PRESET_USER1;
    }
}

static void apply_preset_index(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= PRESET_COUNT) idx = PRESET_COUNT - 1;

    if (idx < PRESET_FIXED_COUNT) {
        const eq_preset_fixed_t *p = &PRESETS[idx];
        g_sewu_state.bass_db = p->bass;
        g_sewu_state.low_mid_db = p->low_mid;
        g_sewu_state.mid_db = p->mid;
        g_sewu_state.high_mid_db = p->high_mid;
        g_sewu_state.treble_db = p->treble;
        g_sewu_state.master_gain_percent = p->mgain;
        g_sewu_state.balance_percent = p->bal;
        g_sewu_state.limiter_enabled = p->limiter;
    } else {
        sewu_eq_preset_t p = {0};
        sewu_settings_get_user_preset((idx == PRESET_USER2) ? 1 : 0, &p);
        g_sewu_state.bass_db = p.bass_db;
        g_sewu_state.low_mid_db = p.low_mid_db;
        g_sewu_state.mid_db = p.mid_db;
        g_sewu_state.high_mid_db = p.high_mid_db;
        g_sewu_state.treble_db = p.treble_db;
        g_sewu_state.master_gain_percent = p.master_gain_percent;
        g_sewu_state.balance_percent = p.balance_percent;
        g_sewu_state.limiter_enabled = p.limiter_enabled;
    }

    g_sewu_state.preset_index = idx;
}

static void save_active_user_preset(int slot) {
    if (slot < 0) slot = 0;
    if (slot > 1) slot = 1;
    sewu_eq_preset_t p = {
        .bass_db = g_sewu_state.bass_db,
        .low_mid_db = g_sewu_state.low_mid_db,
        .mid_db = g_sewu_state.mid_db,
        .high_mid_db = g_sewu_state.high_mid_db,
        .treble_db = g_sewu_state.treble_db,
        .master_gain_percent = g_sewu_state.master_gain_percent,
        .balance_percent = g_sewu_state.balance_percent,
        .limiter_enabled = g_sewu_state.limiter_enabled,
    };
    sewu_settings_set_user_preset(slot, &p);
    g_sewu_state.preset_index = (slot == 0) ? PRESET_USER1 : PRESET_USER2;
    ESP_LOGI(TAG, "saved user preset %d", slot + 1);
}

/* ---------- Settings value adjustment ---------- */
static void apply_settings_delta(int cursor, int delta) {
    switch (cursor) {
    case SET_VOLUME:
        g_sewu_state.volume_percent = clamp_int(g_sewu_state.volume_percent + delta, 0, 100);
        break;
    case SET_BASS:
        g_sewu_state.bass_db = clamp_int(g_sewu_state.bass_db + delta, -12, 12);
        mark_preset_dirty();
        break;
    case SET_LOW_MID:
        g_sewu_state.low_mid_db = clamp_int(g_sewu_state.low_mid_db + delta, -12, 12);
        mark_preset_dirty();
        break;
    case SET_MID:
        g_sewu_state.mid_db = clamp_int(g_sewu_state.mid_db + delta, -12, 12);
        mark_preset_dirty();
        break;
    case SET_HIGH_MID:
        g_sewu_state.high_mid_db = clamp_int(g_sewu_state.high_mid_db + delta, -12, 12);
        mark_preset_dirty();
        break;
    case SET_TREBLE:
        g_sewu_state.treble_db = clamp_int(g_sewu_state.treble_db + delta, -12, 12);
        mark_preset_dirty();
        break;
    case SET_MASTER_GAIN:
        g_sewu_state.master_gain_percent = clamp_int(g_sewu_state.master_gain_percent + delta, 50, 150);
        mark_preset_dirty();
        break;
    case SET_BALANCE:
        g_sewu_state.balance_percent = clamp_int(g_sewu_state.balance_percent + (delta * 2), -100, 100);
        mark_preset_dirty();
        break;
    case SET_LIMITER:
        if (delta != 0) {
            g_sewu_state.limiter_enabled = !g_sewu_state.limiter_enabled;
        }
        break;
    case SET_PRESET: {
        int next = g_sewu_state.preset_index + delta;
        while (next < 0) next += PRESET_COUNT;
        while (next >= PRESET_COUNT) next -= PRESET_COUNT;
        apply_preset_index(next);
        break;
    }
    case SET_VIS_MODE: {
        int mode = g_sewu_state.visualizer_mode + delta;
        while (mode < 0) mode += 3;
        while (mode > 2) mode -= 3;
        g_sewu_state.visualizer_mode = mode;
        break;
    }
    case SET_PERFORMANCE: {
        int profile = g_sewu_state.performance_profile + delta;
        while (profile < 0) profile += 3;
        while (profile > 2) profile -= 3;
        g_sewu_state.performance_profile = profile;
        g_sewu_state.vis_active_bands = (profile >= 2) ? 24 : 16;
        break;
    }
    case SET_BACKLIGHT:
        g_sewu_state.backlight_percent = clamp_int(g_sewu_state.backlight_percent + delta, 5, 100);
        break;
    case SET_DIM_TIMEOUT: {
        int sec = (int)(g_sewu_state.auto_dim_timeout_ms / 1000U);
        sec = clamp_int(sec + delta, 5, 120);
        g_sewu_state.auto_dim_timeout_ms = (uint32_t)sec * 1000U;
        break;
    }
    case SET_STANDBY_TIMEOUT: {
        int sec = (int)(g_sewu_state.standby_timeout_ms / 1000U);
        sec = clamp_int(sec + delta, 5, 300);
        g_sewu_state.standby_timeout_ms = (uint32_t)sec * 1000U;
        break;
    }
    default:
        break;
    }
}

static bool is_action_item(int cursor) {
    return (cursor == SET_SAVE_USER1 || cursor == SET_SAVE_USER2 || cursor == SET_RESET_STATS);
}

static void execute_action(int cursor) {
    switch (cursor) {
    case SET_SAVE_USER1:
        save_active_user_preset(0);
        break;
    case SET_SAVE_USER2:
        save_active_user_preset(1);
        break;
    case SET_RESET_STATS:
        sewu_usb_audio_reset_stats();
        ESP_LOGI(TAG, "USB stats reset");
        break;
    default:
        break;
    }
}

/* ========== Single-click helpers ========== */
static void do_btn2_single_click(void) {
    int current_page = g_sewu_state.ui_page;
    if (current_page == 1) {
        int cur = g_sewu_state.ui_settings_cursor;
        if (is_action_item(cur)) {
            execute_action(cur);
        } else if (g_sewu_state.ui_settings_editing) {
            g_sewu_state.ui_settings_editing = false;
        } else {
            g_sewu_state.ui_settings_editing = true;
        }
    } else if (current_page == 2) {
        g_sewu_state.ui_page = 0;
    } else {
        int next = g_sewu_state.preset_index + 1;
        if (next >= PRESET_COUNT) next = 0;
        apply_preset_index(next);
    }
    g_sewu_state.last_input_ms = (uint32_t)esp_log_timestamp();
}

static void do_btn2_double_click(void) {
    // Toggle mute: if volume > 0 save & mute, else restore
    if (g_sewu_state.volume_percent > 0) {
        g_sewu_state.prev_volume_percent = g_sewu_state.volume_percent;
        g_sewu_state.volume_percent = 0;
    } else {
        g_sewu_state.volume_percent = g_sewu_state.prev_volume_percent;
    }
    ESP_LOGI(TAG, "BTN2 double: volume %d%%", g_sewu_state.volume_percent);
    g_sewu_state.last_input_ms = (uint32_t)esp_log_timestamp();
}

static void do_k0_single_click(void) {
    g_sewu_state.source_mode = (g_sewu_state.source_mode == 0) ? 1 : 0;
    g_sewu_state.tone_enabled = false;
    g_sewu_state.last_input_ms = (uint32_t)esp_log_timestamp();
}

static void do_k0_double_click(void) {
    g_sewu_state.dsp_bypass = !g_sewu_state.dsp_bypass;
    ESP_LOGI(TAG, "K0 double: DSP bypass %s",
             g_sewu_state.dsp_bypass ? "ON" : "OFF");
    g_sewu_state.last_input_ms = (uint32_t)esp_log_timestamp();
}

/* ---------- Public API ---------- */

void sewu_input_init(void) {
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << SEWU_PIN_ENC_SW) |
                        (1ULL << SEWU_PIN_KEY_K0) |
                        (1ULL << SEWU_PIN_BTN2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << SEWU_PIN_ENC_A) | (1ULL << SEWU_PIN_ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&enc_cfg);

    int a = gpio_get_level(SEWU_PIN_ENC_A) ? 1 : 0;
    int b = gpio_get_level(SEWU_PIN_ENC_B) ? 1 : 0;
    s_last_ab = (uint8_t)((a << 1) | b);

    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        gpio_isr_handler_add(SEWU_PIN_ENC_A, encoder_isr_handler, (void*)SEWU_PIN_ENC_A);
        gpio_isr_handler_add(SEWU_PIN_ENC_B, encoder_isr_handler, (void*)SEWU_PIN_ENC_B);
    }

    g_sewu_state.active_control_index = 0;
    g_sewu_state.ui_settings_cursor = 0;
    g_sewu_state.ui_settings_editing = false;

    if (g_sewu_state.preset_index < 0 || g_sewu_state.preset_index >= PRESET_COUNT) {
        g_sewu_state.preset_index = PRESET_USER1;
    }

    ESP_LOGI(TAG, "input ready (encoder ISR enabled)");
}

void sewu_input_update(void) {
    uint32_t now = (uint32_t)esp_log_timestamp();
    int current_page = g_sewu_state.ui_page;

    /* ---- Encoder rotation ---- */
    int step = read_encoder_step();
    if (step != 0) {
        if (current_page == 1) {
            if (g_sewu_state.ui_settings_editing) {
                apply_settings_delta(g_sewu_state.ui_settings_cursor, step);
            } else {
                int c = g_sewu_state.ui_settings_cursor + step;
                g_sewu_state.ui_settings_cursor = clamp_int(c, 0, SET_COUNT - 1);
            }
        } else if (current_page == 2) {
            int mode = g_sewu_state.visualizer_mode + step;
            while (mode < 0) mode += 4;
            while (mode > 3) mode -= 4;
            g_sewu_state.visualizer_mode = mode;
        } else {
            g_sewu_state.volume_percent = clamp_int(g_sewu_state.volume_percent + step, 0, 100);
        }
        g_sewu_state.last_input_ms = now;
    }

    /* ---- Encoder switch (SW) ---- */
    {
        bool sw_now = gpio_get_level(SEWU_PIN_ENC_SW) ? true : false;

        if (s_last_sw && !sw_now) {
            s_sw_down_ms = now;
            s_sw_long_consumed = false;
        }

        if (!sw_now && !s_sw_long_consumed && (now - s_sw_down_ms >= ENC_LONG_MS)) {
            s_sw_long_consumed = true;
            if (current_page == 1 || current_page == 2 || current_page == 3) {
                g_sewu_state.ui_settings_editing = false;
                g_sewu_state.ui_page = 0;
            } else {
                g_sewu_state.ui_settings_cursor = 0;
                g_sewu_state.ui_settings_editing = false;
                g_sewu_state.ui_page = 3;
            }
            g_sewu_state.last_input_ms = now;
        }

        if (!s_last_sw && sw_now && !s_sw_long_consumed) {
            if (current_page == 1) {
                int cur = g_sewu_state.ui_settings_cursor;
                if (is_action_item(cur)) {
                    execute_action(cur);
                } else if (g_sewu_state.ui_settings_editing) {
                    g_sewu_state.ui_settings_editing = false;
                } else {
                    g_sewu_state.ui_settings_editing = true;
                }
            } else {
                g_sewu_state.limiter_enabled = !g_sewu_state.limiter_enabled;
            }
            g_sewu_state.last_input_ms = now;
        }
        s_last_sw = sw_now;
    }

    /* ---- BTN2 — with double-press (release-count based) ---- */
    {
        bool btn2_now = gpio_get_level(SEWU_PIN_BTN2) ? true : false;

        if (s_last_btn2 && !btn2_now) {
            // Button pressed: check if it follows a previous release quickly
            s_btn2_down_ms = now;
            s_btn2_long_consumed = false;

            if (s_btn2_release_count == 1 && (now - s_btn2_last_release_ms <= BTN2_DOUBLE_MS)) {
                // This is the second press of a double-click
                // Will be handled on release
            }
        }

        if (!btn2_now && !s_btn2_long_consumed && (now - s_btn2_down_ms >= BTN2_LONG_MS)) {
            s_btn2_long_consumed = true;
            s_btn2_release_count = 0; // cancel double sequence
            if (current_page == 1 || current_page == 2 || current_page == 3) {
                g_sewu_state.ui_settings_editing = false;
                g_sewu_state.ui_page = 0;
            } else {
                g_sewu_state.ui_settings_cursor = 0;
                g_sewu_state.ui_settings_editing = false;
                g_sewu_state.ui_page = 1;
            }
            g_sewu_state.last_input_ms = now;
        }

        if (!s_last_btn2 && btn2_now && !s_btn2_long_consumed) {
            // Button released
            if (s_btn2_release_count == 1 && (now - s_btn2_last_release_ms <= BTN2_DOUBLE_MS)) {
                // Second release within double time = DOUBLE CLICK!
                s_btn2_release_count = 0;
                do_btn2_double_click();
            } else {
                // First release: record time, set count, wait for possible second press
                s_btn2_last_release_ms = now;
                s_btn2_release_count = 1;
            }
        }

        s_last_btn2 = btn2_now;

        // Check timeout: if only one release so far, execute single click
        if (s_btn2_release_count == 1 && (now - s_btn2_last_release_ms > BTN2_DOUBLE_MS)) {
            s_btn2_release_count = 0;
            do_btn2_single_click();
        }
    }

    /* ---- K0 — with double-press (release-count based) ---- */
    {
        bool k0_now = gpio_get_level(SEWU_PIN_KEY_K0) ? true : false;

        if (s_last_k0 && !k0_now) {
            s_k0_down_ms = now;
            s_k0_long_consumed = false;
        }

        if (!k0_now && !s_k0_long_consumed && (now - s_k0_down_ms >= K0_LONG_MS)) {
            s_k0_long_consumed = true;
            s_k0_release_count = 0;
            if (current_page == 2) {
                g_sewu_state.ui_page = 0;
            } else {
                g_sewu_state.ui_page = 2;
            }
            g_sewu_state.last_input_ms = now;
        }

        if (!s_last_k0 && k0_now && !s_k0_long_consumed) {
            if (s_k0_release_count == 1 && (now - s_k0_last_release_ms <= K0_DOUBLE_MS)) {
                // Double click!
                s_k0_release_count = 0;
                do_k0_double_click();
            } else {
                s_k0_last_release_ms = now;
                s_k0_release_count = 1;
            }
        }

        s_last_k0 = k0_now;

        if (s_k0_release_count == 1 && (now - s_k0_last_release_ms > K0_DOUBLE_MS)) {
            s_k0_release_count = 0;
            do_k0_single_click();
        }
    }
}