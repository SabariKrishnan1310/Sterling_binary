#include "network.h"
#include "storage.h"
#include "event_log.h"
#include "led.h"
#include "config.h"
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

static void sync_time(void)
{
    setenv("TZ", "IST-5:30", 1);
    tzset();

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.wait_for_sync = false;
    sntp_cfg.start = true;
    esp_netif_sntp_init(&sntp_cfg);

    ESP_LOGI(TAG, "Time sync started (IST timezone)");
    s_time_synced = true;
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
        led_send(LED_PATTERN_OFFLINE);

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
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err == ESP_OK) {
        snprintf(buf, len, "%s-%02X%02X%02X", DEVICE_PREFIX,
                 mac[3], mac[4], mac[5]);
    } else {
        snprintf(buf, len, "%s-UNKNOWN", DEVICE_PREFIX);
    }
}

static esp_err_t send_batch(upload_entry_t *entries, int count)
{
    if (count == 0) return ESP_OK;

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    size_t payload_size = 512 + (size_t)count * 128;
    char *payload = (char *)malloc(payload_size);
    if (!payload) return ESP_ERR_NO_MEM;

    size_t offset = 0;
    offset += snprintf(payload + offset, payload_size - offset,
                       "{\"device_id\":\"%s\",\"taps\":[", device_id);

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            offset += snprintf(payload + offset, payload_size - offset, ",");
        }
        offset += snprintf(payload + offset, payload_size - offset,
                           "{\"seq\":%lu,\"uid\":\"%s\",\"timestamp\":%llu}",
                           (unsigned long)entries[i].seq, entries[i].uid,
                           (unsigned long long)entries[i].timestamp);
    }
    offset += snprintf(payload + offset, payload_size - offset, "]}");

    char hmac_hex[65];
    esp_err_t err = compute_hmac(payload, offset, hmac_hex);
    if (err != ESP_OK) {
        free(payload);
        return err;
    }

    esp_http_client_config_t cfg = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .skip_cert_common_name_check = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(payload);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, HMAC_HEADER, hmac_hex);
    esp_http_client_set_post_field(client, payload, (int)offset);

    err = esp_http_client_perform(client);

    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }

    esp_http_client_cleanup(client);
    free(payload);

    if (err == ESP_OK && status >= 200 && status < 300) {
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "Server returned %d", status);
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
    ESP_LOGI(TAG, "[DBG] network_send_tap_single: uid=%s", uid);

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "[DBG] send_tap_single: WiFi not connected");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[DBG] send_tap_single: WiFi OK");

    int seq = load_immediate_seq() + 1;
    ESP_LOGI(TAG, "[DBG] send_tap_single: seq=%d", seq);

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));
    ESP_LOGI(TAG, "[DBG] send_tap_single: device_id=%s", device_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "raw_hex", uid);
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "seq", seq);
    cJSON_AddNullToObject(root, "timestamp");
    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "[DBG] send_tap_single: json_body=%s", json_body);

    char sig_data[512];
    snprintf(sig_data, sizeof(sig_data), "%d%s", seq, json_body);
    ESP_LOGI(TAG, "[DBG] send_tap_single: sig_data len=%zu", strlen(sig_data));

    char signature[65];
    esp_err_t err = compute_hmac(sig_data, strlen(sig_data), signature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[DBG] send_tap_single: HMAC failed");
        free(json_body);
        return err;
    }
    ESP_LOGI(TAG, "[DBG] send_tap_single: signature=%s", signature);

    esp_http_client_config_t cfg = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };
    ESP_LOGI(TAG, "[DBG] send_tap_single: HTTP config ready");

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "[DBG] send_tap_single: http client init failed");
        free(json_body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, HMAC_HEADER, signature);
    esp_http_client_set_post_field(client, json_body, strlen(json_body));
    ESP_LOGI(TAG, "[DBG] send_tap_single: headers set, performing POST");

    err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "[DBG] send_tap_single: HTTP status=%d", status);
    } else {
        ESP_LOGW(TAG, "[DBG] send_tap_single: HTTP error=%s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_body);

    if (err == ESP_OK && status == 202) {
        save_immediate_seq(seq);
        ESP_LOGI(TAG, "[DBG] send_tap_single: SUCCESS seq=%d", seq);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "[DBG] send_tap_single: FAILED (err=%s status=%d)",
             err == ESP_OK ? "OK" : esp_err_to_name(err), status);
    return ESP_FAIL;
}

void upload_task(void *pvParameters)
{
    upload_entry_t entries[UPLOAD_BATCH_SIZE];
    TickType_t last_upload_tick = xTaskGetTickCount();

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));

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
            ESP_LOGW(TAG, "Upload failed, will retry");
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_RETRY_DELAY_MS));
        }

        last_upload_tick = xTaskGetTickCount();
    }
}
