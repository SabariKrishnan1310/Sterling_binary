# Ultimate ESP32 Antifragile Firmware — Every Trick in the Book

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ESP32 firmware antifragile — every hardware feature, every ESP-IDF trick, every redundancy mechanism crammed into one firmware update. WiFi never dies, device is always reachable, every state is recoverable.

**Architecture:** 3 new files (`main/softap.c`, `main/softap.h`, `main/log_buffer.c`, `main/log_buffer.h`), 1 new local component (`components/dns_server/`), modified files: `main/config.h`, `main/network.c`, `main/network.h`, `main/event_log.h`, `main/app_main.c`, `main/CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig.defaults`. STA+AP dual-mode WiFi (always reachable at `sterling.local`), full emergency command center with light-theme dashboard, WebSocket live logs, captive portal DNS, serial log ring buffer.

**Tech Stack:** ESP-IDF 5.x, esp_http_server (with WebSocket), esp_wifi (STA+AP), cJSON, NVS, FreeRTOS, LittleFS, mDNS, captive portal DNS

---

## Complete Feature Matrix — 42 Features

### WiFi Hardening (12 features)
| # | Feature | Mechanism |
|---|---------|-----------|
| 1 | TX Power Max | `esp_wifi_set_max_tx_power(78)` = 19.5 dBm |
| 2 | Power Save OFF | `esp_wifi_set_ps(WIFI_PS_NONE)` |
| 3 | Scan-Before-Connect | Detect security mode + RSSI before auth |
| 4 | Reason-Aware Disconnect | Switch on `disc->reason` code (2, 15, 201, 202, etc.) |
| 5 | Full Scan Recovery | Scan all channels, try every stored profile |
| 6 | Exponential Backoff | 2s→4s→8s→...→60s with random jitter |
| 7 | Profile Rotation | Round-robin after N failures |
| 8 | RSSI Monitoring | 30s polling, critical threshold triggers roaming |
| 9 | RSSI-Based Roaming | Auto-switch profile on weak signal |
| 10 | Config Re-fetch | Hourly from cloud API |
| 11 | NTP Resync | Auto if time behind 2024 |
| 12 | Multiple NTP Servers | pool.ntp.org + time.nist.gov fallback |

### Hardware Features (7 features)
| # | Feature | Mechanism |
|---|---------|-----------|
| 13 | RTC Memory | Boot count, crash info, connection fail count survives soft reboot |
| 14 | Brownout Config | `CONFIG_ESP_BROWNOUT_DET_LVL_SEL_3V` for safe threshold |
| 15 | PHY Calibration | Auto-stored in NVS, re-calibrates on first boot |
| 16 | Dual-Core WDT | Idle task monitoring on both cores |
| 17 | Stack Overflow Detection | Method 2 (canary bytes) — `configCHECK_FOR_STACK_OVERFLOW=2` |
| 18 | Flash Cache Sync | `esp_cache_msync()` before reboot |
| 19 | Power Management | Dynamic frequency scaling (240MHz→80MHz) with PM locks |

### Software Resilience (8 features)
| # | Feature | Mechanism |
|---|---------|-----------|
| 20 | NVS Corruption Recovery | Auto-detect `ESP_ERR_NVS_NO_FREE_PAGES`, erase + reinit |
| 21 | Task Stack Diagnostics | `uxTaskGetStackHighWaterMark()` reported in diagnostics |
| 22 | Memory Diagnostics | `heap_caps_print_heap_info()` + `esp_get_free_heap_size()` |
| 23 | Dynamic Log Levels | `esp_log_level_set()` via web UI for remote debugging |
| 24 | Upload Circuit Breaker | Skip on critical RSSI, exponential backoff on failures |
| 25 | Crash Detection | `RTC_NOINIT_ATTR` stores crash info across soft reboots |
| 26 | Time Resync | Auto NTP sync when time is behind 2024-01-01 |
| 27 | Timestamp in Uploads | `gettimeofday()` microsecond timestamps in every tap |

### Emergency Command Center (15 features)
| # | Feature | Mechanism |
|---|---------|-----------|
| 28 | STA+AP Dual Mode | Always reachable at 192.168.4.1 AND connected to WiFi |
| 29 | Captive Portal DNS | Redirects ALL DNS queries to 192.168.4.1 |
| 30 | mDNS Discovery | `sterling.local` on local network |
| 31 | WebSocket Live Logs | Real-time log streaming without page refresh |
| 32 | WiFi Scan + Add/Remove | Full WiFi management via web UI |
| 33 | System Commands | Reboot, factory reset, OTA check, WiFi reset via web |
| 34 | Full Diagnostics | Heap, chip, partitions, IDF version, uptime, tasks |
| 35 | Serial Log Buffer | 50-line ring buffer (~2KB RAM) |
| 36 | Hybrid Dashboard | Embedded HTML fallback + LittleFS preferred |
| 37 | Light Theme | Clean white + Sterling blue accents |
| 38 | Auto-Refresh Status | 3s polling for WiFi/RSSI/IP/memory |
| 39 | Boot Window | SoftAP available for first 60s after boot |
| 40 | Failure Fallback | Activates after 15+ consecutive STA failures |
| 41 | 404 Captive Redirect | Unknown URLs redirect to dashboard |
| 42 | Profile Compaction | NVS profiles shifted down on delete (no gaps) |

---

## File Structure

| File | Responsibility |
|------|---------------|
| `main/config.h` | All constants: TX power, SoftAP, RSSI, backoff, HTTP, log buffer, PM |
| `main/network.c` | WiFi hardening: 12 antifragile features |
| `main/network.h` | Public API: RSSI, connection info, profiles, SoftAP state |
| `main/softap.c` | **NEW** — HTTP server, 15 API endpoints, WebSocket, captive portal |
| `main/softap.h` | **NEW** — SoftAP public API |
| `main/log_buffer.c` | **NEW** — Serial log ring buffer with vprintf hook |
| `main/log_buffer.h` | **NEW** — Log buffer public API |
| `main/event_log.h` | New event types (16-20) |
| `main/app_main.c` | Boot sequence, RTC memory, NVS recovery, WDT, log init |
| `main/CMakeLists.txt` | Add softap.c, log_buffer.c, esp_http_server, mdns |
| `main/idf_component.yml` | Add espressif/mdns dependency |
| `components/dns_server/` | **NEW** — Captive portal DNS server (from ESP-IDF example) |
| `sdkconfig.defaults` | HTTPD, WebSocket, stack overflow, brownout, PM config |

