#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t ota_manager_init(void);
bool      ota_manager_check_version(void);
esp_err_t ota_manager_start(const char *url);
esp_err_t ota_manager_rollback(void);
void      ota_manager_set_channel(uint8_t channel);
uint8_t   ota_manager_get_channel(void);
