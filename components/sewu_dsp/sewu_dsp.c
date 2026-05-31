#include "sewu_dsp.h"

#include <math.h>
#include "esp_log.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"

#include "sewu_app_state.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------- Biquad Peaking EQ ---------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} biquad_t;

static biquad_t s_bands[5][2]; /* [band][ch] 0=L,1=R */

static const float EQ_FREQ[5] = { 80.0f, 300.0f, 1000.0f, 3200.0f, 8500.0f };
static const float EQ_Q[5]    = { 0.70f, 0.70f,  0.70f,   0.70f,   0.70f   };

static float s_meter_l;
static float s_meter_r;
static float s_limiter_meter;
static float s_peak_hold_l;
static float s_peak_hold_r;
static float s_peak_hold_limiter;

static float s_viz_bands[SEWU_VIS_BAND_COUNT];
static float s_viz_peaks[SEWU_VIS_BAND_COUNT];
static int s_viz_start_bin[SEWU_VIS_BAND_COUNT + 1][SEWU_VIS_BAND_COUNT];
static int s_viz_end_bin[SEWU_VIS_BAND_COUNT + 1][SEWU_VIS_BAND_COUNT];
static float s_viz_weight[SEWU_VIS_BAND_COUNT + 1][SEWU_VIS_BAND_COUNT];

// Real FFT configuration
#define FFT_SIZE 256
static float s_fft_input[FFT_SIZE];
static float s_fft_window[FFT_SIZE];
static float s_fft_output[FFT_SIZE * 2] __attribute__((aligned(16)));
static int s_fft_sample_idx = 0;
static bool s_fft_initialized = false;
static const char *TAG = "sewu_dsp";

/* ---------- Cached gains per band (dB) ---------- */
static int s_last_db[5] = {999, 999, 999, 999, 999};
static float s_linear_gain[5];

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static void update_biquad_coeffs(float freq, float Q, float peak_gain_linear, biquad_t *bq) {
    float A = sqrtf(peak_gain_linear);
    float w0 = 2.0f * (float)M_PI * freq / (float)SEWU_AUDIO_SAMPLE_RATE;
    float alpha = sinf(w0) / (2.0f * Q);
    float cos_w0 = cosf(w0);

    bq->b0 = 1.0f + alpha * A;
    bq->b1 = -2.0f * cos_w0;
    bq->b2 = 1.0f - alpha * A;
    bq->a1 = -2.0f * cos_w0;
    bq->a2 = 1.0f - alpha / A;

    /* Normalize by a0 */
    float a0 = 1.0f + alpha / A;
    bq->b0 /= a0;
    bq->b1 /= a0;
    bq->b2 /= a0;
    bq->a1 /= a0;
    bq->a2 /= a0;

    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0.0f;
}

static float biquad_process(biquad_t *bq, float x) {
    float y = bq->b0 * x + bq->b1 * bq->x1 + bq->b2 * bq->x2
                          - bq->a1 * bq->y1 - bq->a2 * bq->y2;
    bq->x2 = bq->x1;
    bq->x1 = x;
    bq->y2 = bq->y1;
    bq->y1 = y;
    return y;
}

/* ---------- Utility ---------- */
static float sewu_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float apply_soft_limiter(float x, float threshold, float *gr) {
    float abs_x = fabsf(x);
    if (abs_x <= threshold) {
        return x;
    }
    float over = abs_x - threshold;
    float compressed = threshold + (over / (1.0f + over * 8.0f));
    float y = (x >= 0.0f) ? compressed : -compressed;
    float ratio = (abs_x > 0.0001f) ? (fabsf(y) / abs_x) : 1.0f;
    *gr = 1.0f - ratio;
    return sewu_clampf(y, -1.0f, 1.0f);
}

static void update_eq_gains(void) {
    int cur[5] = { g_sewu_state.bass_db, g_sewu_state.low_mid_db,
                   g_sewu_state.mid_db, g_sewu_state.high_mid_db,
                   g_sewu_state.treble_db };

    bool changed = false;
    for (int i = 0; i < 5; i++) {
        if (cur[i] != s_last_db[i]) {
            s_last_db[i] = cur[i];
            s_linear_gain[i] = db_to_linear((float)cur[i]);
            changed = true;
        }
    }

    if (changed) {
        for (int i = 0; i < 5; i++) {
            float g = s_linear_gain[i];
            /* Clamp gain to avoid instability (0.01 = -40dB, 100 = +40dB) */
            if (g < 0.01f) g = 0.01f;
            if (g > 100.0f) g = 100.0f;
            update_biquad_coeffs(EQ_FREQ[i], EQ_Q[i], g, &s_bands[i][0]);
            update_biquad_coeffs(EQ_FREQ[i], EQ_Q[i], g, &s_bands[i][1]);
        }
        ESP_LOGI(TAG, "EQ updated: bass=%+d lowmid=%+d mid=%+d himid=%+d treb=%+d",
                 cur[0], cur[1], cur[2], cur[3], cur[4]);
    }
}

