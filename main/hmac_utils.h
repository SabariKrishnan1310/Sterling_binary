#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void hmac_set_key(const uint8_t *key, size_t key_len);
void hmac_compute(const uint8_t *data, size_t data_len, uint8_t out_hmac[32]);
bool hmac_verify(const uint8_t *data, size_t data_len, const uint8_t expected_hmac[32]);
