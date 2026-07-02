#pragma once

// ======================================================
// STERLING PROD
// ======================================================

#define FW_VERSION                     "1.0.8"

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

// Override MAC-based device_id with fixed name (comment out to auto-generate)
#define DEVICE_ID                      "Sterling-Main-Demo"

// Fallback name if MAC generation fails (used when DEVICE_ID not defined)
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

// OTA check every 60 seconds
#define OTA_CHECK_INTERVAL_MS          60000

// ======================================================
// NETWORK
// ======================================================

#define WIFI_CONNECT_TIMEOUT_MS        15000

#define HTTP_TIMEOUT_MS                8000

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

// WiFi API provisioning (replaces NTP as primary time source)
#define WIFI_API_URL                "http://api.sabarikrishnan.me/api/v1/wifi"

// Unlimited WiFi profiles (was 5)
#define WIFI_MAX_PROFILES           250

// OTA check every 60 seconds
#define OTA_CHECK_INTERVAL_MS       60000

// WiFi NVS namespace and reconnect delay
#define WIFI_NVS_NAMESPACE             "wifi_profiles"
#define WIFI_RECONNECT_DELAY_MS        5000

// Factory reset — boot partition index for factory
#define FACTORY_PARTITION_INDEX     0  // factory is always slot 0 in ESP-IDF

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