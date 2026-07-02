#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ota_init(void);
void ota_task(void *pvParameters);
esp_err_t ota_check_update(void);
extern bool ota_trigger_flag;
extern char ota_custom_url[256];
