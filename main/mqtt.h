#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t mqtt_init(void);
esp_err_t mqtt_start(void);
esp_err_t mqtt_stop(void);
bool mqtt_is_connected(void);
esp_err_t mqtt_publish(const char *topic, const char *data, int len, int qos, int retain);
esp_err_t mqtt_subscribe(const char *topic, int qos);
void* mqtt_get_client(void);
void mqtt_task(void *pvParameters);
