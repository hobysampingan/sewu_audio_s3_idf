#include "sewu_app_state.h"

#include <string.h>

sewu_app_state_t g_sewu_state;

void sewu_app_state_init_defaults(void) {
    memset(&g_sewu_state, 0, sizeof(g_sewu_state));

    g_sewu_state.volume_percent = 60;
    g_sewu_state.balance_percent = 0;
    g_sewu_state.master_gain_percent = 100;
    g_sewu_state.bass_db = 0;
    g_sewu_state.low_mid_db = 0;
    g_sewu_state.mid_db = 0;
    g_sewu_state.high_mid_db = 0;
    g_sewu_state.treble_db = 0;
    g_sewu_state.tone_hz = 440.0f;
    g_sewu_state.tone_enabled = false;

    g_sewu_state.limiter_enabled = true;
    g_sewu_state.source_mode = 0;
    g_sewu_state.active_source = 0;
    g_sewu_state.active_control_index = 0;
    g_sewu_state.preset_index = 0;
    g_sewu_state.performance_profile = 2;
    g_sewu_state.visualizer_mode = 0;
    g_sewu_state.vis_active_bands = 16;
    g_sewu_state.ui_page = 0;

    g_sewu_state.backlight_percent = 100;
    g_sewu_state.backlight_dim_percent = 25;
    g_sewu_state.backlight_standby_percent = 5;
    g_sewu_state.auto_dim_timeout_ms = 15000;
    g_sewu_state.standby_timeout_ms = 45000;

    g_sewu_state.usb_target_fill_percent = 25;
    g_sewu_state.usb_health_state = 2;
    g_sewu_state.usb_host_volume_percent = 100;
    g_sewu_state.usb_host_muted = false;
    g_sewu_state.usb_last_reset_ms = 0;
}
