/* sewu_ui.c — Modern Minimalist LCD UI (ST7789 320x240) for Sewu Audio S3 */

#include "sewu_ui.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "sewu_app_state.h"
#include "sewu_gfx.h"
#include "sewu_usb_audio.h"

static const char *TAG = "sewu_ui";

/* ================================================================
   HARDWARE / DRIVER CONSTANTS
   ================================================================ */
#define TFT_HOST          SPI2_HOST
#define TFT_WIDTH         320
#define TFT_HEIGHT        240
#define TFT_LCD_CMD_BITS  8
#define TFT_LCD_PARAM_BITS 8
#define TFT_PCLK_HZ      (40 * 1000 * 1000)

#define BL_PWM_TIMER      LEDC_TIMER_0
#define BL_PWM_MODE       LEDC_LOW_SPEED_MODE
#define BL_PWM_CHANNEL    LEDC_CHANNEL_0
#define BL_PWM_RES        LEDC_TIMER_8_BIT
#define BL_PWM_HZ         12000

#define UI_FPS_BASE_MS    100U
#define FILL_CHUNK_ROWS   16

/* ================================================================
   MODERN LAYOUT CONSTANTS (Redesigned minimalist layout)
   ================================================================ */
#define HDR_H       32
#define FTR_H       20
#define FTR_Y       (TFT_HEIGHT - FTR_H)
#define CONTENT_Y   HDR_H
#define CONTENT_H   (TFT_HEIGHT - HDR_H - FTR_H)

/* HOME PAGE: LEFT PANEL - FULL HEIGHT L/R INPUT LEVELS + VOLUME HORIZONTAL */
#define LEFT_PANEL_X      4
#define LEFT_PANEL_W      155
#define LEFT_PANEL_Y      CONTENT_Y
#define LEFT_PANEL_H      CONTENT_H

/* Three items side by side: L bar, R bar, Volume bar - ramping & lebih panjang */
#define LEFT_ITEM_W       38             /* width per item (lebih ramping) */
#define LEFT_ITEM_SPACE   10             /* space between items */
#define LEFT_ITEMS_TOTAL  (LEFT_ITEM_W * 3 + LEFT_ITEM_SPACE * 2)
#define LEFT_ITEMS_OFFSET ((LEFT_PANEL_W - LEFT_ITEMS_TOTAL) / 2)

/* L bar position - mulai dari header sampai footer */
#define INPUT_L_X         (LEFT_PANEL_X + LEFT_ITEMS_OFFSET)
#define INPUT_VIZ_BAR_W   36             /* lebih ramping */
#define INPUT_VIZ_TOP     (LEFT_PANEL_Y + 2)
#define INPUT_VIZ_H       (CONTENT_H - 12)  /* full height panel */

/* R bar position */
#define INPUT_R_X         (INPUT_L_X + LEFT_ITEM_W + LEFT_ITEM_SPACE)

/* Volume bar position (rightmost item) */
#define VOL_X             (INPUT_R_X + LEFT_ITEM_W + LEFT_ITEM_SPACE)
#define VOL_BAR_W         36
#define VOL_SECTION_Y     INPUT_VIZ_TOP

/* Right panel: Preset, Limiter, DSP Bypass, Host Vol, EQ bars */
#define RIGHT_PANEL_X     161
#define RIGHT_PANEL_W     155
#define RIGHT_PANEL_Y     CONTENT_Y
#define RIGHT_PANEL_H     CONTENT_H

/* Status section - PRESET, LIMITER, DSP BYP (inline) */
#define PRESET_SEC_Y      (RIGHT_PANEL_Y + 2)
#define STATUS_LABEL_X    (RIGHT_PANEL_X + 8)
#define STATUS_VAL_X      (RIGHT_PANEL_X + 80)

#define PRESET_ROW_Y      (PRESET_SEC_Y + 2)
#define LIMIT_ROW_Y       (PRESET_ROW_Y + 14)
#define DSPBP_ROW_Y       (LIMIT_ROW_Y + 14)
#define STATUS_SEC_H      (DSPBP_ROW_Y + 14 - PRESET_SEC_Y + 4)  /* ~44 */

/* EQ section + HOST VOL bar (6 bar sejajar) - full height */
#define EQ_SEC_Y          (PRESET_SEC_Y + STATUS_SEC_H + 2)
#define EQ_SEC_H          120                /* more height */

#define EQ_BAR_W          14
#define EQ_BAR_H          110               /* much taller */
#define EQ_CENTER_X       (RIGHT_PANEL_X + RIGHT_PANEL_W / 2)

/* 6 bar evenly spread in 155px panel */
#define EQ_BAR_SPACING    10
#define EQ_TOTAL_BARS     6
#define EQ_TOTAL_W        (EQ_TOTAL_BARS * EQ_BAR_W + (EQ_TOTAL_BARS - 1) * EQ_BAR_SPACING)
#define EQ_START_X        (EQ_CENTER_X - EQ_TOTAL_W / 2)

/* Settings page: Compact item rows */
#define SET_VISIBLE     6
#define SET_ITEM_H      28
#define SET_LABEL_X     12
#define SET_VALUE_X     220
#define SET_BAR_Y_OFFSET 16

/* ================================================================
   RUNTIME STATE
   ================================================================ */
static bool s_backlight_ready;
static int  s_last_backlight_target = -1;
static uint32_t s_last_ui_log_ms;

static bool s_tft_ready;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint32_t s_last_ui_ms;
static int s_last_page = -1;
static bool s_home_values_dirty = true;

/* ================================================================
   MODERN COLOR PALETTE (Minimalist: Neutral + Accent)
   ================================================================ */
static uint16_t C_BG;           /* Deep charcoal background */
static uint16_t C_SURFACE;      /* Subtle surface layer */
static uint16_t C_SURFACE_ALT;  /* Alternate surface for depth */
static uint16_t C_BORDER;       /* Soft divider */
static uint16_t C_TEXT_PRIMARY;   /* Main text */
static uint16_t C_TEXT_SECONDARY; /* Dim text */
static uint16_t C_ACCENT_CYAN;  /* Primary accent */
static uint16_t C_ACCENT_GOLD;  /* Secondary accent */
static uint16_t C_STATUS_OK;
static uint16_t C_STATUS_WARN;
static uint16_t C_STATUS_ERROR;
static uint16_t C_SPECTRUM_LOW;
static uint16_t C_SPECTRUM_MID;
static uint16_t C_SPECTRUM_HIGH;
static uint16_t C_SPECTRUM_PEAK;

/* ================================================================
   UTILITY
   ================================================================ */