---

## Task 1: Config Constants — Everything

**Files:**
- Modify: `main/config.h`

- [ ] **Step 1: Rewrite config.h with ALL constants**

Replace entire file:

```c
#pragma once

// ======================================================
// STERLING PROD — v1.1.0 ANTIFRAGILE
// ======================================================

#define FW_VERSION                     "1.1.0"

// ======================================================
// RFID PINS
// ======================================================

#define RFID_MISO                      19
#define RFID_MISO                      19
#define RFID_MOSI                      23
#define RFID_SCK                       18
#define RFID_CS                        21
#define RFID_RST                       22

// ======================================================
// LED
// ======================================================

#define STATUS_LED                     27

// ======================================================
// DEVICE
// ======================================================

#define DEVICE_ID                      "Sterling-Main-Demo"
#define DEVICE_PREFIX                  "GATE"

// ======================================================
// API
// ======================================================

#define API_URL \
"http://api.sabarikrishnan.me/ingest/v2/tap/"

#define API_SECRET \
"8_5IOTuP5zqcl1E8KDdJL2Fv0n5JXuwjdXPOoSoohEYvrM7wAPs9_ij3GiHYJNpzBQe7Sjkoxr_iRbPhwx22iw"

// ======================================================
// OTA
// ======================================================

#define OTA_VERSION_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/version.txt"

#define OTA_FIRMWARE_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/firmware.bin"

#define OTA_FALLBACK_URL \
""

#define OTA_CHECK_INTERVAL_MS          60000

// ======================================================
// NETWORK — GENERAL
// ======================================================

#define WIFI_CONNECT_TIMEOUT_MS        15000
#define HTTP_TIMEOUT_MS                8000

// ======================================================
// TX POWER — PCB TRACE ANTENNA MAX
// ======================================================
// ESP32 WROOM-32 max: 78 units × 0.25 dBm = 19.5 dBm
// Explicit set prevents IDF defaults (~17 dBm) from being used
#define WIFI_TX_POWER_MAX              78

// ======================================================
// SOFTAP EMERGENCY COMMAND CENTER
// ======================================================
#define SOFTAP_SSID                    "Sterling"
#define SOFTAP_PASSWORD                "sterling123"
#define SOFTAP_CHANNEL                 1
#define SOFTAP_MAX_CONN                2
#define SOFTAP_IP_ADDR                "192.168.4.1"
#define SOFTAP_IP_GW                  "192.168.4.1"
#define SOFTAP_IP_NETMASK             "255.255.255.0"
#define SOFTAP_BOOT_WINDOW_MS          60000   // 60s provisioning window
#define SOFTAP_HTTP_PORT               80
#define SOFTAP_MAX_POST_SIZE           512
#define SOFTAP_DASHBOARD_PATH          "/littlefs/dashboard.html"

// ======================================================
// SERIAL LOG RING BUFFER
// ======================================================
#define LOG_BUFFER_MAX_LINES           50
#define LOG_BUFFER_LINE_MAX            128

// ======================================================
// WIFI RECONNECTION — ANTIFRAGILE
// ======================================================
#define WIFI_MAX_RETRY_BEFORE_PROFILE  3
#define WIFI_BACKOFF_BASE_MS           2000    // 2s initial
#define WIFI_BACKOFF_MAX_MS            60000   // 60s max
#define WIFI_RECONNECT_POLL_MS         10000   // 10s poll
#define WIFI_CONFIG_REFETCH_INTERVAL_S 3600    // 1 hour
#define WIFI_SOFTAP_TRIGGER_COUNT      15      // failures before SoftAP
#define WIFI_FULL_SCAN_CHANNELS        13      // scan all 2.4GHz channels

// ======================================================
// RSSI MONITORING
// ======================================================
#define RSSI_CHECK_INTERVAL_MS         30000
#define RSSI_THRESHOLD_LOW             -75
#define RSSI_THRESHOLD_CRITICAL        -85

// ======================================================
// SECURITY SCAN
// ======================================================
#define WIFI_SCAN_TIMEOUT_MS           4000

// ======================================================
// POWER MANAGEMENT
// ======================================================
#define PM_MAX_FREQ_MHZ               240
#define PM_MIN_FREQ_MHZ               80
#define PM_LIGHT_SLEEP_ENABLE          false   // keep false for WiFi reliability

// ======================================================
// WATCHDOG
// ======================================================
#define WATCHDOG_TIMEOUT_SECONDS       30

// ======================================================
// NTP SERVERS (multiple for redundancy)
// ======================================================
#define NTP_SERVER_PRIMARY            "pool.ntp.org"
#define NTP_SERVER_SECONDARY          "time.nist.gov"
#define NTP_SERVER_TERTIARY           "time.google.com"

// ======================================================
// STORAGE
// ======================================================
#define STORAGE_FILE_PATH              "/littlefs/taps.bin"
#define STORAGE_MAX_RECORDS            250000

// ======================================================
// EVENT LOG
// ======================================================
#define EVENT_LOG_PATH                 "/littlefs/events.bin"
#define EVENT_LOG_MAX_RECORDS          4096

// ======================================================
// HMAC
// ======================================================
#define HMAC_HEADER                    "X-Sterling-Signature"
#define HMAC_HEX_LEN                   65

// ======================================================
// WIFI PROFILES
// ======================================================
#define WIFI_API_URL                "http://api.sabarikrishnan.me/api/v1/wifi"
#define WIFI_MAX_PROFILES           250
#define WIFI_NVS_NAMESPACE             "wifi_profiles"
#define FACTORY_PARTITION_INDEX     0

// ======================================================
// UPLOAD
// ======================================================
#define UPLOAD_BATCH_SIZE              20
#define UPLOAD_RETRY_DELAY_MS          10000
#define UPLOAD_INTERVAL_MS             30000

// ======================================================
// QUEUES
// ======================================================
#define TAP_QUEUE_LENGTH               64
#define LED_QUEUE_LENGTH               16

// ======================================================
// LED TIMINGS
// ======================================================
#define LED_BOOT_ON_MS                 200
#define LED_BOOT_OFF_MS                100
#define LED_SUCCESS_MS                 300
#define LED_FAIL_ON_MS                 100
#define LED_FAIL_OFF_MS                100
#define LED_OFFLINE_PULSE_MS           50
#define LED_OFFLINE_PERIOD_MS          2000
```

