#pragma once

#include "esp_err.h"

esp_err_t device_init(void);

const char* device_get_id(void);

esp_err_t device_refresh(void);

esp_err_t device_get_mac_str(char *buf, size_t len);

esp_err_t device_get_mac_raw(uint8_t mac[6]);
