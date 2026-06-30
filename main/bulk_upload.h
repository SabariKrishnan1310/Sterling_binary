#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint64_t rfid_uid;
    uint32_t timestamp;
    int16_t  rssi;
    uint8_t  status;
} bulk_entry_data_t;

esp_err_t bulk_upload_init(void);
esp_err_t bulk_upload_store_entry(const bulk_entry_data_t *entry);
int       bulk_upload_get_count(void);
esp_err_t bulk_upload_send(int max_entries);
esp_err_t bulk_upload_clear(void);
