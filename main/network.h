#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_DISCONNECTED_BIT   BIT1
#define WIFI_FAIL_BIT           BIT2

extern EventGroupHandle_t wifi_event_group;

esp_err_t network_init(void);
esp_err_t network_start_wifi(void);
// Idempotently start the WiFi radio (esp_wifi_start). Safe to call multiple
// times. Needed because SoftAP is brought up before the wifi task, so the
// radio may not be running yet when the AP interface is configured.
esp_err_t network_ensure_wifi_started(void);
esp_err_t network_send_tap_single(const char *uid);
void upload_task(void *pvParameters);
void network_wifi_task(void *pvParameters);

// SoftAP helpers
int network_get_rssi(void);
int network_get_profile_count(void);
esp_err_t network_get_profile_ssid(int idx, char *buf, size_t len);
bool network_is_softap_active(void);
void network_set_softap_active(bool active);

// WiFi mutex — lock before any esp_wifi_* sequence that must be atomic
SemaphoreHandle_t network_get_wifi_mutex(void);

// Rolling WiFi event log (for dashboard /api/events)
#define WIFI_EVT_MAX 16
typedef struct {
    uint32_t t_sec;
    char     msg[96];
} wifi_evt_t;
const wifi_evt_t *wifi_evt_get_log(uint8_t *count_out);
uint8_t wifi_evt_get_head(void);