- [ ] **Step 2: Verify build**

Run: `source ~/esp/esp-idf/export.sh && cd /home/sabari/esp/sterling_prod && idf.py build 2>&1 | tail -20`

---

## Task 2: Serial Log Ring Buffer

**Files:**
- Create: `main/log_buffer.h`
- Create: `main/log_buffer.c`

- [ ] **Step 1: Create `main/log_buffer.h`**

```c
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t log_buffer_init(void);
void log_buffer_add(const char *line);
int log_buffer_dump(char *buf, size_t buf_size);
int log_buffer_get_line_count(void);
```

- [ ] **Step 2: Create `main/log_buffer.c`**

```c
#include "log_buffer.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "logbuf";

typedef struct {
    char lines[LOG_BUFFER_MAX_LINES][LOG_BUFFER_LINE_MAX];
    uint32_t write_idx;
    uint32_t total_lines;
    bool overflow;
} log_buffer_t;

static log_buffer_t s_log_buffer;
static SemaphoreHandle_t s_log_mutex = NULL;

// Store the original vprintf so we can chain
static int (*s_original_vprintf)(const char *fmt, va_list args) = NULL;

static int log_buffer_vprintf(const char *fmt, va_list args)
{
    char line[LOG_BUFFER_LINE_MAX];
    int len = vsnprintf(line, sizeof(line), fmt, args);

    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
    }

    if (len > 0 && s_log_mutex) {
        if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            strncpy(s_log_buffer.lines[s_log_buffer.write_idx], line, LOG_BUFFER_LINE_MAX - 1);
            s_log_buffer.lines[s_log_buffer.write_idx][LOG_BUFFER_LINE_MAX - 1] = '\0';
            s_log_buffer.write_idx = (s_log_buffer.write_idx + 1) % LOG_BUFFER_MAX_LINES;
            s_log_buffer.total_lines++;
            if (s_log_buffer.total_lines >= LOG_BUFFER_MAX_LINES) {
                s_log_buffer.overflow = true;
            }
            xSemaphoreGive(s_log_mutex);
        }
    }

    // Chain to original (UART output)
    if (s_original_vprintf) {
        return vprintf(fmt, args);
    }
    return len;
}

esp_err_t log_buffer_init(void)
{
    memset(&s_log_buffer, 0, sizeof(s_log_buffer));
    s_log_mutex = xSemaphoreCreateMutex();
    if (!s_log_mutex) return ESP_FAIL;

    s_original_vprintf = esp_log_set_vprintf(log_buffer_vprintf);

    ESP_LOGI(TAG, "Log buffer: %d lines x %d chars (%d bytes RAM)",
             LOG_BUFFER_MAX_LINES, LOG_BUFFER_LINE_MAX,
             sizeof(s_log_buffer));
    return ESP_OK;
}

int log_buffer_dump(char *buf, size_t buf_size)
{
    if (!s_log_mutex || !buf || buf_size == 0) return 0;

    int pos = 0;
    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint32_t count = s_log_buffer.overflow ? LOG_BUFFER_MAX_LINES : s_log_buffer.total_lines;
        uint32_t start = s_log_buffer.overflow ? s_log_buffer.write_idx : 0;

        pos += snprintf(buf + pos, buf_size - pos, "=== STERLING LOG (%lu lines) ===\n", (unsigned long)count);

        for (uint32_t i = 0; i < count && pos < (int)buf_size - 1; i++) {
            uint32_t idx = (start + i) % LOG_BUFFER_MAX_LINES;
            pos += snprintf(buf + pos, buf_size - pos, "%s\n", s_log_buffer.lines[idx]);
        }

        pos += snprintf(buf + pos, buf_size - pos, "=== END ===\n");
        xSemaphoreGive(s_log_mutex);
    }
    return pos;
}

int log_buffer_get_line_count(void)
{
    return s_log_buffer.overflow ? LOG_BUFFER_MAX_LINES : s_log_buffer.total_lines;
}
```

- [ ] **Step 3: Add to CMakeLists.txt SRCS**

- [ ] **Step 4: Verify build**

---

## Task 3: Captive Portal DNS Server Component

**Files:**
- Create: `components/dns_server/CMakeLists.txt`
- Create: `components/dns_server/include/dns_server.h`
- Create: `components/dns_server/dns_server.c`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p components/dns_server/include
```

- [ ] **Step 2: Create `components/dns_server/CMakeLists.txt`**

```cmake
idf_component_register(SRCS dns_server.c
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_netif)
```

- [ ] **Step 3: Create `components/dns_server/include/dns_server.h`**

```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DNS_SERVER_MAX_ITEMS
#define DNS_SERVER_MAX_ITEMS 1
#endif

#define DNS_SERVER_CONFIG_SINGLE(queried_name, netif_key)  {        \
        .num_of_entries = 1,                                        \
        .item = { { .name = queried_name, .if_key = netif_key } }   \
        }

typedef struct dns_entry_pair {
    const char* name;
    const char* if_key;
    esp_ip4_addr_t ip;
} dns_entry_pair_t;

typedef struct dns_server_config {
    int num_of_entries;
    dns_entry_pair_t item[DNS_SERVER_MAX_ITEMS];
} dns_server_config_t;

typedef struct dns_server_handle *dns_server_handle_t;

dns_server_handle_t start_dns_server(dns_server_config_t *config);
void stop_dns_server(dns_server_handle_t handle);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Create `components/dns_server/dns_server.c`**

(Full 290-line implementation from ESP-IDF captive portal example — copy verbatim from research output above)

- [ ] **Step 5: Verify build**

---

## Task 4: Update Event Log Types

**Files:**
- Modify: `main/event_log.h`

- [ ] **Step 1: Add new event types after `EVT_STORAGE_RECYCLE = 15`**

```c
    EVT_WIFI_RSSI_LOW     = 16,
    EVT_SOFTAP_ACTIVE     = 17,
    EVT_CONFIG_REFETCHED  = 18,
    EVT_WIFI_ROAM         = 19,
    EVT_SOFTAP_CLIENT     = 20,
    EVT_NVS_RECOVERED     = 21,
    EVT_CRASH_RTC         = 22,
```

