#pragma once

#include "esp_err.h"

esp_err_t ota_init(void);
void ota_task(void *pvParameters);
esp_err_t ota_check_update(void);
