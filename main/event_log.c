#include "event_log.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "evt";

static const char *s_events[EVENT_LOG_SIZE];
static int s_head = 0;
static int s_count = 0;

static const char *event_names[] = {
    "WIFI_CONN", "WIFI_DISC", "WG_UP", "WG_DOWN",
    "MQTT_CONN", "MQTT_DISC", "PROV", "PROV_DONE",
    "TAG_READ", "TAG_SENT", "CMD", "OTA_START",
    "OTA_OK", "OTA_FAIL", "ERROR", "REBOOT"
};

esp_err_t event_log_init(void)
{
    memset(s_events, 0, sizeof(s_events));
    s_head = 0;
    s_count = 0;
    return ESP_OK;
}

void event_log_write(event_type_t event)
{
    if (event < sizeof(event_names) / sizeof(event_names[0])) {
        s_events[s_head] = event_names[event];
    } else {
        s_events[s_head] = "UNKNOWN";
    }
    s_head = (s_head + 1) % EVENT_LOG_SIZE;
    if (s_count < EVENT_LOG_SIZE) {
        s_count++;
    }
}

void event_log_dump(void)
{
    for (int i = 0; i < s_count; i++) {
        int idx = (s_head - s_count + i + EVENT_LOG_SIZE) % EVENT_LOG_SIZE;
        ESP_LOGI(TAG, "[%d] %s", i, s_events[idx] ? s_events[idx] : "NULL");
    }
}
