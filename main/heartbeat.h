#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t heartbeat_init(void);
int64_t heartbeat_get_drift_ms(void);
uint32_t heartbeat_get_missed_count(void);
void heartbeat_handle_pong(const char *data, int len);
void heartbeat_task(void *pvParameters);
