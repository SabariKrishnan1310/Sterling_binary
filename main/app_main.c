#include "config.h"
#include "rfid.h"
#include "storage.h"
#include "device.h"
#include "network.h"
#include "provision.h"
#include "ota.h"
#include "led.h"
#include "event_log.h"
#include "wireguard.h"
#include "mqtt.h"
#include "console.h"
#include "heartbeat.h"
#include "telemetry.h"
#include "tap_dispatcher.h"
#include "bulk.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static const uint32_t RFID_STACK_SIZE       = 4096;
static const uint32_t LED_STACK_SIZE        = 2048;
static const uint32_t WIFI_STACK_SIZE       = 6144;
static const uint32_t UPLOAD_STACK_SIZE     = 6144;
static const uint32_t OTA_STACK_SIZE        = 8192;
static const uint32_t MQTT_STACK_SIZE       = 4096;
static const uint32_t HEARTBEAT_STACK_SIZE  = 2048;
static const uint32_t WG_STACK_SIZE         = 4096;
static const uint32_t TELEMETRY_STACK_SIZE  = 4096;
static const uint32_t BULK_STACK_SIZE       = 4096;

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
    // Fix A (C-3): Confirm rollback BEFORE any init — prevents infinite reboot loop
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        ESP_LOGI(TAG, "Running partition: %s", running->label);
        esp_ota_img_states_t ota_state;
        esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
        if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "New firmware booted, confirming rollback cancel");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // Initialize WDT with generous timeout (monitor idle tasks on both cores)
    // NOTE: ESP-IDF boot already initializes TWDT before app_main();
    // we must deinit first, then reinit with our config.
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);

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

    err = device_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Device init failed (non-fatal)");
    }

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

    storage_dump_stats();

    TaskHandle_t rfid_handle = NULL;
    TaskHandle_t upload_handle = NULL;
    TaskHandle_t ota_handle = NULL;
    TaskHandle_t led_handle = NULL;

    xTaskCreatePinnedToCore(
        led_task, "led_task", LED_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, &led_handle, 1);

    xTaskCreatePinnedToCore(
        rfid_task, "rfid_task", RFID_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 3, &rfid_handle, 1);

    xTaskCreatePinnedToCore(
        network_wifi_task, "wifi_task", WIFI_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 2, NULL, 0);

    xTaskCreatePinnedToCore(
        upload_task, "upload_task", UPLOAD_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, &upload_handle, 0);

    if (!provision_is_done()) {
        ESP_LOGI(TAG, "Device not provisioned, creating provision task");
        xTaskCreatePinnedToCore(
            provision_task, "provision_task", 8192, NULL,
            tskIDLE_PRIORITY + 2, NULL, 0);
    } else {
        ESP_LOGI(TAG, "Device is already provisioned");
    }

    xTaskCreatePinnedToCore(
        ota_task, "ota_task", OTA_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, &ota_handle, 0);

    xTaskCreatePinnedToCore(
        wg_task, "wg_task", WG_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, NULL, 0);

    xTaskCreatePinnedToCore(
        mqtt_task, "mqtt_task", MQTT_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, NULL, 0);

    xTaskCreatePinnedToCore(
        heartbeat_task, "heartbeat", HEARTBEAT_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 2, NULL, 0);

    xTaskCreatePinnedToCore(
        telemetry_task, "telemetry_task", TELEMETRY_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 2, NULL, 0);

    xTaskCreatePinnedToCore(
        bulk_task, "bulk_task", BULK_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, NULL, 0);

    tap_dispatcher_init();

    register_watchdog(rfid_handle, "rfid");
    register_watchdog(upload_handle, "upload");
    register_watchdog(ota_handle, "ota");
    register_watchdog(led_handle, "led");

    xTaskCreatePinnedToCore(
        console_task, "console_task", CONSOLE_STACK_SIZE, NULL,
        CONSOLE_TASK_PRIORITY, NULL, 0);

    ESP_LOGI(TAG, "All tasks created. System running.");
    ESP_LOGI(TAG, "  Core 0: wifi, upload, ota, wg, console");
    ESP_LOGI(TAG, "  Core 1: rfid, storage, led");

    vTaskDelete(NULL);
}
