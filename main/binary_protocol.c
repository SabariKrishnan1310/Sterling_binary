#include "binary_protocol.h"
#include "hmac_utils.h"
#include "config.h"
#include <string.h>
#include <endian.h>

void telemetry_pack(telemetry_packet_t *pkt,
                    uint64_t seq, uint32_t uptime_ms, int32_t rssi_signed,
                    int32_t free_heap, int16_t wifi_rssi, uint16_t battery_mv,
                    int16_t temp_x10, uint16_t num_tags, uint32_t fw_ver,
                    uint32_t timestamp_ist)
{
    pkt->sequence_number    = htobe64(seq);
    pkt->uptime_ms          = htobe32(uptime_ms);
    pkt->rssi               = htobe32((uint32_t)(int32_t)rssi_signed);
    pkt->free_heap          = htobe32(free_heap);
    pkt->wifi_rssi          = htobe16(wifi_rssi);
    pkt->battery_mv         = htobe16(battery_mv);
    pkt->temperature_celsius_x10 = htobe16(temp_x10);
    pkt->num_tags_read      = htobe16(num_tags);
    pkt->firmware_version   = htobe32(fw_ver);
    pkt->reserved           = htobe32(0);
    pkt->timestamp_ist      = htobe32(timestamp_ist);

    hmac_compute((const uint8_t *)pkt, TELEMETRY_HMAC_OFFSET, pkt->hmac);
}

bool command_verify(const command_packet_t *pkt)
{
    uint8_t computed[HMAC_LEN];
    hmac_compute((const uint8_t *)pkt, COMMAND_HMAC_OFFSET, computed);
    return memcmp(computed, pkt->hmac, HMAC_LEN) == 0;
}

void diagnostics_pack(diagnostics_packet_t *pkt,
                      uint32_t seq, uint32_t uptime_ms, int32_t free_heap,
                      int32_t rssi, int16_t battery_mv, int16_t temp_x10,
                      uint8_t status_flags)
{
    pkt->sequence_number         = htobe32(seq);
    pkt->uptime_ms               = htobe32(uptime_ms);
    pkt->free_heap               = htobe32(free_heap);
    pkt->rssi                    = htobe32(rssi);
    pkt->battery_mv              = htobe16(battery_mv);
    pkt->temperature_celsius_x10 = htobe16(temp_x10);
    pkt->status_flags            = status_flags;
    memset(pkt->reserved, 0, sizeof(pkt->reserved));

    hmac_compute((const uint8_t *)pkt, DIAGNOSTICS_HMAC_OFFSET, pkt->hmac);
}

void timesync_pack(timesync_packet_t *pkt,
                   uint32_t device_ts, uint32_t server_ts, uint32_t drift_ms)
{
    pkt->device_timestamp = htobe32(device_ts);
    pkt->server_timestamp = htobe32(server_ts);
    pkt->drift_ms         = htobe32(drift_ms);

    hmac_compute((const uint8_t *)pkt, TIMESYNC_HMAC_OFFSET, pkt->hmac);
}

bool timesync_verify(const timesync_packet_t *pkt)
{
    uint8_t computed[HMAC_LEN];
    hmac_compute((const uint8_t *)pkt, TIMESYNC_HMAC_OFFSET, computed);
    return memcmp(computed, pkt->hmac, HMAC_LEN) == 0;
}
