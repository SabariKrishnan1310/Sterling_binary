#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WG_STATE_UNPROVISIONED,
    WG_STATE_KEYS_STORED,
    WG_STATE_TUNNEL_UP,
    WG_STATE_TUNNEL_DOWN,
    WG_STATE_ERROR,
} wg_state_t;

esp_err_t wg_manager_init(void);
esp_err_t wg_manager_start(void);
esp_err_t wg_manager_stop(void);
bool      wg_manager_is_up(void);
wg_state_t wg_manager_get_state(void);
esp_err_t wg_manager_restart(void);
void      wg_manager_set_config(const char *private_key, const char *address,
                                const char *server_pubkey, const char *endpoint);
