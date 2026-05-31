#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "sewu_app_state.h"
#include "sewu_audio_engine.h"
#include "sewu_dsp.h"
#include "sewu_input.h"
#include "sewu_settings.h"
#include "sewu_ui.h"
#include "sewu_usb_audio.h"
#include "sewu_wifi.h"

static const char *TAG = "sewu_main";

static uint32_t s_led_tick_ms;
static bool s_led_state;
static uint32_t s_log_tick_ms;
static uint32_t s_last_risk_log_ms;
static uint32_t s_last_frames_in;
static uint32_t s_last_underruns;
static uint32_t s_last_overruns;

#define STRESS_LOG_INTERVAL_MS 10000U
#define STRESS_RISK_LOG_INTERVAL_MS 15000U
#define AUDIO_TASK_STACK_WORDS 6144
#define AUDIO_TASK_PRIO 23
#define AUDIO_TASK_CORE 1
#define SERVICE_TASK_STACK_WORDS 8192
#define SERVICE_TASK_PRIO 6
#define SERVICE_TASK_CORE 0

static const char *profile_label(int profile) {
    switch (profile) {
        case 0: return "STABLE";
        case 1: return "BAL";
        default: return "MAX";
    }
}

static const char *source_mode_label(int mode) {
    switch (mode) {
        case 0: return "AUTO";
        case 1: return "USB";
        case 2: return "TONE";
        default: return "UNK";
    }
}

static const char *active_source_label(int src) {
    switch (src) {
        case 1: return "USB";
        case 2: return "TONE";
        case 3: return "SIL";
        default: return "N/A";
    }
}

static const char *health_label(int state) {
    switch (state) {
        case 0: return "OK";
        case 1: return "BUSY";
        default: return "RISK";
    }
}

static void update_stress_logger(void) {
    uint32_t now = (uint32_t)esp_log_timestamp();
    if ((now - s_log_tick_ms) < STRESS_LOG_INTERVAL_MS) {
        return;
    }
    s_log_tick_ms = now;

    uint32_t d_frames = g_sewu_state.usb_frames_in - s_last_frames_in;
    uint32_t d_underrun = g_sewu_state.usb_underruns - s_last_underruns;
    uint32_t d_overrun = g_sewu_state.usb_overruns - s_last_overruns;

    s_last_frames_in = g_sewu_state.usb_frames_in;
    s_last_underruns = g_sewu_state.usb_underruns;
    s_last_overruns = g_sewu_state.usb_overruns;

    ESP_LOGI(
        TAG,
        "[STRESS] t=%lus pf=%s mode=%s src=%s strm=%d fill=%d/%d lat=%dms h=%s(%d) band=%d ur=%lu(+%lu) or=%lu(+%lu) fin=%lu(+%lu) awe=%lu\n",
        (unsigned long)(now / 1000U),
        profile_label(g_sewu_state.performance_profile),
        source_mode_label(g_sewu_state.source_mode),
        active_source_label(g_sewu_state.active_source),
        g_sewu_state.usb_streaming ? 1 : 0,
        g_sewu_state.usb_fill_percent,
        g_sewu_state.usb_target_fill_percent,
        g_sewu_state.usb_latency_ms,
        health_label(g_sewu_state.usb_health_state),
        g_sewu_state.usb_health_percent,
        g_sewu_state.vis_active_bands,
        (unsigned long)g_sewu_state.usb_underruns,
        (unsigned long)d_underrun,
        (unsigned long)g_sewu_state.usb_overruns,
        (unsigned long)d_overrun,
        (unsigned long)g_sewu_state.usb_frames_in,
        (unsigned long)d_frames,
        (unsigned long)g_sewu_state.audio_write_errors);

    ESP_LOGI(TAG,
             "[HOST] vol=%d%% mute=%d drv=%d\n",
             g_sewu_state.usb_host_volume_percent,
             g_sewu_state.usb_host_muted ? 1 : 0,
             g_sewu_state.usb_driver_ready ? 1 : 0);

if ((d_underrun > 0U || d_overrun > 0U || g_sewu_state.usb_health_percent < 30) &&
         (now - s_last_risk_log_ms) >= STRESS_RISK_LOG_INTERVAL_MS) {
        s_last_risk_log_ms = now;
        ESP_LOGW(
            TAG,
            "[ALERT] t=%lus health=%s(%d) ur+%lu or+%lu standby=%d src=%s",
            (unsigned long)(now / 1000U),
            health_label(g_sewu_state.usb_health_state),
            g_sewu_state.usb_health_percent,
            (unsigned long)d_underrun,
            (unsigned long)d_overrun,
            g_sewu_state.standby_active ? 1 : 0,
            active_source_label(g_sewu_state.active_source));
    }
}

