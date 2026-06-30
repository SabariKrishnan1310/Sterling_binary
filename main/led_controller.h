#pragma once
#include "esp_err.h"

typedef enum {
    LED_PATTERN_BOOT,
    LED_PATTERN_TAG,
    LED_PATTERN_FAILURE,
    LED_PATTERN_WAVE,
    LED_PATTERN_PROVISIONING,
    LED_PATTERN_WG_CONNECTING,
    LED_PATTERN_MQTT_CONNECTED,
    LED_PATTERN_IDLE,
    LED_PATTERN_INDICATOR,
    LED_PATTERN_OFF,
} led_pattern_t;

esp_err_t led_controller_init(void);
esp_err_t led_controller_play(led_pattern_t pattern);
void      led_controller_task(void *pvParameters);
