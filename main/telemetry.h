#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// EXACTLY 74 bytes — verified by static_assert
typedef struct __attribute__((packed)) {
    uint64_t sequence_number;     // offset 0, size 8
    int32_t  uptime_ms;           // offset 8, size 4
    uint32_t rssi;                // offset 12, size 4
    int32_t  free_heap;           // offset 16, size 4
    int16_t  wifi_rssi;           // offset 20, size 2
    uint16_t battery_mv;          // offset 22, size 2 — MUST BE 0
    uint16_t temperature_celsius; // offset 24, size 2 — MUST BE 0xFFFF
    uint32_t num_tags_read;       // offset 26, size 4
    uint32_t firmware_version;    // offset 30, size 4
    uint32_t reserved;            // offset 34, size 4 — MUST BE 0
    uint32_t timestamp_ist;       // offset 38, size 4
    uint8_t  hmac[32];            // offset 42, size 32 — HMAC-SHA256 of bytes 0-41
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_packet_t) == 74, "Telemetry packet must be exactly 74 bytes");

esp_err_t telemetry_init(void);
esp_err_t telemetry_send(void);
esp_err_t telemetry_set_interval(uint32_t interval_ms);
uint32_t telemetry_get_interval(void);
telemetry_packet_t* telemetry_get_last(void);
void telemetry_increment_tags(uint32_t count);
void telemetry_task(void *pvParameters);
