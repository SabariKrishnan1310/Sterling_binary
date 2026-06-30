#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef void (*mqtt_data_cb_t)(const char *topic, const uint8_t *payload, int len);

esp_err_t mqtt_manager_init(void);
esp_err_t mqtt_manager_start(void);
esp_err_t mqtt_manager_stop(void);
bool      mqtt_manager_is_connected(void);
esp_err_t mqtt_manager_publish(const char *topic, const uint8_t *payload,
                               int len, int qos, int retain);
esp_err_t mqtt_manager_subscribe(const char *topic, int qos);
esp_err_t mqtt_manager_unsubscribe(const char *topic);

/* Register callback for incoming messages on a specific topic prefix */
void mqtt_manager_register_cb(const char *topic_prefix, mqtt_data_cb_t cb);
