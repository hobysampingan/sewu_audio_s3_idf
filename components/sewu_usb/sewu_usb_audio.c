#include "sewu_usb_audio.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "sewu_app_state.h"

#if defined(__has_include)
#if __has_include("usb_device_uac.h")
#include "usb_device_uac.h"
#define SEWU_HAS_IDF_UAC 1
#else
#define SEWU_HAS_IDF_UAC 0
#endif
#else
#define SEWU_HAS_IDF_UAC 0
#endif

static const char *TAG = "sewu_usb";

#define USB_RING_FRAMES 16384U
#define USB_FRAME_BYTES (sizeof(int16_t) * 2U)
#define USB_RING_BYTES (USB_RING_FRAMES * USB_FRAME_BYTES)
#define USB_WRITE_CHUNK_FRAMES 256U

static RingbufHandle_t s_usb_ring;
static uint8_t *s_rx_item;
static size_t s_rx_item_size;
static size_t s_rx_item_off;
static uint8_t s_partial_frame[USB_FRAME_BYTES];
static size_t s_partial_len;

static uint32_t s_last_rx_ms;
static uint32_t s_last_tune_ms;
static uint32_t s_last_health_ms;
static uint32_t s_last_underruns_snapshot;
static uint32_t s_last_overruns_snapshot;
static uint32_t s_last_underruns_health;
static uint32_t s_last_overruns_health;