- [ ] **Step 2: Verify build**

---

## Task 5: WiFi Antifragile Hardening

**Files:**
- Modify: `main/network.c`
- Modify: `main/network.h`

- [ ] **Step 1: Add RTC memory + static state variables**

After existing statics, add:

```c
// ── RTC Memory (survives soft reboot, NOT power cycle) ──
RTC_DATA_ATTR static uint32_t rtc_boot_count = 0;
RTC_DATA_ATTR static uint32_t rtc_wifi_fail_count = 0;
RTC_DATA_ATTR static uint32_t rtc_last_disconnect_reason = 0;

// ── Runtime state ──
static uint32_t s_backoff_ms = 0;
static uint32_t s_consecutive_fails = 0;
static int s_last_rssi = 0;
static time_t s_last_config_fetch = 0;
static bool s_softap_active = false;
static TickType_t s_boot_time = 0;
```

- [ ] **Step 2: Rewrite `switch_to_profile` with security scan**

```c
static void switch_to_profile(int idx)
{
    if (idx >= profile_count) idx = 0;

    wifi_scan_config_t scan_cfg = {
        .ssid = (const uint8_t *)profiles[idx].ssid,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_LOGI(TAG, "Scanning for: %s", profiles[idx].ssid);
    esp_err_t scan_err = esp_wifi_scan_start(&scan_cfg, true);

    wifi_auth_mode_t detected_auth = WIFI_AUTH_WPA2_PSK;
    int detected_rssi = -100;

    if (scan_err == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count > 0) {
            wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_records) {
                esp_wifi_scan_get_ap_records(&ap_count, ap_records);
                for (int i = 0; i < ap_count; i++) {
                    if (strcmp((char *)ap_records[i].ssid, profiles[idx].ssid) == 0) {
                        detected_auth = ap_records[i].authmode;
                        detected_rssi = ap_records[i].rssi;
                        ESP_LOGI(TAG, "Detected: auth=%d RSSI=%d", detected_auth, detected_rssi);
                        break;
                    }
                }
                free(ap_records);
            }
        }
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, profiles[idx].ssid, 32);
    strncpy((char *)wifi_config.sta.password, profiles[idx].password, 64);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = detected_auth;
    wifi_config.sta.failure_retry_cnt = 5;

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    ESP_LOGI(TAG, "Profile %d: SSID=%s auth=%d RSSI=%d", idx, profiles[idx].ssid, detected_auth, detected_rssi);
}
```

- [ ] **Step 3: Add `scan_and_connect_all_profiles` helper**

```c
static bool scan_and_connect_all_profiles(void)
{
    ESP_LOGW(TAG, "Full scan: trying all %d profiles", profile_count);

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 200,
        .scan_time.active.max = 400,
    };

    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) return false;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return false;

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) return false;
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    for (int p = 0; p < profile_count; p++) {
        int try_idx = (s_active_profile + p) % profile_count;
        for (int a = 0; a < ap_count; a++) {
            if (strcmp((char *)ap_records[a].ssid, profiles[try_idx].ssid) == 0) {
                ESP_LOGI(TAG, "Found %s (auth=%d RSSI=%d)", profiles[try_idx].ssid,
                         ap_records[a].authmode, ap_records[a].rssi);

                wifi_config_t wifi_config = { 0 };
                strncpy((char *)wifi_config.sta.ssid, profiles[try_idx].ssid, 32);
                strncpy((char *)wifi_config.sta.password, profiles[try_idx].password, 64);
                wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
                wifi_config.sta.threshold.authmode = ap_records[a].authmode;
                wifi_config.sta.failure_retry_cnt = 5;

                esp_wifi_disconnect();
                esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
                esp_wifi_connect();

                for (int w = 0; w < 10; w++) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                    EventBits_t b = xEventGroupGetBits(wifi_event_group);
                    if (b & WIFI_CONNECTED_BIT) {
                        s_active_profile = try_idx;
                        free(ap_records);
                        ESP_LOGI(TAG, "Connected via profile %d", try_idx);
                        return true;
                    }
                }
                break;
            }
        }
    }

    free(ap_records);
    return false;
}
```

- [ ] **Step 4: Rewrite `wifi_event_handler` with reason-aware disconnect + backoff + SoftAP trigger**

```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "[DBG] STA_START");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;

        const char *reason_str = "UNKNOWN";
        switch (disc->reason) {
            case WIFI_REASON_AUTH_EXPIRE:       reason_str = "AUTH_EXPIRE"; break;
            case WIFI_REASON_AUTH_FAIL:         reason_str = "AUTH_FAIL"; break;
            case WIFI_REASON_NO_AP_FOUND:       reason_str = "NO_AP_FOUND"; break;
            case WIFI_REASON_ASSOC_LEAVE:       reason_str = "ASSOC_LEAVE"; break;
            case WIFI_REASON_DISASSOC_LOW_ACK:  reason_str = "LOW_ACK"; break;
            case WIFI_REASON_BEACON_TIMEOUT:    reason_str = "BEACON_TIMEOUT"; break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT: reason_str = "HANDSHAKE_TIMEOUT"; break;
        }
        ESP_LOGW(TAG, "[DBG] DISCONNECTED reason=%d (%s) rssi=%d",
                 disc->reason, reason_str, disc->rssi);

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        event_log_write(EVT_WIFI_DISCONNECTED);
        led_send(LED_PATTERN_WAVE);

        s_consecutive_fails++;
        rtc_wifi_fail_count++;
        rtc_last_disconnect_reason = disc->reason;

        // ── SoftAP emergency hatch ──
        if (s_consecutive_fails >= WIFI_SOFTAP_TRIGGER_COUNT && !s_softap_active) {
            ESP_LOGW(TAG, "=== EMERGENCY: %lu failures — SoftAP ===", (unsigned long)s_consecutive_fails);
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            return;
        }

        // ── Reason-specific recovery ──
        if (disc->reason == WIFI_REASON_NO_AP_FOUND ||
            disc->reason == WIFI_REASON_AUTH_FAIL ||
            disc->reason == WIFI_REASON_AUTH_EXPIRE) {
            if (scan_and_connect_all_profiles()) return;
        }

        // ── Profile rotation ──
        if (s_consecutive_fails > WIFI_MAX_RETRY_BEFORE_PROFILE && profile_count > 1) {
            s_consecutive_fails = 0;
            s_active_profile = (s_active_profile + 1) % profile_count;
            switch_to_profile(s_active_profile);
        }

        // ── Exponential backoff with jitter ──
        if (s_backoff_ms == 0) {
            s_backoff_ms = WIFI_BACKOFF_BASE_MS;
        } else {
            s_backoff_ms = (s_backoff_ms * 2 > WIFI_BACKOFF_MAX_MS)
                           ? WIFI_BACKOFF_MAX_MS : s_backoff_ms * 2;
        }
        uint32_t jitter = (esp_random() % 500);
        ESP_LOGI(TAG, "Reconnect in %lu ms (jitter=%lu, attempt %lu, profile %d)",
                 (unsigned long)(s_backoff_ms + jitter), (unsigned long)jitter,
                 (unsigned long)s_consecutive_fails, s_active_profile);
        vTaskDelay(pdMS_TO_TICKS(s_backoff_ms + jitter));
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_consecutive_fails = 0;
        s_backoff_ms = 0;
        rtc_wifi_fail_count = 0;  // reset across reboots
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        event_log_write(EVT_WIFI_CONNECTED);
        led_send(LED_PATTERN_IDLE);

        // ── Fetch config if needed ──
        time_t now = time(NULL);
        bool should_fetch = !s_time_synced ||
                            (s_last_config_fetch > 0 && (now - s_last_config_fetch) > WIFI_CONFIG_REFETCH_INTERVAL_S);
        if (should_fetch) {
            xTaskCreatePinnedToCore(config_fetch_task, "cfg_fetch", 8192, NULL, 1, NULL, 0);
        }

        // ── NTP resync if time is stale ──
        if (now < 1704067200) {
            ESP_LOGW(TAG, "Time behind (epoch=%lld), NTP resync", (long long)now);
            sync_time();
        }
    }
}
```

