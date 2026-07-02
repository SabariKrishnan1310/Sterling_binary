#pragma once

#include "esp_err.h"

/**
 * @brief Initialize tap dispatcher
 * Routes: MQTT publish → HTTP POST → LittleFS storage
 */
esp_err_t tap_dispatcher_init(void);
