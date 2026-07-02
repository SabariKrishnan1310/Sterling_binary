#include "config.h"
#include "rfid.h"
#include "storage.h"
#include "network.h"
#include "ota.h"
#include "led.h"
#include "event_log.h"
#include "provision.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_chip_info.h"
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
    // ═══ CRITICAL: LINE 1 — rollback confirm BEFORE anything ═══
    esp_ota_mark_app_valid_cancel_rollback();
    
    // ── WDT init ──
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);

    ESP_LOGI(TAG, "Sterling v1.0.6 — WiFi API Provisioning + Factory Recovery");
    
    // ── Task handles ──
    TaskHandle_t rfid_handle = NULL;
    TaskHandle_t upload_handle = NULL;
    TaskHandle_t ota_handle = NULL;

    // ── Init all subsystems ──
    nvs_flash_init();
    storage_init();
    event_log_init();
    event_log_write(EVT_BOOT);
    network_init();
    ota_init();
    storage_dump_stats();

    // ── Create tasks ──
    xTaskCreatePinnedToCore(led_task,   "led_task",   LED_STACK_SIZE,  NULL, 1, NULL,         1);
    xTaskCreatePinnedToCore(rfid_task,  "rfid_task",  RFID_STACK_SIZE, NULL, 3, &rfid_handle,  1);
    xTaskCreatePinnedToCore(network_wifi_task, "wifi_task", WIFI_STACK_SIZE, NULL, 2, NULL,     0);
    xTaskCreatePinnedToCore(upload_task,"upload_task", UPLOAD_STACK_SIZE, NULL, 1, &upload_handle,0);
    xTaskCreatePinnedToCore(ota_task,   "ota_task",   OTA_STACK_SIZE,  NULL, 1, &ota_handle,   0);
    
    // ── NEW: factory trigger monitor ──
    xTaskCreatePinnedToCore(factory_trigger_monitor_task, "factory_mon", 4096, NULL, 1, NULL, 0);

    // ── Register WDT ──
    register_watchdog(rfid_handle, "rfid");
    register_watchdog(upload_handle, "upload");
    register_watchdog(ota_handle, "ota");

    vTaskDelete(NULL);
}