static void init_visualizer_maps(void) {
    for (int active_bands = 1; active_bands <= SEWU_VIS_BAND_COUNT; ++active_bands) {
        for (int i = 0; i < active_bands; ++i) {
            float t = (active_bands > 1) ? ((float)i / (float)(active_bands - 1)) : 0.0f;
            float start_f = powf(110.0f, t);
            float end_f = powf(110.0f, t + 1.0f / (float)active_bands);

            int start_bin = (int)start_f;
            int end_bin = (int)end_f;
            if (start_bin < 1) start_bin = 1;
            if (end_bin <= start_bin) end_bin = start_bin + 1;
            if (end_bin > 120) end_bin = 120;

            s_viz_start_bin[active_bands][i] = start_bin;
            s_viz_end_bin[active_bands][i] = end_bin;
            s_viz_weight[active_bands][i] = 1.0f + t * 2.20f;
        }
    }
}

static void update_visualizer_fft(const float *magnitudes, int active_bands) {
    float attack = (g_sewu_state.performance_profile == 0) ? 0.30f : ((g_sewu_state.performance_profile == 1) ? 0.40f : 0.50f);
    float release = (g_sewu_state.performance_profile == 0) ? 0.08f : ((g_sewu_state.performance_profile == 1) ? 0.11f : 0.16f);
    float peak_decay = (g_sewu_state.performance_profile == 0) ? 0.008f : ((g_sewu_state.performance_profile == 1) ? 0.010f : 0.016f);
    float input_gain = (g_sewu_state.performance_profile >= 2) ? 6.50f : 5.50f;

    for (int i = 0; i < active_bands; ++i) {
        int start_bin = s_viz_start_bin[active_bands][i];
        int end_bin = s_viz_end_bin[active_bands][i];

        float sum = 0.0f;
        for (int b = start_bin; b < end_bin; ++b) {
            sum += magnitudes[b];
        }
        float target = sum / (float)(end_bin - start_bin);

        target *= s_viz_weight[active_bands][i] * input_gain;
        target = sewu_clampf(target, 0.0f, 1.0f);

        // Smooth visualizer bars (attack / release)
        if (target > s_viz_bands[i]) {
            s_viz_bands[i] += attack * (target - s_viz_bands[i]);
        } else {
            s_viz_bands[i] += release * (target - s_viz_bands[i]);
        }

        // Smooth visualizer peaks
        if (s_viz_bands[i] >= s_viz_peaks[i]) {
            s_viz_peaks[i] = s_viz_bands[i];
        } else {
            s_viz_peaks[i] = sewu_clampf(s_viz_peaks[i] - peak_decay, 0.0f, 1.0f);
        }

        g_sewu_state.vis_band_percent[i] = (int)sewu_clampf(s_viz_bands[i] * 100.0f, 0.0f, 100.0f);
        g_sewu_state.vis_band_peak_percent[i] = (int)sewu_clampf(s_viz_peaks[i] * 100.0f, 0.0f, 100.0f);
    }

    // Zero out unused bands
    for (int i = active_bands; i < SEWU_VIS_BAND_COUNT; ++i) {
        s_viz_bands[i] = 0.0f;
        s_viz_peaks[i] = 0.0f;
        g_sewu_state.vis_band_percent[i] = 0;
        g_sewu_state.vis_band_peak_percent[i] = 0;
    }
}

void sewu_dsp_init(void) {
    g_sewu_state.vis_active_bands = (g_sewu_state.performance_profile >= 2) ? 24 : 16;
    init_visualizer_maps();

    // Initialize esp-dsp Radix-2 FFT
    esp_err_t fft_err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (fft_err == ESP_OK) {
        dsps_wind_hann_f32(s_fft_window, FFT_SIZE);
        s_fft_initialized = true;
        ESP_LOGI(TAG, "esp-dsp FFT radix-2 initialized successfully with Hann window");
    } else {
        ESP_LOGE(TAG, "esp-dsp FFT radix-2 init failed: %d", (int)fft_err);
    }

    /* Init biquad filters with flat (0dB) response */
    for (int i = 0; i < 5; i++) {
        for (int ch = 0; ch < 2; ch++) {
            update_biquad_coeffs(EQ_FREQ[i], EQ_Q[i], 1.0f, &s_bands[i][ch]);
        }
    }

    update_eq_gains();
}

void sewu_dsp_update(void) {
    if (g_sewu_state.performance_profile < 0) g_sewu_state.performance_profile = 0;
    if (g_sewu_state.performance_profile > 2) g_sewu_state.performance_profile = 2;
    update_eq_gains();
}

