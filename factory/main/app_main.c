// ==========================================================
// STERLING BOOTSTRAP v2.0.0
// ==========================================================
// Minimal firmware: SoftAP + Web UI + OTA
// Lives in factory partition. No WiFi connect, no config fetch.
// User configures WiFi via web dashboard, then installs
// main firmware from Sterling_Prod repo.
// ==========================================================

#include "config.h"
#include "softap.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "BOOTSTRAP";

void app_main(void)
{
    // ── LED init — quick "I'm alive" blink ──
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << STATUS_LED);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    for (int i = 0; i < 3; i++) {
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(150 / portTICK_PERIOD_MS);
        gpio_set_level(STATUS_LED, 0);
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }

    // ── NVS init ──
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasing and reinitializing");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // ── Start SoftAP + HTTP server ──
    ESP_LOGI(TAG, "Starting Sterling Bootstrap v%s", BOOTSTRAP_VERSION);
    err = softap_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP init failed: %s — SOS blink forever", esp_err_to_name(err));
        while (1) {
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100 / portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }

    ESP_LOGI(TAG, "Bootstrap running. Connect to WiFi '%s' and open http://%s",
             SOFTAP_SSID, SOFTAP_IP_ADDR);

    // ── LED on = SoftAP ready ──
    gpio_set_level(STATUS_LED, 1);

    // Bootstrap has nothing else to do — HTTP server runs in its own task.
    // The vTaskDelete(NULL) is technically not needed since app_main never returns,
    // but we keep it to free the 8KB app_main stack.
    vTaskDelete(NULL);
}
