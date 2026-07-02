#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define PROVISION_URL_HTTPS "https://api.sabarikrishnan.me/api/v1/provision"
#define PROVISION_URL_HTTP  "http://api.sabarikrishnan.me/api/v1/provision"

bool provision_is_done(void);
esp_err_t provision_do(void);
esp_err_t provision_mark_done(void);
esp_err_t provision_clear(void);
void provision_task(void *pvParameters);
