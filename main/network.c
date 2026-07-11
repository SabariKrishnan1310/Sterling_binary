// ============================================================
// STERLING PROD — v1.0.9 ANTIFRAGILE WIFI
// ============================================================
// Max TX power, scan-before-connect with auto security detect,
// round-robin profile rotation, exponential backoff with jitter,
// patient reconnection, NTP fallback, SoftAP emergency trigger
// ============================================================

#include "network.h"
#include "storage.h"
#include "event_log.h"
#include "led.h"
#include "config.h"
#include "provision.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "esp_timer.h"
#include <inttypes.h>

static const char *TAG = "net";

// Forward declarations
static void config_fetch_task(void *pvParameters);

// ============================================================
// STATE VARIABLES
// ============================================================

EventGroupHandle_t wifi_event_group = NULL;

static int s_retry_count = 0;
static int s_active_profile = 0;
static bool s_time_synced = false;
static bool s_softap_active = false;
static uint32_t s_consecutive_fails = 0;
static uint32_t s_total_connects = 0;
static uint32_t s_total_disconnects = 0;

// Backoff state
static uint32_t s_backoff_ms = WIFI_BACKOFF_BASE_MS;
static bool s_backoff_active = false;
static TickType_t s_backoff_start_tick = 0;

// ============================================================
// WiFi PROFILES
// ============================================================

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

    // Seed 4 default profiles — device tries all of them on boot
    nvs_set_u8(handle, "count", 4);

    nvs_set_str(handle, "ssid_0", "JaayM34");
    nvs_set_str(handle, "pwd_0", "manju@2809");

    nvs_set_str(handle, "ssid_1", "GNXS8220348E");
    nvs_set_str(handle, "pwd_1", "B43D082E1580");

    nvs_set_str(handle, "ssid_2", "ANUPAMA");
    nvs_set_str(handle, "pwd_2", "9900518340");

    nvs_set_str(handle, "ssid_3", "iPhone");
    nvs_set_str(handle, "pwd_3", "manju@2809");

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Seeded 4 default WiFi profiles: JaayM34, GNXS8220348E, ANUPAMA, iPhone");
    } else {
        ESP_LOGE(TAG, "Failed to commit default WiFi profiles");
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

    ESP_LOGI(TAG, "Loaded %d WiFi profiles:", profile_count);
    for (int i = 0; i < profile_count; i++) {
        ESP_LOGI(TAG, "  [%d] SSID=%s", i, profiles[i].ssid);
    }
}

// ============================================================
// SCAN BEFORE CONNECT — AUTO SECURITY TYPE DETECTION
// ============================================================
// Scans for the target SSID, detects its RSSI and auth mode.
// Returns scan results so caller decides whether to connect.

typedef struct {
    bool found;
    int8_t rssi;
    wifi_auth_mode_t authmode;
    uint8_t channel;
} scan_result_t;

static scan_result_t scan_for_ssid(const char *target_ssid)
{
    scan_result_t result = { .found = false, .rssi = -127, .authmode = WIFI_AUTH_OPEN, .channel = 0 };

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = { .min = 100, .max = WIFI_SCAN_TIMEOUT_MS },
        },
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return result;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        ESP_LOGW(TAG, "Scan returned 0 APs");
        return result;
    }

    uint16_t fetch_count = ap_count;
    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!ap_records) return result;

    err = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
    if (err != ESP_OK) {
        free(ap_records);
        return result;
    }

    for (int i = 0; i < fetch_count; i++) {
        if (strcmp((char *)ap_records[i].ssid, target_ssid) == 0) {
            result.found = true;
            result.rssi = ap_records[i].rssi;
            result.authmode = ap_records[i].authmode;
            result.channel = ap_records[i].primary;
            ESP_LOGI(TAG, "Scan: found %s rssi=%d auth=%d ch=%u",
                     target_ssid, result.rssi, result.authmode, result.channel);
            break;
        }
    }

    free(ap_records);
    return result;
}

