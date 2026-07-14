#include "ota.h"
#include "event_log.h"
#include "led.h"
#include "network.h"
#include "health.h"
#include "config.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include <string.h>
#include <sys/param.h>
#include <inttypes.h>

static const char *TAG = "ota";

static bool update_in_progress = false;

static esp_err_t http_fetch_to_buffer(const char *url, char *buf, size_t buf_size)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "status %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int total = 0;
    while (total < buf_size - 1) {
        int r = esp_http_client_read(client, buf + total, buf_size - total - 1);
        if (r <= 0) break;
        total += r;
    }

    esp_http_client_cleanup(client);
    if (total <= 0) return ESP_FAIL;

    buf[total] = '\0';
    ESP_LOGI(TAG, "fetched: %s", buf);
    return ESP_OK;
}

static int parse_version(const char *version_str)
{
    // Skip 'v' prefix if present
    const char *p = version_str;
    while (*p == 'v' || *p == 'V') p++;
    int major = 0, minor = 0, patch = 0;
    if (sscanf(p, "%d.%d.%d", &major, &minor, &patch) >= 1) {
        return major * 10000 + minor * 100 + patch;
    }
    return -1;
}

static esp_err_t perform_ota(const char *firmware_url)
{
    ESP_LOGI(TAG, "Starting OTA from: %s", firmware_url);

    event_log_write(EVT_OTA_STARTED);
    led_send(LED_PATTERN_BOOT);

    // Remove ourselves from WDT — OTA download + SHA256 verification
    // can take 30-60+ seconds, which exceeds the WDT timeout
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "Removed OTA task from WDT for download");

    esp_http_client_config_t cfg = {
        .url = firmware_url,
        .timeout_ms = 120000,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);

    // Re-add ourselves to WDT
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        event_log_write(EVT_OTA_FAILED);
        led_send(LED_PATTERN_FAILURE);
        return err;
    }

    ESP_LOGI(TAG, "OTA successful, restarting...");
    event_log_write(EVT_OTA_SUCCESS);
    led_send(LED_PATTERN_TAG);

    // Give the newly-installed firmware a clean boot-loop slate so it is
    // not trapped in safe mode by THIS firmware's crash history.
    health_reset_boot_loop_state();

    health_mark_clean_reboot();  // intentional reboot — don't count as crash

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_check_update(void)
{
    if (update_in_progress) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "Cannot check OTA: WiFi not connected");
        return ESP_FAIL;
    }

    char version_buf[64];
    esp_err_t err = http_fetch_to_buffer(OTA_VERSION_URL, version_buf, sizeof(version_buf));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch version.txt");
        return err;
    }

    char *nl = strchr(version_buf, '\n');
    if (nl) *nl = '\0';
    nl = strchr(version_buf, '\r');
    if (nl) *nl = '\0';

    ESP_LOGI(TAG, "Remote version: %s, Local version: %s",
             version_buf, FW_VERSION);

    int remote_ver = parse_version(version_buf);
    int local_ver = parse_version(FW_VERSION);

    if (remote_ver < 0 || local_ver < 0) {
        ESP_LOGE(TAG, "Failed to parse version strings");
        return ESP_FAIL;
    }

    // Skip blacklisted versions (a build that failed to confirm). This
    // prevents an endless OTA re-install loop of a broken firmware.
    if (remote_ver == (int)health_get_ota_blacklist()) {
        ESP_LOGW(TAG, "Remote version %s is blacklisted (failed previously) — skipping",
                  version_buf);
        return ESP_OK;  // treated as up-to-date; do not re-install
    }

    if (remote_ver <= local_ver) {
        ESP_LOGI(TAG, "Firmware is up to date");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "New firmware available: %s", version_buf);

    update_in_progress = true;
    health_set_ota_attempt(remote_ver);  // record before install (broken-OTA guard)
    err = perform_ota(OTA_FIRMWARE_URL);
    update_in_progress = false;

    if (err != ESP_OK) {
        // Download failed — caller should retry with multi-attempt chain
        return ESP_ERR_NOT_FOUND;
    }

    return err;  // Should never reach here (perform_ota reboots on success)
}

