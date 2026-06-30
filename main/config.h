#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

/* =========================================================
 *  VERSION
 * ========================================================= */
#define FW_VERSION_MAJOR             2
#define FW_VERSION_MINOR             0
#define FW_VERSION_PATCH             1
#define FW_VERSION_ENCODED           ((FW_VERSION_MAJOR << 16) | (FW_VERSION_MINOR << 8) | FW_VERSION_PATCH)
#define FW_VERSION_STR               "2.0.1"

/* =========================================================
 *  PROVISIONING API
 * ========================================================= */
#define PROVISIONING_URL             "https://api.sabarikrishnan.me/api/v1/provision"
#define PROVISIONING_TIMEOUT_MS      15000
#define PROVISION_RETRY_DELAY_S      10
#define PROVISION_RETRY_MAX          10
#define PROVISION_DEEP_SLEEP_S       3600

/* =========================================================
 *  WIREGUARD
 * ========================================================= */
#define WG_ENDPOINT                  "wg-sterling.sabarikrishnan.me:51820"
#define WG_SERVER_PUBKEY             "L5ZkwDFTFgtucRaDBj0+/A2exb/42wt79vQaNoql2Sk="
#define WG_KEEPALIVE_S               25
#define WG_HANDSHAKE_TIMEOUT_S       120
#define WG_RECONNECT_MAX             3

/* =========================================================
 *  MQTT
 * ========================================================= */
#define MQTT_BROKER_HOST             "10.0.0.1"
#define MQTT_BROKER_PORT             1883
#define MQTT_KEEPALIVE_S             30
#define MQTT_RECONNECT_DELAY_S       5
#define MQTT_RECONNECT_MAX_DELAY_S   300
#define MQTT_CMD_QOS                 2
#define MQTT_DEFAULT_QOS             1

/* =========================================================
 *  TELEMETRY
 * ========================================================= */
#define TELEMETRY_INTERVAL_MS        60000
#define TELEMETRY_INTERVAL_MIN_MS    10000
#define TELEMETRY_INTERVAL_MAX_MS    3600000

/* =========================================================
 *  PING
 * ========================================================= */
#define PING_INTERVAL_MS             60000

/* =========================================================
 *  HEARTBEAT
 * ========================================================= */
#define HEARTBEAT_OFFLINE_S          180

/* =========================================================
 *  TIME SYNC
 * ========================================================= */
#define TIMESYNC_INTERVAL_MS         3600000  /* 1 hour */
#define TIMESYNC_DRIFT_THRESHOLD_MS  5000
#define IST_OFFSET                   (5 * 3600 + 30 * 60)

/* =========================================================
 *  WIFI
 * ========================================================= */
#define WIFI_CONNECT_TIMEOUT_MS      15000
#define WIFI_LIST_MAX                32
#define WIFI_SCAN_INTERVAL_MS        30000
#define WIFI_RECONNECT_INTERVAL_MS   5000
#define WIFI_LIST_SYNC_INTERVAL_MS   21600000 /* 6 hours */

/* =========================================================
 *  RFID / TAP
 * ========================================================= */
#define TAP_DEBOUNCE_MS              500   /* minimum gap between same UID publishes */
#define TAP_QUEUE_LENGTH             20
#define TAP_BULK_MAX_ENTRIES         50
#define BULK_ENTRY_SIZE              58

/* =========================================================
 *  OTA
 * ========================================================= */
#define OTA_FW_URL                   "https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/firmware.bin"
#define OTA_VERSION_URL              "https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/version.txt"
#define OTA_HTTP_TIMEOUT_MS          60000

/* =========================================================
 *  LED
 * ========================================================= */
#define LED_BOOT_DELAY_MS            500
#define LED_TAG_MS                   200
#define LED_FAILURE_BLINK_MS         100
#define LED_PROVISIONING_BLINK_MS    1000
#define LED_WAVE_STEPS               40

/* =========================================================
 *  BUZZER
 * ========================================================= */
#define BUZZER_PWM_FREQ              2000
#define BUZZER_TAG_MS                100
#define BUZZER_INDICATOR_MS          2000

/* =========================================================
 *  WATCHDOG
 * ========================================================= */
#define WDT_TIMEOUT_S                10
#define WDT_FEED_INTERVAL_MS         3000

/* =========================================================
 *  TASK STACK SIZES
 * ========================================================= */
#define WIFI_MANAGER_STACK_SIZE      4096
#define WG_MANAGER_STACK_SIZE        4096
#define MQTT_MANAGER_STACK_SIZE      6144
#define RFID_STACK_SIZE              4096
#define TELEMETRY_STACK_SIZE         3072
#define LED_STACK_SIZE               2048
#define CMD_HANDLER_STACK_SIZE       3072

/* =========================================================
 *  PIN ASSIGNMENTS (from FIRMWARE.md §18)
 * ========================================================= */
