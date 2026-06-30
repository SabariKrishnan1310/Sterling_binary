#include "telemetry.h"
#include "binary_protocol.h"
#include "hmac_utils.h"
#include "mqtt_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "wifi_manager.h"
#include "timesync.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "string.h"

static const char *TAG = "telemetry";

static uint64_t s_seq = 0;
static uint32_t s_interval = TELEMETRY_INTERVAL_MS;
static char s_devid[64];
static bool s_initialized = false;

esp_err_t telemetry_init(void)
{
    size_t len = sizeof(s_devid);
    nvs_stl_get_string("mqtt_username", s_devid, &len);

    uint32_t lo = 0;
    nvs_stl_get_u32("sequence_number", &lo);
    s_seq = lo;

    s_initialized = true;
    ESP_LOGI(TAG, "Telemetry initialized, seq: %llu", (unsigned long long)s_seq);
    return ESP_OK;
}

void telemetry_send(void)
{
    if (!s_initialized) {
        return;
    }

    telemetry_packet_t pkt;
    telemetry_pack(&pkt, s_seq, esp_timer_get_time() / 1000, wifi_manager_get_rssi(),
        esp_get_free_heap_size(), wifi_manager_get_rssi(), 0, 0, 0,
        FW_VERSION_ENCODED, timesync_get_ist_seconds());

    char topic[128];
    snprintf(topic, sizeof(topic), "telemetry/%s", s_devid);

    mqtt_manager_publish(topic, (uint8_t *)&pkt, TELEMETRY_PACKET_SIZE, 1, 0);

    s_seq++;
    nvs_stl_set_u32("sequence_number", (uint32_t)s_seq);
}

void telemetry_set_interval(uint32_t interval_ms)
{
    if (interval_ms < 1000) {
        interval_ms = 1000;
    }
    if (interval_ms > 3600000) {
        interval_ms = 3600000;
    }
    s_interval = interval_ms;
    nvs_stl_set_u32("telemetry_interval", s_interval);
}

uint32_t telemetry_get_interval(void)
{
    return s_interval;
}

uint64_t telemetry_next_seq(void)
{
    return s_seq++;
}
