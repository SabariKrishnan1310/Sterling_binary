#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    EVT_BOOT             = 0,
    EVT_WIFI_CONNECTED   = 1,
    EVT_WIFI_DISCONNECTED = 2,
    EVT_OTA_STARTED      = 3,
    EVT_OTA_SUCCESS      = 4,
    EVT_OTA_FAILED       = 5,
    EVT_RFID_READ        = 6,
    EVT_BROWNOUT         = 7,
    EVT_UPLOAD_SUCCESS   = 8,
    EVT_UPLOAD_FAILED    = 9,
    EVT_STORAGE_RECOVERY = 10,
    EVT_WG_UP            = 11,
    EVT_WG_DOWN          = 12,
    EVT_WG_FAILED        = 13,
    EVT_CONSOLE_ATTACK   = 14,
} event_log_type_t;

esp_err_t event_log_init(void);
esp_err_t event_log_write(event_log_type_t event);
void event_log_dump(void);
