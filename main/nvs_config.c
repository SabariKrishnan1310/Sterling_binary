#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "nvs";

static nvs_handle_t s_nvs_handle = 0;
static nvs_handle_t w_nvs_handle = 0;
static bool s_nvs_initialized = false;
static bool w_nvs_initialized = false;

esp_err_t nvs_stl_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs erase, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_init (retry) failed: %s", esp_err_to_name(err));
            return err;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(NVS_STERLING_NS, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", NVS_STERLING_NS, esp_err_to_name(err));
        return err;
    }
    s_nvs_initialized = true;

    err = nvs_open(NVS_WIFI_NS, NVS_READWRITE, &w_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", NVS_WIFI_NS, esp_err_to_name(err));
        return err;
    }
    w_nvs_initialized = true;

    ESP_LOGI(TAG, "NVS initialized: ns=%s, wifi=%s", NVS_STERLING_NS, NVS_WIFI_NS);
    return ESP_OK;
}

static esp_err_t ensure_sterling(void)
{
    if (!s_nvs_initialized) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

static esp_err_t ensure_wifi(void)
{
    if (!w_nvs_initialized) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t nvs_stl_get_string(const char *key, char *out, size_t *len)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_get_str(s_nvs_handle, key, out, len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_str(%s): %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_stl_set_string(const char *key, const char *val)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_set_str(s_nvs_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(%s): %s", key, esp_err_to_name(err));
    } else {
        nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t nvs_stl_get_u32(const char *key, uint32_t *out)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_get_u32(s_nvs_handle, key, out);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u32(%s): %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_stl_set_u32(const char *key, uint32_t val)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_set_u32(s_nvs_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u32(%s): %s", key, esp_err_to_name(err));
    } else {
        nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t nvs_stl_get_u8(const char *key, uint8_t *out)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_get_u8(s_nvs_handle, key, out);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u8(%s): %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_stl_set_u8(const char *key, uint8_t val)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_set_u8(s_nvs_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u8(%s): %s", key, esp_err_to_name(err));
    } else {
        nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t nvs_stl_get_blob(const char *key, void *out, size_t *len)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_get_blob(s_nvs_handle, key, out, len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_blob(%s): %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_stl_set_blob(const char *key, const void *val, size_t len)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_set_blob(s_nvs_handle, key, val, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob(%s): %s", key, esp_err_to_name(err));
    } else {
        nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t nvs_stl_erase_key(const char *key)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_erase_key(s_nvs_handle, key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key(%s): %s", key, esp_err_to_name(err));
    } else {
        nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t nvs_stl_erase_all(void)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_erase_all(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_all: %s", esp_err_to_name(err));
    } else {
        nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t nvs_stl_commit(void)
{
    esp_err_t err = ensure_sterling();
    if (err != ESP_OK) return err;
    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_wifi_get_ssid(char *out, size_t *len)
{
    esp_err_t err = ensure_wifi();
    if (err != ESP_OK) return err;
    err = nvs_get_str(w_nvs_handle, "ssid", out, len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_wifi_get_ssid: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_wifi_set_ssid(const char *ssid)
{
    esp_err_t err = ensure_wifi();
    if (err != ESP_OK) return err;
    err = nvs_set_str(w_nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_wifi_set_ssid: %s", esp_err_to_name(err));
    } else {
        nvs_commit(w_nvs_handle);
    }
    return err;
}

esp_err_t nvs_wifi_get_password(char *out, size_t *len)
{
    esp_err_t err = ensure_wifi();
    if (err != ESP_OK) return err;
    err = nvs_get_str(w_nvs_handle, "password", out, len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_wifi_get_password: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_wifi_set_password(const char *pwd)
{
    esp_err_t err = ensure_wifi();
    if (err != ESP_OK) return err;
    err = nvs_set_str(w_nvs_handle, "password", pwd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_wifi_set_password: %s", esp_err_to_name(err));
    } else {
        nvs_commit(w_nvs_handle);
    }
    return err;
}
