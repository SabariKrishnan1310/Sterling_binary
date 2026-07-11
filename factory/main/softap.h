#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t softap_init(void);
esp_err_t softap_start(void);
bool softap_is_active(void);
