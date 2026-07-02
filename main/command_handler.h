#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Command IDs
#define CMD_OTA_TRIGGER             0x01
#define CMD_OTA_CHANNEL             0x02
#define CMD_OTA_ROLLBACK            0x03
#define CMD_WIFI_CONFIGURE          0x04
#define CMD_WIFI_SCAN               0x05
#define CMD_BLE_CONFIGURE           0x06
#define CMD_STORAGE_CLEAR           0x07
#define CMD_STORAGE_STATS           0x08
#define CMD_DISPLAY_CONFIG          0x09
#define CMD_BUZZER_CONFIG           0x0A
#define CMD_LED_CONFIG              0x0B
#define CMD_READER_POWER            0x0C
#define CMD_READER_CONFIG           0x0D
#define CMD_TRIGGER_INDICATOR       0x0E
#define CMD_RESERVED                0x0F
#define CMD_LOCK                    0x10
#define CMD_UNLOCK                  0x11
#define CMD_SET_PIN                 0x12
#define CMD_RESET_AUTH              0x13
#define CMD_SECURITY_MODE           0x14
#define CMD_REBOOT                  0x15
#define CMD_FACTORY_RESET           0x16
#define CMD_SET_TELEMETRY_INTERVAL  0x17
#define CMD_REQUEST_DIAGNOSTICS     0x18
#define CMD_REQUEST_BULK_UPLOAD     0x19
#define CMD_PROVISION_SUCCESS       0x1A
#define CMD_PROVISION_REJECTED      0x1B

// Command packet: 44 bytes
typedef struct __attribute__((packed)) {
    uint32_t command_id;
    uint32_t token;
    uint32_t parameter;
    uint8_t  hmac[32];
} command_packet_t;

_Static_assert(sizeof(command_packet_t) == 44, "Command packet must be exactly 44 bytes");

esp_err_t command_handler_process(const uint8_t *data, size_t len);
bool command_is_locked(void);
