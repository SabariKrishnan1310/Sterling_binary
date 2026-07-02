#include "command_handler.h"
#include "config.h"
#include "device.h"
#include "network.h"
#include "mqtt.h"
#include "provision.h"
#include "storage.h"
#include "ota.h"
#include "led.h"
#include "event_log.h"
#include "telemetry.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "mbedtls/md.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "cmd";

static bool s_device_locked = false;
static char s_hmac_secret[64] = {0};

static const char* get_hmac_secret(void)
{
    if (strlen(s_hmac_secret) > 0) return s_hmac_secret;

    nvs_handle_t h;
    if (nvs_open(HMAC_SECRET_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_hmac_secret);
        nvs_get_str(h, HMAC_SECRET_NVS_KEY, s_hmac_secret, &len);
        nvs_close(h);
    }
    if (strlen(s_hmac_secret) == 0) {
        strncpy(s_hmac_secret, HMAC_SECRET_DEFAULT, sizeof(s_hmac_secret) - 1);
    }
    return s_hmac_secret;
}

static bool verify_hmac(const uint8_t *data, size_t data_len, const uint8_t *received_hmac)
{
    uint8_t computed[32];
    const char *key = get_hmac_secret();

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) { mbedtls_md_free(&ctx); return false; }
    mbedtls_md_setup(&ctx, md_info, 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key, strlen(key));
    mbedtls_md_hmac_update(&ctx, data, data_len);
    mbedtls_md_hmac_finish(&ctx, computed);
    mbedtls_md_free(&ctx);

    return memcmp(computed, received_hmac, 32) == 0;
}

// --- Command handlers ---

static esp_err_t cmd_ota_trigger(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x01: OTA trigger (param=%lu)", param);
    ota_trigger_flag = true;
    return ESP_OK;
}

static esp_err_t cmd_ota_channel(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x02: OTA channel=%lu", param);
    nvs_handle_t h;
    if (nvs_open("ota", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "channel", param);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

static esp_err_t cmd_ota_rollback(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD 0x03: OTA rollback");
    const esp_partition_t *prev = esp_ota_get_last_invalid_partition();
    if (prev) {
        esp_ota_set_boot_partition(prev);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    return ESP_FAIL;
}

static esp_err_t cmd_wifi_configure(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x04: WiFi configure version hint (param=%lu)", param);
    // Server contract: version hint ONLY, no network list
    return ESP_OK;
}

static esp_err_t cmd_wifi_scan(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x05: WiFi scan max_aps=%lu", param);
    wifi_scan_config_t scan_cfg = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    esp_wifi_scan_start(&scan_cfg, true);
    return ESP_OK;
}

static esp_err_t cmd_ble_configure(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x06: BLE configure mode=%lu", param);
    // ESP32 HAS BLE — store config in NVS
    nvs_handle_t h;
    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "ble_mode", param);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "BLE mode %lu stored (BLE init deferred to BLE task)", param);
    return ESP_OK;
}

static esp_err_t cmd_storage_clear(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD 0x07: Storage clear type=%lu", param);
    if (param == 0 || param == 2) {
        FILE *f = fopen(STORAGE_FILE_PATH, "w");
        if (f) fclose(f);
        storage_mark_uploaded(0);
        ESP_LOGI(TAG, "Tags cleared");
    }
    if (param == 1 || param == 2) {
        FILE *f = fopen(EVENT_LOG_PATH, "w");
        if (f) fclose(f);
        ESP_LOGI(TAG, "Event log cleared");
    }
    return ESP_OK;
}

static esp_err_t cmd_storage_stats(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x08: Storage stats");
    storage_dump_stats();
    return ESP_OK;
}

static esp_err_t cmd_not_supported(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD: Not supported on this hardware");
    return ESP_OK;
}

static esp_err_t cmd_led_config(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x0B: LED config param=%lu", param);
    led_pattern_t pattern = LED_PATTERN_IDLE;
    switch (param) {
        case 0: pattern = LED_PATTERN_IDLE; break;
        case 1: pattern = LED_PATTERN_BOOT; break;
        case 2: pattern = LED_PATTERN_TAG; break;
        case 3: pattern = LED_PATTERN_FAILURE; break;
        case 4: pattern = LED_PATTERN_WAVE; break;
    }
    led_send(pattern);
    return ESP_OK;
}

static esp_err_t cmd_trigger_indicator(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x0E: Trigger indicator %lu sec (LED only)", param);
    for (uint32_t i = 0; i < param && i < 30; i++) {
        led_send(LED_PATTERN_TAG);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return ESP_OK;
}

static esp_err_t cmd_reserved(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD 0x0F: Reserved command received");
    return ESP_OK;
}

static esp_err_t cmd_lock(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD 0x10: Device LOCK mode=%lu", param);
    s_device_locked = true;
    nvs_handle_t h;
    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "locked", 1);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

static esp_err_t cmd_unlock(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x11: Device UNLOCK");
    s_device_locked = false;
    nvs_handle_t h;
    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "locked", 0);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

static esp_err_t cmd_set_pin(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x12: Set PIN hash=%08lx", param);
    nvs_handle_t h;
    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "pin_hash", param);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

static esp_err_t cmd_reset_auth(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x13: Reset auth");
    nvs_handle_t h;
    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "pin_hash");
        nvs_set_u8(h, "locked", 0);
        nvs_commit(h);
        nvs_close(h);
    }
    s_device_locked = false;
    return ESP_OK;
}

