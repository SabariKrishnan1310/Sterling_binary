#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_DISCONNECTED_BIT   BIT1
#define WIFI_FAIL_BIT           BIT2
#define PROVISION_DONE_BIT      BIT3
#define WG_UP_BIT               BIT4
#define MQTT_CONNECTED_BIT      BIT5

extern EventGroupHandle_t wifi_event_group;
extern volatile bool upload_force_flag;

esp_err_t network_init(void);
esp_err_t network_start_wifi(void);
esp_err_t network_send_tap_single(const char *uid);
void upload_task(void *pvParameters);
void network_wifi_task(void *pvParameters);
void network_wait_for_time_sync(void);
