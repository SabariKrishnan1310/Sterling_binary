#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char    hex_uid[32];   /* uppercase hex string of UID */
    uint8_t uid_bytes[10];
    int     uid_len;       /* 4, 7, or 10 bytes */
    int16_t rssi;          /* RSSI at time of read */
} rfid_tag_t;

esp_err_t rfid_reader_init(void);
bool      rfid_reader_read_tag(rfid_tag_t *tag);
void      rfid_reader_set_power(uint8_t power);