static int clamp_int(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

#define inv_c(r, g, b) gfx_color565(255-(r), 255-(g), 255-(b))

static void init_palette(void) {
    /* Modern minimalist palette */
    C_BG              = inv_c(12,  12,  14);   /* Almost black background */
    C_SURFACE         = inv_c(22,  22,  26);   /* Card surface */
    C_SURFACE_ALT     = inv_c(18,  18,  22);   /* Alt surface for contrast */
    C_BORDER          = inv_c(45,  45,  55);   /* Soft divider */
    C_TEXT_PRIMARY    = inv_c(235, 235, 240);  /* Nearly white text */
    C_TEXT_SECONDARY  = inv_c(130, 130, 145);  /* Muted secondary */
    C_ACCENT_CYAN     = inv_c(20,  200, 200);  /* Vibrant cyan accent */
    C_ACCENT_GOLD     = inv_c(255, 180, 40);   /* Warm gold accent */
    C_STATUS_OK       = inv_c(80,  220, 100);  /* Soft green */
    C_STATUS_WARN     = inv_c(255, 180, 40);   /* Amber warning */
    C_STATUS_ERROR    = inv_c(255, 100, 100);  /* Soft red */
    C_SPECTRUM_LOW    = inv_c(80,  200, 150);  /* Teal */
    C_SPECTRUM_MID    = inv_c(255, 180, 40);   /* Amber */
    C_SPECTRUM_HIGH   = inv_c(255, 100, 100);  /* Coral */
    C_SPECTRUM_PEAK   = inv_c(255, 255, 255);  /* White peak */
}

/* ================================================================
   LABEL HELPERS
   ================================================================ */
static const char *health_label(int state) {
    if (state == 0) return "STABLE";
    if (state == 1) return "BUSY";
    return "RISK";
}

static const char *preset_label(int idx) {
    switch (idx) {
    case 0:  return "DEFAULT";
    case 1:  return "BASS+";
    case 2:  return "FLAT";
    case 3:  return "V-SHAPE";
    case 4:  return "PODCAST";
    case 5:  return "CLARITY";
    case 6:  return "CLASSIC";
    case 7:  return "NEURAL";
    case 8:  return "ROCK";
    case 9:  return "JAZZ";
    case 10: return "ACOUSTC";
    case 11: return "LOUDNES";
    case 12: return "USER1";
    case 13: return "USER2";
    default: return "USER";
    }
}

static const char *vis_mode_label(int mode) {
    switch (mode) {
    case 0: return "BARS";
    case 1: return "PEAKS";
    case 2: return "WAVE";
    default: return "MIRROR";
    }
}

static const char *profile_label(int profile) {
    switch (profile) {
    case 0: return "STABLE";
    case 1: return "BAL";
    default: return "MAX";
    }
}

static uint16_t health_color(int state) {
    if (state == 0) return C_STATUS_OK;
    if (state == 1) return C_STATUS_WARN;
    return C_STATUS_ERROR;
}

/* ================================================================
   MODERN UI PRIMITIVES
   ================================================================ */

/* Visualizer-style input level bar - animated column with modern styling */
static void draw_input_viz_bar(int x, int y, int w, int h, int percent, int peak_percent, uint16_t c_low, uint16_t c_mid, uint16_t c_high) {
    percent = clamp_int(percent, 0, 100);
    peak_percent = clamp_int(peak_percent, 0, 100);
    
    /* Background with rounded effect */
    gfx_fill_rect(x, y, w, h, C_SURFACE_ALT);
    gfx_draw_rect(x, y, w, h, C_BORDER);
    
    /* Calculate fill height */
    int fill_h = (h * percent) / 100;
    int fill_y = y + h - fill_h;
    
    /* Color gradient based on level */
    uint16_t fill_color;
    if (percent < 30) fill_color = c_low;
    else if (percent < 70) fill_color = c_mid;
    else fill_color = c_high;
    
    /* Draw fill from bottom with rounded top */
    if (fill_h > 0) {
        gfx_fill_rect(x + 2, fill_y + 2, w - 4, fill_h - 2, fill_color);
    }
    
    /* Draw peak indicator line */
    if (peak_percent > 0) {
        int peak_y = y + h - (h * peak_percent) / 100;
        gfx_fill_rect(x + 1, peak_y, w - 2, 2, C_SPECTRUM_PEAK);
    }
}

/* Compact horizontal progress bar with modern styling */
static void draw_mini_slider(int x, int y, int w, int h, int percent, uint16_t color) {
    percent = clamp_int(percent, 0, 100);
    gfx_draw_rect(x - 1, y - 1, w + 2, h + 2, C_BORDER);
    gfx_fill_rect(x, y, w, h, C_SURFACE_ALT);
    int fill_w = (w * percent) / 100;
    if (fill_w > 0) {
        gfx_fill_rect(x, y, fill_w, h, color);
    }
}

/* Status indicator badge */
static void draw_status_badge(int x, int y, const char *text, uint16_t color) {
    int text_w = 42;
    int text_h = 16;
    gfx_fill_round_rect(x, y, text_w, text_h, 4, C_SURFACE);
    gfx_draw_round_rect(x, y, text_w, text_h, 4, color);
    gfx_draw_text(x + 3, y + 2, text, color, C_SURFACE);
}

/* EQ bar with value label below */
static void draw_eq_bar_with_value(int x, int y, int bar_h, int max_h, int value_db, uint16_t color) {
    int h = ((value_db + 12) * max_h) / 24;  /* Scale from -12 to +12 dB */
    h = clamp_int(h, 0, max_h);
    
    /* Draw bar border */
    gfx_draw_rect(x - 1, y - 1, EQ_BAR_W + 2, max_h + 2, C_BORDER);
    
    /* Draw background */
    gfx_fill_rect(x, y, EQ_BAR_W, max_h, C_SURFACE_ALT);
    
    /* Draw fill from bottom */
    int fill_y = y + max_h - h;
    if (h > 0) {
        gfx_fill_rect(x + 1, fill_y + 1, EQ_BAR_W - 2, h - 1, color);
    }
    
    /* Draw center line (0 dB reference) */
    int center_y = y + max_h / 2;
    gfx_fill_rect(x, center_y, EQ_BAR_W, 1, C_SURFACE);
    
    /* Draw value text below */
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%+d", value_db);
    gfx_draw_text(x - 4, y + max_h + 6, val_str, color, C_BG);
}

/* ================================================================
   HOME PAGE - FULL-HEIGHT LEFT PANEL WITH 2 WIDE INPUT BARS + VOLUME
   ================================================================ */
static void draw_home_layout(void) {
    gfx_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, C_BG);
    
    /* Header: Minimal, clean */
    gfx_fill_rect(0, 0, TFT_WIDTH, HDR_H, C_SURFACE);
    gfx_draw_text(12, 8, "SEWU AUDIO S3", C_ACCENT_CYAN, C_SURFACE);
    
    /* Status badge in header */
    draw_status_badge(250, 8, health_label(g_sewu_state.usb_health_state), health_color(g_sewu_state.usb_health_state));
    
    /* Divider */
    gfx_fill_rect(0, HDR_H - 1, TFT_WIDTH, 1, C_BORDER);
    
    /* LEFT PANEL: L/R INPUT BARS + VOLUME SIDE BY SIDE (full height) */
    gfx_fill_rect(LEFT_PANEL_X - 1, LEFT_PANEL_Y - 1, LEFT_PANEL_W + 2, LEFT_PANEL_H + 2, C_SURFACE_ALT);
    gfx_draw_rect(LEFT_PANEL_X - 1, LEFT_PANEL_Y - 1, LEFT_PANEL_W + 2, LEFT_PANEL_H + 2, C_BORDER);
    
    /* Labels di atas bar */
    gfx_draw_text(INPUT_L_X + 6, INPUT_VIZ_TOP - 12, "L", C_TEXT_SECONDARY, C_SURFACE_ALT);
    gfx_draw_text(INPUT_R_X + 6, INPUT_VIZ_TOP - 12, "R", C_TEXT_SECONDARY, C_SURFACE_ALT);
    gfx_draw_text(VOL_X + 4, INPUT_VIZ_TOP - 12, "VOL", C_TEXT_SECONDARY, C_SURFACE_ALT);
    
    /* RIGHT PANEL: Preset, Limiter, DSP Bypass, Host Vol, EQ */
    gfx_fill_rect(RIGHT_PANEL_X - 1, RIGHT_PANEL_Y - 1, RIGHT_PANEL_W + 2, RIGHT_PANEL_H + 2, C_SURFACE);
    gfx_draw_rect(RIGHT_PANEL_X - 1, RIGHT_PANEL_Y - 1, RIGHT_PANEL_W + 2, RIGHT_PANEL_H + 2, C_BORDER);
    
    /* Footer: Instruction text, minimal */
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, FTR_H, C_SURFACE);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, 1, C_BORDER);
    gfx_draw_text(8, FTR_Y + 3, "ENC:NAV  K0:VIZ  HOLD ENC:SETTINGS", C_TEXT_SECONDARY, C_SURFACE);
}

