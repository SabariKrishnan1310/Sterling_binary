#include "network.h"
#include "storage.h"
#include "event_log.h"
#include "led.h"
#include "config.h"
#include "mqtt.h"
#include "telemetry.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "network";

EventGroupHandle_t wifi_event_group = NULL;
volatile bool upload_force_flag = false;

static int s_retry_count = 0;
static int s_active_profile = 0;
static bool s_time_synced = false;

typedef struct {
    char ssid[64];
    char password[64];
} wifi_profile_t;

static wifi_profile_t profiles[WIFI_MAX_PROFILES];
static int profile_count = 0;

static void seed_default_profiles(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for seeding profiles");
        return;
    }

    uint8_t count = 1;
    nvs_set_u8(handle, "count", count);

    nvs_set_str(handle, "ssid_0", "JaayM34");
    nvs_set_str(handle, "pwd_0", "manju@2809");

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Seeded default WiFi profile: JaayM34");
    } else {
        ESP_LOGE(TAG, "Failed to commit default WiFi profile");
    }
}

static void load_wifi_profiles(void)
{
    nvs_handle_t handle;
    memset(profiles, 0, sizeof(profiles));
    profile_count = 0;

    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi profiles in NVS, seeding defaults");
        seed_default_profiles();
        err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) return;
    }

    uint8_t count = 0;
    if (nvs_get_u8(handle, "count", &count) != ESP_OK) {
        nvs_close(handle);
        ESP_LOGW(TAG, "No profile count in NVS, seeding defaults");
        seed_default_profiles();
        err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) return;
        if (nvs_get_u8(handle, "count", &count) != ESP_OK) {
            nvs_close(handle);
            return;
        }
    }

    for (uint8_t i = 0; i < count && i < WIFI_MAX_PROFILES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "ssid_%u", i);
        size_t len = sizeof(profiles[i].ssid);
        if (nvs_get_str(handle, key, profiles[i].ssid, &len) == ESP_OK) {
            snprintf(key, sizeof(key), "pwd_%u", i);
            len = sizeof(profiles[i].password);
            if (nvs_get_str(handle, key, profiles[i].password, &len) == ESP_OK) {
                profile_count++;
            }
        }
    }
    nvs_close(handle);

    for (int i = 0; i < profile_count; i++) {
        ESP_LOGI(TAG, "Profile %d: SSID=%s", i, profiles[i].ssid);
    }
}

static void switch_to_profile(int idx)
{
    if (idx >= profile_count) idx = 0;

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, profiles[idx].ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, profiles[idx].password, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    ESP_LOGI(TAG, "Switched to profile %d: SSID=%s", idx, profiles[idx].ssid);
}

static void on_time_sync(struct timeval *tv)
{
    s_time_synced = true;
    ESP_LOGI(TAG, "Time synchronized");
}

static void sync_time(void)
{
    setenv("TZ", "IST-5:30", 1);
    tzset();

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.wait_for_sync = true;     // creates internal sync semaphore
    sntp_cfg.sync_cb = on_time_sync;   // callback on sync event
    sntp_cfg.start = true;
    esp_netif_sntp_init(&sntp_cfg);

    ESP_LOGI(TAG, "Time sync started (IST timezone)");
    // s_time_synced set by on_time_sync callback, NOT immediately
}

void network_wait_for_time_sync(void)
{
    if (s_time_synced) {
        return;  // already synced
    }
    ESP_LOGI(TAG, "Waiting for NTP time sync...");
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NTP sync wait complete");
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "NTP sync timeout (15s), continuing with unsynced clock");
    } else {
        ESP_LOGW(TAG, "NTP sync wait returned %s", esp_err_to_name(err));
    }
    // s_time_synced may still be false, but we don't block further
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "[DBG] wifi_event: STA_START");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "[DBG] wifi_event: DISCONNECTED reason=%d", disc->reason);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        event_log_write(EVT_WIFI_DISCONNECTED);
        led_send(LED_PATTERN_WAVE);

        s_retry_count++;
        if (s_retry_count > 3 && profile_count > 1) {
            s_retry_count = 0;
            s_active_profile = (s_active_profile + 1) % profile_count;
            switch_to_profile(s_active_profile);
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        event_log_write(EVT_WIFI_CONNECTED);
        led_send(LED_PATTERN_IDLE);

        if (!s_time_synced) {
            sync_time();
        }
    }
}