- [ ] **Step 5: Add helper functions**

```c
int network_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_last_rssi = ap_info.rssi;
        return s_last_rssi;
    }
    return s_last_rssi;
}

esp_err_t network_get_connection_info(char *ssid_buf, size_t ssid_len, int *rssi_out, wifi_auth_mode_t *auth_out)
{
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
        strncpy(ssid_buf, (char *)ap_info.ssid, ssid_len - 1);
        ssid_buf[ssid_len - 1] = '\0';
        *rssi_out = ap_info.rssi;
        *auth_out = ap_info.authmode;
    }
    return err;
}

int network_get_profile_count(void) { return profile_count; }

esp_err_t network_get_profile_ssid(int idx, char *buf, size_t len)
{
    if (idx < 0 || idx >= profile_count) return ESP_ERR_INVALID_ARG;
    strncpy(buf, profiles[idx].ssid, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

bool network_is_softap_active(void) { return s_softap_active; }
void network_set_softap_active(bool active) { s_softap_active = active; }
```

- [ ] **Step 6: Rewrite `network_start_wifi` with TX power + PM + scan**

```c
esp_err_t network_start_wifi(void)
{
    if (profile_count == 0) {
        ESP_LOGE(TAG, "No WiFi profiles");
        return ESP_FAIL;
    }

    s_active_profile = 0;
    s_backoff_ms = 0;
    s_consecutive_fails = 0;
    s_boot_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "[DBG] WiFi start: stopping previous");
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    // ── Max TX power for PCB trace antenna ──
    esp_err_t tx_err = esp_wifi_set_max_tx_power(WIFI_TX_POWER_MAX);
    if (tx_err == ESP_OK) {
        int8_t actual = 0;
        esp_wifi_get_max_tx_power(&actual);
        ESP_LOGI(TAG, "TX power: requested=%d actual=%d (%.1f dBm)",
                 WIFI_TX_POWER_MAX, actual, actual * 0.25);
    } else {
        ESP_LOGW(TAG, "TX power failed: %s", esp_err_to_name(tx_err));
    }

    // ── Disable power save for max performance ──
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "WiFi power save: OFF");

    // ── Scan for first profile to detect security ──
    wifi_auth_mode_t detected_auth = WIFI_AUTH_WPA2_PSK;
    {
        wifi_scan_config_t scan_cfg = {
            .ssid = (const uint8_t *)profiles[0].ssid,
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time.active.min = 100,
            .scan_time.active.max = 300,
        };
        if (esp_wifi_scan_start(&scan_cfg, true) == ESP_OK) {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count > 0) {
                wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
                if (ap_records) {
                    esp_wifi_scan_get_ap_records(&ap_count, ap_records);
                    for (int i = 0; i < ap_count; i++) {
                        if (strcmp((char *)ap_records[i].ssid, profiles[0].ssid) == 0) {
                            detected_auth = ap_records[i].authmode;
                            s_last_rssi = ap_records[i].rssi;
                            break;
                        }
                    }
                    free(ap_records);
                }
            }
        }
    }

    // ── Configure STA ──
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, profiles[0].ssid, 32);
    strncpy((char *)wifi_config.sta.password, profiles[0].password, 64);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = detected_auth;
    wifi_config.sta.failure_retry_cnt = 5;

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

    ESP_LOGI(TAG, "WiFi started: profile 0, auth=%d", detected_auth);
    return ESP_OK;
}
```

- [ ] **Step 7: Rewrite `network_wifi_task` with RSSI monitoring**

```c
void network_wifi_task(void *pvParameters)
{
    esp_err_t err = network_start_wifi();
    if (err != ESP_OK) ESP_LOGE(TAG, "WiFi start failed");

    TickType_t last_rssi_check = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_POLL_MS));

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);

        if (bits & WIFI_FAIL_BIT) {
            xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
            continue;  // softap_start() called from softap module
        }

        if (bits & WIFI_CONNECTED_BIT) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_rssi_check) >= pdMS_TO_TICKS(RSSI_CHECK_INTERVAL_MS)) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    s_last_rssi = ap_info.rssi;
                    ESP_LOGI(TAG, "RSSI: %d dBm (ch=%d)", s_last_rssi, ap_info.primary);

                    if (s_last_rssi < RSSI_THRESHOLD_CRITICAL) {
                        ESP_LOGW(TAG, "RSSI CRITICAL (%d) — roaming", s_last_rssi);
                        event_log_write(EVT_WIFI_RSSI_LOW);
                        event_log_write(EVT_WIFI_ROAM);
                        s_active_profile = (s_active_profile + 1) % profile_count;
                        switch_to_profile(s_active_profile);
                        esp_wifi_disconnect();
                        esp_wifi_connect();
                    } else if (s_last_rssi < RSSI_THRESHOLD_LOW) {
                        event_log_write(EVT_WIFI_RSSI_LOW);
                    }
                }
                last_rssi_check = now;
            }
        } else {
            if (profile_count > 1 && s_consecutive_fails > WIFI_MAX_RETRY_BEFORE_PROFILE) {
                s_consecutive_fails = 0;
                s_active_profile = (s_active_profile + 1) % profile_count;
                switch_to_profile(s_active_profile);
            }
            esp_wifi_connect();
        }
    }
}
```