static void update_home_values(void) {
    static int s_hv_vu_l = -1;
    static int s_hv_vu_r = -1;
    static int s_hv_vu_l_pk = -1;
    static int s_hv_vu_r_pk = -1;
    static int s_hv_volume = -1;
    static int s_hv_preset = -1;
    static int s_hv_limiter = -1;
    static bool s_hv_dsp_bypass = false;
    static int s_hv_eq[5] = {-1, -1, -1, -1, -1};
    
    bool force = s_home_values_dirty;
    s_home_values_dirty = false;
    
     /* ===== UPDATE INPUT LEVEL VISUALIZER (LEFT PANEL - 2 WIDE BARS) ===== */
     
     int vu_l = g_sewu_state.vu_left_percent;
     int vu_l_pk = g_sewu_state.vu_left_peak_percent;
     int vu_r = g_sewu_state.vu_right_percent;
     int vu_r_pk = g_sewu_state.vu_right_peak_percent;
     
     if (force || vu_l != s_hv_vu_l || vu_l_pk != s_hv_vu_l_pk) {
         s_hv_vu_l = vu_l;
         s_hv_vu_l_pk = vu_l_pk;
         
         /* Draw L channel bar */
         draw_input_viz_bar(INPUT_L_X, INPUT_VIZ_TOP, INPUT_VIZ_BAR_W, INPUT_VIZ_H,
                           vu_l, vu_l_pk, C_SPECTRUM_LOW, C_SPECTRUM_MID, C_SPECTRUM_HIGH);
     }
     
     if (force || vu_r != s_hv_vu_r || vu_r_pk != s_hv_vu_r_pk) {
         s_hv_vu_r = vu_r;
         s_hv_vu_r_pk = vu_r_pk;
         
         /* Draw R channel bar */
         draw_input_viz_bar(INPUT_R_X, INPUT_VIZ_TOP, INPUT_VIZ_BAR_W, INPUT_VIZ_H,
                           vu_r, vu_r_pk, C_SPECTRUM_LOW, C_SPECTRUM_MID, C_SPECTRUM_HIGH);
     }
     
     /* ===== UPDATE VOLUME (VERTICAL BAR IN RIGHT POSITION OF LEFT PANEL) ===== */
     
     int vol = g_sewu_state.volume_percent;
     if (force || vol != s_hv_volume) {
         s_hv_volume = vol;
         
         /* Draw volume as vertical bar (similar style to L/R bars) */
         draw_input_viz_bar(VOL_X, VOL_SECTION_Y, INPUT_VIZ_BAR_W, INPUT_VIZ_H,
                           vol, 0, C_ACCENT_CYAN, C_ACCENT_CYAN, C_ACCENT_CYAN);
         
         /* Draw volume percentage text below */
         char vol_str[16];
         snprintf(vol_str, sizeof(vol_str), "%d%%", vol);
         gfx_draw_text(VOL_X + 6, VOL_SECTION_Y + INPUT_VIZ_H + 6, vol_str, C_TEXT_PRIMARY, C_SURFACE_ALT);
     }
    
    /* ===== RIGHT PANEL: PRESET (inline) ===== */
    
    int pre = g_sewu_state.preset_index;
    if (force || pre != s_hv_preset) {
        s_hv_preset = pre;
        gfx_fill_rect(STATUS_LABEL_X, PRESET_ROW_Y, STATUS_VAL_X - STATUS_LABEL_X + 40, 14, C_SURFACE);
        gfx_draw_text(STATUS_LABEL_X, PRESET_ROW_Y, "PRESET:", C_TEXT_SECONDARY, C_SURFACE);
        gfx_draw_text(STATUS_VAL_X, PRESET_ROW_Y, preset_label(pre), C_ACCENT_GOLD, C_SURFACE);
    }
    
    /* ===== RIGHT PANEL: LIMITER (inline) ===== */
    
    int lim = g_sewu_state.limiter_enabled ? 1 : 0;
    if (force || lim != s_hv_limiter) {
        s_hv_limiter = lim;
        gfx_fill_rect(STATUS_LABEL_X, LIMIT_ROW_Y, STATUS_VAL_X - STATUS_LABEL_X + 40, 14, C_SURFACE);
        gfx_draw_text(STATUS_LABEL_X, LIMIT_ROW_Y, "LIMITER:", C_TEXT_SECONDARY, C_SURFACE);
        uint16_t lim_color = lim ? C_STATUS_OK : C_TEXT_SECONDARY;
        gfx_draw_text(STATUS_VAL_X, LIMIT_ROW_Y, lim ? "ON" : "OFF", lim_color, C_SURFACE);
    }
    
    /* ===== RIGHT PANEL: DSP BYPASS (inline) ===== */
    
    bool bypass = g_sewu_state.dsp_bypass;
    if (force || bypass != s_hv_dsp_bypass) {
        s_hv_dsp_bypass = bypass;
        gfx_fill_rect(STATUS_LABEL_X, DSPBP_ROW_Y, STATUS_VAL_X - STATUS_LABEL_X + 40, 14, C_SURFACE);
        gfx_draw_text(STATUS_LABEL_X, DSPBP_ROW_Y, "DSP BYP:", C_TEXT_SECONDARY, C_SURFACE);
        uint16_t bp_color = bypass ? C_STATUS_WARN : C_TEXT_SECONDARY;
        gfx_draw_text(STATUS_VAL_X, DSPBP_ROW_Y, bypass ? "ON" : "OFF", bp_color, C_SURFACE);
    }
    
    /* ===== RIGHT PANEL: 6 BAR SEJAJAR (5 EQ + 1 HOST VOL) ===== */
    
    int eq_vals[5] = {
        g_sewu_state.bass_db,
        g_sewu_state.low_mid_db,
        g_sewu_state.mid_db,
        g_sewu_state.high_mid_db,
        g_sewu_state.treble_db
    };
    
    bool eq_changed = force;
    for (int i = 0; i < 5; i++) {
        if (eq_vals[i] != s_hv_eq[i]) eq_changed = true;
    }
    
    static int s_hv_host_vol = -1;
    int host_vol = g_sewu_state.usb_host_volume_percent;
    bool host_changed = force || (host_vol != s_hv_host_vol);
    
    if (eq_changed || host_changed) {
        /* Title */
        gfx_fill_rect(RIGHT_PANEL_X + 4, EQ_SEC_Y + 4, RIGHT_PANEL_W - 8, CONTENT_Y + CONTENT_H - EQ_SEC_Y - 8, C_SURFACE);
        gfx_draw_text(RIGHT_PANEL_X + 8, EQ_SEC_Y + 6, "EQ/HOST VOL", C_TEXT_SECONDARY, C_SURFACE);
        
        uint16_t eq_colors[] = {C_SPECTRUM_LOW, C_SPECTRUM_MID, C_ACCENT_GOLD, C_SPECTRUM_MID, C_SPECTRUM_HIGH};
        
        int bar_start_y = EQ_SEC_Y + 20;
        int max_h = CONTENT_Y + CONTENT_H - 8 - EQ_SEC_Y - 20 - 18; /* full height sampai footer */
        if (max_h < 30) max_h = 30;
        int y_bottom = bar_start_y + max_h;
        
        /* Draw 5 EQ bars */
        for (int i = 0; i < 5; i++) {
            int bar_x = EQ_START_X + i * (EQ_BAR_W + EQ_BAR_SPACING);
            draw_eq_bar_with_value(bar_x, bar_start_y, EQ_BAR_H, max_h, eq_vals[i], eq_colors[i]);
            s_hv_eq[i] = eq_vals[i];
        }
        
        /* Draw 6th bar: HOST VOL */
        int hv_x = EQ_START_X + 5 * (EQ_BAR_W + EQ_BAR_SPACING);
        s_hv_host_vol = host_vol;
        
        /* Bersihkan area + border */
        gfx_draw_rect(hv_x - 1, bar_start_y - 1, EQ_BAR_W + 2, max_h + 2, C_BORDER);
        gfx_fill_rect(hv_x, bar_start_y, EQ_BAR_W, max_h, C_SURFACE_ALT);
        
        /* Fill from bottom (0-100 scale) */
        int fill_h = (host_vol * max_h) / 100;
        if (fill_h > 0) {
            int fill_y = bar_start_y + max_h - fill_h;
            gfx_fill_rect(hv_x + 1, fill_y, EQ_BAR_W - 2, fill_h, C_ACCENT_CYAN);
        }
        
        /* Label host vol value di bawah */
        char hv_str[16];
        snprintf(hv_str, sizeof(hv_str), "%d%%", host_vol);
        gfx_draw_text(hv_x - 4, y_bottom + 6, hv_str, C_ACCENT_CYAN, C_BG);
    }
}

