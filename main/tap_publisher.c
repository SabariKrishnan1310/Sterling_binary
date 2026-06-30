#include "tap_publisher.h"
#include "mqtt_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "bulk_upload.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "timesync.h"
#include "esp_http_client.h"

static const char *TAG = "tap";

static char s_device_id[64];

esp_err_t tap_publisher_init(void)
{
    size_t len = sizeof(s_device_id);
    nvs_stl_get_string("mqtt_username", s_device_id, &len);
    ESP_LOGI(TAG, "Tap publisher initialized for device: %s", s_device_id);
    return ESP_OK;
}

void tap_publisher_publish(const char *hex_uid, int16_t rssi)
{
    char topic[128] = "tap/";
    strcat(topic, s_device_id);

    bool connected = mqtt_manager_is_connected();
    if (connected) {
        mqtt_manager_publish(topic, (uint8_t *)hex_uid, strlen(hex_uid), 1, 0);
    }

    bulk_entry_data_t entry = {
        .rfid_uid = strtoull(hex_uid, NULL, 16),
        .timestamp = timesync_get_unix_seconds(),
        .rssi = rssi,
        .status = 0,
    };
    bulk_upload_store_entry(&entry);

    if (!connected) {
        char url[256];
        snprintf(url, sizeof(url), "http://api.sabarikrishnan.me/ingest/v2/tap/");

        char body[256];
        snprintf(body, sizeof(body),
            "{\"raw_hex\":\"%s\",\"device_id\":\"%s\",\"timestamp\":%lu}",
            hex_uid, s_device_id, (unsigned long)timesync_get_unix_seconds());

        esp_http_client_config_t http_config = {
            .url = url,
            .timeout_ms = 5000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&http_config);
        if (client) {
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_post_field(client, body, strlen(body));
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_err_t err = esp_http_client_perform(client);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "HTTP fallback failed: %s", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        }
    }
}

void tap_publisher_publish_from_storage(void)
{
    bulk_upload_send(50);
}

int tap_publisher_get_pending_count(void)
{
    return bulk_upload_get_count();
}