static void sewu_audio_task(void *arg) {
    (void)arg;
    while (1) {
        sewu_audio_engine_update();
        if (!g_sewu_state.i2s_ready) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void sewu_service_task(void *arg) {
    (void)arg;
    while (1) {
        sewu_input_update();
        sewu_dsp_update();
        sewu_usb_audio_update();
        sewu_wifi_update();
        sewu_ui_update();
        sewu_settings_update();
        update_stress_logger();

        uint32_t now = (uint32_t)esp_log_timestamp();
        uint32_t blink_period = g_sewu_state.i2s_ready ? 400U : 120U;
        if ((now - s_led_tick_ms) >= blink_period) {
            s_led_tick_ms = now;
            s_led_state = !s_led_state;
            gpio_set_level(SEWU_PIN_LED_STATUS, s_led_state ? 1 : 0);
        }

        /* Give the service task more idle time to avoid CPU0 WDT issues. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "boot: app_main start");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "boot: nvs ready");

    sewu_app_state_init_defaults();
    g_sewu_state.boot_ms = (uint32_t)esp_log_timestamp();
    g_sewu_state.last_input_ms = g_sewu_state.boot_ms;
    ESP_LOGI(TAG, "boot: state ready");

    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << SEWU_PIN_LED_STATUS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(SEWU_PIN_LED_STATUS, 0);
    ESP_LOGI(TAG, "boot: led ready");

    sewu_settings_init();
    ESP_LOGI(TAG, "boot: settings ready");
    sewu_input_init();
    ESP_LOGI(TAG, "boot: input ready");
    sewu_dsp_init();
    ESP_LOGI(TAG, "boot: dsp ready");
    sewu_usb_audio_init();
    ESP_LOGI(TAG, "boot: usb ready");
    sewu_wifi_init();
    ESP_LOGI(TAG, "boot: wifi ready");
    sewu_audio_engine_init();
    ESP_LOGI(TAG, "boot: audio ready");
    sewu_ui_init();
    ESP_LOGI(TAG, "boot: ui init returned");

    s_log_tick_ms = (uint32_t)esp_log_timestamp();
    s_last_risk_log_ms = s_log_tick_ms;

    ESP_LOGI(TAG, "SEWU AUDIO S3 IDF build ready");
    ESP_LOGI(TAG, "Stress logger active (period=10s)");
    ESP_LOGI(TAG, "Starting tasks: audio(core=%d,prio=%d), service(core=%d,prio=%d)",
             AUDIO_TASK_CORE, AUDIO_TASK_PRIO, SERVICE_TASK_CORE, SERVICE_TASK_PRIO);

    BaseType_t ok_audio = xTaskCreatePinnedToCore(
        sewu_audio_task,
        "sewu_audio_task",
        AUDIO_TASK_STACK_WORDS,
        NULL,
        AUDIO_TASK_PRIO,
        NULL,
        AUDIO_TASK_CORE);

    BaseType_t ok_service = xTaskCreatePinnedToCore(
        sewu_service_task,
        "sewu_service_task",
        SERVICE_TASK_STACK_WORDS,
        NULL,
        SERVICE_TASK_PRIO,
        NULL,
        SERVICE_TASK_CORE);

    if (ok_audio != pdPASS || ok_service != pdPASS) {
        ESP_LOGE(TAG, "Task creation failed audio=%d service=%d", (int)ok_audio, (int)ok_service);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