// Full channel scan: try every stored profile against every found AP.
// Returns best profile index or -1.
static int full_scan_recovery(void)
{
    ESP_LOGI(TAG, "Full scan recovery: scanning all channels");

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = { .min = 200, .max = WIFI_SCAN_TIMEOUT_MS * 2 },
        },
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) return -1;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return -1;

    uint16_t fetch_count = ap_count;
    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!ap_records) return -1;

    err = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
    if (err != ESP_OK) {
        free(ap_records);
        return -1;
    }

    int best_profile = -1;
    int8_t best_rssi = -127;

    for (int p = 0; p < profile_count; p++) {
        for (int a = 0; a < fetch_count; a++) {
            if (strcmp((char *)ap_records[a].ssid, profiles[p].ssid) == 0) {
                if (ap_records[a].rssi > best_rssi) {
                    best_rssi = ap_records[a].rssi;
                    best_profile = p;
                }
            }
        }
    }

    free(ap_records);

    if (best_profile >= 0) {
        ESP_LOGI(TAG, "Full scan: best match profile %d (%s) rssi=%d",
                 best_profile, profiles[best_profile].ssid, best_rssi);
    }
    return best_profile;
}

// ============================================================
// ROUND-ROBIN + SCAN-BEFORE-CONNECT
// ============================================================
// Scans first. Only connects if target AP is visible.
// Uses detected auth mode (not hardcoded WPA2).

static bool switch_to_profile(int idx)
{
    if (idx < 0 || idx >= profile_count) idx = 0;

    scan_result_t scan = scan_for_ssid(profiles[idx].ssid);

    if (!scan.found) {
        ESP_LOGW(TAG, "Profile %d SSID '%s' not visible, skipping",
                 idx, profiles[idx].ssid);
        return false;
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, profiles[idx].ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, profiles[idx].password,
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    // Use detected auth mode — supports OPEN, WPA, WPA2, WPA3
    wifi_config.sta.threshold.authmode = scan.authmode;
    wifi_config.sta.failure_retry_cnt = 3;

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed for profile %d: %s",
                 idx, esp_err_to_name(err));
        return false;
    }

    s_active_profile = idx;
    ESP_LOGI(TAG, "Switched to profile %d: SSID=%s (rssi=%d auth=%d ch=%u)",
             idx, profiles[idx].ssid, scan.rssi, scan.authmode, scan.channel);
    return true;
}

// ============================================================
// EXPONENTIAL BACKOFF WITH JITTER
// ============================================================

static uint32_t compute_backoff(int retry_count)
{
    uint32_t base = WIFI_BACKOFF_BASE_MS;
    for (int i = 0; i < retry_count && base < WIFI_BACKOFF_MAX_MS; i++) {
        base *= 2;
    }
    if (base > WIFI_BACKOFF_MAX_MS) base = WIFI_BACKOFF_MAX_MS;

    // +/-25% jitter to prevent thundering herd
    uint32_t jitter_range = base / 4;
    uint32_t jitter = (esp_random() % (jitter_range * 2 + 1));
    uint32_t result = base - jitter_range + jitter;
    if (result > WIFI_BACKOFF_MAX_MS) result = WIFI_BACKOFF_MAX_MS;
    if (result < 1000) result = 1000;

    return result;
}

static void backoff_reset(void)
{
    s_backoff_ms = WIFI_BACKOFF_BASE_MS;
    s_backoff_active = false;
}

static bool backoff_should_retry(void)
{
    if (!s_backoff_active) return true;
    TickType_t elapsed = xTaskGetTickCount() - s_backoff_start_tick;
    TickType_t delay_ticks = pdMS_TO_TICKS(s_backoff_ms);
    return elapsed >= delay_ticks;
}

static void backoff_start(void)
{
    s_backoff_ms = compute_backoff(s_retry_count);
    s_backoff_active = true;
    s_backoff_start_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "Backoff: %lu ms (retry=%d)", (unsigned long)s_backoff_ms, s_retry_count);
}

// ============================================================
// DISCONNECT REASON HANDLER — ROUND-ROBIN + FULL SCAN
// ============================================================

