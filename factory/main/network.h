#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>

void wifi_connect_sta(void);
bool wifi_is_connected(void);
void wifi_reconnect(void);
void wifi_connect_profile(uint16_t index);

// Rolling WiFi event log (for dashboard diagnostics)
#define WIFI_EVT_MAX 16
typedef struct {
    uint32_t t_sec;
    char     msg[96];
} wifi_evt_t;
const wifi_evt_t *wifi_evt_get_log(uint8_t *count_out);
uint8_t wifi_evt_get_head(void);
const char *wifi_get_cur_ssid(void);
esp_err_t wifi_fetch_config(void);
void wifi_store_networks(cJSON *networks);
esp_err_t ota_force_update(void);
