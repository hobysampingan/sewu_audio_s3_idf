#include "sewu_audio_engine.h"

#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "sewu_app_state.h"
#include "sewu_dsp.h"
#include "sewu_usb_audio.h"

static const char *TAG = "sewu_audio";

#define SOURCE_AUTO 0
#define SOURCE_USB 1
#define SOURCE_TONE 2
#define SOURCE_SILENCE 3
#define I2S_DMA_BUF_COUNT 12
#define I2S_WRITE_TIMEOUT_MS 20U

static i2s_chan_handle_t s_i2s_tx_chan;

static float s_phase;
static bool s_warned_write_fail;
static float s_hold_l;
static float s_hold_r;
static int s_auto_latched_source = SOURCE_TONE;
static uint16_t s_auto_usb_miss_count;
static float s_auto_usb_blend;
static float s_output_gate;

// Batch pre-fetch buffer: holds a full block of stereo float samples
// read from the ringbuffer in ONE call before the DSP loop starts.
#define BATCH_FRAMES SEWU_AUDIO_BUFFER_SAMPLES
static float s_batch_l[BATCH_FRAMES];
static float s_batch_r[BATCH_FRAMES];

static float clampf_audio(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float host_usb_gain(void) {
    if (g_sewu_state.usb_host_muted) {
        return 0.0f;
    }
    float gain = (float)g_sewu_state.usb_host_volume_percent / 100.0f;
    return clampf_audio(gain, 0.0f, 1.0f);
}

static float next_tone_sample(float volume, float phase_step) {
    return 0.0f;
}

void sewu_audio_engine_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = SEWU_AUDIO_BUFFER_SAMPLES;
    chan_cfg.auto_clear = true;
    chan_cfg.intr_priority = 1;

    esp_err_t new_err = i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SEWU_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SEWU_PIN_I2S_BCK,
            .ws = SEWU_PIN_I2S_WS,
            .dout = SEWU_PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t init_err = ESP_FAIL;
    esp_err_t en_err = ESP_FAIL;
    if (new_err == ESP_OK) {
        init_err = i2s_channel_init_std_mode(s_i2s_tx_chan, &std_cfg);
        if (init_err == ESP_OK) {
            en_err = i2s_channel_enable(s_i2s_tx_chan);
        }
    }

    g_sewu_state.i2s_ready = (new_err == ESP_OK && init_err == ESP_OK && en_err == ESP_OK);

    s_auto_latched_source = (g_sewu_state.source_mode == SOURCE_USB) ? SOURCE_USB : SOURCE_TONE;
    s_auto_usb_blend = (s_auto_latched_source == SOURCE_USB) ? 1.0f : 0.0f;
    s_output_gate = (g_sewu_state.source_mode == SOURCE_USB) ? 0.0f : 1.0f;

    ESP_LOGI(TAG, "i2s_std init new=%d init=%d en=%d ready=%d", (int)new_err, (int)init_err, (int)en_err, g_sewu_state.i2s_ready ? 1 : 0);
}

