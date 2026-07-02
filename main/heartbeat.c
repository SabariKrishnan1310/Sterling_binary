#include "heartbeat.h"
#include "mqtt.h"
#include "device.h"
#include "network.h"
#include "config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>

static const char *TAG = "heartbeat";
static int64_t s_drift_ms = 0;
static uint32_t s_missed_count = 0;
static uint32_t s_sent_count = 0;
static uint32_t s_pong_count = 0;

static const uint32_t HEARTBEAT_INTERVAL_MS = 120000;
static const uint32_t MAX_CONSECUTIVE_MISSED = 3;

esp_err_t heartbeat_init(void)
{
    s_drift_ms = 0;
    s_missed_count = 0;
    s_sent_count = 0;
    s_pong_count = 0;
    return ESP_OK;
}

int64_t heartbeat_get_drift_ms(void)
{
    return s_drift_ms;
}

uint32_t heartbeat_get_missed_count(void)
{
    return s_missed_count;
}

void heartbeat_handle_pong(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse pong JSON");
        return;
    }

    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (!server_time || !cJSON_IsNumber(server_time)) {
        ESP_LOGW(TAG, "Pong missing server_time field");
        cJSON_Delete(root);
        return;
    }

    int64_t drift = (int64_t)(server_time->valuedouble * 1000.0) - (int64_t)((double)time(NULL) * 1000.0);
    s_drift_ms = drift;
    s_pong_count++;
    s_missed_count = 0;

    ESP_LOGI(TAG, "Pong received: drift=%lldms, pongs=%lu", (long long)s_drift_ms, (unsigned long)s_pong_count);
    cJSON_Delete(root);
}

void heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Heartbeat task started");
    heartbeat_init();

    char topic[64];
    char payload[64];
    const char *device_id = device_get_id();

    xEventGroupWaitBits(wifi_event_group, MQTT_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "MQTT connected, starting heartbeat loop");

    uint32_t consecutive_missed = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));

        if (!mqtt_is_connected()) {
            consecutive_missed++;
            s_missed_count++;
            if (consecutive_missed >= MAX_CONSECUTIVE_MISSED) {
                ESP_LOGW(TAG, "Heartbeat: %lu consecutive missed", (unsigned long)consecutive_missed);
            }
            continue;
        }

        time_t now = time(NULL);
        snprintf(payload, sizeof(payload), "{\"t\":%ld}", (long)now);

        snprintf(topic, sizeof(topic), "ping/%s", device_id);
        esp_err_t err = mqtt_publish(topic, payload, strlen(payload), 0, 0);
        if (err != ESP_OK) {
            consecutive_missed++;
            s_missed_count++;
            if (consecutive_missed >= MAX_CONSECUTIVE_MISSED) {
                ESP_LOGW(TAG, "Heartbeat: %lu consecutive missed", (unsigned long)consecutive_missed);
            }
        } else {
            s_sent_count++;
            consecutive_missed = 0;
            ESP_LOGD(TAG, "Heartbeat sent to %s (seq=%lu)", topic, (unsigned long)s_sent_count);
        }
    }
}
