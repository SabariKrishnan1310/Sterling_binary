#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WG_STATE_DISABLED = 0,
    WG_STATE_CONNECTING,
    WG_STATE_UP,
    WG_STATE_DOWN,
    WG_STATE_FAILED,
} wg_state_t;

esp_err_t wg_init(void);
esp_err_t wg_start(void);
esp_err_t wg_stop(void);
bool wg_is_up(void);
wg_state_t wg_get_state(void);
void wg_task(void *pvParameters);
