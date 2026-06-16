#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_DISCONNECTED_BIT   BIT1
#define WIFI_FAIL_BIT           BIT2

extern EventGroupHandle_t wifi_event_group;

esp_err_t network_init(void);
esp_err_t network_start_wifi(void);
esp_err_t network_send_tap_single(const char *uid);
void upload_task(void *pvParameters);
void network_wifi_task(void *pvParameters);
