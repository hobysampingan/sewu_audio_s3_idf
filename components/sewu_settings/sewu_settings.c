#include "sewu_settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "sewu_settings";

static nvs_handle_t s_nvs;
static bool s_ready;
static uint32_t s_last_save_ms;

static int s_saved_volume = -1;
static float s_saved_tone = -1.0f;
static bool s_saved_tone_enabled;
static int s_saved_bass;
static int s_saved_low_mid;
static int s_saved_mid;
static int s_saved_high_mid;
static int s_saved_treble;
static int s_saved_master_gain = 100;
static int s_saved_balance;
static bool s_saved_limiter = true;
static int s_saved_source_mode;
static int s_saved_preset_index;
static int s_saved_performance_profile = 1;
static int s_saved_visualizer_mode;
static int s_saved_backlight_percent = 100;
static int s_saved_backlight_dim_percent = 25;
static int s_saved_backlight_standby_percent = 5;
static uint32_t s_saved_auto_dim_timeout_ms = 15000;
static uint32_t s_saved_standby_timeout_ms = 45000;
static bool s_has_snapshot;

static sewu_eq_preset_t s_user_presets[2] = {
    {.bass_db = 2, .low_mid_db = 1, .mid_db = 0, .high_mid_db = 1, .treble_db = 2, .master_gain_percent = 102, .balance_percent = 0, .limiter_enabled = true},
    {.bass_db = 1, .low_mid_db = 0, .mid_db = 0, .high_mid_db = 1, .treble_db = 2, .master_gain_percent = 95, .balance_percent = 0, .limiter_enabled = true},
};

