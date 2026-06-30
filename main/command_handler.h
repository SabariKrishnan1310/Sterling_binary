#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t command_handler_init(void);
void      command_handler_process(const uint8_t *packet, int len);
void      command_handler_ack(uint32_t command_id, uint32_t token);