/* ================================================================
   SETTINGS PAGE - Compact, scannable layout
   ================================================================ */
static const char *settings_label(int idx) {
    switch (idx) {
    case 0:  return "VOLUME";
    case 1:  return "BASS";
    case 2:  return "LOW MID";
    case 3:  return "MID";
    case 4:  return "HIGH MID";
    case 5:  return "TREBLE";
    case 6:  return "GAIN";
    case 7:  return "BALANCE";
    case 8:  return "LIMITER";
    case 9:  return "PRESET";
    case 10: return "VIS MODE";
    case 11: return "PROFILE";
    case 12: return "BACKLIGHT";
    case 13: return "DIM TIME";
    case 14: return "STBY TIME";
    case 15: return "SAVE USR1";
    case 16: return "SAVE USR2";
    case 17: return "RST STATS";
    default: return "???";
    }
}

static void settings_value_str(int idx, char *out, int out_sz, int *bar_pct) {
    *bar_pct = 0;
    switch (idx) {
    case 0:  snprintf(out, out_sz, "%d%%", g_sewu_state.volume_percent); 
             *bar_pct = g_sewu_state.volume_percent; break;
    case 1:  snprintf(out, out_sz, "%+d", g_sewu_state.bass_db);
             *bar_pct = ((g_sewu_state.bass_db + 12) * 100) / 24; break;
    case 2:  snprintf(out, out_sz, "%+d", g_sewu_state.low_mid_db);
             *bar_pct = ((g_sewu_state.low_mid_db + 12) * 100) / 24; break;
    case 3:  snprintf(out, out_sz, "%+d", g_sewu_state.mid_db);
             *bar_pct = ((g_sewu_state.mid_db + 12) * 100) / 24; break;
    case 4:  snprintf(out, out_sz, "%+d", g_sewu_state.high_mid_db);
             *bar_pct = ((g_sewu_state.high_mid_db + 12) * 100) / 24; break;
    case 5:  snprintf(out, out_sz, "%+d", g_sewu_state.treble_db);
             *bar_pct = ((g_sewu_state.treble_db + 12) * 100) / 24; break;
    case 6:  snprintf(out, out_sz, "%d%%", g_sewu_state.master_gain_percent);
             *bar_pct = ((g_sewu_state.master_gain_percent - 50) * 100) / 100; break;
    case 7:  snprintf(out, out_sz, "%d", g_sewu_state.balance_percent);
             *bar_pct = ((g_sewu_state.balance_percent + 100) * 100) / 200; break;
    case 8:  snprintf(out, out_sz, "%s", g_sewu_state.limiter_enabled ? "ON" : "OFF");
             *bar_pct = g_sewu_state.limiter_enabled ? 100 : 0; break;
    case 9:  snprintf(out, out_sz, "%s", preset_label(g_sewu_state.preset_index));
             *bar_pct = (g_sewu_state.preset_index * 100) / 13; break;
    case 10: snprintf(out, out_sz, "%s", vis_mode_label(g_sewu_state.visualizer_mode));
             *bar_pct = (g_sewu_state.visualizer_mode * 100) / 3; break;
    case 11: snprintf(out, out_sz, "%s", profile_label(g_sewu_state.performance_profile));
             *bar_pct = (g_sewu_state.performance_profile * 100) / 2; break;
    case 12: snprintf(out, out_sz, "%d%%", g_sewu_state.backlight_percent);
             *bar_pct = g_sewu_state.backlight_percent; break;
    case 13: snprintf(out, out_sz, "%lus", (unsigned long)(g_sewu_state.auto_dim_timeout_ms / 1000U));
             *bar_pct = (int)(((g_sewu_state.auto_dim_timeout_ms / 1000U) - 5) * 100 / 115); break;
    case 14: snprintf(out, out_sz, "%lus", (unsigned long)(g_sewu_state.standby_timeout_ms / 1000U));
             *bar_pct = (int)(((g_sewu_state.standby_timeout_ms / 1000U) - 5) * 100 / 295); break;
    case 15: case 16: case 17: snprintf(out, out_sz, "EXEC"); *bar_pct = -1; break;
    default: snprintf(out, out_sz, "?");
    }
}

