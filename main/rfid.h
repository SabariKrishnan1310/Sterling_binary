#pragma once

#include "esp_err.h"

esp_err_t rfid_init(void);
void rfid_task(void *pvParameters);
