#include "timesync.h"
#include "binary_protocol.h"
#include "hmac_utils.h"
#include "mqtt_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sys/time.h"
#include <string.h>
#include <endian.h>

static const char *TAG = "timesync";

static int64_t s_offset = 0;
static char s_devid[64];
static bool s_initialized = false;

esp_err_t timesync_init(void)
{
    size_t len = sizeof(s_devid);
    nvs_stl_get_string("mqtt_username", s_devid, &len);
    s_initialized = true;
    ESP_LOGI(TAG, "Timesync initialized for device: %s", s_devid);
    return ESP_OK;
}

void timesync_request(void)
{
    timesync_packet_t pkt;
    uint32_t now_guess = (esp_timer_get_time() / 1000000) + (s_offset / 1000);
    timesync_pack(&pkt, now_guess, 0, 0);

    char topic[128];
    snprintf(topic, sizeof(topic), "timesync/%s", s_devid);
    mqtt_manager_publish(topic, (uint8_t *)&pkt, TIMESYNC_PACKET_SIZE, 1, 0);
}

bool timesync_process_response(const uint8_t *packet, int len)
{
    if (!timesync_verify((const timesync_packet_t *)packet)) {
        return false;
    }

    (void)len;
    const timesync_packet_t *resp = (const timesync_packet_t *)packet;
    uint32_t device_ts = be32toh(resp->device_timestamp);
    uint32_t server_ts = be32toh(resp->server_timestamp);

    s_offset = (server_ts - device_ts) * 1000;
    ESP_LOGI(TAG, "Time sync: offset=%lld ms", s_offset);
    return true;
}

int64_t timesync_get_unix_time(void)
{
    return (esp_timer_get_time() / 1000) + s_offset;
}

uint32_t timesync_get_unix_seconds(void)
{
    return (uint32_t)(timesync_get_unix_time() / 1000);
}

uint32_t timesync_get_ist_seconds(void)
{
    return timesync_get_unix_seconds() + IST_OFFSET;
}

void timesync_set_rtc(int64_t ms)
{
    s_offset = ms - (esp_timer_get_time() / 1000);
}
