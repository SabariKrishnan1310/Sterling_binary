#pragma once

#include "esp_err.h"
#include <stdint.h>

#define BULK_MAX_ENTRIES    50
#define BULK_ENTRY_SIZE     58
#define BULK_HEADER_SIZE    8

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint16_t count;
    uint16_t entry_size; // MUST be 58
} bulk_header_t;

typedef struct __attribute__((packed)) {
    uint64_t rfid_uid;
    uint32_t timestamp_ist;
    int16_t  rssi;
    uint8_t  status;
    uint8_t  reserved[43];
} bulk_entry_t;

_Static_assert(sizeof(bulk_header_t) == 8, "Bulk header must be 8 bytes");
_Static_assert(sizeof(bulk_entry_t) == 58, "Bulk entry must be 58 bytes");

esp_err_t bulk_send(uint32_t max_entries);
void bulk_trigger(uint32_t max_entries);
void bulk_task(void *pvParameters);
