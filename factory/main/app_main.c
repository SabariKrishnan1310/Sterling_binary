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
#include "network.h"
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
    // On a fresh device (or after NVS erase) nvs_flash_init() returns
    // ESP_ERR_NVS_NOT_FOUND — the partition exists but is unformatted.
    // If we don't erase+reinit here, every later nvs_open() fails and the
    // STA can never connect, so the recovery can't reach GitHub to install
    // the main firmware. Handle NOT_FOUND exactly like the other corrupt
    // states (matches main/app_main.c).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NOT_FOUND ||
        err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasing and reinitializing (was %s)",
                  esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init FAILED: %s — STA WiFi will be unavailable",
                  esp_err_to_name(err));
    }

    // ── Start SoftAP + HTTP server ──
    // The SoftAP + dashboard is the ONLY recovery surface on this partition.
    // If it fails to come up we must NOT freeze forever (a frozen device is a
    // brick). Instead we retry a bounded number of times, then restart the chip
    // — the bootloader returns here (factory is permanent), so a restart just
    // retries cleanly. If the radio is truly wedged, a power cycle clears it.
    ESP_LOGI(TAG, "Starting Sterling Bootstrap v%s", BOOTSTRAP_VERSION);
    #define SOFTAP_INIT_RETRIES 3
    int sap_tries = 0;
    while ((err = softap_init()) != ESP_OK) {
        sap_tries++;
        ESP_LOGE(TAG, "SoftAP init failed (try %d/%d): %s",
                  sap_tries, SOFTAP_INIT_RETRIES, esp_err_to_name(err));
        // SOS blink while we wait before retrying
        for (int i = 0; i < 5; i++) {
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100 / portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        if (sap_tries >= SOFTAP_INIT_RETRIES) {
            ESP_LOGE(TAG, "SoftAP init failed %d times — restarting chip (returns to factory)",
                      sap_tries);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    }

    // ── Bring up STA and connect to the stored/default WiFi profile ──
    // This runs ALONGSIDE the SoftAP (APSTA mode) so the bootstrap can reach
    // the internet and download the main firmware, while the dashboard stays
    // reachable over the SoftAP.
    wifi_connect_sta();

    ESP_LOGI(TAG, "Bootstrap running. Connect to WiFi '%s' and open http://%s",
             SOFTAP_SSID, SOFTAP_IP_ADDR);

    // ── LED on = SoftAP ready ──
    gpio_set_level(STATUS_LED, 1);

    // Bootstrap has nothing else to do — HTTP server runs in its own task.
    // The vTaskDelete(NULL) is technically not needed since app_main never returns,
    // but we keep it to free the 8KB app_main stack.
    vTaskDelete(NULL);
}
