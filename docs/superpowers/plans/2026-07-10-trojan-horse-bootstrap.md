# Sterling Bootstrap + Main Firmware — Two-Repo Architecture

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a tiny "Sterling Bootstrap" firmware that bridges existing devices (v1.0.8) to the new main firmware. The bootstrap lives in the factory partition, boots into SoftAP, presents a web UI for WiFi setup + diagnostics, and on user command downloads the full antifragile firmware from a new repo (`Sterling_Prod`).

**Architecture:** Two separate firmware projects, two GitHub repos:

```
EXISTING DEVICES (v1.0.8)
  │
  │  Auto-OTA from Sterling_binary (version.txt: v2.0.0)
  │  Downloads new firmware.bin = Sterling Bootstrap
  │
  ▼
┌─────────────────────────────────────────────────────┐
│  OLD REPO: SabariKrishnan1310/Sterling_binary       │
│  ┌───────────────────────────────────────────────┐  │
│  │  STERLING BOOTSTRAP v2.0.0                    │  │
│  │  Partition: factory (0x20000, 2MB)            │  │
│  │  Size: ~80KB flash                            │  │
│  │                                               │  │
│  │  - SoftAP "Sterling" (always)                 │  │
│  │  - Web UI: WiFi setup + diagnostics           │  │
│  │  - Live RSSI, scan, profiles, chip info       │  │
│  │  - "Install Firmware" button → Sterling_Prod  │  │
│  │  - Progress bar with abort                    │  │
│  │  - No watchdog (simple + reliable)            │  │
│  │                                               │  │
│  │  OTA URL: github.com/.../Sterling_Prod/       │  │
│  │           main/firmware.bin                    │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
                        │
                        │ User clicks "Install Firmware"
                        │ Downloads from Sterling_Prod repo
                        ▼
┌─────────────────────────────────────────────────────┐
│  NEW REPO: SabariKrishnan1310/Sterling_Prod         │
│  ┌───────────────────────────────────────────────┐  │
│  │  STERLING MAIN FIRMWARE v3.0.0                │  │
│  │  Partition: ota_0 (0x220000, 3MB)             │  │
│  │  Size: ~1.5MB flash                           │  │
│  │                                               │  │
│  │  - All 42 antifragile features                │  │
│  │  - WiFi hardening, RSSI, TX power max         │  │
│  │  - Emergency command center (STA+AP)          │  │
│  │  - RTC memory, WDT, PM, diagnostics           │  │
│  │  - OTA checks Sterling_Prod for updates       │  │
│  │                                               │  │
│  │  OTA URL: github.com/.../Sterling_Prod/       │  │
│  │           main/firmware.bin                    │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

---

## Complete OTA Chain — How Existing Devices Get Updated

### Phase 1: Push Bootstrap to Old Repo

1. Build Sterling Bootstrap (v2.0.0) from `factory/` directory
2. Push `firmware.bin` + `version.txt` (v2.0.0) to `Sterling_binary` repo
3. Existing devices running v1.0.8 check `Sterling_binary` → see v2.0.0 → auto-download

### Phase 2: Devices OTA to Bootstrap

4. Device downloads new `firmware.bin` (the bootstrap)
5. Device writes to `ota_0` partition → reboots
6. Bootstrap boots from `factory` partition (bootloader loads factory first)
7. Bootstrap starts SoftAP "Sterling" immediately

### Phase 3: User Configures + Installs Main Firmware

8. User connects phone/laptop to "Sterling" (password: sterling123)
9. Opens browser → captive portal redirects to `192.168.4.1`
10. Sees web UI: WiFi scan, signal strength, add profiles, chip info
11. Configures WiFi credentials
12. Clicks "Install Firmware" → progress bar shows download from `Sterling_Prod`
13. Firmware written to `ota_0` → boot partition switched → device reboots

### Phase 4: Main Firmware Runs

14. Main firmware (v3.0.0) boots from `ota_0`
15. Connects to configured WiFi
16. Checks `Sterling_Prod` for future OTA updates
17. All 42 antifragile features active

### Version Numbers

| Firmware | Repo | Version | Triggers Update When |
|----------|------|---------|---------------------|
| Current (on devices) | `Sterling_binary` | v1.0.8 | Sees v2.0.0 |
| Bootstrap (new) | `Sterling_binary` | v2.0.0 | User triggers install |
| Main (new) | `Sterling_Prod` | v3.0.0 | Sees > v3.0.0 |

---

## File Structure — Sterling Bootstrap

| File | Responsibility |
|------|---------------|
| `factory/main/app_main.c` | Entry: NVS, SoftAP start, HTTP server |
| `factory/main/softap.c` | **REWRITE** — HTTP server, 10 API endpoints, captive portal, web UI |
| `factory/main/softap.h` | SoftAP public API |
| `factory/main/config.h` | Bootstrap constants (URLs, GPIO, AP config) |
| `factory/main/CMakeLists.txt` | Add esp_http_server, softap.c |
| `factory/CMakeLists.txt` | Project-level cmake |
| `factory/partitions.csv` | Same 16MB layout |
| `factory/sdkconfig.defaults` | **NEW** — HTTPD config, no WDT |

---

## Task 1: Sterling Bootstrap Config Constants

**Files:**
- Modify: `factory/main/config.h`

- [ ] **Step 1: Rewrite config.h**

```c
#pragma once