static int get_setting_value(int idx) {
    switch (idx) {
    case 0:  return g_sewu_state.volume_percent;
    case 1:  return g_sewu_state.bass_db;
    case 2:  return g_sewu_state.low_mid_db;
    case 3:  return g_sewu_state.mid_db;
    case 4:  return g_sewu_state.high_mid_db;
    case 5:  return g_sewu_state.treble_db;
    case 6:  return g_sewu_state.master_gain_percent;
    case 7:  return g_sewu_state.balance_percent;
    case 8:  return g_sewu_state.limiter_enabled ? 1 : 0;
    case 9:  return g_sewu_state.preset_index;
    case 10: return g_sewu_state.visualizer_mode;
    case 11: return g_sewu_state.performance_profile;
    case 12: return g_sewu_state.backlight_percent;
    case 13: return (int)(g_sewu_state.auto_dim_timeout_ms / 1000U);
    case 14: return (int)(g_sewu_state.standby_timeout_ms / 1000U);
    default: return 0;
    }
}

static void draw_settings_layout(void) {
    gfx_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, C_BG);
    gfx_fill_rect(0, 0, TFT_WIDTH, HDR_H, C_SURFACE);
    gfx_draw_text(12, 8, "SETTINGS", C_ACCENT_CYAN, C_SURFACE);
    gfx_fill_rect(0, HDR_H - 1, TFT_WIDTH, 1, C_BORDER);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, FTR_H, C_SURFACE);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, 1, C_BORDER);
    gfx_draw_text(8, FTR_Y + 3, "ROT:ADJ  PRESS:EDIT  HOLD:BACK", C_TEXT_SECONDARY, C_SURFACE);
}

static void draw_settings_item(int idx, int slot_y, bool selected, bool editing) {
    uint16_t bg_color = selected ? (editing ? inv_c(60, 20, 20) : inv_c(30, 50, 60)) : C_BG;
    uint16_t border_color = selected ? (editing ? C_STATUS_ERROR : C_ACCENT_CYAN) : C_BORDER;
    
    gfx_fill_rect(8, slot_y, TFT_WIDTH - 16, SET_ITEM_H, bg_color);
    gfx_draw_rect(8, slot_y, TFT_WIDTH - 16, SET_ITEM_H, border_color);
    
    if (selected) {
        gfx_fill_rect(8, slot_y, 2, SET_ITEM_H, border_color);
    }
    
    gfx_draw_text(SET_LABEL_X, slot_y + 6, settings_label(idx), C_TEXT_PRIMARY, bg_color);
    
    char val_str[24];
    int bar_pct = 0;
    settings_value_str(idx, val_str, sizeof(val_str), &bar_pct);
    
    if (bar_pct >= 0) {
        gfx_draw_text(SET_VALUE_X, slot_y + 6, val_str, C_TEXT_SECONDARY, bg_color);
        draw_mini_slider(SET_LABEL_X, slot_y + SET_BAR_Y_OFFSET, 200, 4, bar_pct, 
                        selected ? C_ACCENT_CYAN : C_STATUS_OK);
    } else {
        gfx_fill_round_rect(SET_VALUE_X - 30, slot_y + 4, 60, 20, 3, 
                           selected ? inv_c(60, 20, 20) : C_SURFACE);
        gfx_draw_round_rect(SET_VALUE_X - 30, slot_y + 4, 60, 20, 3, border_color);
        gfx_draw_text(SET_VALUE_X - 20, slot_y + 8, val_str, border_color, 
                     selected ? inv_c(60, 20, 20) : C_SURFACE);
    }
}

static int s_settings_top_idx = 0;
static int s_last_settings_top_idx = -1;
static int s_last_cursor = -1;
static bool s_last_editing = false;
static int s_last_settings_values[18];

static void update_settings_list(void) {
    int cursor = g_sewu_state.ui_settings_cursor;
    bool editing = g_sewu_state.ui_settings_editing;
    int top = cursor - 2;
    if (top < 0) top = 0;
    if (top > 18 - SET_VISIBLE) top = 18 - SET_VISIBLE;
    
    bool need_full_redraw = (s_last_settings_top_idx == -1 || top != s_last_settings_top_idx);
    if (need_full_redraw) {
        s_settings_top_idx = top;
        s_last_settings_top_idx = top;
        gfx_fill_rect(8, CONTENT_Y + 4, TFT_WIDTH - 16, CONTENT_H - 8, C_BG);
        for (int i = 0; i < SET_VISIBLE; i++) {
            int idx = top + i;
            int slot_y = CONTENT_Y + 6 + i * SET_ITEM_H;
            draw_settings_item(idx, slot_y, idx == cursor, idx == cursor && editing);
            s_last_settings_values[idx] = get_setting_value(idx);
        }
    } else {
        bool cursor_changed = (cursor != s_last_cursor || editing != s_last_editing);
        for (int i = 0; i < SET_VISIBLE; i++) {
            int idx = top + i;
            int slot_y = CONTENT_Y + 6 + i * SET_ITEM_H;
            int val = get_setting_value(idx);
            bool val_changed = (val != s_last_settings_values[idx]);
            if (val_changed || (cursor_changed && (idx == cursor || idx == s_last_cursor))) {
                draw_settings_item(idx, slot_y, idx == cursor, idx == cursor && editing);
                s_last_settings_values[idx] = val;
            }
        }
    }
    s_last_cursor = cursor;
    s_last_editing = editing;
}

/* ================================================================
   VISUALIZER PAGE
   ================================================================ */
#define VIZ_TOP     32
#define VIZ_BOTTOM  220
#define VIZ_H       188
#define VIZ_BUF_W   320
#define VIZ_BUF_H   188

static uint16_t s_viz_line_buf[VIZ_BUF_W];

static void draw_visualizer_layout(void) {
    gfx_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, C_BG);
    gfx_fill_rect(0, 0, TFT_WIDTH, HDR_H, C_SURFACE);
    gfx_draw_text(12, 8, "VISUALIZER", C_ACCENT_CYAN, C_SURFACE);
    gfx_draw_text(220, 8, vis_mode_label(g_sewu_state.visualizer_mode), C_ACCENT_GOLD, C_SURFACE);
    gfx_fill_rect(0, HDR_H - 1, TFT_WIDTH, 1, C_BORDER);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, FTR_H, C_SURFACE);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, 1, C_BORDER);
    gfx_draw_text(8, FTR_Y + 3, "ROT:MODE  K0:HOME", C_TEXT_SECONDARY, C_SURFACE);
}

