#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t timesync_init(void);
void      timesync_request(void);
bool      timesync_process_response(const uint8_t *packet, int len);
int64_t   timesync_get_unix_time(void);
uint32_t  timesync_get_unix_seconds(void);
uint32_t  timesync_get_ist_seconds(void);
void      timesync_set_rtc(int64_t unix_time_ms);