esp_err_t network_init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    (void)instance_any_id;
    (void)instance_got_ip;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    load_wifi_profiles();
    if (profile_count == 0) {
        ESP_LOGW(TAG, "No WiFi profiles configured");
    }

    return ESP_OK;
}

esp_err_t network_start_wifi(void)
{
    if (profile_count == 0) {
        ESP_LOGE(TAG, "Cannot start WiFi: no profiles");
        return ESP_FAIL;
    }

    s_active_profile = 0;
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, profiles[0].ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, profiles[0].password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.failure_retry_cnt = 3;

    ESP_LOGI(TAG, "Connecting to profile 0: SSID=%s", profiles[0].ssid);

    ESP_LOGI(TAG, "[DBG] wifi_start: stopping previous connection");
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "[DBG] wifi_start: setting config for %s", profiles[0].ssid);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "[DBG] wifi_start: starting WiFi");
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "[DBG] wifi_start: OK");
    return ESP_OK;
}

void network_wifi_task(void *pvParameters)
{
    esp_err_t err = network_start_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed on first attempt");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if (!(bits & WIFI_CONNECTED_BIT)) {
            if (profile_count > 1 && s_retry_count > 3) {
                s_retry_count = 0;
                s_active_profile = (s_active_profile + 1) % profile_count;
                switch_to_profile(s_active_profile);
            }
            esp_wifi_connect();
        }
    }
}

static esp_err_t compute_hmac(const char *payload, size_t len, char *hmac_hex)
{
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }

    mbedtls_md_setup(&ctx, md_info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)API_SECRET, strlen(API_SECRET));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, len);
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    for (int i = 0; i < 32; i++) {
        sprintf(hmac_hex + (i * 2), "%02x", hmac[i]);
    }
    hmac_hex[64] = '\0';

    return ESP_OK;
}

typedef struct {
    char uid[64];
    uint64_t timestamp;
    uint32_t seq;
} upload_entry_t;

static void get_device_id(char *buf, size_t len)
{
#ifdef DEVICE_ID
    strncpy(buf, DEVICE_ID, len - 1);
    buf[len - 1] = '\0';
#else
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err == ESP_OK) {
        snprintf(buf, len, "%s-%02X%02X%02X", DEVICE_PREFIX,
                 mac[3], mac[4], mac[5]);
    } else {
        snprintf(buf, len, "%s-UNKNOWN", DEVICE_PREFIX);
    }
#endif
}

static esp_err_t send_one_tap(const char *uid, int seq, const char *device_id, char *response_buf, size_t response_buf_size);

static esp_err_t send_batch(upload_entry_t *entries, int count)
{
    if (count == 0) return ESP_OK;
    ESP_LOGI(TAG, "Sending %d stored records one by one", count);

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    for (int i = 0; i < count; i++) {
        esp_err_t err = send_one_tap(entries[i].uid, (int)entries[i].seq, device_id, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Record %d (seq=%lu) failed, stopping batch", i, (unsigned long)entries[i].seq);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_OK;
}

static esp_err_t send_one_tap(const char *uid, int seq, const char *device_id, char *response_buf, size_t response_buf_size)
{
    ESP_LOGI(TAG, "---[REQUEST]--- seq=%d uid=%s device=%s", seq, uid, device_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "raw_hex", uid);
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "seq", seq);
    cJSON_AddNullToObject(root, "timestamp");
    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "[REQ BODY] %s", json_body);

    char sig_data[512];
    snprintf(sig_data, sizeof(sig_data), "%d\n%s", seq, json_body);

    char signature[65];
    esp_err_t err = compute_hmac(sig_data, strlen(sig_data), signature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[REQ ERR] HMAC failed");
        free(json_body);
        return err;
    }
    ESP_LOGI(TAG, "[REQ HMAC] %s", signature);

    esp_http_client_config_t cfg = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(json_body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, HMAC_HEADER, signature);
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);
        if (content_len > 0 && response_buf && response_buf_size > 0) {
            int read = esp_http_client_read(client, response_buf, response_buf_size - 1);
            if (read > 0) response_buf[read] = '\0';
        }
        ESP_LOGI(TAG, "---[RESPONSE]--- HTTP %d body=%s", status, response_buf && response_buf[0] ? response_buf : "(empty)");
    } else {
        ESP_LOGE(TAG, "---[RESPONSE]--- HTTP FAIL: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_body);

    if (err == ESP_OK && status == 202) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static int load_immediate_seq(void)
{
    nvs_handle_t handle;
    int32_t seq = 0;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_i32(handle, "seq", &seq);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "[DBG] load_immediate_seq: %d", (int)seq);
    return (int)seq;
}

static void save_immediate_seq(int seq)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_i32(handle, "seq", seq);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "[DBG] save_immediate_seq: %d", seq);
}