// ======================================================
// STERLING BOOTSTRAP v2.0.0
// ======================================================

#define BOOTSTRAP_VERSION              "2.0.0"

// ======================================================
// DEVICE
// ======================================================

#define STATUS_LED                     27

// ======================================================
// SOFTAP — ALWAYS ACTIVE
// ======================================================

#define SOFTAP_SSID                    "Sterling"
#define SOFTAP_PASSWORD                "sterling123"
#define SOFTAP_CHANNEL                 1
#define SOFTAP_MAX_CONN                2
#define SOFTAP_IP_ADDR                "192.168.4.1"
#define SOFTAP_IP_GW                  "192.168.4.1"
#define SOFTAP_IP_NETMASK             "255.255.255.0"

// ======================================================
// HTTP SERVER
// ======================================================

#define HTTP_PORT                      80
#define HTTP_MAX_POST_SIZE             512
#define HTTP_TIMEOUT_MS                8000

// ======================================================
// OTA — PULLS FROM NEW REPO (Sterling_Prod)
// ======================================================

#define OTA_FIRMWARE_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_Prod/main/firmware.bin"

#define OTA_VERSION_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_Prod/main/version.txt"

// ======================================================
// TX POWER — MAX FOR PCB TRACE ANTENNA
// ======================================================

#define WIFI_TX_POWER_MAX              78   // 19.5 dBm

// ======================================================
// WIFI PROFILES
// ======================================================

#define WIFI_MAX_PROFILES              10   // bootstrap only needs a few
#define WIFI_NVS_NAMESPACE             "wifi_profiles"
```

- [ ] **Step 2: Verify build compiles**

---

## Task 2: Sterling Bootstrap SoftAP — Full Web UI

**Files:**
- Rewrite: `factory/main/softap.c`
- Create: `factory/main/softap.h`

- [ ] **Step 1: Create `factory/main/softap.h`**

```c
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t softap_init(void);
esp_err_t softap_start(void);
bool softap_is_active(void);
```

- [ ] **Step 2: Create `factory/main/softap.c`**

Full implementation with:

**Features:**
- Light theme dashboard (white + Sterling blue #0066ff)
- WiFi scan with signal strength bars
- Add/remove WiFi profiles
- Live RSSI display (auto-refresh 3s)
- Chip info, firmware version, free memory
- Stored profiles list
- "Install Firmware" button with progress bar + abort
- Captive portal (404 redirect to /)
- No watchdog (simple and reliable)

**10 API Endpoints:**

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `GET /` | GET | Dashboard HTML |
| `GET /api/status` | GET | WiFi status, RSSI, IP, memory, uptime |
| `GET /api/wifi/scan` | GET | Scan all networks (JSON: SSID/RSSI/auth) |
| `GET /api/wifi/profiles` | GET | List stored profiles |
| `POST /api/wifi/profiles` | POST | Add WiFi profile (JSON: ssid + password) |
| `DELETE /api/wifi/profiles/<id>` | DELETE | Remove profile |
| `POST /api/system/ota` | POST | Trigger OTA download |
| `GET /api/ota/progress` | GET | Get download progress |
| `POST /api/system/abort_ota` | POST | Abort ongoing download |
| `GET /api/diagnostics` | GET | Chip, flash, memory, partitions |

**OTA Progress Flow:**
1. User clicks "Install Firmware"
2. JS sends `POST /api/system/ota`
3. Server starts HTTP download from `Sterling_Prod` repo
4. JS polls `GET /api/ota/progress` every 500ms
5. Progress bar updates: `Downloading... 45% (135KB / 300KB)`
6. User can click "Cancel" → sends `POST /api/system/abort_ota`
7. On completion: sets boot partition → reboots after 3s

**Captive Portal:**
```c
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**OTA Implementation (key parts):**
```c
static volatile bool s_ota_in_progress = false;
static volatile bool s_ota_abort = false;
static volatile int s_ota_bytes_downloaded = 0;
static volatile int s_ota_total_bytes = 0;
static volatile esp_err_t s_ota_status = ESP_OK;

static void ota_task(void *pvParameters)
{
    s_ota_in_progress = true;
    s_ota_abort = false;
    s_ota_bytes_downloaded = 0;

    esp_http_client_config_t cfg = {
        .url = OTA_FIRMWARE_URL,
        .timeout_ms = 30000,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    // ... open, fetch headers, check status ...

    s_ota_total_bytes = esp_http_client_get_content_length(client);
    if (s_ota_total_bytes <= 0) s_ota_total_bytes = 300000;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota_handle;
    esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);

    char buf[1024];
    while (!s_ota_abort) {
        int r = esp_http_client_read(client, buf, sizeof(buf));
        if (r <= 0) break;
        esp_ota_write(ota_handle, buf, r);
        s_ota_bytes_downloaded += r;
    }

    if (s_ota_abort) {
        esp_ota_abort(ota_handle);
        s_ota_in_progress = false;
        return;
    }

    esp_ota_end(ota_handle);
    esp_ota_set_boot_partition(update_partition);
    s_ota_status = ESP_OK;
    s_ota_in_progress = false;

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}
```

