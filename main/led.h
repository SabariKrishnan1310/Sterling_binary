#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

typedef enum {
    LED_PATTERN_BOOT,
    LED_PATTERN_SUCCESS,
    LED_PATTERN_FAILURE,
    LED_PATTERN_OFFLINE,
    LED_PATTERN_IDLE,
} led_pattern_t;

typedef struct {
    led_pattern_t pattern;
} led_message_t;

esp_err_t led_init(void);
esp_err_t led_send(led_pattern_t pattern);
QueueHandle_t led_get_queue(void);