static int clamp_int(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static size_t ring_items_waiting_bytes(void) {
    if (s_usb_ring == NULL) {
        return 0U;
    }
    UBaseType_t waiting = 0;
    vRingbufferGetInfo(s_usb_ring, NULL, NULL, NULL, NULL, &waiting);
    return (size_t)waiting;
}

static size_t ring_buffered_bytes(void) {
    size_t buffered = ring_items_waiting_bytes();
    if (s_rx_item != NULL && s_rx_item_size > s_rx_item_off) {
        buffered += (s_rx_item_size - s_rx_item_off);
    }
    buffered += s_partial_len;
    return buffered;
}

static void rx_item_release_if_needed(void) {
    if (s_rx_item != NULL) {
        vRingbufferReturnItem(s_usb_ring, s_rx_item);
        s_rx_item = NULL;
        s_rx_item_size = 0U;
        s_rx_item_off = 0U;
    }
}

static bool rx_item_acquire_if_needed(void) {
    if (s_rx_item != NULL && s_rx_item_off < s_rx_item_size) {
        return true;
    }

    rx_item_release_if_needed();
    if (s_usb_ring == NULL) {
        return false;
    }

    s_rx_item = (uint8_t *)xRingbufferReceiveUpTo(s_usb_ring, &s_rx_item_size, 0, USB_FRAME_BYTES * 128U);
    s_rx_item_off = 0U;
    return (s_rx_item != NULL && s_rx_item_size > 0U);
}


static void ring_push_with_overwrite(const uint8_t *data, size_t bytes) {
    if (s_usb_ring == NULL || data == NULL || bytes == 0U) {
        return;
    }

    if (bytes > USB_RING_BYTES) {
        data += (bytes - USB_RING_BYTES);
        bytes = USB_RING_BYTES;
    }

    /* If the buffer is full, do NOT try to consume from it on the Producer thread!
     * FreeRTOS ringbuffers are Single-Producer Single-Consumer. Calling receive
     * from this thread corrupts the ringbuffer if the Audio thread is also reading.
     * Just drop the packet and count it as an overrun. */
    if (xRingbufferSend(s_usb_ring, data, bytes, 0) != pdTRUE) {
        g_sewu_state.usb_overruns++;
    }
}

#if SEWU_HAS_IDF_UAC
static esp_err_t sewu_uac_output_cb(uint8_t *buf, size_t len, void *cb_ctx) {
    (void)cb_ctx;
    sewu_usb_audio_write_pcm16_bytes(buf, len, 2);
    return ESP_OK;
}

static void sewu_uac_set_mute_cb(uint32_t mute, void *cb_ctx) {
    (void)cb_ctx;
    g_sewu_state.usb_host_muted = (mute != 0U);
    ESP_LOGI(TAG, "host mute=%d", g_sewu_state.usb_host_muted ? 1 : 0);
}

static void sewu_uac_set_volume_cb(uint32_t volume, void *cb_ctx) {
    (void)cb_ctx;
    int host_volume = clamp_int((int)volume, 0, 100);
    g_sewu_state.usb_host_volume_percent = host_volume;
    ESP_LOGI(TAG, "host volume=%d%%", host_volume);
}
#endif

void sewu_usb_audio_init(void) {
    g_sewu_state.usb_ready = false;
    g_sewu_state.usb_driver_ready = false;
    g_sewu_state.usb_streaming = false;
    g_sewu_state.standby_active = false;
    g_sewu_state.usb_target_fill_percent = 25;
    g_sewu_state.usb_health_percent = 0;
    g_sewu_state.usb_health_state = 2;
    g_sewu_state.usb_latency_ms = 0;
    g_sewu_state.usb_host_volume_percent = 100;
    g_sewu_state.usb_host_muted = false;
    if (g_sewu_state.source_mode < 0 || g_sewu_state.source_mode > 2) {
        g_sewu_state.source_mode = 0;
    }
    g_sewu_state.active_source = (g_sewu_state.source_mode == 1) ? 3 : 2;

    if (s_usb_ring != NULL) {
        rx_item_release_if_needed();
        vRingbufferDelete(s_usb_ring);
        s_usb_ring = NULL;
    }

    s_usb_ring = xRingbufferCreate(USB_RING_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (s_usb_ring == NULL) {
        ESP_LOGE(TAG, "ring buffer alloc failed (%u bytes)", (unsigned)USB_RING_BYTES);
    }

#if SEWU_HAS_IDF_UAC
    uac_device_config_t cfg = {
        .skip_tinyusb_init = false,
        .output_cb = sewu_uac_output_cb,
        .input_cb = NULL,
        .set_mute_cb = sewu_uac_set_mute_cb,
        .set_volume_cb = sewu_uac_set_volume_cb,
        .cb_ctx = NULL,
    };

    g_sewu_state.usb_driver_ready = (uac_device_init(&cfg) == ESP_OK);
    ESP_LOGI(TAG, "usb_device_uac=%s", g_sewu_state.usb_driver_ready ? "ready" : "init-failed");
#else
    ESP_LOGW(TAG, "usb_device_uac header not found, running in bridge-only mode");
#endif
}

void sewu_usb_audio_update(void) {
    uint32_t now = (uint32_t)(esp_log_timestamp());
    bool has_recent_rx = (now - s_last_rx_ms) < 500U;

    size_t buffered_frames = ring_buffered_bytes() / USB_FRAME_BYTES;
    size_t ring_capacity = sewu_usb_audio_capacity_frames();

    g_sewu_state.usb_streaming = has_recent_rx && (buffered_frames > 64U);
    g_sewu_state.usb_ready = g_sewu_state.usb_streaming || g_sewu_state.usb_driver_ready;
    g_sewu_state.usb_fill_percent = clamp_int((int)((buffered_frames * 100U) / ring_capacity), 0, 100);

    if (g_sewu_state.source_mode == 1) {
        const uint32_t stby_ms = (g_sewu_state.standby_timeout_ms < 5000U) ? 5000U : g_sewu_state.standby_timeout_ms;
        g_sewu_state.standby_active = !g_sewu_state.usb_streaming && ((now - s_last_rx_ms) >= stby_ms);
    } else {
        g_sewu_state.standby_active = false;
    }

    g_sewu_state.usb_latency_ms = (int)(((uint32_t)buffered_frames * 1000U) / SEWU_AUDIO_SAMPLE_RATE);

    if ((now - s_last_health_ms) >= 500U) {
        size_t target_frames = ((size_t)g_sewu_state.usb_target_fill_percent * ring_capacity) / 100U;
        size_t diff = (buffered_frames > target_frames) ? (buffered_frames - target_frames) : (target_frames - buffered_frames);

        int health = 100 - (int)((diff * 100U) / (ring_capacity / 2U));
        if (health < 0) health = 0;
        if (now - s_last_health_ms >= 10000) {
            s_last_underruns_health = g_sewu_state.usb_underruns;
            s_last_overruns_health = g_sewu_state.usb_overruns;
        }

        /* Minimal health never below 70 when streaming normally */
        if (g_sewu_state.usb_streaming) {
            if (health < 70) health = 70;
        }

        if (health < 0) health = 0;
        if (health > 100) health = 100;
        g_sewu_state.usb_health_percent = health;

        /* Health thresholds: OK >=90, BUSY >=50, RISK <50 */
        if (health >= 90) {
            g_sewu_state.usb_health_state = 0;
        } else if (health >= 50) {
            g_sewu_state.usb_health_state = 1;
        } else {
            g_sewu_state.usb_health_state = 2;
        }

        s_last_health_ms = now;
    }

    if ((now - s_last_tune_ms) >= 1500U) {
        uint32_t du = g_sewu_state.usb_underruns - s_last_underruns_snapshot;
        uint32_t dov = g_sewu_state.usb_overruns - s_last_overruns_snapshot;

        if (du > 2U && g_sewu_state.usb_target_fill_percent < 60) {
            g_sewu_state.usb_target_fill_percent += 5;
        } else if (dov > 2U && g_sewu_state.usb_target_fill_percent > 15) {
            g_sewu_state.usb_target_fill_percent -= 5;
        }

        g_sewu_state.usb_target_fill_percent = clamp_int(g_sewu_state.usb_target_fill_percent, 10, 70);
        s_last_underruns_snapshot = g_sewu_state.usb_underruns;
        s_last_overruns_snapshot = g_sewu_state.usb_overruns;
        s_last_tune_ms = now;
    }
}

void sewu_usb_audio_write_pcm16_bytes(const uint8_t *pcm_bytes, size_t bytes, uint8_t channels) {
    if (pcm_bytes == NULL || bytes < 2U || channels == 0U) {
        return;
    }

    const size_t total_samples = bytes / sizeof(int16_t);
    const int16_t *samples = (const int16_t *)pcm_bytes;
    const size_t frames = (channels == 1U) ? total_samples : (total_samples / channels);

    if (frames == 0U) {
        return;
    }

    int16_t stage[USB_WRITE_CHUNK_FRAMES * 2U];
    size_t idx = 0U;
    while (idx < frames) {
        size_t block = frames - idx;
        if (block > USB_WRITE_CHUNK_FRAMES) {
            block = USB_WRITE_CHUNK_FRAMES;
        }

        for (size_t i = 0; i < block; ++i) {
            int16_t l;
            int16_t r;
            size_t src = idx + i;

            if (channels == 1U) {
                l = samples[src];
                r = l;
            } else {
                l = samples[src * channels];
                r = samples[src * channels + 1U];
            }

            stage[i * 2U] = l;
            stage[i * 2U + 1U] = r;
        }

        ring_push_with_overwrite((const uint8_t *)stage, block * USB_FRAME_BYTES);
        idx += block;
    }

    g_sewu_state.usb_frames_in += (uint32_t)frames;
    s_last_rx_ms = (uint32_t)esp_log_timestamp();
}

void sewu_usb_audio_write_frames(const int16_t *interleaved_stereo, size_t frames) {
    if (interleaved_stereo == NULL || frames == 0U) {
        return;
    }

    ring_push_with_overwrite((const uint8_t *)interleaved_stereo, frames * USB_FRAME_BYTES);
    g_sewu_state.usb_frames_in += (uint32_t)frames;
    s_last_rx_ms = (uint32_t)esp_log_timestamp();
}

bool sewu_usb_audio_read_frame(float *left, float *right) {
    if (left == NULL || right == NULL) {
        return false;
    }

    while (s_partial_len < USB_FRAME_BYTES) {
        if (!rx_item_acquire_if_needed()) {
            break;
        }

        size_t avail = s_rx_item_size - s_rx_item_off;
        size_t need = USB_FRAME_BYTES - s_partial_len;
        size_t take = (avail < need) ? avail : need;

        memcpy(&s_partial_frame[s_partial_len], &s_rx_item[s_rx_item_off], take);
        s_partial_len += take;
        s_rx_item_off += take;

        if (s_rx_item_off >= s_rx_item_size) {
            rx_item_release_if_needed();
        }
    }

    if (s_partial_len < USB_FRAME_BYTES) {
        g_sewu_state.usb_underruns++;
        *left = 0.0f;
        *right = 0.0f;
        return false;
    }

    int16_t l = (int16_t)((uint16_t)s_partial_frame[0] | ((uint16_t)s_partial_frame[1] << 8));
    int16_t r = (int16_t)((uint16_t)s_partial_frame[2] | ((uint16_t)s_partial_frame[3] << 8));

    *left = (float)l / 32768.0f;
    *right = (float)r / 32768.0f;

    s_partial_len = 0U;
    return true;
}

// ---------------------------------------------------------------------------
// sewu_usb_audio_read_batch()
//
// Reads up to num_frames stereo samples from the ringbuffer in one tight loop,
// re-using the existing sliding-window item mechanism so we never hold more than
// one ringbuffer item token at a time (safe with FreeRTOS BYTEBUF).
//
// This replaces num_frames individual sewu_usb_audio_read_frame() calls and
// their associated per-frame FreeRTOS locking with a single contiguous pass,
// which eliminates lock-contention jitter on the real-time audio Core 1.
//
// Returns: number of frames actually decoded (may be < num_frames if underrun).
// ---------------------------------------------------------------------------
size_t sewu_usb_audio_read_batch(float *left_buf, float *right_buf, size_t num_frames) {
    if (left_buf == NULL || right_buf == NULL || num_frames == 0) {
        return 0;
    }

    size_t frames_done = 0;

    while (frames_done < num_frames) {
        // Ensure partial_frame is fully assembled for this frame
        while (s_partial_len < USB_FRAME_BYTES) {
            if (!rx_item_acquire_if_needed()) {
                goto batch_done;  // Ringbuffer empty, underrun
            }

            size_t avail = s_rx_item_size - s_rx_item_off;
            size_t need  = USB_FRAME_BYTES - s_partial_len;
            size_t take  = (avail < need) ? avail : need;

            memcpy(&s_partial_frame[s_partial_len], &s_rx_item[s_rx_item_off], take);
            s_partial_len  += take;
            s_rx_item_off  += take;

            if (s_rx_item_off >= s_rx_item_size) {
                rx_item_release_if_needed();
            }
        }

        // Decode the assembled frame (little-endian PCM16 stereo)
        int16_t l16 = (int16_t)((uint16_t)s_partial_frame[0] | ((uint16_t)s_partial_frame[1] << 8));
        int16_t r16 = (int16_t)((uint16_t)s_partial_frame[2] | ((uint16_t)s_partial_frame[3] << 8));

        left_buf[frames_done]  = (float)l16 / 32768.0f;
        right_buf[frames_done] = (float)r16 / 32768.0f;
        s_partial_len = 0;
        frames_done++;
    }

batch_done:
    if (frames_done < num_frames) {
        g_sewu_state.usb_underruns += (uint32_t)(num_frames - frames_done);
    }
    return frames_done;
}

size_t sewu_usb_audio_buffered_frames(void) {
    return ring_buffered_bytes() / USB_FRAME_BYTES;
}

size_t sewu_usb_audio_capacity_frames(void) {
    return USB_RING_FRAMES;
}

void sewu_usb_audio_reset_stats(void) {
    g_sewu_state.usb_frames_in = 0;
    g_sewu_state.usb_underruns = 0;
    g_sewu_state.usb_overruns = 0;
    g_sewu_state.usb_last_reset_ms = (uint32_t)esp_log_timestamp();
    s_last_underruns_snapshot = 0;
    s_last_overruns_snapshot = 0;
}
