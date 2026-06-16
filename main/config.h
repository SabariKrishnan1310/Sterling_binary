#pragma once

// ======================================================
// STERLING PROD
// ======================================================

#define FW_VERSION                     "1.0.3"

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

// Fallback name if MAC generation fails
#define DEVICE_PREFIX                  "GATE"

// ======================================================
// API
// ======================================================

#define API_URL \
"http://api.sabarikrishnan.me/ingest/v2/tap/"

#define API_SECRET \
"I9tw7cybkxY7-UQ3MrJOHJ11x_Sl2e9TRDmbIE43Jllnxsd-J0RPNkHvjeqyFPhzIMmT1k0IC7Pbtr0RsdPPzg"

// ======================================================
// OTA
// ======================================================

#define OTA_VERSION_URL \
"https://github.com/SabariKrishnan1310/Sterling_binary/releases/latest/download/version.txt"

#define OTA_FIRMWARE_URL \
"https://github.com/SabariKrishnan1310/Sterling_binary/releases/latest/download/firmware.bin"

#define OTA_CHECK_INTERVAL_MS          10000

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

#define WIFI_MAX_PROFILES              5
#define WIFI_NVS_NAMESPACE             "wifi_profiles"
#define WIFI_RECONNECT_DELAY_MS        5000

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