static void viz_render_line(int y, const int *bar_heights, const int *peak_ys, int num_bands,
                            const uint16_t *colors_low, const uint16_t *colors_mid, const uint16_t *colors_high,
                            int mode) {
    int band_w = VIZ_BUF_W / num_bands;
    if (band_w < 2) band_w = 2;
    int used = band_w * num_bands;
    int start_x = (VIZ_BUF_W - used) / 2;

    if (mode == 2) {
        for (int x = 0; x < VIZ_BUF_W; x++) {
            int band = (x - start_x) / band_w;
            if (band < 0) band = 0;
            if (band >= num_bands) band = num_bands - 1;
            int bar_h = bar_heights[band];
            int bar_top = VIZ_BOTTOM - bar_h;
            int peak_y = peak_ys[band];
            uint16_t c = colors_low[band];
            uint16_t px = C_BG;
            if (y == peak_y) px = C_SPECTRUM_PEAK;
            else if (y == bar_top || y == bar_top + 1) px = c;
            s_viz_line_buf[x] = px;
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, VIZ_BUF_W, y + 1, s_viz_line_buf);
        return;
    }

    if (mode == 3) {
        int center = VIZ_BUF_W / 2;
        for (int x = 0; x < VIZ_BUF_W; x++) {
            int dist_from_center = x - center;
            if (dist_from_center < 0) dist_from_center = -dist_from_center;
            int band = (dist_from_center * num_bands) / center;
            if (band < 0) band = 0;
            if (band >= num_bands) band = num_bands - 1;
            int bar_h = bar_heights[band];
            int peak_y = peak_ys[band];
            uint16_t c_low = colors_low[band];
            uint16_t c_mid = colors_mid[band];
            uint16_t c_high = colors_high[band];
            uint16_t px = C_BG;
            if (y == peak_y) px = C_SPECTRUM_PEAK;
            else if (y >= VIZ_BOTTOM - bar_h) {
                int dist_from_bottom = VIZ_BOTTOM - y;
                if (dist_from_bottom > VIZ_H * 70 / 100) px = c_high;
                else if (dist_from_bottom > VIZ_H * 40 / 100) px = c_mid;
                else px = c_low;
            }
            s_viz_line_buf[x] = px;
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, VIZ_BUF_W, y + 1, s_viz_line_buf);
        return;
    }

    for (int x = 0; x < VIZ_BUF_W; x++) {
        int band = (x - start_x) / band_w;
        if (band < 0) band = 0;
        if (band >= num_bands) band = num_bands - 1;
        int bar_h = bar_heights[band];
        int peak_y = peak_ys[band];
        uint16_t c_low = colors_low[band];
        uint16_t c_mid = colors_mid[band];
        uint16_t c_high = colors_high[band];
        uint16_t px = C_BG;
        if (y == peak_y && peak_y > 0) px = C_SPECTRUM_PEAK;
        else if (y >= VIZ_BOTTOM - bar_h) {
            int dist_from_bottom = VIZ_BOTTOM - y;
            if (dist_from_bottom > VIZ_H * 70 / 100) px = c_high;
            else if (dist_from_bottom > VIZ_H * 40 / 100) px = c_mid;
            else px = c_low;
        }
        if (mode == 1) {
            int bar_top = VIZ_BOTTOM - bar_h;
            if (y != peak_y && y != bar_top) px = C_BG;
            if (y == peak_y || y == bar_top) px = c_low;
        }
        s_viz_line_buf[x] = px;
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, VIZ_BUF_W, y + 1, s_viz_line_buf);
}

static void update_visualizer_values(void) {
    static int s_last_vis_mode = -1;
    int mode = g_sewu_state.visualizer_mode;
    if (mode != s_last_vis_mode) {
        s_last_vis_mode = mode;
        gfx_fill_rect(220, 8, 40, 16, C_SURFACE);
        gfx_draw_text(220, 8, vis_mode_label(mode), C_ACCENT_GOLD, C_SURFACE);
    }

    int num_bands = g_sewu_state.vis_active_bands;
    int bar_heights[24], peak_ys[24];
    uint16_t colors_low[24], colors_mid[24], colors_high[24];

    for (int i = 0; i < num_bands; i++) {
        int pct = clamp_int(g_sewu_state.vis_band_percent[i], 0, 100);
        int pk = clamp_int(g_sewu_state.vis_band_peak_percent[i], 0, 100);
        bar_heights[i] = (pct * VIZ_H) / 100;
        peak_ys[i] = VIZ_BOTTOM - (pk * VIZ_H) / 100;
        if (peak_ys[i] < VIZ_TOP) peak_ys[i] = VIZ_TOP;

        if (i < num_bands / 3) {
            colors_low[i] = C_SPECTRUM_LOW; colors_mid[i] = C_SPECTRUM_MID; colors_high[i] = C_SPECTRUM_HIGH;
        } else if (i < (num_bands * 2) / 3) {
            colors_low[i] = C_SPECTRUM_LOW; colors_mid[i] = C_SPECTRUM_MID; colors_high[i] = C_SPECTRUM_HIGH;
        } else {
            colors_low[i] = C_ACCENT_GOLD; colors_mid[i] = C_SPECTRUM_HIGH; colors_high[i] = C_SPECTRUM_PEAK;
        }
    }

    for (int y = VIZ_TOP; y < VIZ_BOTTOM; y++) {
        viz_render_line(y, bar_heights, peak_ys, num_bands,
                        colors_low, colors_mid, colors_high, mode);
    }
}

/* ================================================================
   LOG PAGE
   ================================================================ */
static uint32_t s_log_last_frames_in = 0;
static uint32_t s_log_last_underruns = 0;
static uint32_t s_log_last_overruns = 0;
static int s_log_last_buf_fill = -1;
static int s_log_last_health = -1;
static int s_log_last_latency = -1;
static int s_log_last_source_mode = -1;
static int s_log_last_usb_ready = -1;
static int s_log_last_usb_driver = -1;
static int s_log_last_usb_streaming = -1;
static int s_log_last_host_vol = -1;
static int s_log_last_host_muted = -1;
static int s_log_last_reset_ago = -1;

