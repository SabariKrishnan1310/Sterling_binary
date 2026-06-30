#include "ota_manager.h"
#include "config.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include <string.h>

static const char *TAG = "ota";
static uint8_t s_channel = 0;

esp_err_t ota_manager_init(void)
{
    esp_err_t ret = nvs_stl_get_u8(NVS_KEY_FW_CHANNEL, &s_channel);
    if (ret != ESP_OK) {
        s_channel = 0;
    }
    return ESP_OK;
}

bool ota_manager_check_version(void)
{
    esp_http_client_config_t http_cfg = {
        .url = OTA_VERSION_URL,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) return false;

    char buf[64];
    int len = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        len = esp_http_client_read(client, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = 0;
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                buf[--len] = 0;
            }
        }
    }
    esp_http_client_cleanup(client);

    if (len <= 0) return false;

    return strncmp(buf, FW_VERSION_STR, sizeof(buf)) != 0;
}

esp_err_t ota_manager_start(const char *url)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &cfg,
    };
    esp_https_ota_handle_t handle = NULL;

    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    while (esp_https_ota_perform(handle) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        // continue
    }

    err = esp_https_ota_finish(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA success, restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
    }
    return err;
}

esp_err_t ota_manager_rollback(void)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) return ESP_FAIL;
    esp_err_t err = esp_ota_set_boot_partition(next);
    if (err == ESP_OK) {
        esp_restart();
    }
    return err;
}

void ota_manager_set_channel(uint8_t channel)
{
    s_channel = channel;
    nvs_stl_set_u8(NVS_KEY_FW_CHANNEL, channel);
}

uint8_t ota_manager_get_channel(void)
{
    return s_channel;
}
