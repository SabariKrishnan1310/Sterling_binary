#include "bulk_upload.h"
#include "binary_protocol.h"
#include "mqtt_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "telemetry.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <endian.h>

static const char *TAG = "bulk";

static bulk_entry_data_t s_entries[50];
static int s_count = 0;
static char s_devid[64];

esp_err_t bulk_upload_init(void)
{
    size_t len = sizeof(s_devid);
    nvs_stl_get_string("mqtt_username", s_devid, &len);

    size_t sz = sizeof(s_entries);
    if (nvs_stl_get_blob("bulk_data", s_entries, &sz) == ESP_OK) {
        s_count = sz / sizeof(bulk_entry_data_t);
    }
    ESP_LOGI(TAG, "Bulk upload initialized, %d pending entries", s_count);
    return ESP_OK;
}

esp_err_t bulk_upload_store_entry(const bulk_entry_data_t *entry)
{
    if (s_count >= 50) {
        return ESP_FAIL;
    }
    memcpy(&s_entries[s_count++], entry, sizeof(*entry));
    nvs_stl_set_blob("bulk_data", s_entries, s_count * sizeof(bulk_entry_data_t));
    return ESP_OK;
}

int bulk_upload_get_count(void)
{
    return s_count;
}

esp_err_t bulk_upload_send(int max)
{
    if (s_count == 0) {
        return ESP_OK;
    }

    int n = (max > 0 && max < s_count) ? max : s_count;
    size_t total = BULK_HEADER_SIZE + n * BULK_ENTRY_SIZE;
    uint8_t *buf = heap_caps_malloc(total, MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate bulk buffer");
        return ESP_FAIL;
    }

    bulk_header_t *h = (bulk_header_t *)buf;
    h->sequence_number = htobe32((uint32_t)telemetry_next_seq());
    h->entry_count = htobe16(n);
    h->entry_size = htobe16(BULK_ENTRY_SIZE);

    for (int i = 0; i < n; i++) {
        bulk_entry_t *e = (bulk_entry_t *)(buf + BULK_HEADER_SIZE + i * BULK_ENTRY_SIZE);
        e->rfid_uid = htobe64(s_entries[i].rfid_uid);
        e->timestamp = htobe32(s_entries[i].timestamp);
        e->rssi = htobe16(s_entries[i].rssi);
        e->status = s_entries[i].status;
        memset(e->reserved, 0, 43);
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "bulk/%s", s_devid);
    mqtt_manager_publish(topic, buf, total, 1, 0);
    free(buf);

    memmove(s_entries, s_entries + n, (s_count - n) * sizeof(bulk_entry_data_t));
    s_count -= n;

    if (s_count > 0) {
        nvs_stl_set_blob("bulk_data", s_entries, s_count * sizeof(bulk_entry_data_t));
    } else {
        nvs_stl_erase_key("bulk_data");
    }

    return ESP_OK;
}

esp_err_t bulk_upload_clear(void)
{
    s_count = 0;
    return nvs_stl_erase_key("bulk_data");
}
