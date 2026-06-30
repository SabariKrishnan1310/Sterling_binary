#pragma once
#include "esp_err.h"

/* --- sterling namespace wrappers --- */
esp_err_t nvs_stl_init(void);
esp_err_t nvs_stl_get_string(const char *key, char *out, size_t *len);
esp_err_t nvs_stl_set_string(const char *key, const char *val);
esp_err_t nvs_stl_get_u32(const char *key, uint32_t *out);
esp_err_t nvs_stl_set_u32(const char *key, uint32_t val);
esp_err_t nvs_stl_get_u8(const char *key, uint8_t *out);
esp_err_t nvs_stl_set_u8(const char *key, uint8_t val);
esp_err_t nvs_stl_get_blob(const char *key, void *out, size_t *len);
esp_err_t nvs_stl_set_blob(const char *key, const void *val, size_t len);
esp_err_t nvs_stl_erase_key(const char *key);
esp_err_t nvs_stl_erase_all(void);
esp_err_t nvs_stl_commit(void);

/* --- wifi namespace --- */
esp_err_t nvs_wifi_get_ssid(char *out, size_t *len);
esp_err_t nvs_wifi_set_ssid(const char *ssid);
esp_err_t nvs_wifi_get_password(char *out, size_t *len);
esp_err_t nvs_wifi_set_password(const char *pwd);
