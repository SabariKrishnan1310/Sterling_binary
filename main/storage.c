#include "storage.h"
#include "config.h"
#include "event_log.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <sys/stat.h>

static const char *TAG = "storage";

#define NVS_NAMESPACE       "storage_ns"
#define NVS_KEY_CURSOR      "upload_csr"

#define CURSOR_NONE         UINT32_MAX

static FILE *taps_fp = NULL;
static uint32_t next_seq = 0;
static uint32_t upload_cursor = CURSOR_NONE;
static uint32_t pending_count = 0;
static uint32_t total_count = 0;

static esp_err_t load_cursor(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        upload_cursor = CURSOR_NONE;
        return ESP_OK;
    }
    uint32_t val;
    err = nvs_get_u32(handle, NVS_KEY_CURSOR, &val);
    if (err == ESP_OK) {
        upload_cursor = val;
    } else {
        upload_cursor = CURSOR_NONE;
    }
    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t save_cursor(uint32_t cursor)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, NVS_KEY_CURSOR, cursor);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void update_pending_count(void)
{
    if (next_seq == 0) {
        pending_count = 0;
    } else if (upload_cursor == CURSOR_NONE) {
        pending_count = next_seq;
    } else if (upload_cursor >= next_seq - 1) {
        pending_count = 0;
    } else {
        pending_count = next_seq - (upload_cursor + 1);
    }
}

esp_err_t storage_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(err));
        return err;
    }

    size_t total_bytes = 0, used_bytes = 0;
    if (esp_littlefs_info(conf.partition_label, &total_bytes, &used_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS: %lu/%lu bytes used",
                 (unsigned long)used_bytes, (unsigned long)total_bytes);
    }

    taps_fp = fopen(STORAGE_FILE_PATH, "a+b");
    if (!taps_fp) {
        ESP_LOGE(TAG, "Failed to open taps file at %s", STORAGE_FILE_PATH);
        return ESP_FAIL;
    }

    setvbuf(taps_fp, NULL, _IONBF, 0);

    fseek(taps_fp, 0, SEEK_END);
    long file_size = ftell(taps_fp);

    size_t record_size = sizeof(tap_record_t);
    total_count = 0;

    if (file_size > 0) {
        size_t remainder = (size_t)file_size % record_size;
        if (remainder > 0) {
            long truncated_size = file_size - (long)remainder;
            ESP_LOGW(TAG, "Partial write detected (%u bytes), truncating to %ld",
                     remainder, truncated_size);
            if (ftruncate(fileno(taps_fp), truncated_size) != 0) {
                ESP_LOGE(TAG, "Failed to truncate corrupted records");
                fclose(taps_fp);
                taps_fp = NULL;
                return ESP_FAIL;
            }
            event_log_write(EVT_STORAGE_RECOVERY);
            file_size = truncated_size;
        }
        total_count = (uint32_t)(file_size / record_size);
    }

    load_cursor();

    if (upload_cursor != CURSOR_NONE && upload_cursor >= total_count) {
        ESP_LOGW(TAG, "Cursor (%lu) beyond EOF (%lu), resetting",
                 upload_cursor, total_count);
        upload_cursor = CURSOR_NONE;
        save_cursor(CURSOR_NONE);
    }

    next_seq = total_count;
    update_pending_count();

    ESP_LOGI(TAG, "init: total=%lu pending=%lu cursor=%s%lu next_seq=%lu",
             total_count, pending_count,
             upload_cursor == CURSOR_NONE ? "NONE/" : "",
             upload_cursor == CURSOR_NONE ? 0 : upload_cursor,
             next_seq);
    return ESP_OK;
}