static void draw_log_layout(void) {
    gfx_fill_screen(C_BG);
    gfx_fill_rect(0, 0, TFT_WIDTH, HDR_H, C_SURFACE);
    gfx_draw_text(12, 8, "SYSTEM LOGS", C_ACCENT_CYAN, C_SURFACE);
    gfx_fill_rect(0, HDR_H - 1, TFT_WIDTH, 1, C_BORDER);
    
    const int left_x = 12;
    const int right_x = 170;
    const int label_y = 50;
    const int row_h = 18;
    
    gfx_draw_text(left_x, label_y + 0 * row_h, "MODE:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(left_x, label_y + 1 * row_h, "USB RDY:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(left_x, label_y + 2 * row_h, "DRIVER:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(left_x, label_y + 3 * row_h, "STREAM:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(left_x, label_y + 4 * row_h, "HOST VOL:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(left_x, label_y + 5 * row_h, "MUTED:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(right_x, label_y + 0 * row_h, "FILL:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(right_x, label_y + 1 * row_h, "LATENCY:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(right_x, label_y + 2 * row_h, "HEALTH:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(right_x, label_y + 3 * row_h, "FRAMES:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(right_x, label_y + 4 * row_h, "UNDERRUN:", C_TEXT_SECONDARY, C_BG);
    gfx_draw_text(right_x, label_y + 5 * row_h, "OVERRUN:", C_TEXT_SECONDARY, C_BG);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, FTR_H, C_SURFACE);
    gfx_fill_rect(0, FTR_Y, TFT_WIDTH, 1, C_BORDER);
    gfx_draw_text(8, FTR_Y + 3, "K0:HOME  SETTINGS:SAVE/RST", C_TEXT_SECONDARY, C_SURFACE);
    
    s_log_last_frames_in = 0xFFFFFFFF;
    s_log_last_underruns = 0xFFFFFFFF;
    s_log_last_overruns = 0xFFFFFFFF;
    s_log_last_buf_fill = -1;
    s_log_last_health = -1;
    s_log_last_latency = -1;
    s_log_last_source_mode = -1;
    s_log_last_usb_ready = -1;
    s_log_last_usb_driver = -1;
    s_log_last_usb_streaming = -1;
    s_log_last_host_vol = -1;
    s_log_last_host_muted = -1;
    s_log_last_reset_ago = -2;
}

static void update_log_values(void) {
    char buf[32];
    uint32_t now = (uint32_t)esp_log_timestamp();
    int reset_ago = -1;
    if (g_sewu_state.usb_last_reset_ms > 0U) {
        reset_ago = (int)((now - g_sewu_state.usb_last_reset_ms) / 1000U);
        if (reset_ago < 0) reset_ago = 0;
    }
    
    const int left_val_x = 90;
    const int right_val_x = 240;
    const int label_y = 50;
    const int row_h = 18;

    if (s_log_last_source_mode != g_sewu_state.source_mode) {
        s_log_last_source_mode = g_sewu_state.source_mode;
        gfx_fill_rect(left_val_x, label_y + 0 * row_h, 60, 12, C_BG);
        gfx_draw_text(left_val_x, label_y + 0 * row_h, g_sewu_state.source_mode == 0 ? "AUTO" : "USB", C_TEXT_PRIMARY, C_BG);
    }
    if (s_log_last_usb_ready != (int)g_sewu_state.usb_ready) {
        s_log_last_usb_ready = (int)g_sewu_state.usb_ready;
        gfx_fill_rect(left_val_x, label_y + 1 * row_h, 60, 12, C_BG);
        gfx_draw_text(left_val_x, label_y + 1 * row_h, g_sewu_state.usb_ready ? "YES" : "NO", g_sewu_state.usb_ready ? C_STATUS_OK : C_STATUS_ERROR, C_BG);
    }
    if (s_log_last_usb_driver != (int)g_sewu_state.usb_driver_ready) {
        s_log_last_usb_driver = (int)g_sewu_state.usb_driver_ready;
        gfx_fill_rect(left_val_x, label_y + 2 * row_h, 60, 12, C_BG);
        gfx_draw_text(left_val_x, label_y + 2 * row_h, g_sewu_state.usb_driver_ready ? "RDY" : "OFF", g_sewu_state.usb_driver_ready ? C_STATUS_OK : C_STATUS_ERROR, C_BG);
    }
    if (s_log_last_usb_streaming != (int)g_sewu_state.usb_streaming) {
        s_log_last_usb_streaming = (int)g_sewu_state.usb_streaming;
        gfx_fill_rect(left_val_x, label_y + 3 * row_h, 60, 12, C_BG);
        gfx_draw_text(left_val_x, label_y + 3 * row_h, g_sewu_state.usb_streaming ? "RUN" : "IDLE", g_sewu_state.usb_streaming ? C_STATUS_OK : C_TEXT_SECONDARY, C_BG);
    }
    if (s_log_last_host_vol != g_sewu_state.usb_host_volume_percent) {
        s_log_last_host_vol = g_sewu_state.usb_host_volume_percent;
        gfx_fill_rect(left_val_x, label_y + 4 * row_h, 60, 12, C_BG);
        snprintf(buf, sizeof(buf), "%d%%", s_log_last_host_vol);
        gfx_draw_text(left_val_x, label_y + 4 * row_h, buf, C_TEXT_PRIMARY, C_BG);
    }
    if (s_log_last_host_muted != (int)g_sewu_state.usb_host_muted) {
        s_log_last_host_muted = (int)g_sewu_state.usb_host_muted;
        gfx_fill_rect(left_val_x, label_y + 5 * row_h, 60, 12, C_BG);
        gfx_draw_text(left_val_x, label_y + 5 * row_h, g_sewu_state.usb_host_muted ? "YES" : "NO", g_sewu_state.usb_host_muted ? C_STATUS_ERROR : C_STATUS_OK, C_BG);
    }
    if (s_log_last_buf_fill != g_sewu_state.usb_fill_percent) {
        s_log_last_buf_fill = g_sewu_state.usb_fill_percent;
        gfx_fill_rect(right_val_x, label_y + 0 * row_h, 60, 12, C_BG);
        snprintf(buf, sizeof(buf), "%d%%", s_log_last_buf_fill);
        gfx_draw_text(right_val_x, label_y + 0 * row_h, buf, C_TEXT_PRIMARY, C_BG);
    }
    if (s_log_last_latency != g_sewu_state.usb_latency_ms) {
        s_log_last_latency = g_sewu_state.usb_latency_ms;
        gfx_fill_rect(right_val_x, label_y + 1 * row_h, 60, 12, C_BG);
        snprintf(buf, sizeof(buf), "%d ms", s_log_last_latency);
        gfx_draw_text(right_val_x, label_y + 1 * row_h, buf, C_TEXT_PRIMARY, C_BG);
    }
    if (s_log_last_health != g_sewu_state.usb_health_state) {
        s_log_last_health = g_sewu_state.usb_health_state;
        gfx_fill_rect(right_val_x, label_y + 2 * row_h, 60, 12, C_BG);
        gfx_draw_text(right_val_x, label_y + 2 * row_h, health_label(s_log_last_health), health_color(s_log_last_health), C_BG);
    }
    if (s_log_last_frames_in != g_sewu_state.usb_frames_in) {
        s_log_last_frames_in = g_sewu_state.usb_frames_in;
        gfx_fill_rect(right_val_x, label_y + 3 * row_h, 60, 12, C_BG);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_log_last_frames_in);
        gfx_draw_text(right_val_x, label_y + 3 * row_h, buf, C_TEXT_PRIMARY, C_BG);
    }
    if (s_log_last_underruns != g_sewu_state.usb_underruns) {
        s_log_last_underruns = g_sewu_state.usb_underruns;
        gfx_fill_rect(right_val_x, label_y + 4 * row_h, 60, 12, C_BG);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_log_last_underruns);
        gfx_draw_text(right_val_x, label_y + 4 * row_h, buf, s_log_last_underruns > 0 ? C_STATUS_ERROR : C_TEXT_SECONDARY, C_BG);
    }
    if (s_log_last_overruns != g_sewu_state.usb_overruns) {
        s_log_last_overruns = g_sewu_state.usb_overruns;
        gfx_fill_rect(right_val_x, label_y + 5 * row_h, 60, 12, C_BG);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_log_last_overruns);
        gfx_draw_text(right_val_x, label_y + 5 * row_h, buf, s_log_last_overruns > 0 ? C_STATUS_ERROR : C_TEXT_SECONDARY, C_BG);
    }
}

/* ================================================================
   BACKLIGHT
   ================================================================ */
static void apply_backlight_percent(int percent) {
    if (!s_backlight_ready) return;
    int p = clamp_int(percent, 0, 100);
    uint32_t duty = (uint32_t)((p * 255) / 100);
    ledc_set_duty(BL_PWM_MODE, BL_PWM_CHANNEL, duty);
    ledc_update_duty(BL_PWM_MODE, BL_PWM_CHANNEL);
}

static void update_backlight_state(void) {
    uint32_t now = (uint32_t)esp_log_timestamp();
    int target = g_sewu_state.backlight_percent;
    if (g_sewu_state.standby_active) {
        target = g_sewu_state.backlight_standby_percent;
    } else if (g_sewu_state.auto_dim_timeout_ms > 0U &&
               (now - g_sewu_state.last_input_ms) >= g_sewu_state.auto_dim_timeout_ms) {
        target = g_sewu_state.backlight_dim_percent;
    }
    target = clamp_int(target, 0, 100);
    if (target != s_last_backlight_target) {
        apply_backlight_percent(target);
        s_last_backlight_target = target;
    }
}

/* ================================================================
   FRAME RATE MANAGEMENT
   ================================================================ */
static uint32_t ui_frame_interval_ms(void) {
    uint32_t interval = UI_FPS_BASE_MS;
    if (g_sewu_state.performance_profile == 0) interval = 120;
    if (g_sewu_state.performance_profile == 2) interval = 85;
    if (g_sewu_state.ui_page == 0) {
        if (g_sewu_state.usb_health_state == 2) interval += 25;
        if (g_sewu_state.usb_health_state == 0 && interval > 70) interval -= 10;
    }
    if (g_sewu_state.standby_active) interval += 35;
    return interval;
}

/* ================================================================
   TFT HARDWARE INIT
   ================================================================ */
static bool tft_init(void) {
    ESP_LOGI(TAG, "TFT init: pins mosi=%d sclk=%d cs=%d dc=%d rst=%d bl=%d",
             SEWU_PIN_TFT_MOSI, SEWU_PIN_TFT_SCLK, SEWU_PIN_TFT_CS,
             SEWU_PIN_TFT_DC, SEWU_PIN_TFT_RST, SEWU_PIN_TFT_BLK);
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = SEWU_PIN_TFT_SCLK,
        .mosi_io_num = SEWU_PIN_TFT_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * FILL_CHUNK_ROWS * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(TFT_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { 
        ESP_LOGE(TAG, "SPI bus init failed"); 
        return false; 
    }
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = SEWU_PIN_TFT_DC, 
        .cs_gpio_num = SEWU_PIN_TFT_CS,
        .pclk_hz = TFT_PCLK_HZ, 
        .lcd_cmd_bits = TFT_LCD_CMD_BITS,
        .lcd_param_bits = TFT_LCD_PARAM_BITS, 
        .spi_mode = 0, 
        .trans_queue_depth = 10,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_HOST, &io_cfg, &s_io);
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "Panel IO creation failed"); 
        return false; 
    }
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = SEWU_PIN_TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, 
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "Panel creation failed"); 
        return false; 
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    s_tft_ready = true;
    gfx_init(s_panel, GFX_ROT_90);
    return true;
}

