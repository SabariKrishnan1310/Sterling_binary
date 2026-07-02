#pragma once
#include "esp_err.h"

esp_err_t console_init(void);
void console_task(void *pvParameters);
