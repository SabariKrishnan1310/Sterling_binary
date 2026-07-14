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

void app_main(void)
{
    // ═══ CRITICAL: Do NOT confirm OTA here ═══
    // The health monitor's self-test will confirm after verifying
    // flash, heap, and partition are healthy. If self-test fails,
    // the bootloader rolls back to the previous firmware.
    // This is the emergency hatch — never weld it shut early.

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
    TaskHandle_t health_handle = NULL;

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

    // ═══ STEP 1 — WIFI STACK (safe, no connect) ═══
    // Must come before SoftAP. Non-fatal: if it fails we still try SoftAP
    // in AP-only fallback so the recovery dashboard can come up.
    esp_err_t net_err = network_init();
    if (net_err != ESP_OK) {
        ESP_LOGE(TAG, "network_init failed (%s) — will still attempt SoftAP",
                  esp_err_to_name(net_err));
    }

    // ═══ STEP 2 — SOFTAP FIRST (the emergency hatch) ═══
    // The recovery dashboard is brought up BEFORE anything risky so it is
    // always reachable, even if the rest of the system crashes. This is the
    // core of graceful degradation: the device can never be bricked because
    // the user can always reach http://192.168.4.1 and recover it.
    softap_init();
    esp_err_t sap_err = softap_start();
    if (sap_err != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP start FAILED (%s) — emergency hatch unavailable!",
                  esp_err_to_name(sap_err));
    } else {
        ESP_LOGI(TAG, "SoftAP dashboard UP FIRST at http://%s (SSID: %s)",
                  SOFTAP_IP_ADDR, SOFTAP_SSID);
    }

    // ═══ STEP 3 — storage / event log / health ═══
    storage_init();
    event_log_init();
    event_log_write(EVT_BOOT);
    health_init();  // Reads crash data from RTC; may set safe_mode

    // ═══ STEP 4 — SAFE MODE GATE ═══
    // Boot loop detected: run ONLY SoftAP + health monitor + OTA-recovery.
    // No RFID, no upload, no normal WiFi-connect task. The dashboard stays
    // up so the user can connect WiFi and the OTA task pulls a known-good
    // firmware from the server (the recovery path). Without OTA, safe mode
    // would be a dead end with no way to recover over the air.
    if (health_is_safe_mode()) {
        ESP_LOGE(TAG, "══════════════════════════════════════════════════");
        ESP_LOGE(TAG, "  SAFE MODE — SoftAP-only recovery.");
        ESP_LOGE(TAG, "  RFID/upload/normal-WiFi disabled. OTA recovery ON.");
        ESP_LOGE(TAG, "  Dashboard: http://%s (connect WiFi to pull good FW)",
                  SOFTAP_IP_ADDR);
        ESP_LOGE(TAG, "══════════════════════════════════════════════════");

        // ota_task calls ota_init() internally; it only acts when WiFi is
        // connected (via the dashboard), so it is safe to run here.
        xTaskCreatePinnedToCore(led_task, "led_task", LED_STACK_SIZE, NULL, 1, NULL, 1);
        xTaskCreatePinnedToCore(health_monitor_task, "health", 8192, NULL, 1, &health_handle, 0);
        xTaskCreatePinnedToCore(ota_task, "ota_task", OTA_STACK_SIZE, NULL, 1, &ota_handle, 0);

        if (health_handle) {
            health_register_task("health", health_handle, 8192);
            register_watchdog(health_handle, "health");
        }
        if (ota_handle) {
            health_register_task("ota", ota_handle, OTA_STACK_SIZE);
            register_watchdog(ota_handle, "ota");
        }
        ESP_LOGI(TAG, "Safe mode: SoftAP + health + OTA(recovery) started.");
        vTaskDelete(NULL);
    }

    // ═══ STEP 5 — NORMAL BOOT: full system ═══
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

    // ── Health monitor (red team + self-healing + crash tracking) ──
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

    ESP_LOGI(TAG, "All tasks started. SoftAP SSID: %s (always up)", SOFTAP_SSID);

    vTaskDelete(NULL);
}