void sewu_dsp_process_frame(float *left, float *right) {
    float l = *left;
    float r = *right;

    /* Apply biquad peaking EQ: 5 bands in series */
    for (int i = 0; i < 5; i++) {
        l = biquad_process(&s_bands[i][0], l);
        r = biquad_process(&s_bands[i][1], r);
    }

    /* Accumulate mono samples for Real FFT visualizer */
    float mono = 0.5f * (l + r);
    if (s_fft_initialized) {
        s_fft_input[s_fft_sample_idx++] = mono;
        if (s_fft_sample_idx >= FFT_SIZE) {
            // Apply Hanning Window
            for (int idx = 0; idx < FFT_SIZE; ++idx) {
                s_fft_output[idx * 2] = s_fft_input[idx] * s_fft_window[idx];
                s_fft_output[idx * 2 + 1] = 0.0f; // Imaginary
            }

            // Execute Radix-2 FFT (hardware accelerated via assembly on ESP32-S3)
            dsps_fft2r_fc32(s_fft_output, FFT_SIZE);

            // Reorder output (bit reversal)
            dsps_bit_rev_fc32(s_fft_output, FFT_SIZE);

            // Calculate magnitudes for first half (symmetric frequency spectrum)
            float magnitudes[FFT_SIZE / 2];
            for (int idx = 0; idx < FFT_SIZE / 2; ++idx) {
                float real = s_fft_output[idx * 2];
                float imag = s_fft_output[idx * 2 + 1];
                magnitudes[idx] = sqrtf(real * real + imag * imag) / (FFT_SIZE / 2.0f);
            }

            // Determine current active bands based on USB/Performance health
            int active_bands = (g_sewu_state.performance_profile >= 2) ? 24 : 16;
            if (g_sewu_state.usb_health_state >= 2 && g_sewu_state.source_mode == 1) {
                active_bands -= 8;
            } else if (g_sewu_state.usb_health_state == 1 && g_sewu_state.source_mode == 1) {
                active_bands -= 4;
            }
            active_bands = (int)sewu_clampf((float)active_bands, 12.0f, (float)SEWU_VIS_BAND_COUNT);
            g_sewu_state.vis_active_bands = active_bands;

            // Map magnitudes to the visualizer bands
            update_visualizer_fft(magnitudes, active_bands);

            s_fft_sample_idx = 0;
        }
    }

    float master = (float)g_sewu_state.master_gain_percent / 100.0f;
    l *= master;
    r *= master;

    float bal = (float)g_sewu_state.balance_percent / 100.0f;
    if (bal > 0.0f) {
        l *= (1.0f - bal);
    } else if (bal < 0.0f) {
        r *= (1.0f + bal);
    }

    float gr_l = 0.0f;
    float gr_r = 0.0f;
    if (g_sewu_state.limiter_enabled) {
        l = apply_soft_limiter(l, 0.90f, &gr_l);
        r = apply_soft_limiter(r, 0.90f, &gr_r);
    } else {
        l = sewu_clampf(l, -1.0f, 1.0f);
        r = sewu_clampf(r, -1.0f, 1.0f);
    }

    float peak_l = fabsf(l);
    float peak_r = fabsf(r);
    float attack = 0.50f;
    float release = 0.10f;
    float peak_hold_decay = 0.00035f;

    s_meter_l = (peak_l > s_meter_l) ? (s_meter_l + attack * (peak_l - s_meter_l))
                                     : (s_meter_l + release * (peak_l - s_meter_l));
    s_meter_r = (peak_r > s_meter_r) ? (s_meter_r + attack * (peak_r - s_meter_r))
                                     : (s_meter_r + release * (peak_r - s_meter_r));

    if (peak_l >= s_peak_hold_l) {
        s_peak_hold_l = peak_l;
    } else {
        s_peak_hold_l = sewu_clampf(s_peak_hold_l - peak_hold_decay, 0.0f, 1.0f);
    }

    if (peak_r >= s_peak_hold_r) {
        s_peak_hold_r = peak_r;
    } else {
        s_peak_hold_r = sewu_clampf(s_peak_hold_r - peak_hold_decay, 0.0f, 1.0f);
    }

    float gr = (gr_l > gr_r) ? gr_l : gr_r;
    float limiter_release = 0.15f;
    float limiter_peak_decay = 0.0012f;
    if (gr > s_limiter_meter) {
        s_limiter_meter = gr;
    } else {
        s_limiter_meter += limiter_release * (gr - s_limiter_meter);
    }

    if (gr >= s_peak_hold_limiter) {
        s_peak_hold_limiter = gr;
    } else {
        s_peak_hold_limiter = sewu_clampf(s_peak_hold_limiter - limiter_peak_decay, 0.0f, 1.0f);
    }

    g_sewu_state.vu_left_percent = (int)sewu_clampf(s_meter_l * 100.0f, 0.0f, 100.0f);
    g_sewu_state.vu_right_percent = (int)sewu_clampf(s_meter_r * 100.0f, 0.0f, 100.0f);
    g_sewu_state.vu_left_peak_percent = (int)sewu_clampf(s_peak_hold_l * 100.0f, 0.0f, 100.0f);
    g_sewu_state.vu_right_peak_percent = (int)sewu_clampf(s_peak_hold_r * 100.0f, 0.0f, 100.0f);
    g_sewu_state.limiter_percent = (int)sewu_clampf(s_limiter_meter * 100.0f, 0.0f, 100.0f);
    g_sewu_state.limiter_peak_percent = (int)sewu_clampf(s_peak_hold_limiter * 100.0f, 0.0f, 100.0f);

    *left = l;
    *right = r;
}