esp_err_t storage_append_tap(const char *uid, uint32_t *out_seq)
{
    if (!taps_fp) {
        ESP_LOGE(TAG, "[DBG] append: file not open");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[DBG] append: uid=%s next_seq=%lu", uid, (unsigned long)next_seq);

    if (pending_count >= STORAGE_MAX_RECORDS) {
        ESP_LOGW(TAG, "Storage full (%lu pending), recycling oldest record",
                 (unsigned long)pending_count);
        if (upload_cursor == CURSOR_NONE) {
            upload_cursor = 0;
        } else {
            upload_cursor++;
        }
        esp_err_t save_err = save_cursor(upload_cursor);
        if (save_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save cursor during recycling");
        }
        update_pending_count();
    }

    tap_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.seq = next_seq;
    rec.status = TAP_PENDING;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    rec.timestamp = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;

    size_t uid_len = strlen(uid);
    if (uid_len > sizeof(rec.uid) - 1) {
        uid_len = sizeof(rec.uid) - 1;
    }
    rec.uid_len = (uint8_t)uid_len;
    memcpy(rec.uid, uid, uid_len);
    rec.uid[uid_len] = '\0';

    fseek(taps_fp, 0, SEEK_END);
    size_t written = fwrite(&rec, sizeof(rec), 1, taps_fp);
    if (written != 1) {
        ESP_LOGE(TAG, "Failed to append tap record");
        return ESP_FAIL;
    }

    fflush(taps_fp);
    fsync(fileno(taps_fp));

    if (out_seq) *out_seq = rec.seq;
    next_seq++;
    total_count++;
    pending_count++;

    ESP_LOGI(TAG, "[DBG] append: OK seq=%lu total=%lu pending=%lu",
             (unsigned long)rec.seq, (unsigned long)total_count, (unsigned long)pending_count);
    return ESP_OK;
}

esp_err_t storage_get_next_pending(tap_record_t *record)
{
    if (!taps_fp || !record) return ESP_FAIL;

    uint32_t target_seq;
    if (upload_cursor == CURSOR_NONE) {
        target_seq = 0;
    } else {
        target_seq = upload_cursor + 1;
    }

    if (target_seq >= next_seq) return ESP_FAIL;

    return storage_read_at(target_seq, record);
}

esp_err_t storage_read_at(uint32_t seq, tap_record_t *record)
{
    if (!taps_fp || !record) return ESP_FAIL;
    if (seq >= next_seq) return ESP_FAIL;

    long pos = (long)seq * (long)sizeof(tap_record_t);
    fseek(taps_fp, pos, SEEK_SET);

    if (fread(record, sizeof(tap_record_t), 1, taps_fp) != 1) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint32_t storage_first_pending_seq(void)
{
    if (next_seq == 0) return 0;
    if (upload_cursor == CURSOR_NONE) return 0;
    return upload_cursor + 1;
}

esp_err_t storage_mark_uploaded(uint32_t seq)
{
    if (upload_cursor == CURSOR_NONE || seq > upload_cursor) {
        upload_cursor = seq;
        esp_err_t err = save_cursor(upload_cursor);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save upload cursor");
            return err;
        }
        update_pending_count();
    }
    return ESP_OK;
}

uint32_t storage_get_next_sequence(void)
{
    return next_seq;
}

uint32_t storage_get_pending_count(void)
{
    return pending_count;
}

uint32_t storage_get_total_count(void)
{
    return total_count;
}

void storage_dump_stats(void)
{
    ESP_LOGI(TAG, "=== Storage Stats ===");
    ESP_LOGI(TAG, "  Total records:  %lu", total_count);
    ESP_LOGI(TAG, "  Pending:        %lu", pending_count);
    if (upload_cursor == CURSOR_NONE) {
        ESP_LOGI(TAG, "  Upload cursor:  NONE");
    } else {
        ESP_LOGI(TAG, "  Upload cursor:  %lu", upload_cursor);
    }
    ESP_LOGI(TAG, "  Next sequence:  %lu", next_seq);
    ESP_LOGI(TAG, "  Max capacity:   %u", STORAGE_MAX_RECORDS);

    long file_size = 0;
    if (taps_fp) {
        fseek(taps_fp, 0, SEEK_END);
        file_size = ftell(taps_fp);
    }
    ESP_LOGI(TAG, "  File size:      %ld bytes", file_size);
}
