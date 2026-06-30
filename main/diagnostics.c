#include "diagnostics.h"
#include "binary_protocol.h"
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

static const char *TAG = "diag";

static uint8_t s_flags = 0;
static char s_devid[64];
static uint32_t s_seq = 0;

esp_err_t diagnostics_init(void)
{
    size_t len = sizeof(s_devid);
    nvs_stl_get_string("mqtt_username", s_devid, &len);
    ESP_LOGI(TAG, "Diagnostics initialized for device: %s", s_devid);
    return ESP_OK;
}

void diagnostics_send(void)
{
    diagnostics_packet_t pkt;
    diagnostics_pack(&pkt, s_seq++, esp_timer_get_time() / 1000,
        esp_get_free_heap_size(), wifi_manager_get_rssi(), 0, 0, s_flags);

    char topic[128];
    snprintf(topic, sizeof(topic), "telemetry/%s", s_devid);

    mqtt_manager_publish(topic, (uint8_t *)&pkt, DIAGNOSTICS_PACKET_SIZE, 1, 0);
}

uint8_t diagnostics_get_status_flags(void)
{
    return s_flags;
}

void diagnostics_set_status_bit(int bit, bool set)
{
    if (set) {
        s_flags |= (1 << bit);
    } else {
        s_flags &= ~(1 << bit);
    }
}
