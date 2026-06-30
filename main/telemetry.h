#pragma once
#include "esp_err.h"

esp_err_t telemetry_init(void);
void      telemetry_send(void);
void      telemetry_set_interval(uint32_t interval_ms);
uint32_t  telemetry_get_interval(void);
uint64_t  telemetry_next_seq(void);
