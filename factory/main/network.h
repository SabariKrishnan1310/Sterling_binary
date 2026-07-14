#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>

void wifi_connect_sta(void);
bool wifi_is_connected(void);
esp_err_t wifi_fetch_config(void);
void wifi_store_networks(cJSON *networks);
esp_err_t ota_force_update(void);
