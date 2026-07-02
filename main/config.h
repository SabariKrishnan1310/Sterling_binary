#pragma once

// ======================================================
// STERLING PROD
// ======================================================

#define FW_VERSION                     "1.0.5"

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

// Override MAC-based device_id with fixed name (uncomment to override auto-generate)
// Note: device.c now generates "ESP32-{MAC}" from efuse, stored in NVS
// #define DEVICE_ID                      "EUROKIDS-GATE-MAIN"

// Fallback name if MAC generation fails (used when DEVICE_ID not defined)
#define DEVICE_PREFIX                  "GATE"

// ======================================================
// API
// ======================================================

// Fix C (H-1): Upgraded from HTTP to HTTPS
#define API_URL \
"https://api.sabarikrishnan.me/ingest/v2/tap/"

#define API_SECRET \
"8_5IOTuP5zqcl1E8KDdJL2Fv0n5JXuwjdXPOoSoohEYvrM7wAPs9_ij3GiHYJNpzBQe7Sjkoxr_iRbPhwx22iw"

// ======================================================
// OTA
// ======================================================

#define OTA_VERSION_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/version.txt"

#define OTA_FIRMWARE_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/firmware.bin"

// Fix D (H-3): Reduced check frequency from 10s to 1h
#define OTA_CHECK_INTERVAL_MS          3600000

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
// MQTT
// ======================================================

#define MQTT_NVS_NAMESPACE              "mqtt"
#define MQTT_DEFAULT_HOST               "10.0.0.1"
#define MQTT_DEFAULT_PORT               1883
#define MQTT_HEALTH_CHECK_INTERVAL_MS   10000
#define MQTT_RESTART_TIMEOUT_MS         300000
#define MQTT_TASK_STACK_SIZE            4096

// ======================================================
// HMAC — TWO SEPARATE SECRETS
// ======================================================

// API_SECRET = used for HTTP X-Sterling-Signature header (tap uploads)
// HMAC_SECRET = used for binary packet signing (telemetry, commands)

#define HMAC_HEADER                    "X-Sterling-Signature"
#define HMAC_HEX_LEN                   65

#define HMAC_SECRET_NVS_NAMESPACE       "device"
#define HMAC_SECRET_NVS_KEY             "hmac_secret"
#define HMAC_SECRET_DEFAULT             "super-secret-key-change-me"

// ======================================================
// TELEMETRY
// ======================================================

#define TELEMETRY_NVS_NAMESPACE         "telemetry_ns"
#define TELEMETRY_NVS_KEY_SEQ           "seq"
#define TELEMETRY_DEFAULT_INTERVAL_MS   60000

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

#define WATCHDOG_TIMEOUT_SECONDS       120

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

// ======================================================
// CONSOLE
// ======================================================

#define CONSOLE_NVS_NAMESPACE           "device"
#define CONSOLE_NVS_KEY_PASSWORD        "console_pw"
#define CONSOLE_DEFAULT_PASSWORD        "1310"
#define CONSOLE_MAX_FAILED_ATTEMPTS     3
#define CONSOLE_LOCKOUT_SECONDS         30
#define CONSOLE_STACK_SIZE              4096
#define CONSOLE_TASK_PRIORITY           1