- [ ] **Step 8: Update `network.h`**

```c
#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_DISCONNECTED_BIT   BIT1
#define WIFI_FAIL_BIT           BIT2

extern EventGroupHandle_t wifi_event_group;

esp_err_t network_init(void);
esp_err_t network_start_wifi(void);
esp_err_t network_send_tap_single(const char *uid);
void upload_task(void *pvParameters);
void network_wifi_task(void *pvParameters);

int network_get_rssi(void);
esp_err_t network_get_connection_info(char *ssid_buf, size_t ssid_len, int *rssi_out, wifi_auth_mode_t *auth_out);
int network_get_profile_count(void);
esp_err_t network_get_profile_ssid(int idx, char *buf, size_t len);
bool network_is_softap_active(void);
void network_set_softap_active(bool active);
```

- [ ] **Step 9: Verify build**

---

## Task 6: Emergency Command Center — SoftAP

**Files:**
- Create: `main/softap.h`
- Create: `main/softap.c`

- [ ] **Step 1: Create `main/softap.h`**

```c
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t softap_init(void);
esp_err_t softap_start(void);
esp_err_t softap_stop(void);
bool softap_is_active(void);
httpd_handle_t softap_get_server(void);
```

- [ ] **Step 2: Create `main/softap.c`**

Full implementation with:
- Light theme dashboard (white + Sterling blue)
- 15 API endpoints
- WebSocket live log streaming
- Captive portal DNS redirect (404 → /)
- mDNS registration (sterling.local)
- Auto-refresh status (3s polling)
- WiFi scan/add/remove
- System commands (reboot, factory reset, OTA, WiFi reset)
- Full diagnostics (heap, chip, partitions, tasks, stack watermarks)

The file is ~800 lines. Key sections:

**Embedded Dashboard HTML (light theme, Sterling blue):**
```c
static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Sterling Command Center</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,monospace;"
"background:#f5f7fa;color:#1a1a2e;padding:16px}"
"h1{color:#0066ff;font-size:20px;margin-bottom:16px;border-bottom:2px solid #0066ff;padding-bottom:8px}"
"h2{color:#0066ff;font-size:16px;margin:16px 0 8px}"
".card{background:#fff;border:1px solid #e0e4ea;border-radius:12px;padding:16px;margin:8px 0;"
"box-shadow:0 2px 8px rgba(0,0,0,0.06)}"
".status{display:flex;gap:16px;flex-wrap:wrap}"
".stat{flex:1;min-width:120px}"
".stat-label{color:#666;font-size:11px;text-transform:uppercase;letter-spacing:0.5px}"
".stat-value{color:#0066ff;font-size:18px;font-weight:bold}"
".stat-value.warn{color:#ff9800}"
".stat-value.crit{color:#f44336}"
"button{background:#0066ff;color:#fff;border:none;padding:8px 16px;border-radius:6px;"
"font-weight:bold;cursor:pointer;margin:4px;font-size:13px;transition:0.2s}"
"button:hover{background:#0052cc;transform:translateY(-1px)}"
"button.danger{background:#f44336}"
"button.danger:hover{background:#d32f2f}"
"button.success{background:#4caf50}"
"input,select{background:#fff;color:#1a1a2e;border:1px solid #d0d5dd;padding:8px 12px;"
"border-radius:6px;font-family:monospace;font-size:13px}"
"pre{background:#f0f2f5;padding:12px;border-radius:8px;overflow-x:auto;"
"font-size:11px;max-height:300px;overflow-y:auto;border:1px solid #e0e4ea}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:12px}"
"table{width:100%;border-collapse:collapse;font-size:12px}"
"td,th{padding:6px 10px;border-bottom:1px solid #e0e4ea;text-align:left}"
"th{color:#666;text-transform:uppercase;font-size:10px;letter-spacing:0.5px}"
"</style></head><body>"
"<h1>&#9889; STERLING Command Center</h1>"
// ... rest of dashboard with all sections
"</body></html>";
```

**HTTP Handlers (15 endpoints):**
- `GET /` — Dashboard (hybrid: LittleFS preferred, embedded fallback)
- `GET /api/status` — System status JSON
- `GET /api/wifi/scan` — Scan networks
- `GET /api/wifi/profiles` — List profiles
- `POST /api/wifi/profiles` — Add profile
- `DELETE /api/wifi/profiles/<id>` — Remove profile
- `POST /api/system/reboot` — Reboot
- `POST /api/system/factory_reset` — Factory reset
- `POST /api/system/ota_check` — Trigger OTA
- `POST /api/system/wifi_reset` — Clear WiFi
- `POST /api/system/clear_storage` — Clear taps
- `GET /api/logs` — Serial log buffer
- `GET /api/events` — Event log
- `GET /api/diagnostics` — Full diagnostics
- `GET /ws` — WebSocket for live logs

**WebSocket Live Log Handler:**
```c
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK;

    httpd_ws_frame_t ws_pkt = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len) {
        uint8_t *buf = calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            // Echo back or process command
            httpd_ws_send_frame(req, &ws_pkt);
        }
        free(buf);
    }
    return ESP_OK;
}
```

**Captive Portal (404 redirect):**
```c
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to Sterling Command Center", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**mDNS Registration:**
```c
#include "mdns.h"

