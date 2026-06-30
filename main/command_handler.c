#include "command_handler.h"
#include "binary_protocol.h"
#include "hmac_utils.h"
#include "mqtt_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "ota_manager.h"
#include "wifi_manager.h"
#include "wg_manager.h"
#include "led_controller.h"
#include "provisioning.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>
#include <endian.h>

/* Forward-declared externs for modules not included via headers */
extern void telemetry_set_interval(uint32_t ms);
extern void diagnostics_send(void);
extern void rfid_reader_set_power(uint8_t power);
extern esp_err_t bulk_upload_send(int max_entries);
extern int bulk_upload_get_count(void);
extern esp_err_t bulk_upload_clear(void);

static const char *TAG = "cmd";

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static void get_device_id(char *buf, size_t len)
{
    size_t out_len = len;
    if (nvs_stl_get_string(NVS_KEY_MQTT_USERNAME, buf, &out_len) != ESP_OK) {
        buf[0] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/*  Ack                                                                */
/* ------------------------------------------------------------------ */

void command_handler_ack(uint32_t command_id, uint32_t token)
{
    command_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.command_id = htobe32(command_id);
    pkt.token      = htobe32(token);
    pkt.parameter  = 0;

    hmac_compute((const uint8_t *)&pkt, COMMAND_HMAC_OFFSET, pkt.hmac);

    char device_id[64];
    get_device_id(device_id, sizeof(device_id));

    char topic[128];
    snprintf(topic, sizeof(topic), "cmd/%s/ack", device_id);

    mqtt_manager_publish(topic, (const uint8_t *)&pkt, COMMAND_PACKET_SIZE, 2, 0);

    ESP_LOGI(TAG, "Ack sent cmd=0x%02x token=%lu", (unsigned int)command_id, (unsigned long)token);
}

/* ------------------------------------------------------------------ */
/*  Process                                                            */
/* ------------------------------------------------------------------ */

void command_handler_process(const uint8_t *packet, int len)
{
    if (len != COMMAND_PACKET_SIZE) {
        ESP_LOGW(TAG, "Invalid packet size %d (expected %d)", len, COMMAND_PACKET_SIZE);
        return;
    }

    if (!hmac_verify(packet, COMMAND_HMAC_OFFSET, packet + COMMAND_HMAC_OFFSET)) {
        ESP_LOGW(TAG, "HMAC mismatch, ignoring command");
        return;
    }

    const command_packet_t *cmd = (const command_packet_t *)packet;
    uint32_t cmd_id = be32toh(cmd->command_id);
    uint32_t token  = be32toh(cmd->token);
    uint32_t param  = be32toh(cmd->parameter);

    ESP_LOGI(TAG, "Processing cmd=0x%02x token=%lu param=%lu",
             (unsigned int)cmd_id, (unsigned long)token, (unsigned long)param);

    switch (cmd_id) {
        case CMD_OTA_TRIGGER:
            ota_manager_start(OTA_FW_URL);
            break;
        case CMD_OTA_CHANNEL:
            ota_manager_set_channel((uint8_t)param);
            break;
        case CMD_OTA_ROLLBACK:
            ota_manager_rollback();
            break;
        case CMD_WIFI_CONFIGURE:
            wifi_manager_fetch_and_update_list();
            wifi_manager_connect();
            break;
        case CMD_WIFI_SCAN:
            ESP_LOGI(TAG, "WiFi scan requested, max_aps=%lu", (unsigned long)param);
            break;
        case CMD_BLE_CONFIGURE:
            ESP_LOGI(TAG, "BLE configure not implemented");
            break;
        case CMD_STORAGE_CLEAR:
            if (param == 0 || param == 2) {
                bulk_upload_clear();
            }
            ESP_LOGI(TAG, "Storage clear type=%lu", (unsigned long)param);
            break;
        case CMD_STORAGE_STATS:
            ESP_LOGI(TAG, "Stored entries: %d", bulk_upload_get_count());
            break;
        case CMD_DISPLAY_CONFIG:
            ESP_LOGI(TAG, "Display brightness=%lu", (unsigned long)param);
            break;
        case CMD_BUZZER_CONFIG:
            ESP_LOGI(TAG, "Buzzer mode=%lu", (unsigned long)param);
            break;
        case CMD_LED_CONFIG:
            led_controller_play((led_pattern_t)param);
            break;
        case CMD_READER_POWER:
            rfid_reader_set_power((uint8_t)param);
            break;
        case CMD_READER_CONFIG:
            ESP_LOGI(TAG, "Reader config=%lu", (unsigned long)param);
            break;
        case CMD_TRIGGER_INDICATOR:
            led_controller_play(LED_PATTERN_INDICATOR);
            break;
        case CMD_LOCK:
            gpio_set_level(PIN_RELAY, 1);
            ESP_LOGI(TAG, "LOCKED");
            break;
        case CMD_UNLOCK:
            gpio_set_level(PIN_RELAY, 0);
            ESP_LOGI(TAG, "UNLOCKED");
            break;
        case CMD_SET_PIN:
            ESP_LOGI(TAG, "Set PIN not implemented");
            break;
        case CMD_RESET_AUTH:
            ESP_LOGI(TAG, "Reset auth not implemented");
            break;
        case CMD_SECURITY_MODE:
            ESP_LOGI(TAG, "Security mode=%lu", (unsigned long)param);
            break;
        case CMD_REBOOT:
            ESP_LOGI(TAG, "Rebooting...");
            esp_restart();
            break;
        case CMD_FACTORY_RESET:
            nvs_stl_erase_all();
            esp_restart();
            break;
        case CMD_SET_TELEMETRY_INTERVAL:
            telemetry_set_interval(param);
            break;
        case CMD_REQUEST_DIAGNOSTICS:
            diagnostics_send();
            break;
        case CMD_REQUEST_BULK_UPLOAD:
            bulk_upload_send((int)param);
            break;
        case CMD_PROVISION_SUCCESS:
            provisioning_mark_provisioned();
            led_controller_play(LED_PATTERN_MQTT_CONNECTED);
            break;
        case CMD_PROVISION_REJECTED:
            ESP_LOGW(TAG, "Provisioning rejected");
            led_controller_play(LED_PATTERN_FAILURE);
            break;
        default:
            ESP_LOGW(TAG, "Unknown command 0x%02x", (unsigned int)cmd_id);
            return;
    }

    command_handler_ack(cmd_id, token);
}

/* ------------------------------------------------------------------ */
/*  Static callback                                                    */
/* ------------------------------------------------------------------ */

static void command_handler_cb(const char *topic, const uint8_t *payload, int len)
{
    command_handler_process(payload, len);
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

esp_err_t command_handler_init(void)
{
    mqtt_manager_register_cb("cmd/", command_handler_cb);
    ESP_LOGI(TAG, "Command handler initialised");
    return ESP_OK;
}