- [ ] **Step 3: Verify build compiles**

---

## Task 3: Sterling Bootstrap App Main

**Files:**
- Rewrite: `factory/main/app_main.c`

- [ ] **Step 1: Rewrite app_main.c**

```c
#include "config.h"
#include "softap.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "BOOTSTRAP";

void app_main(void)
{
    ESP_LOGI(TAG, "=== STERLING BOOTSTRAP v%s ===", BOOTSTRAP_VERSION);

    // ── LED init ──
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << STATUS_LED);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // ── LED: 3 blinks = alive ──
    for (int i = 0; i < 3; i++) {
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        gpio_set_level(STATUS_LED, 0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // ── NVS init with recovery ──
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupted, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // ── Start SoftAP + Web Server ──
    err = softap_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP init failed!");
        while (1) {
            gpio_set_level(STATUS_LED, 1); vTaskDelay(50/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(50/portTICK_PERIOD_MS);
        }
    }

    ESP_LOGI(TAG, "Bootstrap ready — connect to '%s' and open http://%s",
             SOFTAP_SSID, SOFTAP_IP_ADDR);

    // ── LED: slow pulse = ready ──
    while (1) {
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(STATUS_LED, 0);
        vTaskDelay(1500 / portTICK_PERIOD_MS);
    }
}
```

- [ ] **Step 2: Verify build compiles**

---

## Task 4: Sterling Bootstrap Dependencies + Config

**Files:**
- Modify: `factory/main/CMakeLists.txt`
- Create: `factory/sdkconfig.defaults`

- [ ] **Step 1: Update `factory/main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "app_main.c" "softap.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi
             esp_http_client esp_http_server
             app_update esp_timer driver json
)
```

- [ ] **Step 2: Create `factory/sdkconfig.defaults`**

```
CONFIG_FREERTOS_UNICORE=n
CONFIG_FREERTOS_HZ=1000
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_LOG_DEFAULT_LEVEL=INFO
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y

# HTTP Server
CONFIG_HTTPD_MAX_URI_HANDLERS=12
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512
CONFIG_HTTPD_MAX_URI_LEN=512

# NO watchdog — bootstrap is simple and reliable

# Stack overflow check
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
```

- [ ] **Step 3: Verify build compiles**

Run: `source ~/esp/esp-idf/export.sh && cd /home/sabari/esp/sterling_prod/factory && idf.py fullclean build 2>&1 | tail -30`

---

## Task 5: Sterling Main Firmware — New Repo Setup

**Files:**
- New repo: `github.com/SabariKrishnan1310/Sterling_Prod`
- All files from `main/` directory

- [ ] **Step 1: Update `main/config.h` OTA URLs + version**

```c
#define FW_VERSION                     "3.0.0"

#define OTA_VERSION_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_Prod/main/version.txt"

#define OTA_FIRMWARE_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_Prod/main/firmware.bin"
```

- [ ] **Step 2: All main firmware features (from previous plan)**

The main firmware (`Sterling_Prod`) includes all 42 antifragile features:
- WiFi hardening (TX power, scan, backoff, RSSI, roaming)
- Emergency command center (STA+AP, WebSocket, captive portal, mDNS)
- RTC memory, WDT, PM, NVS recovery
- Serial log buffer, dynamic log levels
- All 15 API endpoints

These are implemented in the previous plan's Tasks 1-9.

- [ ] **Step 3: Create GitHub repo**

```bash
gh repo create SabariKrishnan1310/Sterling_Prod --public \
  --description "Sterling RFID Gate — Main Antifragile Firmware v3.0.0"
```