static void handle_disconnect_reason(uint8_t reason)
{
    s_consecutive_fails++;
    s_total_disconnects++;

    ESP_LOGW(TAG, "Disconnect reason=%d fails=%lu connects=%lu",
             reason, (unsigned long)s_consecutive_fails, (unsigned long)s_total_connects);

    event_log_write(EVT_WIFI_DISCONNECTED);
    led_send(LED_PATTERN_WAVE);

    s_retry_count++;

    // Auth failures: skip to next profile immediately
    if (reason == 202 || reason == 15 || reason == 16) {
        ESP_LOGW(TAG, "Auth/handshake fail (reason=%d), rotating profile", reason);
        s_retry_count = WIFI_MAX_RETRY_BEFORE_ROTATE + 1;
    }

    // Beacon timeout: full scan recovery
    if (reason == 201) {
        ESP_LOGW(TAG, "Beacon timeout, trying full scan recovery");
        int best = full_scan_recovery();
        if (best >= 0) {
            s_retry_count = 0;
            switch_to_profile(best);
            backoff_start();
            return;
        }
    }

    // Round-robin: rotate profile after N retries per profile
    if (s_retry_count > WIFI_MAX_RETRY_BEFORE_ROTATE && profile_count > 1) {
        s_retry_count = 0;
        int old_profile = s_active_profile;
        s_active_profile = (s_active_profile + 1) % profile_count;

        if (!switch_to_profile(s_active_profile)) {
            // Target not visible, try full scan
            int best = full_scan_recovery();
            if (best >= 0 && best != old_profile) {
                switch_to_profile(best);
            }
        }
    }

    backoff_start();

    // SoftAP emergency after too many failures
    if (s_consecutive_fails >= WIFI_SOFTAP_TRIGGER_COUNT && !s_softap_active) {
        ESP_LOGW(TAG, "SoftAP trigger: %lu consecutive failures",
                 (unsigned long)s_consecutive_fails);
        s_softap_active = true;
    }
}

// ============================================================
// WIFI EVENT HANDLER
// ============================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA_START, connecting...");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc =
            (wifi_event_sta_disconnected_t *)event_data;

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        handle_disconnect_reason(disc->reason);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_retry_count = 0;
        s_consecutive_fails = 0;
        s_total_connects++;
        backoff_reset();

        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        event_log_write(EVT_WIFI_CONNECTED);
        led_send(LED_PATTERN_IDLE);

        // Spawn config fetch on first connection
        if (!s_time_synced) {
            BaseType_t ret = xTaskCreatePinnedToCore(config_fetch_task, "cfg_fetch", 8192, NULL, 1, NULL, 0);
            if (ret != pdPASS) {
                ESP_LOGW(TAG, "Failed to create config fetch task");
                s_time_synced = false;  // retry on next connection
            }
        }
    }
}

// ============================================================
// NTP FALLBACK
// ============================================================

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

// ============================================================
// CONFIG FETCH TASK
// ============================================================

static void config_fetch_task(void *pvParameters)
{
    s_time_synced = true;
    ESP_LOGI(TAG, "Config fetch task started");

    esp_err_t cfg_err = wifi_fetch_global_config();
    if (cfg_err != ESP_OK) {
        ESP_LOGW(TAG, "API config failed, falling back to NTP");
        sync_time();
    } else {
        event_log_write(EVT_WIFI_CONFIG_FETCHED);
        // Reload profiles after successful fetch
        load_wifi_profiles();
    }

    vTaskDelete(NULL);
}

// ============================================================
// MAX TX POWER — PCB TRACE ANTENNA (19.5 dBm)
// ============================================================

static void apply_wifi_radio_settings(void)
{
    esp_err_t err = esp_wifi_set_max_tx_power(WIFI_TX_POWER_MAX);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_max_tx_power failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "TX power max: %d (%.1f dBm)",
                 WIFI_TX_POWER_MAX, WIFI_TX_POWER_MAX * 0.25);
    }

    // Power save OFF for maximum range and reliability
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_ps(NONE) failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WiFi power save: OFF (max range)");
    }
}

// ============================================================
// NETWORK INIT
// ============================================================

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

    // NOTE: TX power and power save are applied in network_start_wifi()
    // after esp_wifi_start(), since those APIs require running WiFi.

    load_wifi_profiles();
    if (profile_count == 0) {
        ESP_LOGW(TAG, "No WiFi profiles configured");
    }

    return ESP_OK;
}

// ============================================================
// NETWORK START WIFI — SCAN BEFORE CONNECT
// ============================================================

