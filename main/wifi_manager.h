#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    char ssid[64];
    char password[64];
} wifi_network_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(void);
esp_err_t wifi_manager_disconnect(void);
bool      wifi_manager_is_connected(void);
int32_t   wifi_manager_get_rssi(void);
esp_err_t wifi_manager_fetch_and_update_list(void);
esp_err_t wifi_manager_get_list(wifi_network_t *networks, int *count);
esp_err_t wifi_manager_set_list(const wifi_network_t *networks, int count);
int       wifi_manager_get_current_index(void);
void      wifi_manager_set_current_index(int idx);
uint32_t  wifi_manager_get_list_version(void);
void      wifi_manager_set_list_version(uint32_t ver);
