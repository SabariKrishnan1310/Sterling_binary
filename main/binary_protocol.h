#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================
 *  Packet structs — ALL big-endian on wire
 *  Pack functions set fields, compute HMAC, return final buffer
 * ========================================================= */

#pragma pack(push, 1)

/* Telemetry — 72 bytes (40 fields + 32 HMAC) */
typedef struct {
    uint64_t sequence_number;
    int32_t  uptime_ms;
    uint32_t rssi;           /* signed dBm cast to uint32 */
    int32_t  free_heap;
    int16_t  wifi_rssi;
    uint16_t battery_mv;
    int16_t  temperature_celsius_x10;
    uint16_t num_tags_read;
    uint32_t firmware_version;
    uint32_t reserved;
    uint32_t timestamp_ist;
    uint8_t  hmac[32];
} __attribute__((packed)) telemetry_packet_t;
#define TELEMETRY_PACKET_SIZE 72
#define TELEMETRY_HMAC_OFFSET 40

/* Command — 44 bytes (12 fields + 32 HMAC) */
typedef struct {
    uint32_t command_id;
    uint32_t token;
    uint32_t parameter;
    uint8_t  hmac[32];
} __attribute__((packed)) command_packet_t;
#define COMMAND_PACKET_SIZE 44
#define COMMAND_HMAC_OFFSET 12

/* Diagnostics — 60 bytes (28 fields + 32 HMAC) */
typedef struct {
    uint32_t sequence_number;
    uint32_t uptime_ms;
    int32_t  free_heap;
    int32_t  rssi;            /* signed! different from telemetry */
    int16_t  battery_mv;
    int16_t  temperature_celsius_x10;
    uint8_t  status_flags;
    uint8_t  reserved[11];
    uint8_t  hmac[32];
} __attribute__((packed)) diagnostics_packet_t;
#define DIAGNOSTICS_PACKET_SIZE 60
#define DIAGNOSTICS_HMAC_OFFSET 28

/* Time Sync — 44 bytes (12 fields + 32 HMAC) */
typedef struct {
    uint32_t device_timestamp;
    uint32_t server_timestamp;
    uint32_t drift_ms;
    uint8_t  hmac[32];
} __attribute__((packed)) timesync_packet_t;
#define TIMESYNC_PACKET_SIZE 44
#define TIMESYNC_HMAC_OFFSET 12

/* Bulk Header — 8 bytes, NO HMAC */
typedef struct {
    uint32_t sequence_number;
    uint16_t entry_count;
    uint16_t entry_size;      /* always 58 */
} __attribute__((packed)) bulk_header_t;
#define BULK_HEADER_SIZE 8

/* Bulk Entry — 58 bytes, NO HMAC */
typedef struct {
    uint64_t rfid_uid;
    uint32_t timestamp;
    int16_t  rssi;
    uint8_t  status;
    uint8_t  reserved[43];
} __attribute__((packed)) bulk_entry_t;
#define BULK_ENTRY_SIZE 58

#pragma pack(pop)

/* Status flags for diagnostics */
#define STATUS_RFID_OK       (1 << 0)
#define STATUS_WIFI_OK       (1 << 1)
#define STATUS_WG_UP         (1 << 2)
#define STATUS_MQTT_OK       (1 << 3)
#define STATUS_STORAGE_OK    (1 << 4)
#define STATUS_BATTERY_LOW   (1 << 5)

/* --- Pack helpers (sets fields in host byte order, packs to big-endian + HMAC) --- */
void telemetry_pack(telemetry_packet_t *pkt,
                    uint64_t seq, uint32_t uptime_ms, int32_t rssi_signed,
                    int32_t free_heap, int16_t wifi_rssi, uint16_t battery_mv,
                    int16_t temp_x10, uint16_t num_tags, uint32_t fw_ver,
                    uint32_t timestamp_ist);

bool command_verify(const command_packet_t *pkt);

void diagnostics_pack(diagnostics_packet_t *pkt,
                      uint32_t seq, uint32_t uptime_ms, int32_t free_heap,
                      int32_t rssi, int16_t battery_mv, int16_t temp_x10,
                      uint8_t status_flags);

void timesync_pack(timesync_packet_t *pkt,
                   uint32_t device_ts, uint32_t server_ts, uint32_t drift_ms);
bool timesync_verify(const timesync_packet_t *pkt);
