#include "bulk.h"
#include "storage.h"
#include "mqtt.h"
#include "device.h"
#include "network.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "bulk";
static uint32_t s_bulk_seq = 0;
static bool s_triggered = false;
static uint32_t s_trigger_max = 0;

static uint64_t uid_hex_to_u64(const char *hex)
{
    uint64_t val = 0;
    while (*hex) {
        char c = *hex++;
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (c - '0');
        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
    }
    return val;
}

esp_err_t bulk_send(uint32_t max_entries)
{
    uint32_t pending = storage_get_pending_count();
    if (pending == 0) {
        ESP_LOGI(TAG, "No pending records");
        return ESP_OK;
    }

    uint32_t count = max_entries;
    if (count > BULK_MAX_ENTRIES) count = BULK_MAX_ENTRIES;
    if (count > pending) count = pending;

    size_t packet_size = BULK_HEADER_SIZE + count * BULK_ENTRY_SIZE;
    uint8_t *packet = malloc(packet_size);
    if (!packet) {
        ESP_LOGE(TAG, "OOM for bulk packet (%d bytes)", packet_size);
        return ESP_FAIL;
    }
    memset(packet, 0, packet_size);

    bulk_header_t *header = (bulk_header_t*)packet;
    header->seq = s_bulk_seq;
    header->count = (uint16_t)count;
    header->entry_size = BULK_ENTRY_SIZE;

    uint32_t read_seq = storage_first_pending_seq();
    for (uint32_t i = 0; i < count; i++) {
        tap_record_t rec;
        if (storage_read_at(read_seq + i, &rec) != ESP_OK) {
            header->count = (uint16_t)i; // adjust count
            break;
        }
        bulk_entry_t *entry = (bulk_entry_t*)(packet + BULK_HEADER_SIZE + i * BULK_ENTRY_SIZE);
        entry->rfid_uid = uid_hex_to_u64(rec.uid);
        entry->timestamp_ist = (uint32_t)(rec.timestamp / 1000000);
        entry->rssi = 0;
        entry->status = (uint8_t)rec.status;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "bulk/%s", device_get_id());

    esp_err_t err = mqtt_publish(topic, (const char*)packet, packet_size, 1, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bulk publish failed");
        free(packet);
        return err;
    }

    ESP_LOGI(TAG, "Bulk %lu records sent (seq=%lu)", count, s_bulk_seq);

    uint32_t last_seq = read_seq + count - 1;
    storage_mark_uploaded(last_seq);

    s_bulk_seq++;
    nvs_handle_t h;
    if (nvs_open("storage_ns", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "bulk_seq", s_bulk_seq);
        nvs_commit(h);
        nvs_close(h);
    }

    free(packet);
    return ESP_OK;
}

void bulk_trigger(uint32_t max_entries)
{
    s_triggered = true;
    s_trigger_max = max_entries > 50 ? 50 : max_entries;
}

void bulk_task(void *pvParameters)
{
    nvs_handle_t h;
    if (nvs_open("storage_ns", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "bulk_seq", &s_bulk_seq);
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Bulk task started, seq=%lu", s_bulk_seq);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_triggered) {
            s_triggered = false;

            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (!(bits & MQTT_CONNECTED_BIT)) {
                ESP_LOGW(TAG, "Bulk: MQTT not connected, deferring");
                s_triggered = true;
                continue;
            }

            for (int attempt = 0; attempt < 3; attempt++) {
                if (bulk_send(s_trigger_max) == ESP_OK) break;
                ESP_LOGW(TAG, "Bulk attempt %d failed, retrying...", attempt + 1);
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }
    }
}
