#include "telemetry.h"
#include "config.h"
#include "mqtt.h"
#include "device.h"
#include "heartbeat.h"
#include "network.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "mbedtls/md.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

static const char *TAG = "telemetry";

static telemetry_packet_t s_last_packet;
static uint32_t s_interval_ms = TELEMETRY_DEFAULT_INTERVAL_MS;
static uint32_t s_tags_since_last = 0;
static char s_hmac_secret[64] = {0};

static uint32_t parse_fw_version(const char *ver)
{
    int major = 0, minor = 0, patch = 0;
    sscanf(ver, "%d.%d.%d", &major, &minor, &patch);
    return (uint32_t)(major * 10000 + minor * 100 + patch);
}

esp_err_t telemetry_init(void)
{
    // Read HMAC_SECRET from NVS
    nvs_handle_t h;
    esp_err_t err = nvs_open(HMAC_SECRET_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = sizeof(s_hmac_secret);
        err = nvs_get_str(h, HMAC_SECRET_NVS_KEY, s_hmac_secret, &len);
        nvs_close(h);
    }
    if (err != ESP_OK || strlen(s_hmac_secret) == 0) {
        strncpy(s_hmac_secret, HMAC_SECRET_DEFAULT, sizeof(s_hmac_secret) - 1);
        ESP_LOGI(TAG, "Using default HMAC_SECRET");
    }

    s_tags_since_last = 0;
    memset(&s_last_packet, 0, sizeof(s_last_packet));
    ESP_LOGI(TAG, "Telemetry initialized (interval=%lums)", s_interval_ms);
    return ESP_OK;
}

esp_err_t telemetry_send(void)
{
    telemetry_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    // Monotonic sequence from NVS
    nvs_handle_t h;
    uint32_t seq = 0;
    if (nvs_open(TELEMETRY_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, TELEMETRY_NVS_KEY_SEQ, &seq);
        seq++;
        nvs_set_u32(h, TELEMETRY_NVS_KEY_SEQ, seq);
        nvs_commit(h);
        nvs_close(h);
    }
    pkt.sequence_number = seq;

    // Uptime
    pkt.uptime_ms = (int32_t)(esp_timer_get_time() / 1000);

    // WiFi RSSI
    wifi_ap_record_t ap_info;
    int16_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    pkt.rssi = (uint32_t)rssi;           // offset 12 as uint32
    pkt.wifi_rssi = rssi;                 // offset 20 as int16

    // Free heap
    pkt.free_heap = esp_get_free_heap_size();

    // Fixed values
    pkt.battery_mv = 0;                   // mains powered
    pkt.temperature_celsius = 0xFFFF;     // no external temp sensor

    // Tags read since last telemetry
    pkt.num_tags_read = s_tags_since_last;
    s_tags_since_last = 0;

    // Firmware version
    pkt.firmware_version = parse_fw_version(FW_VERSION);

    // Reserved
    pkt.reserved = 0;

    // Timestamp with drift compensation
    pkt.timestamp_ist = (uint32_t)(time(NULL) + (heartbeat_get_drift_ms() / 1000));

    // Compute HMAC over bytes 0-41 only
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    mbedtls_md_setup(&ctx, md_info, 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)s_hmac_secret, strlen(s_hmac_secret));
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)&pkt, 42);  // bytes 0-41
    mbedtls_md_hmac_finish(&ctx, pkt.hmac);
    mbedtls_md_free(&ctx);

    // Publish
    char topic[64];
    snprintf(topic, sizeof(topic), "telemetry/%s", device_get_id());
    esp_err_t err = mqtt_publish(topic, (const char*)&pkt, sizeof(pkt), 1, 0);

    if (err == ESP_OK) {
        memcpy(&s_last_packet, &pkt, sizeof(pkt));
        ESP_LOGD(TAG, "Telemetry sent: seq=%llu, uptime=%ld, rssi=%d, heap=%ld",
                 (unsigned long long)pkt.sequence_number,
                 (long)pkt.uptime_ms, rssi, (long)pkt.free_heap);
    } else {
        ESP_LOGW(TAG, "Telemetry publish failed");
    }

    return err;
}

esp_err_t telemetry_set_interval(uint32_t interval_ms)
{
    s_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Telemetry interval set to %lums", interval_ms);
    return ESP_OK;
}

uint32_t telemetry_get_interval(void)
{
    return s_interval_ms;
}

telemetry_packet_t* telemetry_get_last(void)
{
    return &s_last_packet;
}

void telemetry_increment_tags(uint32_t count)
{
    s_tags_since_last += count;
}

void telemetry_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Telemetry task started, waiting for MQTT...");

    telemetry_init();

    xEventGroupWaitBits(wifi_event_group, MQTT_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "MQTT connected, starting telemetry loop (interval=%lums)", s_interval_ms);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
        telemetry_send();
    }
}