/* SPI — RC522 RFID reader */
#define PIN_RFID_SCLK                GPIO_NUM_18
#define PIN_RFID_MISO                GPIO_NUM_19
#define PIN_RFID_MOSI                GPIO_NUM_23
#define PIN_RFID_CS                  GPIO_NUM_5
#define PIN_RFID_RST                 GPIO_NUM_4

/* GPIO */
#define PIN_BUZZER                   GPIO_NUM_21
#define PIN_LED_DATA                 GPIO_NUM_22      /* WS2812 strip */
#define PIN_OLED_SDA                 GPIO_NUM_23
#define PIN_OLED_SCL                 GPIO_NUM_25
#define PIN_BUTTON                   GPIO_NUM_26
#define PIN_RELAY                    GPIO_NUM_27
#define PIN_PIR                      GPIO_NUM_32
#define PIN_REED                     GPIO_NUM_33
#define PIN_LED_ONBOARD              GPIO_NUM_2
#define PIN_STATUS_LED               GPIO_NUM_13      /* active low on most devkits */
#define PIN_GPIO4                    GPIO_NUM_4
#define PIN_GPIO5                    GPIO_NUM_5

/* =========================================================
 *  NVS
 * ========================================================= */
#define NVS_STERLING_NS              "sterling"
#define NVS_WIFI_NS                  "wifi"

/* NVS keys — sterling namespace */
#define NVS_KEY_WG_PRIV_KEY          "wg_private_key"
#define NVS_KEY_WG_ADDRESS           "wg_address"
#define NVS_KEY_WG_SERVER_PUBKEY     "wg_server_pubkey"
#define NVS_KEY_WG_ENDPOINT          "wg_endpoint"
#define NVS_KEY_MQTT_USERNAME        "mqtt_username"
#define NVS_KEY_MQTT_PASSWORD        "mqtt_password"
#define NVS_KEY_DEVICE_MAC           "device_mac"
#define NVS_KEY_DEVICE_SERIAL        "device_serial"
#define NVS_KEY_HW_VERSION           "hw_version"
#define NVS_KEY_FW_VERSION           "fw_version"
#define NVS_KEY_TELEMETRY_INTERVAL   "telemetry_interval"
#define NVS_KEY_SEQUENCE_NUMBER      "sequence_number"
#define NVS_KEY_FW_CHANNEL           "firmware_channel"
#define NVS_KEY_PROVISIONED          "provisioned"
#define NVS_KEY_WIFI_LIST            "wifi_list"
#define NVS_KEY_WIFI_CURRENT_IDX     "wifi_current_index"
#define NVS_KEY_WIFI_LIST_VERSION    "wifi_list_version"
#define NVS_KEY_HMAC_SECRET          "hmac_secret"

/* =========================================================
 *  HMAC
 * ========================================================= */
#define HMAC_LEN                     32
/* Production HMAC secret for device↔server message signing */
#define HMAC_SECRET_DEFAULT          "8_5IOTuP5zqcl1E8KDdJL2Fv0n5JXuwjdXPOoSoohEYvrM7wAPs9_ij3GiHYJNpzBQe7Sjkoxr_iRbPhwx22iw"
#define HMAC_SECRET_MAX_LEN          128

/* =========================================================
 *  EVENT LOG
 * ========================================================= */
#define EVENT_LOG_SIZE               64

/* =========================================================
 *  COMMAND IDS
 * ========================================================= */
#define CMD_OTA_TRIGGER              0x01
#define CMD_OTA_CHANNEL              0x02
#define CMD_OTA_ROLLBACK             0x03
#define CMD_WIFI_CONFIGURE           0x04
#define CMD_WIFI_SCAN                0x05
#define CMD_BLE_CONFIGURE            0x06
#define CMD_STORAGE_CLEAR            0x07
#define CMD_STORAGE_STATS            0x08
#define CMD_DISPLAY_CONFIG           0x09
#define CMD_BUZZER_CONFIG            0x0A
#define CMD_LED_CONFIG               0x0B
#define CMD_READER_POWER             0x0C
#define CMD_READER_CONFIG            0x0D
#define CMD_TRIGGER_INDICATOR        0x0E
#define CMD_RESERVED                 0x0F
#define CMD_LOCK                     0x10
#define CMD_UNLOCK                   0x11
#define CMD_SET_PIN                  0x12
#define CMD_RESET_AUTH               0x13
#define CMD_SECURITY_MODE            0x14
#define CMD_REBOOT                   0x15
#define CMD_FACTORY_RESET            0x16
#define CMD_SET_TELEMETRY_INTERVAL   0x17
#define CMD_REQUEST_DIAGNOSTICS      0x18
#define CMD_REQUEST_BULK_UPLOAD      0x19
#define CMD_PROVISION_SUCCESS        0x1A
#define CMD_PROVISION_REJECTED       0x1B

/* =========================================================
 *  WIFI CONFIGURE API
 * ========================================================= */
#define WIFI_CONFIG_URL_FMT          "https://sterling.sabarikrishnan.me/edu/api/fleet/device/%s/wifi/"
