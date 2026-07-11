#pragma once

// ======================================================
// STERLING PROD — v1.0.9
// Max TX power, scan-before-connect, round-robin profiles,
// patient reconnection, SoftAP emergency command center
// ======================================================

#define FW_VERSION                     "1.0.9"

// ======================================================
// RFID PINS
// ======================================================

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
// OTA — PULLS FROM GITHUB
// ======================================================

#define OTA_VERSION_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/version.txt"

#define OTA_FIRMWARE_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/firmware.bin"

#define OTA_FALLBACK_URL \
""

#define OTA_CHECK_INTERVAL_MS          60000

// ======================================================
// NETWORK
// ======================================================

#define WIFI_CONNECT_TIMEOUT_MS        15000
#define HTTP_TIMEOUT_MS                120000   // 2 min for OTA downloads

// ======================================================
// WiFi TX POWER — PCB TRACE ANTENNA MAX (19.5 dBm)
// ESP32 WROOM-32: 78 * 0.25 dBm = 19.5 dBm
// Explicit set prevents IDF defaults (~17 dBm)
// ======================================================

#define WIFI_TX_POWER_MAX              78

// ======================================================
// WiFi RECONNECTION — PATIENT + ROUND-ROBIN
// ======================================================
#define WIFI_MAX_RETRY_BEFORE_ROTATE   3       // retries per profile before rotate
#define WIFI_BACKOFF_BASE_MS           2000    // 2s initial backoff
#define WIFI_BACKOFF_MAX_MS            120000  // 2 min max backoff
#define WIFI_RECONNECT_POLL_MS         5000    // 5s poll when disconnected
#define WIFI_SOFTAP_TRIGGER_COUNT      20      // failures before SoftAP fallback
#define WIFI_SCAN_TIMEOUT_MS           4000    // scan timeout per channel

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
#define SOFTAP_BOOT_WINDOW_MS          120000  // 2 min provisioning window on boot
#define SOFTAP_HTTP_PORT               80
#define SOFTAP_MAX_POST_SIZE           512

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
#define WIFI_RECONNECT_DELAY_MS        5000
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
// WATCHDOG
// ======================================================

#define WATCHDOG_TIMEOUT_SECONDS       30

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
