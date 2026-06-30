#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t diagnostics_init(void);
void      diagnostics_send(void);
uint8_t   diagnostics_get_status_flags(void);
void      diagnostics_set_status_bit(int bit, bool set);
