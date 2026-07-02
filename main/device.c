#include "device.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "device";

static char s_device_id[32];
static char s_mac_str[18];

esp_err_t device_init(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC from efuse: %s", esp_err_to_name(err));
        strncpy(s_device_id, "ESP32-UNKNOWN", sizeof(s_device_id));
        strncpy(s_mac_str, "00:00:00:00:00:00", sizeof(s_mac_str));
        return err;
    }

    snprintf(s_mac_str, sizeof(s_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char default_id[32];
    snprintf(default_id, sizeof(default_id), "ESP32-%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);

    nvs_handle_t handle;
    err = nvs_open("device", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'device': %s", esp_err_to_name(err));
        strncpy(s_device_id, default_id, sizeof(s_device_id));
        return err;
    }

    size_t len = sizeof(s_device_id);
    err = nvs_get_str(handle, "device_id", s_device_id, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_str(handle, "device_id", default_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write device_id to NVS: %s", esp_err_to_name(err));
        }
        err = nvs_set_str(handle, "mac", s_mac_str);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write mac to NVS: %s", esp_err_to_name(err));
        }
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        }
        strncpy(s_device_id, default_id, sizeof(s_device_id));
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device_id from NVS: %s", esp_err_to_name(err));
        strncpy(s_device_id, default_id, sizeof(s_device_id));
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Device ID: %s", s_device_id);
    ESP_LOGI(TAG, "MAC: %s", s_mac_str);

    return ESP_OK;
}

const char* device_get_id(void)
{
    if (s_device_id[0] == '\0') {
        return "ESP32-UNKNOWN";
    }
    return s_device_id;
}

esp_err_t device_refresh(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for refresh: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = sizeof(s_device_id);
    err = nvs_get_str(handle, "device_id", s_device_id, &len);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t mac[6];
        err = esp_efuse_mac_get_default(mac);
        if (err != ESP_OK) {
            strncpy(s_device_id, "ESP32-UNKNOWN", sizeof(s_device_id));
            return err;
        }
        snprintf(s_device_id, sizeof(s_device_id), "ESP32-%02X%02X%02X%02X",
                 mac[2], mac[3], mac[4], mac[5]);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device_id from NVS: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t device_get_mac_str(char *buf, size_t len)
{
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t device_get_mac_raw(uint8_t mac[6])
{
    return esp_efuse_mac_get_default(mac);
}