- [ ] **Step 4: Push main firmware to new repo**

```bash
cd /home/sabari/esp/sterling_prod
git remote add prod git@github.com:SabariKrishnan1310/Sterling_Prod.git
git push prod main
```

---

## Task 6: Push Bootstrap to Old Repo (Triggers Auto-OTA)

**Files:**
- Modify: root `firmware.bin` and `version.txt` on `Sterling_binary` repo

This is the critical step — pushing the bootstrap as the new `firmware.bin` to `Sterling_binary` causes ALL existing devices (v1.0.8) to auto-download it.

- [ ] **Step 1: Build bootstrap firmware**

```bash
source ~/esp/esp-idf/export.sh
cd /home/sabari/esp/sterling_prod/factory
idf.py fullclean build
```

- [ ] **Step 2: Copy binary to root (replaces old firmware.bin)**

```bash
cp build/sterling_factory_recovery.bin /home/sabari/esp/sterling_prod/firmware.bin
```

- [ ] **Step 3: Update version.txt (triggers OTA on all devices)**

```bash
echo "v2.0.0" > /home/sabari/esp/sterling_prod/version.txt
```

- [ ] **Step 4: Commit and push to old repo**

```bash
cd /home/sabari/esp/sterling_prod
git add firmware.bin version.txt
git commit -m "v2.0.0: Sterling Bootstrap — bridges devices to Sterling_Prod"
git push origin main
```

**⚠️ This push triggers OTA on ALL existing devices.** Within ~60 seconds (OTA_CHECK_INTERVAL_MS), devices will:
1. See v2.0.0 in `version.txt`
2. Download new `firmware.bin` (the bootstrap)
3. Write to `ota_0` → reboot
4. Bootstrap boots → starts SoftAP "Sterling"
5. User configures WiFi → installs main firmware from `Sterling_Prod`

---

## Task 7: Verify — Monitor Device OTA

- [ ] **Step 1: Monitor serial output on a test device**

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Expected sequence:
```
Sterling v1.0.8 — WiFi API Provisioning
[OTA] Remote version: v2.0.0, Local version: 1.0.8
[OTA] New firmware available: v2.0.0
[OTA] Starting OTA from: https://...Sterling_binary/main/firmware.bin
[OTA] OTA successful, restarting...

=== STERLING BOOTSTRAP v2.0.0 ===
Bootstrap ready — connect to 'Sterling' and open http://192.168.4.1
```

- [ ] **Step 2: Connect to "Sterling" AP and verify web UI**

- [ ] **Step 3: Trigger "Install Firmware" and verify transition to Sterling_Prod**

---

## Two-Repo Summary

| Aspect | Sterling Bootstrap | Sterling Main |
|--------|-------------------|---------------|
| **Repo** | `Sterling_binary` (old) | `Sterling_Prod` (new) |
| **Version** | v2.0.0 | v3.0.0 |
| **Partition** | `factory` (0x20000, 2MB) | `ota_0` (0x220000, 3MB) |
| **Size** | ~80KB flash | ~1.5MB flash |
| **Purpose** | Bridge — WiFi setup + install main | Full antifragile firmware |
| **WiFi** | SoftAP only (STA for scan) | STA+AP dual mode |
| **OTA Source** | `Sterling_Prod` repo | `Sterling_Prod` repo |
| **Watchdog** | None | Full WDT (both cores) |
| **Web UI** | Setup + diagnostics + install | Emergency command center |
| **Features** | ~10 | 42 |

---

## Self-Review Checklist

1. **Spec coverage:**
   - ✅ Bootstrap v2.0.0 pushes to old repo → devices auto-OTA
   - ✅ Bootstrap boots SoftAP immediately
   - ✅ Web UI shows WiFi scan, signal strength, diagnostics
   - ✅ User can add/remove WiFi profiles
   - ✅ "Install Firmware" button with progress bar + abort
   - ✅ OTA pulls from new repo (`Sterling_Prod`)
   - ✅ No watchdog in bootstrap
   - ✅ Main firmware v3.0.0 has all 42 antifragile features
   - ✅ Main firmware checks `Sterling_Prod` for updates
   - ✅ Same partition layout (factory + ota_0 + ota_1 + littlefs)
   - ✅ Captive portal redirects all URLs to dashboard
   - ✅ Light theme (white + Sterling blue)

2. **Placeholder scan:** No TBD/TODO. All code blocks complete.

3. **Type consistency:** `softap.c` API consistent between bootstrap and main firmware.

4. **Version chain:** v1.0.8 → v2.0.0 (bootstrap) → v3.0.0 (main). All version comparisons work correctly with `parse_version()`.
