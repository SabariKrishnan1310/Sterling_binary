#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t tap_publisher_init(void);
void      tap_publisher_publish(const char *hex_uid, int16_t rssi);
void      tap_publisher_publish_from_storage(void);
int       tap_publisher_get_pending_count(void);
