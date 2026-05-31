#include "sewu_wifi.h"

#include "esp_log.h"
#include "sewu_app_state.h"

static const char *TAG = "sewu_wifi";

void sewu_wifi_init(void) {
    g_sewu_state.wifi_ready = false;
    ESP_LOGI(TAG, "wifi stream placeholder initialized (phase 7 in progress)");
}

void sewu_wifi_update(void) {
    // Phase 7 target: DLNA/HTTP receiver and source switching.
}