void sewu_audio_engine_update(void) {
    if (!g_sewu_state.i2s_ready || s_i2s_tx_chan == NULL) {
        return;
    }

    int16_t samples[SEWU_AUDIO_BUFFER_SAMPLES * SEWU_AUDIO_CHANNELS];
    float volume = (float)g_sewu_state.volume_percent / 100.0f;
    float phase_step = (2.0f * (float)M_PI * g_sewu_state.tone_hz) / (float)SEWU_AUDIO_SAMPLE_RATE;
    float usb_gain = host_usb_gain();

    size_t ring_capacity = sewu_usb_audio_capacity_frames();
    if (ring_capacity == 0U) {
        ring_capacity = 4096U;
    }
    size_t target_frames = ((size_t)g_sewu_state.usb_target_fill_percent * ring_capacity) / 100U;
    size_t enter_threshold = (target_frames < 64U) ? 64U : target_frames;
    size_t exit_threshold = (target_frames > 96U) ? (target_frames / 3U) : 32U;
    size_t buffered_frames = sewu_usb_audio_buffered_frames();

    // ----------------------------------------------------------------
    // OPTIMIZATION: Batch pre-fetch USB frames only when this block will
    // actually consume them. In AUTO mode, avoid draining the ringbuffer
    // before the USB enter threshold is reached.
    // ----------------------------------------------------------------
    size_t usb_batch_fetched = 0;
    bool usb_should_prefetch = false;
    if (g_sewu_state.source_mode == SOURCE_USB) {
        usb_should_prefetch = true;
    } else if (g_sewu_state.source_mode == SOURCE_AUTO) {
        usb_should_prefetch =
            (s_auto_latched_source == SOURCE_USB) ||
            (g_sewu_state.usb_streaming && buffered_frames >= enter_threshold);
    }

    if (usb_should_prefetch) {
        usb_batch_fetched = sewu_usb_audio_read_batch(s_batch_l, s_batch_r, BATCH_FRAMES);
    }
    size_t usb_batch_idx = 0;

    for (size_t i = 0; i < SEWU_AUDIO_BUFFER_SAMPLES; ++i) {
        float l = 0.0f;
        float r = 0.0f;

        if (g_sewu_state.source_mode == SOURCE_AUTO) {
            if ((i & 0x0FU) == 0U) {
                buffered_frames = sewu_usb_audio_buffered_frames();
            }

            float tone = next_tone_sample(volume, phase_step);
            float usb_l = s_hold_l;
            float usb_r = s_hold_r;

            if (s_auto_latched_source == SOURCE_USB) {
                // Use pre-fetched batch data if available.
                if (usb_batch_idx < usb_batch_fetched) {
                    usb_l = s_batch_l[usb_batch_idx];
                    usb_r = s_batch_r[usb_batch_idx];
                    usb_batch_idx++;
                    if (buffered_frames > 0U) buffered_frames--;
                    s_hold_l = usb_l;
                    s_hold_r = usb_r;
                    s_auto_usb_miss_count = 0;
                } else {
                    s_auto_usb_miss_count++;
                    s_hold_l *= 0.96f;
                    s_hold_r *= 0.96f;
                    usb_l = s_hold_l;
                    usb_r = s_hold_r;
                    if (s_auto_usb_miss_count > 192U || (!g_sewu_state.usb_streaming && buffered_frames < exit_threshold)) {
                        s_auto_latched_source = SOURCE_TONE;
                    }
                }
            } else if (g_sewu_state.usb_streaming && buffered_frames >= enter_threshold) {
                if (usb_batch_idx < usb_batch_fetched) {
                    usb_l = s_batch_l[usb_batch_idx];
                    usb_r = s_batch_r[usb_batch_idx];
                    usb_batch_idx++;
                    if (buffered_frames > 0U) buffered_frames--;
                    s_hold_l = usb_l;
                    s_hold_r = usb_r;
                    s_auto_latched_source = SOURCE_USB;
                    s_auto_usb_miss_count = 0;
                }
            }

            float blend_target = (s_auto_latched_source == SOURCE_USB) ? 1.0f : 0.0f;
            s_auto_usb_blend += (blend_target - s_auto_usb_blend) * 0.02f;
            s_auto_usb_blend = clampf_audio(s_auto_usb_blend, 0.0f, 1.0f);

            usb_l *= usb_gain;
            usb_r *= usb_gain;
            l = (tone * (1.0f - s_auto_usb_blend)) + (usb_l * s_auto_usb_blend);
            r = (tone * (1.0f - s_auto_usb_blend)) + (usb_r * s_auto_usb_blend);
            g_sewu_state.active_source = (s_auto_usb_blend >= 0.6f) ? SOURCE_USB : SOURCE_TONE;
        } else if (g_sewu_state.source_mode == SOURCE_USB) {
            s_auto_latched_source = SOURCE_USB;
            s_auto_usb_blend = 1.0f;

            // Use pre-fetched batch data if available
            if (usb_batch_idx < usb_batch_fetched) {
                l = s_batch_l[usb_batch_idx];
                r = s_batch_r[usb_batch_idx];
                usb_batch_idx++;
                s_hold_l = l;
                s_hold_r = r;
                g_sewu_state.active_source = SOURCE_USB;
            } else {
                s_hold_l *= 0.96f;
                s_hold_r *= 0.96f;
                l = s_hold_l;
                r = s_hold_r;
                g_sewu_state.active_source = SOURCE_SILENCE;
            }
            l *= usb_gain;
            r *= usb_gain;
        } else {
            s_auto_latched_source = SOURCE_TONE;
            s_auto_usb_blend = 0.0f;

            float tone = next_tone_sample(volume, phase_step);
            l = tone;
            r = tone;
            g_sewu_state.active_source = SOURCE_TONE;
        }

        sewu_dsp_process_frame(&l, &r);

        float gate_target = 1.0f;
        if (g_sewu_state.standby_active) {
            gate_target = 0.0f;
        } else if (g_sewu_state.source_mode == SOURCE_USB && g_sewu_state.active_source == SOURCE_SILENCE) {
            gate_target = 0.0f;
        }
        s_output_gate += (gate_target - s_output_gate) * 0.01f;
        s_output_gate = clampf_audio(s_output_gate, 0.0f, 1.0f);

        l *= s_output_gate;
        r *= s_output_gate;
        
        l *= volume;
        r *= volume;

        samples[i * 2U] = (int16_t)(l * 32767.0f);
        samples[i * 2U + 1U] = (int16_t)(r * 32767.0f);
    }

    size_t bytes_written = 0;
    esp_err_t wr_err = i2s_channel_write(s_i2s_tx_chan, samples, sizeof(samples), &bytes_written, I2S_WRITE_TIMEOUT_MS);
    if (wr_err != ESP_OK) {
        g_sewu_state.audio_write_errors++;
        if (!s_warned_write_fail) {
            s_warned_write_fail = true;
            ESP_LOGW(TAG, "i2s write error=%d", (int)wr_err);
        }
    }
}