static esp_err_t cmd_security_mode(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x14: Security mode=%lu", param);
    nvs_handle_t h;
    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "sec_mode", param);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

static esp_err_t cmd_reboot(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD 0x15: Reboot mode=%lu", param);
    if (param == 1) {
        ESP_LOGW(TAG, "Bootloader mode not supported via SW, doing normal restart");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK; // never reached
}

static esp_err_t cmd_factory_reset(uint32_t token, uint32_t param)
{
    ESP_LOGW(TAG, "CMD 0x16: FACTORY RESET");
    provision_clear();
    FILE *f = fopen(STORAGE_FILE_PATH, "w");
    if (f) fclose(f);
    f = fopen(EVENT_LOG_PATH, "w");
    if (f) fclose(f);
    esp_restart();
    return ESP_OK;
}

static esp_err_t cmd_set_telemetry_interval(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x17: Telemetry interval=%lu ms", param);
    telemetry_set_interval(param);
    return ESP_OK;
}

static esp_err_t cmd_request_diagnostics(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x18: Diagnostics (server IGNORES this topic)");
    uint8_t diag[44];
    memset(diag, 0, sizeof(diag));
    uint32_t seq = 0;
    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000);
    int32_t free_heap = esp_get_free_heap_size();
    memcpy(diag, &seq, 4);
    memcpy(diag + 4, &uptime, 4);
    memcpy(diag + 8, &free_heap, 4);
    char topic[64];
    snprintf(topic, sizeof(topic), "diagnostics/%s", device_get_id());
    mqtt_publish(topic, (const char*)diag, 44, 1, 0);
    return ESP_OK;
}

static esp_err_t cmd_request_bulk_upload(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x19: Bulk upload max=%lu", param);
    extern void bulk_trigger(uint32_t max_entries);
    bulk_trigger(param);
    return ESP_OK;
}

static esp_err_t cmd_provision_success(uint32_t token, uint32_t param)
{
    ESP_LOGI(TAG, "CMD 0x1A: Provision confirmed");
    provision_mark_done();
    return ESP_OK;
}

static esp_err_t cmd_provision_rejected(uint32_t token, uint32_t param)
{
    ESP_LOGE(TAG, "CMD 0x1B: Provision REJECTED");
    provision_clear();
    return ESP_OK;
}

// --- Dispatch table ---

typedef esp_err_t (*cmd_handler_t)(uint32_t token, uint32_t param);

static const cmd_handler_t s_handlers[] = {
    NULL,                          // 0x00 - invalid
    cmd_ota_trigger,               // 0x01
    cmd_ota_channel,               // 0x02
    cmd_ota_rollback,              // 0x03
    cmd_wifi_configure,            // 0x04
    cmd_wifi_scan,                 // 0x05
    cmd_ble_configure,             // 0x06
    cmd_storage_clear,             // 0x07
    cmd_storage_stats,             // 0x08
    cmd_not_supported,             // 0x09 - no display
    cmd_not_supported,             // 0x0A - no buzzer
    cmd_led_config,                // 0x0B
    cmd_not_supported,             // 0x0C - RC522 power not configurable
    cmd_not_supported,             // 0x0D - RC522 mode not configurable
    cmd_trigger_indicator,         // 0x0E
    cmd_reserved,                  // 0x0F
    cmd_lock,                      // 0x10
    cmd_unlock,                    // 0x11
    cmd_set_pin,                   // 0x12
    cmd_reset_auth,                // 0x13
    cmd_security_mode,             // 0x14
    cmd_reboot,                    // 0x15
    cmd_factory_reset,             // 0x16
    cmd_set_telemetry_interval,    // 0x17
    cmd_request_diagnostics,       // 0x18
    cmd_request_bulk_upload,       // 0x19
    cmd_provision_success,         // 0x1A
    cmd_provision_rejected,        // 0x1B
};

// --- Public API ---

esp_err_t command_handler_process(const uint8_t *data, size_t len)
{
    if (len != sizeof(command_packet_t)) {
        ESP_LOGW(TAG, "Invalid cmd length: %d (expected %d)", len, sizeof(command_packet_t));
        return ESP_ERR_INVALID_SIZE;
    }

    const command_packet_t *cmd = (const command_packet_t*)data;

    // Verify HMAC over bytes 0-11
    if (!verify_hmac(data, 12, cmd->hmac)) {
        ESP_LOGW(TAG, "CMD 0x%02X: HMAC mismatch, dropped", (unsigned int)cmd->command_id);
        return ESP_ERR_INVALID_ARG;
    }

    if (cmd->command_id == 0 || cmd->command_id > 0x1B) {
        ESP_LOGW(TAG, "CMD: Unknown 0x%02X", (unsigned int)cmd->command_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cmd_handler_t handler = s_handlers[cmd->command_id];
    if (!handler) {
        ESP_LOGW(TAG, "CMD 0x%02X: No handler", (unsigned int)cmd->command_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "CMD 0x%02X: token=%lu param=%lu", (unsigned int)cmd->command_id, cmd->token, cmd->parameter);
    return handler(cmd->token, cmd->parameter);
}

bool command_is_locked(void)
{
    return s_device_locked;
}