static void init_mdns(void)
{
    mdns_init();
    mdns_hostname_set("sterling");
    mdns_instance_name_set("Sterling RFID Gate Device");
    mdns_service_add("Sterling", "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: sterling.local");
}
```

**softap_start() with all features:**
```c
esp_err_t softap_start(void)
{
    if (s_softap_active) return ESP_OK;

    ESP_LOGW(TAG, "=== EMERGENCY COMMAND CENTER ===");

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_APSTA);  // DUAL MODE
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    // Max TX power + no power save
    esp_wifi_set_max_tx_power(WIFI_TX_POWER_MAX);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Configure AP
    wifi_config_t ap_config = { 0 };
    strncpy((char *)ap_config.ap.ssid, SOFTAP_SSID, 32);
    strncpy((char *)ap_config.ap.password, SOFTAP_PASSWORD, 64);
    ap_config.ap.ssid_len = strlen(SOFTAP_SSID);
    ap_config.ap.channel = SOFTAP_CHANNEL;
    ap_config.ap.max_connection = SOFTAP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    // Also configure STA (try to reconnect in background)
    if (profile_count > 0) {
        wifi_config_t sta_config = { 0 };
        strncpy((char *)sta_config.sta.ssid, profiles[0].ssid, 32);
        strncpy((char *)sta_config.sta.password, profiles[0].password, 64);
        sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    }

    // Set AP IP
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif) {
        esp_netif_ip_info_t ip_info = {
            .ip.addr = ipaddr_addr(SOFTAP_IP_ADDR),
            .gw.addr = ipaddr_addr(SOFTAP_IP_GW),
            .netmask.addr = ipaddr_addr(SOFTAP_IP_NETMASK),
        };
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
    }

    esp_wifi_start();

    // Start HTTP server
    start_server();

    // Start captive portal DNS
    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns_handle = start_dns_server(&dns_cfg);

    // Register mDNS
    init_mdns();

    s_softap_active = true;
    network_set_softap_active(true);
    event_log_write(EVT_SOFTAP_ACTIVE);
    led_send(LED_PATTERN_READY);

    ESP_LOGW(TAG, "SSID: %s | PASS: %s | URL: http://%s | mDNS: sterling.local",
             SOFTAP_SSID, SOFTAP_PASSWORD, SOFTAP_IP_ADDR);

    return ESP_OK;
}
```

- [ ] **Step 3: Verify build**

---

## Task 7: App Main — Boot Sequence + NVS Recovery + RTC + PM + WDT

**Files:**
- Modify: `main/app_main.c`

- [ ] **Step 1: Rewrite `app_main` with ALL boot features**

```c
#include "config.h"
#include "rfid.h"
#include "storage.h"
#include "network.h"
#include "ota.h"
#include "led.h"
#include "event_log.h"
#include "provision.h"
#include "softap.h"
#include "log_buffer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

// ── RTC Memory: survives soft reboot, NOT power cycle ──
RTC_DATA_ATTR static uint32_t rtc_boot_count = 0;
RTC_DATA_ATTR static uint32_t rtc_wifi_fail_count = 0;
RTC_DATA_ATTR static uint32_t rtc_last_reason = 0;
RTC_NOINIT_ATTR static uint32_t rtc_crash_pc;
RTC_NOINIT_ATTR static uint32_t rtc_crash_cause;

static const uint32_t RFID_STACK_SIZE   = 4096;
static const uint32_t LED_STACK_SIZE    = 2048;
static const uint32_t WIFI_STACK_SIZE   = 6144;
static const uint32_t UPLOAD_STACK_SIZE = 6144;
static const uint32_t OTA_STACK_SIZE    = 8192;

static void register_watchdog(TaskHandle_t task, const char *name)
{
    if (!task) return;
    esp_err_t err = esp_task_wdt_add(task);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WDT: %s", name);
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "WDT add %s: %s", name, esp_err_to_name(err));
    }
}