/* ================================================================
   PUBLIC API
   ================================================================ */
void sewu_ui_init(void) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BL_PWM_MODE, 
        .duty_resolution = BL_PWM_RES,
        .timer_num = BL_PWM_TIMER, 
        .freq_hz = BL_PWM_HZ, 
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t ch_cfg = {
        .gpio_num = SEWU_PIN_TFT_BLK, 
        .speed_mode = BL_PWM_MODE,
        .channel = BL_PWM_CHANNEL, 
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_PWM_TIMER, 
        .duty = 0, 
        .hpoint = 0,
    };
    if (ledc_timer_config(&timer_cfg) == ESP_OK && ledc_channel_config(&ch_cfg) == ESP_OK) {
        s_backlight_ready = true;
        apply_backlight_percent(g_sewu_state.backlight_percent);
    }
    init_palette();
    if (tft_init()) {
        g_sewu_state.ui_page = 0;
        draw_home_layout();
        s_last_page = 0;
        ESP_LOGI(TAG, "UI initialized successfully");
    } else {
        ESP_LOGW(TAG, "TFT initialization failed");
    }
}

void sewu_ui_update(void) {
    update_backlight_state();
    uint32_t now = (uint32_t)esp_log_timestamp();
    if (s_tft_ready && (now - s_last_ui_ms) >= ui_frame_interval_ms()) {
        s_last_ui_ms = now;
        if (g_sewu_state.ui_page != s_last_page) {
            s_last_page = g_sewu_state.ui_page;
            if (g_sewu_state.ui_page == 0) { 
                s_home_values_dirty = true; 
                draw_home_layout(); 
            }
            else if (g_sewu_state.ui_page == 1) { 
                s_last_settings_top_idx = -1; 
                draw_settings_layout(); 
            }
            else if (g_sewu_state.ui_page == 2) { 
                draw_visualizer_layout(); 
            }
            else if (g_sewu_state.ui_page == 3) { 
                draw_log_layout(); 
            }
        }
        if (g_sewu_state.ui_page == 0) update_home_values();
        else if (g_sewu_state.ui_page == 1) update_settings_list();
        else if (g_sewu_state.ui_page == 2) update_visualizer_values();
        else if (g_sewu_state.ui_page == 3) update_log_values();
    }
    uint32_t period = (g_sewu_state.ui_page == 0) ? 1200U : 850U;
    if ((now - s_last_ui_log_ms) < period) return;
    s_last_ui_log_ms = now;
    if (g_sewu_state.ui_page == 0) {
        ESP_LOGI(TAG, "[HOME] vol=%d%% vu=%d/%d lim=%d preset=%s usb=%s buf=%d%%",
            g_sewu_state.volume_percent, g_sewu_state.vu_left_percent, g_sewu_state.vu_right_percent,
            g_sewu_state.limiter_enabled ? 1 : 0, preset_label(g_sewu_state.preset_index),
            health_label(g_sewu_state.usb_health_state), g_sewu_state.usb_fill_percent);
    } else {
        ESP_LOGI(TAG, "[SETTINGS] cursor=%d edit=%d vol=%d%% preset=%s viz=%s",
            g_sewu_state.ui_settings_cursor, g_sewu_state.ui_settings_editing ? 1 : 0,
            g_sewu_state.volume_percent, preset_label(g_sewu_state.preset_index),
            vis_mode_label(g_sewu_state.visualizer_mode));
    }
}
