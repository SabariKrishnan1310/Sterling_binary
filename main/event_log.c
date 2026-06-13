#include "event_log.h"
#include "config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <sys/stat.h>

static const char *TAG = "event_log";

#define EVENT_LOG_MAGIC     0x45564C47
#define EVENT_RECORD_SIZE   (sizeof(uint64_t) + sizeof(uint8_t))

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t write_index;
    uint32_t count;
} event_log_header_t;

typedef struct __attribute__((packed)) {
    uint64_t timestamp;
    uint8_t  event_type;
} event_record_t;

static FILE *log_fp = NULL;
static event_log_header_t header;

static const char *event_names[] = {
    "BOOT", "WIFI_CONNECTED", "WIFI_DISCONNECTED",
    "OTA_STARTED", "OTA_SUCCESS", "OTA_FAILED",
    "RFID_READ", "BROWNOUT", "UPLOAD_SUCCESS", "UPLOAD_FAILED",
    "STORAGE_RECOVERY"
};

esp_err_t event_log_init(void)
{
    log_fp = fopen(EVENT_LOG_PATH, "r+b");
    if (!log_fp) {
        log_fp = fopen(EVENT_LOG_PATH, "w+b");
        if (!log_fp) {
            ESP_LOGE(TAG, "Failed to create event log");
            return ESP_FAIL;
        }
        header.magic = EVENT_LOG_MAGIC;
        header.write_index = 0;
        header.count = 0;
        fwrite(&header, sizeof(header), 1, log_fp);
        fflush(log_fp);
    } else {
        fread(&header, sizeof(header), 1, log_fp);
        if (header.magic != EVENT_LOG_MAGIC) {
            ESP_LOGW(TAG, "Event log corrupted, reinitializing");
            header.magic = EVENT_LOG_MAGIC;
            header.write_index = 0;
            header.count = 0;
            fseek(log_fp, 0, SEEK_SET);
            fwrite(&header, sizeof(header), 1, log_fp);
            fflush(log_fp);
        }
    }
    return ESP_OK;
}

esp_err_t event_log_write(event_log_type_t event)
{
    if (!log_fp) return ESP_FAIL;

    event_record_t rec;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    rec.timestamp = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    rec.event_type = (uint8_t)event;

    long pos = sizeof(event_log_header_t) + (long)header.write_index * (long)EVENT_RECORD_SIZE;
    fseek(log_fp, pos, SEEK_SET);
    fwrite(&rec, EVENT_RECORD_SIZE, 1, log_fp);

    header.write_index++;
    if (header.count < EVENT_LOG_MAX_RECORDS) {
        header.count++;
    }
    if (header.write_index >= EVENT_LOG_MAX_RECORDS) {
        header.write_index = 0;
    }

    fseek(log_fp, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, log_fp);
    fflush(log_fp);

    return ESP_OK;
}

void event_log_dump(void)
{
    if (!log_fp) {
        ESP_LOGI(TAG, "Event log not available");
        return;
    }

    ESP_LOGI(TAG, "=== Event Log (%lu events) ===", header.count);

    uint32_t read_count = header.count;
    uint32_t start_index;

    if (read_count < EVENT_LOG_MAX_RECORDS) {
        start_index = 0;
    } else {
        start_index = header.write_index;
    }

    char buf[64];
    for (uint32_t i = 0; i < read_count; i++) {
        uint32_t idx = (start_index + i) % EVENT_LOG_MAX_RECORDS;
        long pos = sizeof(event_log_header_t) + (long)idx * (long)EVENT_RECORD_SIZE;
        fseek(log_fp, pos, SEEK_SET);

        event_record_t rec;
        if (fread(&rec, sizeof(rec), 1, log_fp) != 1) break;

        uint64_t sec = rec.timestamp / 1000000;
        uint64_t us = rec.timestamp % 1000000;
        snprintf(buf, sizeof(buf), "%llu.%06llu", sec, us);

        const char *name = "UNKNOWN";
        if (rec.event_type < sizeof(event_names) / sizeof(event_names[0])) {
            name = event_names[rec.event_type];
        }

        ESP_LOGI(TAG, "  [%s] %s", buf, name);
    }
}