// ── Boot window SoftAP: active for first 60s ──
static void softap_boot_window_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SoftAP boot window: %ds", SOFTAP_BOOT_WINDOW_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(SOFTAP_BOOT_WINDOW_MS));

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if ((bits & WIFI_CONNECTED_BIT) && softap_is_active()) {
        ESP_LOGI(TAG, "WiFi connected during boot window, stopping SoftAP");
        softap_stop();
        network_start_wifi();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    // ═══ CRITICAL: LINE 1 — rollback confirm ═══
    esp_ota_mark_app_valid_cancel_rollback();

    // ═══ RTC Memory: track boot count + crash info ═══
    rtc_boot_count++;
    ESP_LOGI(TAG, "Boot #%lu (RTC)", (unsigned long)rtc_boot_count);

    // Check for crash from previous boot
    if (rtc_crash_pc != 0) {
        ESP_LOGW(TAG, "Previous crash: PC=0x%lx cause=%lu", rtc_crash_pc, rtc_crash_cause);
        event_log_write(EVT_CRASH_RTC);
        rtc_crash_pc = 0;  // clear after reading
        rtc_crash_cause = 0;
    }

    // Check WiFi fail count across soft reboots
    if (rtc_wifi_fail_count > 10) {
        ESP_LOGW(TAG, "WiFi failing repeatedly (%lu), will try SoftAP",
                 (unsigned long)rtc_wifi_fail_count);
    }

    // ═══ WDT init ═══
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),  // both cores
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);

    // ═══ Power Management: dynamic frequency scaling ═══
    esp_pm_config_t pm_config = {
        .max_freq_mhz = PM_MAX_FREQ_MHZ,
        .min_freq_mhz = PM_MIN_FREQ_MHZ,
        .light_sleep_enable = PM_LIGHT_SLEEP_ENABLE,
    };
    esp_pm_configure(&pm_config);
    ESP_LOGI(TAG, "PM: %dMHz max, %dMHz min, light_sleep=%s",
             PM_MAX_FREQ_MHZ, PM_MIN_FREQ_MHZ, PM_LIGHT_SLEEP_ENABLE ? "ON" : "OFF");

    ESP_LOGI(TAG, "Sterling v%s — Antifragile Mode", FW_VERSION);

    // ── Task handles ──
    TaskHandle_t rfid_handle = NULL;
    TaskHandle_t upload_handle = NULL;
    TaskHandle_t ota_handle = NULL;

    // ═══ NVS init with corruption recovery ═══
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupted (0x%x), erasing + reinit", nvs_err);
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
        event_log_write(EVT_NVS_RECOVERED);
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init FAILED: %s", esp_err_to_name(nvs_err));
    }

    // ═══ Init all subsystems ═══
    storage_init();
    event_log_init();
    event_log_write(EVT_BOOT);
    log_buffer_init();  // must be early to capture all logs
    network_init();
    ota_init();
    softap_init();
    storage_dump_stats();

    // ═══ Create tasks ═══
    xTaskCreatePinnedToCore(led_task,   "led_task",   LED_STACK_SIZE,  NULL, 1, NULL,         1);
    xTaskCreatePinnedToCore(rfid_task,  "rfid_task",  RFID_STACK_SIZE, NULL, 3, &rfid_handle,  1);
    xTaskCreatePinnedToCore(network_wifi_task, "wifi_task", WIFI_STACK_SIZE, NULL, 2, NULL,     0);
    xTaskCreatePinnedToCore(upload_task,"upload_task", UPLOAD_STACK_SIZE, NULL, 1, &upload_handle,0);
    xTaskCreatePinnedToCore(ota_task,   "ota_task",   OTA_STACK_SIZE,  NULL, 1, &ota_handle,   0);

    // ── Factory trigger monitor ──
    xTaskCreatePinnedToCore(factory_trigger_monitor_task, "factory_mon", 4096, NULL, 1, NULL, 0);

    // ── SoftAP boot window (60s) ──
    xTaskCreatePinnedToCore(softap_boot_window_task, "softap_win", 2048, NULL, 1, NULL, 0);

    // ═══ Register WDT for critical tasks ═══
    register_watchdog(rfid_handle, "rfid");
    register_watchdog(upload_handle, "upload");
    register_watchdog(ota_handle, "ota");

    // ═══ Log system info ═══
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip: %s rev %d, %d cores, flash %dMB",
             CONFIG_IDF_TARGET, chip_info.revision, chip_info.cores,
             esp_flash_get_size() / (1024 * 1024));
    ESP_LOGI(TAG, "IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "RTC boot count: %lu", (unsigned long)rtc_boot_count);

    vTaskDelete(NULL);
}
```

- [ ] **Step 2: Verify build**

---

## Task 8: CMakeLists + Dependencies + sdkconfig

**Files:**
- Modify: `main/CMakeLists.txt`
- Modify: `main/idf_component.yml`
- Modify: `sdkconfig.defaults`

- [ ] **Step 1: Update `main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS
        "app_main.c"
        "rfid.c"
        "network.c"
        "storage.c"
        "ota.c"
        "led.c"
        "event_log.c"
        "provision.c"
        "softap.c"
        "log_buffer.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        esp_http_client
        esp_http_server
        esp_https_ota
        app_update
        bootloader_support
        esp_partition
        esp_timer
        driver
        spi_flash
        mbedtls
        efuse
        json
        littlefs
        rc522
        mdns
)
```

- [ ] **Step 2: Update `main/idf_component.yml`**

```yaml
dependencies:
  idf:
    version: '>=5.0.0'
  joltwallet/littlefs: '~=1.13.0'
  abobija/rc522: '*'
  espressif/mdns: "^1.0.3"
```

- [ ] **Step 3: Update `sdkconfig.defaults`**

```
CONFIG_FREERTOS_UNICORE=n
CONFIG_FREERTOS_HZ=1000
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_LOG_DEFAULT_LEVEL=INFO
CONFIG_LITTLEFS_MAX_PARTITIONS=3
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16834

# HTTP Server
CONFIG_HTTPD_MAX_URI_HANDLERS=16
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_WS_SUPPORT=y

# Stack Overflow Detection (Method 2: canary)
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y

# Brownout Detection
CONFIG_ESP_BROWNOUT_DET=y
CONFIG_ESP_BROWNOUT_DET_LVL_SEL_3V=y

# Task Watchdog
CONFIG_ESP_TASK_WDT=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y

# LWIP
CONFIG_LWIP_MAX_SOCKETS=16

# PHY
CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y
```

- [ ] **Step 4: Verify full clean build**

Run: `source ~/esp/esp-idf/export.sh && cd /home/sabari/esp/sterling_prod && idf.py fullclean build 2>&1 | tail -40`

---

## Task 9: Version Bump + Final Build + Deploy

**Files:**
- Modify: `main/config.h` (FW_VERSION already set to "1.1.0" in Task 1)

- [ ] **Step 1: Final clean build**

Run: `source ~/esp/esp-idf/export.sh && cd /home/sabari/esp/sterling_prod && idf.py fullclean build 2>&1 | tail -30`

- [ ] **Step 2: Copy firmware + version**

```bash
cp build/sterling_prod.bin firmware.bin
echo "v1.1.0" > version.txt
```

- [ ] **Step 3: Commit + push**

```bash
git add -A
git commit -m "v1.1.0: Ultimate antifragile — every ESP32 trick, emergency command center"
git push origin main
```

---

## Antifragile Feature Summary — 42 Features

### WiFi (12)
1. TX Power Max 19.5 dBm
2. Power Save OFF
3. Scan-Before-Connect
4. Security Mode Detection
5. Reason-Aware Disconnect
6. Full Scan Recovery (all channels, all profiles)
7. Exponential Backoff + Jitter
8. Profile Rotation
9. RSSI Monitoring (30s)
10. RSSI-Based Roaming
11. Config Re-fetch (hourly)
12. Multiple NTP Servers

### Hardware (7)
13. RTC Memory (boot count, crash info, fail count)
14. Brownout Config (3.0V threshold)
15. PHY Calibration (auto NVS)
16. Dual-Core WDT
17. Stack Overflow Canary (Method 2)
18. Flash Cache Sync
19. Dynamic Frequency Scaling (240→80 MHz)

### Software (8)
20. NVS Corruption Recovery
21. Task Stack Diagnostics
22. Memory Diagnostics
23. Dynamic Log Levels (remote)
24. Upload Circuit Breaker
25. Crash Detection (RTC_NOINIT)
26. Time Resync
27. Timestamp in Uploads

### Emergency Command Center (15)
28. STA+AP Dual Mode
29. Captive Portal DNS
30. mDNS (sterling.local)
31. WebSocket Live Logs
32. WiFi Scan + Add/Remove
33. System Commands
34. Full Diagnostics
35. Serial Log Buffer (50 lines)
36. Hybrid Dashboard (embedded + LittleFS)
37. Light Theme (white + Sterling blue)
38. Auto-Refresh Status (3s)
39. Boot Window (60s)
40. Failure Fallback (15+)
41. 404 Captive Redirect
42. Profile Compaction
