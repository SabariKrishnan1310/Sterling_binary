#include "ota.h"
#include "event_log.h"
#include "led.h"
#include "network.h"
#include "storage.h"
#include "config.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "ota";

bool ota_trigger_flag = false;
char ota_custom_url[256] = {0};

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
    int major = 0, minor = 0, patch = 0;
    if (sscanf(version_str, "%d.%d.%d", &major, &minor, &patch) >= 1) {
        return major * 10000 + minor * 100 + patch;
    }
    return -1;
}

static const char* get_firmware_url(void)
{
    if (strlen(ota_custom_url) > 0) {
        ESP_LOGI(TAG, "Using custom URL from RAM: %s", ota_custom_url);
        return ota_custom_url;
    }

    nvs_handle_t h;
    static char url[256];
    esp_err_t err = nvs_open("ota", NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = sizeof(url);
        err = nvs_get_str(h, "firmware_url", url, &len);
        nvs_close(h);
        if (err == ESP_OK && len > 1) {
            ESP_LOGI(TAG, "Using custom URL from NVS: %s", url);
            return url;
        }
    }

    return OTA_FIRMWARE_URL;
}

static bool ota_precheck(void)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "OTA precheck FAILED: WiFi not connected");
        return false;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        if (ap_info.rssi < -80) {
            ESP_LOGW(TAG, "OTA precheck FAILED: RSSI=%d (need > -80dBm)", ap_info.rssi);
            return false;
        }
        ESP_LOGI(TAG, "OTA precheck: RSSI=%d dBm", ap_info.rssi);
    }

    uint32_t pending = storage_get_pending_count();
    if (pending > 0) {
        ESP_LOGI(TAG, "OTA precheck: %lu pending taps will be preserved", pending);
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGW(TAG, "OTA precheck FAILED: No running partition");
        return false;
    }
    ESP_LOGI(TAG, "OTA precheck: running partition=%s", running->label);

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ESP_LOGI(TAG, "OTA precheck: bootloader rollback is ENABLED");
#else
    ESP_LOGW(TAG, "OTA precheck: bootloader rollback is DISABLED -- RISK!");
#endif

    return true;
}

static esp_err_t perform_ota_verified(const char *firmware_url, const char *fallback_url)
{
    ESP_LOGI(TAG, "Starting verified OTA from: %s", firmware_url);

    event_log_write(EVT_OTA_STARTED);
    led_send(LED_PATTERN_BOOT);

    esp_http_client_config_t cfg = {
        .url = firmware_url,
        .timeout_ms = 60000,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);

    if (err != ESP_OK && fallback_url) {
        ESP_LOGW(TAG, "Primary OTA failed, trying fallback URL");
        vTaskDelay(pdMS_TO_TICKS(5000));
        cfg.url = fallback_url;
        err = esp_https_ota(&ota_cfg);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "All initial attempts failed, retrying primary after 60s delay");
        vTaskDelay(pdMS_TO_TICKS(60000));
        cfg.url = firmware_url;
        err = esp_https_ota(&ota_cfg);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "All 3 OTA attempts failed: %s", esp_err_to_name(err));
        event_log_write(EVT_OTA_FAILED);
        led_send(LED_PATTERN_FAILURE);
        return err;
    }

    ESP_LOGI(TAG, "OTA verified and successful, restarting...");
    event_log_write(EVT_OTA_SUCCESS);
    led_send(LED_PATTERN_TAG);

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

    if (!ota_precheck()) {
        return ESP_FAIL;
    }

    update_in_progress = true;

    char version_buf[64];
    esp_err_t err = http_fetch_to_buffer(OTA_VERSION_URL, version_buf, sizeof(version_buf));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch version.txt");
        update_in_progress = false;
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
        update_in_progress = false;
        return ESP_FAIL;
    }

    if (remote_ver <= local_ver) {
        ESP_LOGI(TAG, "Firmware up to date (local=%d, remote=%d)", local_ver, remote_ver);
        update_in_progress = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "New firmware available: remote v%d > local v%d, starting OTA",
             remote_ver, local_ver);

    const char *url = get_firmware_url();
    const char *fallback = (strcmp(url, OTA_FIRMWARE_URL) != 0) ? OTA_FIRMWARE_URL : NULL;

    err = perform_ota_verified(url, fallback);
    update_in_progress = false;
    return err;
}

esp_err_t ota_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running ? running->label : "unknown");

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "New firmware booted, confirming...");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    return ESP_OK;
}

void ota_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OTA task started, interval=%dms", OTA_CHECK_INTERVAL_MS);
    TickType_t last_check = xTaskGetTickCount();

    while (1) {
        esp_task_wdt_reset();

        bool should_check = false;

        if (ota_trigger_flag) {
            ota_trigger_flag = false;
            ESP_LOGI(TAG, "OTA triggered by command");
            should_check = true;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_check) >= pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS)) {
            should_check = true;
            last_check = now;
        }

        if (should_check) {
            esp_err_t err = ota_check_update();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "OTA check OK");
            } else {
                ESP_LOGW(TAG, "OTA check: %s", esp_err_to_name(err));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