esp_err_t network_start_wifi(void)
{
    if (profile_count == 0) {
        ESP_LOGE(TAG, "Cannot start WiFi: no profiles");
        return ESP_FAIL;
    }

    s_active_profile = 0;

    // Start WiFi FIRST so we can scan
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Use APSTA if SoftAP is active, otherwise pure STA
    if (network_is_softap_active()) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        ESP_LOGI(TAG, "WiFi mode: APSTA (SoftAP active)");
    } else {
        esp_wifi_set_mode(WIFI_MODE_STA);
        ESP_LOGI(TAG, "WiFi mode: STA");
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, profiles[s_active_profile].ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, profiles[s_active_profile].password,
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.failure_retry_cnt = 3;

    // Apply max TX power BEFORE esp_wifi_start
    apply_wifi_radio_settings();

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    // If SoftAP is active, reconfigure AP interface (esp_wifi_stop killed it)
    if (network_is_softap_active()) {
        wifi_config_t ap_cfg = { 0 };
        strncpy((char *)ap_cfg.ap.ssid, SOFTAP_SSID, 32);
        strncpy((char *)ap_cfg.ap.password, SOFTAP_PASSWORD, 64);
        ap_cfg.ap.ssid_len = strlen(SOFTAP_SSID);
        ap_cfg.ap.channel = SOFTAP_CHANNEL;
        ap_cfg.ap.max_connection = SOFTAP_MAX_CONN;
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

        // Restore static IP for AP
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif) {
            esp_netif_dhcps_stop(ap_netif);
            esp_netif_ip_info_t ip_info = {
                .ip.addr = ipaddr_addr(SOFTAP_IP_ADDR),
                .gw.addr = ipaddr_addr(SOFTAP_IP_GW),
                .netmask.addr = ipaddr_addr(SOFTAP_IP_NETMASK),
            };
            esp_netif_set_ip_info(ap_netif, &ip_info);
            esp_netif_dhcps_start(ap_netif);
        }
        ESP_LOGI(TAG, "AP interface reconfigured (SoftAP active)");
    }

    ESP_LOGI(TAG, "WiFi started OK");

    // NOW scan — WiFi is running
    scan_result_t scan = scan_for_ssid(profiles[0].ssid);
    if (!scan.found) {
        ESP_LOGW(TAG, "Profile 0 SSID '%s' not visible, trying full scan",
                 profiles[0].ssid);
        int best = full_scan_recovery();
        if (best >= 0) {
            // Switch to best found profile
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(100));
            s_active_profile = best;
            memset(&wifi_config, 0, sizeof(wifi_config));
            strncpy((char *)wifi_config.sta.ssid, profiles[best].ssid,
                    sizeof(wifi_config.sta.ssid) - 1);
            strncpy((char *)wifi_config.sta.password, profiles[best].password,
                    sizeof(wifi_config.sta.password) - 1);
            wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
            wifi_config.sta.failure_retry_cnt = 3;
            apply_wifi_radio_settings();
            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            esp_wifi_start();
        } else {
            ESP_LOGW(TAG, "No matching AP found, will retry on next poll");
        }
    }

    ESP_LOGI(TAG, "Connecting to profile %d: SSID=%s",
             s_active_profile, profiles[s_active_profile].ssid);
    return ESP_OK;
}

// ============================================================
// WIFI TASK — PATIENT RECONNECTION + ROUND-ROBIN
// ============================================================

void network_wifi_task(void *pvParameters)
{
    esp_err_t err = network_start_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed on first attempt");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_POLL_MS));

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);

        if (bits & WIFI_CONNECTED_BIT) {
            // Connected — nothing to do
            continue;
        }

        // Not connected — patient retry with backoff
        if (!backoff_should_retry()) {
            continue;
        }

        if (profile_count == 0) {
            continue;
        }

        // If all profiles exhausted, do full scan recovery
        if (s_retry_count > WIFI_MAX_RETRY_BEFORE_ROTATE * profile_count) {
            ESP_LOGW(TAG, "All profiles exhausted, full scan recovery");
            int best = full_scan_recovery();
            if (best >= 0) {
                s_retry_count = 0;
                switch_to_profile(best);
                backoff_start();
                continue;
            }
            ESP_LOGW(TAG, "Full scan also failed, waiting max backoff");
            vTaskDelay(pdMS_TO_TICKS(WIFI_BACKOFF_MAX_MS));
            continue;
        }

        esp_wifi_connect();
        backoff_start();
    }
}