static int clamp_int(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static bool nvs_get_i32_default(const char *key, int32_t *out) {
    int32_t val = 0;
    esp_err_t err = nvs_get_i32(s_nvs, key, &val);
    if (err != ESP_OK) {
        return false;
    }
    *out = val;
    return true;
}

static bool nvs_get_u32_default(const char *key, uint32_t *out) {
    uint32_t val = 0;
    esp_err_t err = nvs_get_u32(s_nvs, key, &val);
    if (err != ESP_OK) {
        return false;
    }
    *out = val;
    return true;
}

static bool nvs_get_u8_default(const char *key, uint8_t *out) {
    uint8_t val = 0;
    esp_err_t err = nvs_get_u8(s_nvs, key, &val);
    if (err != ESP_OK) {
        return false;
    }
    *out = val;
    return true;
}

static bool nvs_get_f32_default(const char *key, float *out) {
    uint32_t u = 0;
    if (!nvs_get_u32_default(key, &u)) {
        return false;
    }
    memcpy(out, &u, sizeof(float));
    return true;
}

static void nvs_set_f32(const char *key, float v) {
    uint32_t u = 0;
    memcpy(&u, &v, sizeof(float));
    nvs_set_u32(s_nvs, key, u);
}

static void load_user_presets(void) {
    for (int i = 0; i < 2; ++i) {
        const char *suffix = (i == 0) ? "1" : "2";
        char key[16] = {0};
        int32_t i32 = 0;
        uint8_t u8 = 0;

        snprintf(key, sizeof(key), "u%s_bass", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].bass_db = i32;
        snprintf(key, sizeof(key), "u%s_lmid", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].low_mid_db = i32;
        snprintf(key, sizeof(key), "u%s_mid", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].mid_db = i32;
        snprintf(key, sizeof(key), "u%s_hmid", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].high_mid_db = i32;
        snprintf(key, sizeof(key), "u%s_treb", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].treble_db = i32;
        snprintf(key, sizeof(key), "u%s_mg", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].master_gain_percent = i32;
        snprintf(key, sizeof(key), "u%s_bal", suffix);
        if (nvs_get_i32_default(key, &i32)) s_user_presets[i].balance_percent = i32;
        snprintf(key, sizeof(key), "u%s_lim", suffix);
        if (nvs_get_u8_default(key, &u8)) s_user_presets[i].limiter_enabled = (u8 != 0);

        s_user_presets[i].bass_db = clamp_int(s_user_presets[i].bass_db, -12, 12);
        s_user_presets[i].low_mid_db = clamp_int(s_user_presets[i].low_mid_db, -12, 12);
        s_user_presets[i].mid_db = clamp_int(s_user_presets[i].mid_db, -12, 12);
        s_user_presets[i].high_mid_db = clamp_int(s_user_presets[i].high_mid_db, -12, 12);
        s_user_presets[i].treble_db = clamp_int(s_user_presets[i].treble_db, -12, 12);
        s_user_presets[i].master_gain_percent = clamp_int(s_user_presets[i].master_gain_percent, 50, 150);
        s_user_presets[i].balance_percent = clamp_int(s_user_presets[i].balance_percent, -100, 100);
    }
}

static void snapshot_current(void) {
    s_saved_volume = g_sewu_state.volume_percent;
    s_saved_tone = g_sewu_state.tone_hz;
    s_saved_tone_enabled = g_sewu_state.tone_enabled;
    s_saved_bass = g_sewu_state.bass_db;
    s_saved_low_mid = g_sewu_state.low_mid_db;
    s_saved_mid = g_sewu_state.mid_db;
    s_saved_high_mid = g_sewu_state.high_mid_db;
    s_saved_treble = g_sewu_state.treble_db;
    s_saved_master_gain = g_sewu_state.master_gain_percent;
    s_saved_balance = g_sewu_state.balance_percent;
    s_saved_limiter = g_sewu_state.limiter_enabled;
    s_saved_source_mode = g_sewu_state.source_mode;
    s_saved_preset_index = g_sewu_state.preset_index;
    s_saved_performance_profile = g_sewu_state.performance_profile;
    s_saved_visualizer_mode = g_sewu_state.visualizer_mode;
    s_saved_backlight_percent = g_sewu_state.backlight_percent;
    s_saved_backlight_dim_percent = g_sewu_state.backlight_dim_percent;
    s_saved_backlight_standby_percent = g_sewu_state.backlight_standby_percent;
    s_saved_auto_dim_timeout_ms = g_sewu_state.auto_dim_timeout_ms;
    s_saved_standby_timeout_ms = g_sewu_state.standby_timeout_ms;
    s_has_snapshot = true;
}

void sewu_settings_init(void) {
    esp_err_t err = nvs_open("sewu_audio", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %d", (int)err);
        return;
    }
    s_ready = true;

    int32_t i32 = 0;
    uint32_t u32 = 0;
    uint8_t u8 = 0;
    float f32 = 0.0f;

    if (nvs_get_i32_default("vol", &i32)) g_sewu_state.volume_percent = i32;
    if (nvs_get_f32_default("tone_hz", &f32)) g_sewu_state.tone_hz = f32;
    if (nvs_get_u8_default("tone_en", &u8)) g_sewu_state.tone_enabled = (u8 != 0);

    if (nvs_get_i32_default("bass_db", &i32)) g_sewu_state.bass_db = i32;
    if (nvs_get_i32_default("lmid_db", &i32)) g_sewu_state.low_mid_db = i32;
    if (nvs_get_i32_default("mid_db", &i32)) g_sewu_state.mid_db = i32;
    if (nvs_get_i32_default("hmid_db", &i32)) g_sewu_state.high_mid_db = i32;
    if (nvs_get_i32_default("treble_db", &i32)) g_sewu_state.treble_db = i32;
    if (nvs_get_i32_default("mgain", &i32)) g_sewu_state.master_gain_percent = i32;
    if (nvs_get_i32_default("bal", &i32)) g_sewu_state.balance_percent = i32;
    if (nvs_get_u8_default("lim_en", &u8)) g_sewu_state.limiter_enabled = (u8 != 0);

    if (nvs_get_i32_default("src_mode", &i32)) g_sewu_state.source_mode = i32;
    if (nvs_get_i32_default("preset_idx", &i32)) g_sewu_state.preset_index = i32;
    if (nvs_get_i32_default("perf_prof", &i32)) g_sewu_state.performance_profile = i32;
    if (nvs_get_i32_default("viz_mode", &i32)) g_sewu_state.visualizer_mode = i32;

    if (nvs_get_i32_default("bl_pct", &i32)) g_sewu_state.backlight_percent = i32;
    if (nvs_get_i32_default("bl_dim_pct", &i32)) g_sewu_state.backlight_dim_percent = i32;
    if (nvs_get_i32_default("bl_stby_pct", &i32)) g_sewu_state.backlight_standby_percent = i32;
    if (nvs_get_u32_default("dim_ms", &u32)) g_sewu_state.auto_dim_timeout_ms = u32;
    if (nvs_get_u32_default("stby_ms", &u32)) g_sewu_state.standby_timeout_ms = u32;

    load_user_presets();

    g_sewu_state.volume_percent = clamp_int(g_sewu_state.volume_percent, 0, 100);
    g_sewu_state.bass_db = clamp_int(g_sewu_state.bass_db, -12, 12);
    g_sewu_state.low_mid_db = clamp_int(g_sewu_state.low_mid_db, -12, 12);
    g_sewu_state.mid_db = clamp_int(g_sewu_state.mid_db, -12, 12);
    g_sewu_state.high_mid_db = clamp_int(g_sewu_state.high_mid_db, -12, 12);
    g_sewu_state.treble_db = clamp_int(g_sewu_state.treble_db, -12, 12);
    g_sewu_state.master_gain_percent = clamp_int(g_sewu_state.master_gain_percent, 50, 150);
    g_sewu_state.balance_percent = clamp_int(g_sewu_state.balance_percent, -100, 100);
    g_sewu_state.source_mode = clamp_int(g_sewu_state.source_mode, 0, 2);
    g_sewu_state.preset_index = clamp_int(g_sewu_state.preset_index, 0, 5);
    g_sewu_state.performance_profile = clamp_int(g_sewu_state.performance_profile, 0, 2);
    g_sewu_state.visualizer_mode = clamp_int(g_sewu_state.visualizer_mode, 0, 2);
    g_sewu_state.vis_active_bands = (g_sewu_state.performance_profile >= 2) ? 24 : 16;

    g_sewu_state.backlight_percent = clamp_int(g_sewu_state.backlight_percent, 5, 100);
    g_sewu_state.backlight_dim_percent = clamp_int(g_sewu_state.backlight_dim_percent, 1, 100);
    g_sewu_state.backlight_standby_percent = clamp_int(g_sewu_state.backlight_standby_percent, 0, 100);
    if (g_sewu_state.auto_dim_timeout_ms < 5000U) g_sewu_state.auto_dim_timeout_ms = 5000U;
    if (g_sewu_state.standby_timeout_ms < 5000U) g_sewu_state.standby_timeout_ms = 5000U;

    if (g_sewu_state.source_mode == 1) {
        g_sewu_state.tone_enabled = false;
    }

    snapshot_current();

    ESP_LOGI(TAG, "loaded src=%d preset=%d profile=%d viz=%d bl=%d", g_sewu_state.source_mode, g_sewu_state.preset_index, g_sewu_state.performance_profile, g_sewu_state.visualizer_mode, g_sewu_state.backlight_percent);
}

void sewu_settings_update(void) {
    if (!s_ready || !s_has_snapshot) {
        return;
    }

    bool changed =
        (g_sewu_state.volume_percent != s_saved_volume) ||
        (fabsf(g_sewu_state.tone_hz - s_saved_tone) > 0.1f) ||
        (g_sewu_state.tone_enabled != s_saved_tone_enabled) ||
        (g_sewu_state.bass_db != s_saved_bass) ||
        (g_sewu_state.low_mid_db != s_saved_low_mid) ||
        (g_sewu_state.mid_db != s_saved_mid) ||
        (g_sewu_state.high_mid_db != s_saved_high_mid) ||
        (g_sewu_state.treble_db != s_saved_treble) ||
        (g_sewu_state.master_gain_percent != s_saved_master_gain) ||
        (g_sewu_state.balance_percent != s_saved_balance) ||
        (g_sewu_state.limiter_enabled != s_saved_limiter) ||
        (g_sewu_state.source_mode != s_saved_source_mode) ||
        (g_sewu_state.preset_index != s_saved_preset_index) ||
        (g_sewu_state.performance_profile != s_saved_performance_profile) ||
        (g_sewu_state.visualizer_mode != s_saved_visualizer_mode) ||
        (g_sewu_state.backlight_percent != s_saved_backlight_percent) ||
        (g_sewu_state.backlight_dim_percent != s_saved_backlight_dim_percent) ||
        (g_sewu_state.backlight_standby_percent != s_saved_backlight_standby_percent) ||
        (g_sewu_state.auto_dim_timeout_ms != s_saved_auto_dim_timeout_ms) ||
        (g_sewu_state.standby_timeout_ms != s_saved_standby_timeout_ms);

    uint32_t now = (uint32_t)esp_log_timestamp();
    if (!changed) {
        return;
    }

    if (g_sewu_state.ui_page == 1 && g_sewu_state.ui_settings_editing) {
        /* Defer NVS commit while the user is actively editing settings.
           This prevents long flash write latency from blocking the service task. */
        return;
    }

    if ((now - s_last_save_ms) < 1500U) {
        return;
    }

    nvs_set_i32(s_nvs, "vol", g_sewu_state.volume_percent);
    nvs_set_f32("tone_hz", g_sewu_state.tone_hz);
    nvs_set_u8(s_nvs, "tone_en", g_sewu_state.tone_enabled ? 1 : 0);

    nvs_set_i32(s_nvs, "bass_db", g_sewu_state.bass_db);
    nvs_set_i32(s_nvs, "lmid_db", g_sewu_state.low_mid_db);
    nvs_set_i32(s_nvs, "mid_db", g_sewu_state.mid_db);
    nvs_set_i32(s_nvs, "hmid_db", g_sewu_state.high_mid_db);
    nvs_set_i32(s_nvs, "treble_db", g_sewu_state.treble_db);
    nvs_set_i32(s_nvs, "mgain", g_sewu_state.master_gain_percent);
    nvs_set_i32(s_nvs, "bal", g_sewu_state.balance_percent);
    nvs_set_u8(s_nvs, "lim_en", g_sewu_state.limiter_enabled ? 1 : 0);

    nvs_set_i32(s_nvs, "src_mode", g_sewu_state.source_mode);
    nvs_set_i32(s_nvs, "preset_idx", g_sewu_state.preset_index);
    nvs_set_i32(s_nvs, "perf_prof", g_sewu_state.performance_profile);
    nvs_set_i32(s_nvs, "viz_mode", g_sewu_state.visualizer_mode);

    nvs_set_i32(s_nvs, "bl_pct", g_sewu_state.backlight_percent);
    nvs_set_i32(s_nvs, "bl_dim_pct", g_sewu_state.backlight_dim_percent);
    nvs_set_i32(s_nvs, "bl_stby_pct", g_sewu_state.backlight_standby_percent);
    nvs_set_u32(s_nvs, "dim_ms", g_sewu_state.auto_dim_timeout_ms);
    nvs_set_u32(s_nvs, "stby_ms", g_sewu_state.standby_timeout_ms);

    nvs_commit(s_nvs);

    snapshot_current();
    s_last_save_ms = now;
    ESP_LOGI(TAG, "settings saved");
}

void sewu_settings_get_user_preset(int slot, sewu_eq_preset_t *out) {
    if (out == NULL) return;
    if (slot < 0) slot = 0;
    if (slot > 1) slot = 1;
    *out = s_user_presets[slot];
}

void sewu_settings_set_user_preset(int slot, const sewu_eq_preset_t *in) {
    if (!s_ready || in == NULL) {
        return;
    }

    if (slot < 0) slot = 0;
    if (slot > 1) slot = 1;

    s_user_presets[slot] = *in;
    s_user_presets[slot].bass_db = clamp_int(s_user_presets[slot].bass_db, -12, 12);
    s_user_presets[slot].low_mid_db = clamp_int(s_user_presets[slot].low_mid_db, -12, 12);
    s_user_presets[slot].mid_db = clamp_int(s_user_presets[slot].mid_db, -12, 12);
    s_user_presets[slot].high_mid_db = clamp_int(s_user_presets[slot].high_mid_db, -12, 12);
    s_user_presets[slot].treble_db = clamp_int(s_user_presets[slot].treble_db, -12, 12);
    s_user_presets[slot].master_gain_percent = clamp_int(s_user_presets[slot].master_gain_percent, 50, 150);
    s_user_presets[slot].balance_percent = clamp_int(s_user_presets[slot].balance_percent, -100, 100);

    const char *suffix = (slot == 0) ? "1" : "2";
    char key[16] = {0};

    snprintf(key, sizeof(key), "u%s_bass", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].bass_db);
    snprintf(key, sizeof(key), "u%s_lmid", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].low_mid_db);
    snprintf(key, sizeof(key), "u%s_mid", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].mid_db);
    snprintf(key, sizeof(key), "u%s_hmid", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].high_mid_db);
    snprintf(key, sizeof(key), "u%s_treb", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].treble_db);
    snprintf(key, sizeof(key), "u%s_mg", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].master_gain_percent);
    snprintf(key, sizeof(key), "u%s_bal", suffix);
    nvs_set_i32(s_nvs, key, s_user_presets[slot].balance_percent);
    snprintf(key, sizeof(key), "u%s_lim", suffix);
    nvs_set_u8(s_nvs, key, s_user_presets[slot].limiter_enabled ? 1 : 0);

    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "user preset %d saved", slot + 1);
}