esp_err_t network_send_tap_single(const char *uid)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "send_tap_single: WiFi not connected");
        return ESP_FAIL;
    }

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    // PATH 1: MQTT (primary)
    if (mqtt_is_connected()) {
        char topic[64];
        snprintf(topic, sizeof(topic), "tap/%s", device_id);
        esp_err_t mqtt_err = mqtt_publish(topic, uid, strlen(uid), 1, 0);
        if (mqtt_err == ESP_OK) {
            ESP_LOGI(TAG, "Tap via MQTT: %s", uid);
            telemetry_increment_tags(1);
            int seq = load_immediate_seq() + 1;
            save_immediate_seq(seq);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "MQTT publish failed, falling back to HTTP");
    }

    // PATH 2: HTTP (fallback)
    int seq = load_immediate_seq() + 1;
    esp_err_t err = send_one_tap(uid, seq, device_id, NULL, 0);
    if (err == ESP_OK) {
        save_immediate_seq(seq);
        telemetry_increment_tags(1);
        return ESP_OK;
    }

    // PATH 3: LittleFS (last resort) — handled by caller
    return ESP_FAIL;
}

void upload_task(void *pvParameters)
{
    upload_entry_t entries[UPLOAD_BATCH_SIZE];
    TickType_t last_upload_tick = xTaskGetTickCount();
    static const uint32_t BACKOFF_DELAYS[] = {30, 60, 120, 300, 600, 1800, 3600};
    int backoff_stage = 0;
    int consecutive_failures = 0;

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (upload_force_flag) {
            upload_force_flag = false;
            last_upload_tick = 0;
        }

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if (!(bits & WIFI_CONNECTED_BIT)) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_upload_tick) < pdMS_TO_TICKS(UPLOAD_INTERVAL_MS)) {
            continue;
        }

        uint32_t pending = storage_get_pending_count();
        if (pending == 0) {
            last_upload_tick = now;
            continue;
        }

        ESP_LOGI(TAG, "Uploading: %lu pending records", (unsigned long)pending);

        uint32_t read_seq = storage_first_pending_seq();
        int batch_count = 0;
        for (int i = 0; i < UPLOAD_BATCH_SIZE; i++) {
            tap_record_t rec;
            esp_err_t err = storage_read_at(read_seq + i, &rec);
            if (err != ESP_OK) break;

            entries[batch_count].seq = rec.seq;
            entries[batch_count].timestamp = rec.timestamp;
            strncpy(entries[batch_count].uid, rec.uid, sizeof(entries[batch_count].uid) - 1);
            entries[batch_count].uid[sizeof(entries[batch_count].uid) - 1] = '\0';
            batch_count++;

            if (read_seq + i + 1 >= storage_get_next_sequence()) break;
        }

        if (batch_count == 0) {
            last_upload_tick = now;
            continue;
        }

        esp_err_t upload_err = send_batch(entries, batch_count);
        if (upload_err == ESP_OK) {
            consecutive_failures = 0;
            backoff_stage = 0;
            uint32_t last_seq = entries[batch_count - 1].seq;
            esp_err_t mark_err = storage_mark_uploaded(last_seq);
            if (mark_err == ESP_OK) {
                event_log_write(EVT_UPLOAD_SUCCESS);
                ESP_LOGI(TAG, "Uploaded %d records, cursor now %lu",
                         batch_count, (unsigned long)last_seq);
            } else {
                ESP_LOGE(TAG, "Failed to save upload cursor: %s",
                         esp_err_to_name(mark_err));
            }
        } else {
            event_log_write(EVT_UPLOAD_FAILED);
            consecutive_failures++;
            if (consecutive_failures >= sizeof(BACKOFF_DELAYS) / sizeof(BACKOFF_DELAYS[0])) {
                backoff_stage = sizeof(BACKOFF_DELAYS) / sizeof(BACKOFF_DELAYS[0]) - 1;
            } else {
                backoff_stage = consecutive_failures - 1;
            }
            if (backoff_stage < 0) backoff_stage = 0;
            uint32_t delay_s = BACKOFF_DELAYS[backoff_stage];
            ESP_LOGW(TAG, "Upload failed (%d consecutive), backing off %lus",
                     consecutive_failures, delay_s);
            vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        }

        last_upload_tick = xTaskGetTickCount();
    }
}
