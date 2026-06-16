#include "ota.h"
#include "event_log.h"
#include "led.h"
#include "network.h"
#include "config.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "ota";

static bool update_in_progress = false;

static esp_err_t http_fetch_to_buffer(const char *url, char *buf, size_t buf_size)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .skip_cert_common_name_check = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0 || (size_t)content_length >= buf_size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int read_len = esp_http_client_read(client, buf, buf_size - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) return ESP_FAIL;

    buf[read_len] = '\0';
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

static esp_err_t perform_ota(const char *firmware_url)
{
    ESP_LOGI(TAG, "Starting OTA from: %s", firmware_url);

    event_log_write(EVT_OTA_STARTED);
    led_send(LED_PATTERN_BOOT);

    esp_http_client_config_t cfg = {
        .url = firmware_url,
        .timeout_ms = 60000,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = false,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        event_log_write(EVT_OTA_FAILED);
        led_send(LED_PATTERN_FAILURE);
        return err;
    }

    ESP_LOGI(TAG, "OTA successful, restarting...");
    event_log_write(EVT_OTA_SUCCESS);
    led_send(LED_PATTERN_SUCCESS);

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

    if (remote_ver <= local_ver) {
        ESP_LOGI(TAG, "Firmware is up to date");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "New firmware available: %s", version_buf);

    update_in_progress = true;
    err = perform_ota(OTA_FIRMWARE_URL);
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
            } else {
                ESP_LOGW(TAG, "[DBG] ota_task: check returned %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGD(TAG, "[DBG] ota_task: WiFi not connected, skipping");
        }

        ESP_LOGD(TAG, "[DBG] ota_task: sleeping %dms", OTA_CHECK_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
    }
}