// ============================================================
// GETTERS FOR SOFTAP / EXTERNAL ACCESS
// ============================================================

int network_get_rssi(void)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) return -127;

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) return -127;

    return (int)ap_info.rssi;
}

int network_get_profile_count(void)
{
    return profile_count;
}

esp_err_t network_get_profile_ssid(int idx, char *buf, size_t len)
{
    if (idx < 0 || idx >= profile_count || !buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(buf, profiles[idx].ssid, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

bool network_is_softap_active(void)
{
    return s_softap_active;
}

void network_set_softap_active(bool active)
{
    s_softap_active = active;
}

// ============================================================
// HMAC + SEND TAP (preserved from v1.0.8)
// ============================================================

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
        snprintf(hmac_hex + (i * 2), 3, "%02x", hmac[i]);
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

static esp_err_t send_one_tap(const char *uid, int seq, const char *device_id,
                               char *response_buf, size_t response_buf_size);

static esp_err_t send_batch(upload_entry_t *entries, int count)
{
    if (count == 0) return ESP_OK;
    ESP_LOGI(TAG, "Sending %d stored records one by one", count);

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    for (int i = 0; i < count; i++) {
        esp_err_t err = send_one_tap(entries[i].uid, (int)entries[i].seq,
                                      device_id, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Record %d (seq=%lu) failed, stopping batch",
                     i, (unsigned long)entries[i].seq);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_OK;
}

static esp_err_t send_one_tap(const char *uid, int seq, const char *device_id,
                               char *response_buf, size_t response_buf_size)
{
    ESP_LOGI(TAG, "---[REQUEST]--- seq=%d uid=%s device=%s", seq, uid, device_id);

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "raw_hex", uid);
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "seq", seq);
    cJSON_AddNullToObject(root, "timestamp");
    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "[REQ BODY] %s", json_body);

    char sig_data[512];
    snprintf(sig_data, sizeof(sig_data), "%d%s", seq, json_body);

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
        ESP_LOGI(TAG, "---[RESPONSE]--- HTTP %d body=%s", status,
                 response_buf && response_buf[0] ? response_buf : "(empty)");
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
}

esp_err_t network_send_tap_single(const char *uid)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "send_tap_single: WiFi not connected");
        return ESP_FAIL;
    }

    int seq = load_immediate_seq() + 1;

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    esp_err_t err = send_one_tap(uid, seq, device_id, NULL, 0);
    if (err == ESP_OK) {
        save_immediate_seq(seq);
    }
    return err;
}

// ============================================================
// UPLOAD TASK — CIRCUIT BREAKER (preserved from v1.0.8)
// ============================================================

void upload_task(void *pvParameters)
{
    upload_entry_t entries[UPLOAD_BATCH_SIZE];
    TickType_t last_upload_tick = xTaskGetTickCount();

    static uint32_t upload_backoff_ms = 30000;
    static uint32_t consecutive_failures = 0;

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if (!(bits & WIFI_CONNECTED_BIT)) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_upload_tick) < pdMS_TO_TICKS(upload_backoff_ms)) {
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
            strncpy(entries[batch_count].uid, rec.uid,
                    sizeof(entries[batch_count].uid) - 1);
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
            consecutive_failures = 0;
            upload_backoff_ms = 30000;
        } else {
            event_log_write(EVT_UPLOAD_FAILED);
            consecutive_failures++;
            upload_backoff_ms = (consecutive_failures <= 1) ? 30000 :
                                (consecutive_failures <= 3) ? 60000 :
                                (consecutive_failures <= 5) ? 120000 :
                                (consecutive_failures <= 7) ? 300000 :
                                (consecutive_failures <= 10) ? 600000 :
                                3600000;
            ESP_LOGW(TAG, "Upload failed (%lu consecutive), backoff: %lums",
                     consecutive_failures, upload_backoff_ms);
        }

        last_upload_tick = xTaskGetTickCount();
    }
}
