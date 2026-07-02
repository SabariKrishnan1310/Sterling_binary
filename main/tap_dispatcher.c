#include "tap_dispatcher.h"
#include "esp_log.h"

static const char *TAG = "tap_dispatch";

esp_err_t tap_dispatcher_init(void)
{
    ESP_LOGI(TAG, "Tap dispatcher initialized");
    ESP_LOGI(TAG, "Delivery order: MQTT -> HTTP -> LittleFS");
    ESP_LOGI(TAG, "  (handled by network_send_tap_single with MQTT-first fallback chain)");
    return ESP_OK;
}
