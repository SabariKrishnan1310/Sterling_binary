#include "config.h"
#include "rfid.h"
#include "storage.h"
#include "network.h"
#include "ota.h"
#include "led.h"
#include "event_log.h"
#include "provision.h"
#include "softap.h"
#include "health.h"
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

// SoftAP boot window: always active for first 2 minutes
// then stops if WiFi is connected
static void softap_boot_window_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SoftAP boot window: %ds (SSID: %s)",
             SOFTAP_BOOT_WINDOW_MS / 1000, SOFTAP_SSID);

    // Always start SoftAP on boot
    softap_init();
    softap_start();

    // Wait for boot window to expire
    vTaskDelay(pdMS_TO_TICKS(SOFTAP_BOOT_WINDOW_MS));

    // If WiFi is connected, we can stop SoftAP
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if ((bits & WIFI_CONNECTED_BIT) && softap_is_active()) {
        ESP_LOGI(TAG, "WiFi connected, stopping SoftAP");
        softap_stop();
    }
    vTaskDelete(NULL);
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

    ESP_LOGI(TAG, "Sterling v%s — Max TX Power + Scan + Round-Robin + SoftAP", FW_VERSION);
    
    // ── Task handles ──
    TaskHandle_t rfid_handle = NULL;
    TaskHandle_t upload_handle = NULL;
    TaskHandle_t ota_handle = NULL;

    // ── Init NVS with corruption recovery ──
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupted (0x%x), erasing + reinit", nvs_err);
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init FAILED: %s", esp_err_to_name(nvs_err));
    }

    // ── Init all subsystems ──
    storage_init();
    event_log_init();
    event_log_write(EVT_BOOT);
    health_init();  // Must be BEFORE network_init — reads crash data from RTC
    network_init();
    ota_init();
    storage_dump_stats();

    // ── Create tasks ──
    xTaskCreatePinnedToCore(led_task,   "led_task",   LED_STACK_SIZE,  NULL, 1, NULL,         1);
    xTaskCreatePinnedToCore(rfid_task,  "rfid_task",  RFID_STACK_SIZE, NULL, 3, &rfid_handle,  1);
    xTaskCreatePinnedToCore(network_wifi_task, "wifi_task", WIFI_STACK_SIZE, NULL, 2, NULL,     0);
    xTaskCreatePinnedToCore(upload_task,"upload_task", UPLOAD_STACK_SIZE, NULL, 1, &upload_handle,0);
    xTaskCreatePinnedToCore(ota_task,   "ota_task",   OTA_STACK_SIZE,  NULL, 1, &ota_handle,   0);
    
    // ── Factory trigger monitor ──
    xTaskCreatePinnedToCore(factory_trigger_monitor_task, "factory_mon", 4096, NULL, 1, NULL, 0);

    // ── SoftAP boot window (always active for 2 min on boot) ──
    xTaskCreatePinnedToCore(softap_boot_window_task, "softap_win", 8192, NULL, 1, NULL, 0);

    // ── Health monitor (red team + self-healing + crash tracking) ──
    TaskHandle_t health_handle = NULL;
    xTaskCreatePinnedToCore(health_monitor_task, "health", 8192, NULL, 1, &health_handle, 0);

    // ── Register critical tasks with health monitor ──
    health_register_task("rfid",   rfid_handle,   RFID_STACK_SIZE);
    health_register_task("upload", upload_handle,  UPLOAD_STACK_SIZE);
    health_register_task("ota",    ota_handle,     OTA_STACK_SIZE);
    health_register_task("health", health_handle,  8192);

    // ── Register WDT ──
    register_watchdog(rfid_handle, "rfid");
    register_watchdog(upload_handle, "upload");
    register_watchdog(ota_handle, "ota");

    ESP_LOGI(TAG, "All tasks started. SoftAP SSID: %s", SOFTAP_SSID);

    vTaskDelete(NULL);
}


