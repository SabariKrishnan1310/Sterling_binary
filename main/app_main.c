#include "config.h"
#include "rfid.h"
#include "storage.h"
#include "network.h"
#include "ota.h"
#include "led.h"
#include "event_log.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static const uint32_t RFID_STACK_SIZE   = 4096;
static const uint32_t LED_STACK_SIZE    = 2048;
static const uint32_t WIFI_STACK_SIZE   = 6144;
static const uint32_t UPLOAD_STACK_SIZE = 6144;
static const uint32_t OTA_STACK_SIZE    = 8192;

static void register_watchdog(TaskHandle_t task, const char *name)
{
    if (!task) return;
    esp_err_t err = esp_task_wdt_add(task);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WDT registered for %s", name);
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "WDT add for %s: %s", name, esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Sterling Prod v%s", FW_VERSION);
    ESP_LOGI(TAG, "========================================");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip: rev %d, cores: %d", chip_info.revision, chip_info.cores);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, performing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s, rebooting...", esp_err_to_name(err));
        esp_restart();
    }
    ESP_LOGI(TAG, "NVS initialized");

    err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Storage init failed: %s, rebooting...", esp_err_to_name(err));
        esp_restart();
    }

    err = event_log_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Event log init failed (non-fatal)");
    }

    event_log_write(EVT_BOOT);

    err = network_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed (non-fatal)");
    }

    ota_init();

    storage_dump_stats();

    TaskHandle_t rfid_handle = NULL;
    TaskHandle_t upload_handle = NULL;
    TaskHandle_t ota_handle = NULL;

    xTaskCreatePinnedToCore(
        led_task, "led_task", LED_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, NULL, 1);

    xTaskCreatePinnedToCore(
        rfid_task, "rfid_task", RFID_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 3, &rfid_handle, 1);

    xTaskCreatePinnedToCore(
        network_wifi_task, "wifi_task", WIFI_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 2, NULL, 0);

    xTaskCreatePinnedToCore(
        upload_task, "upload_task", UPLOAD_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, &upload_handle, 0);

    xTaskCreatePinnedToCore(
        ota_task, "ota_task", OTA_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, &ota_handle, 0);

    register_watchdog(rfid_handle, "rfid");
    register_watchdog(upload_handle, "upload");
    register_watchdog(ota_handle, "ota");

    ESP_LOGI(TAG, "All tasks created. System running.");
    ESP_LOGI(TAG, "  Core 0: wifi, upload, ota");
    ESP_LOGI(TAG, "  Core 1: rfid, storage, led");

    vTaskDelete(NULL);
}