esp_err_t ota_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running ? running->label : "unknown");

    // NOTE: Do NOT call esp_ota_mark_app_valid_cancel_rollback() here!
    // The health monitor's self-test (health.c) runs after all subsystems
    // are initialized and is the SOLE confirmer of OTA. If the self-test
    // passes, it confirms. If not, the bootloader rolls back.
    // This is the emergency hatch — never weld it shut early.

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Running in PENDING_VERIFY — awaiting self-test confirmation");
    }

    // ── Broken-OTA guard ──
    // If we previously attempted to install a version and are now running a
    // DIFFERENT version, the attempted build failed to confirm (bad self-test
    // or crash) and the bootloader rolled us back. Blacklist it so we don't
    // endlessly re-install a broken build.
    int current_ver = parse_version(FW_VERSION);
    uint32_t attempted = health_get_ota_last_attempt();
    if (attempted != 0 && attempted != (uint32_t)current_ver) {
        ESP_LOGW(TAG, "OTA: attempted v%" PRIu32 " but now running v%d — blacklisting v%" PRIu32,
                 attempted, current_ver, attempted);
        health_set_ota_blacklist(attempted);
        health_set_ota_attempt(0);  // clear marker so we don't re-blacklist
    }

    return ESP_OK;
}

static esp_err_t ota_perform_with_retries(void)
{
    const char *urls[] = {
        OTA_FIRMWARE_URL,                          // Attempt 1: Primary
        OTA_FALLBACK_URL,                          // Attempt 2: Fallback from config
        OTA_FIRMWARE_URL,                          // Attempt 3: Primary again
    };
    
    size_t num_urls = sizeof(urls) / sizeof(urls[0]);
    
    for (int attempt = 0; attempt < num_urls; attempt++) {
        // Skip empty fallback URL
        if (strlen(urls[attempt]) == 0) continue;
        
        ESP_LOGI(TAG, "OTA attempt %d/%d from: %s", attempt + 1, num_urls, urls[attempt]);
        
        esp_err_t err = perform_ota(urls[attempt]);
        if (err == ESP_OK) {
            return ESP_OK;  // Success — should never reach here (reboot happens in perform_ota)
        }
        
        ESP_LOGW(TAG, "OTA attempt %d failed: %s", attempt + 1, esp_err_to_name(err));
        
        // 5s delay between attempts (except after last)
        if (attempt < num_urls - 1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    
    ESP_LOGE(TAG, "All %d OTA attempts failed", num_urls);
    event_log_write(EVT_OTA_FAILED);
    led_send(LED_PATTERN_FAILURE);
    return ESP_FAIL;
}

void ota_task(void *pvParameters)
{
    ESP_LOGI(TAG, "[DBG] ota_task: starting...");
    ota_init();

    while (1) {
        esp_task_wdt_reset();

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "[DBG] ota_task: WiFi connected, checking for updates");
            esp_err_t err = ota_check_update();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "[DBG] ota_task: check OK (up to date or updated)");
            } else if (err == ESP_ERR_NOT_FOUND) {
                // New version was found but download failed — retry with multi-attempt
                ESP_LOGW(TAG, "[DBG] ota_task: OTA download failed, retrying...");
                err = ota_perform_with_retries();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "All OTA retries failed, will retry in 60s");
                }
            } else {
                // Version check failed (network etc) — just log, retry on next poll
                ESP_LOGW(TAG, "[DBG] ota_task: check failed (%s), will retry in 60s",
                         esp_err_to_name(err));
            }
        } else {
            ESP_LOGD(TAG, "[DBG] ota_task: WiFi not connected, skipping");
        }

        ESP_LOGD(TAG, "[DBG] ota_task: sleeping %dms", OTA_CHECK_INTERVAL_MS);
        // Sleep in 1-second chunks so WDT stays fed
        for (int i = 0; i < OTA_CHECK_INTERVAL_MS / 1000; i++) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
