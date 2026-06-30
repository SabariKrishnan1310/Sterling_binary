#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef enum {
    EVT_WIFI_CONNECTED,
    EVT_WIFI_DISCONNECTED,
    EVT_WG_UP,
    EVT_WG_DOWN,
    EVT_MQTT_CONNECTED,
    EVT_MQTT_DISCONNECTED,
    EVT_PROVISIONING,
    EVT_PROVISIONED,
    EVT_TAG_READ,
    EVT_TAG_SENT,
    EVT_COMMAND_RECEIVED,
    EVT_OTA_STARTED,
    EVT_OTA_SUCCESS,
    EVT_OTA_FAILED,
    EVT_ERROR,
    EVT_REBOOT,
} event_type_t;

esp_err_t event_log_init(void);
void      event_log_write(event_type_t event);
void      event_log_dump(void);
