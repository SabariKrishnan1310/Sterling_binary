#include "mqtt_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

typedef struct {
    char prefix[32];
    mqtt_data_cb_t cb;
} cb_entry_t;

static cb_entry_t s_cbs[8];
static int s_cb_count = 0;

static void get_device_id(char *buf, size_t len)
{
    size_t out_len = len;
    if (nvs_stl_get_string(NVS_KEY_MQTT_USERNAME, buf, &out_len) != ESP_OK) {
        buf[0] = '\0';
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            s_connected = true;
            ESP_LOGI(TAG, "MQTT connected");

            char device_id[64];
            get_device_id(device_id, sizeof(device_id));

            char topic[128];
            snprintf(topic, sizeof(topic), "cmd/%s", device_id);
            esp_mqtt_client_subscribe(s_client, topic, 2);

            snprintf(topic, sizeof(topic), "prov/%s", device_id);
            esp_mqtt_client_subscribe(s_client, topic, 1);

            snprintf(topic, sizeof(topic), "firmware/%s", device_id);
            esp_mqtt_client_subscribe(s_client, topic, 1);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_DATA:
            for (int i = 0; i < s_cb_count; i++) {
                if (strncmp(event->topic, s_cbs[i].prefix,
                            strlen(s_cbs[i].prefix)) == 0) {
                    s_cbs[i].cb(event->topic, (const uint8_t *)event->data, event->data_len);
                }
            }
            break;
        default:
            break;
    }
}

esp_err_t mqtt_manager_init(void)
{
    char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);

    char username[64] = {0};
    char password[128] = {0};
    size_t len = sizeof(username);
    if (nvs_stl_get_string(NVS_KEY_MQTT_USERNAME, username, &len) != ESP_OK) {
        username[0] = '\0';
    }
    len = sizeof(password);
    if (nvs_stl_get_string(NVS_KEY_MQTT_PASSWORD, password, &len) != ESP_OK) {
        password[0] = '\0';
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.username = username[0] ? username : NULL,
        .credentials.authentication.password = password[0] ? password : NULL,
        .session.keepalive = MQTT_KEEPALIVE_S,
        .session.disable_clean_session = false,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;

    esp_err_t err;
    err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY,
                                         mqtt_event_handler, NULL);
    if (err != ESP_OK) return err;

    err = esp_mqtt_client_start(s_client);
    return err;
}

esp_err_t mqtt_manager_stop(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_stop(s_client);
}

bool mqtt_manager_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_manager_publish(const char *topic, const uint8_t *payload,
                               int len, int qos, int retain)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(s_client, topic,
                                         (const char *)payload, len, qos, retain);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_manager_subscribe(const char *topic, int qos)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_subscribe(s_client, (char *)topic, qos);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_manager_unsubscribe(const char *topic)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_unsubscribe(s_client, topic);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

void mqtt_manager_register_cb(const char *topic_prefix, mqtt_data_cb_t cb)
{
    if (s_cb_count >= 8) {
        ESP_LOGE(TAG, "Callback table full, cannot register %s", topic_prefix);
        return;
    }
    strncpy(s_cbs[s_cb_count].prefix, topic_prefix,
            sizeof(s_cbs[s_cb_count].prefix) - 1);
    s_cbs[s_cb_count].prefix[sizeof(s_cbs[s_cb_count].prefix) - 1] = '\0';
    s_cbs[s_cb_count].cb = cb;
    s_cb_count++;
    ESP_LOGI(TAG, "Registered callback for prefix '%s' (%d/%d)",
             topic_prefix, s_cb_count, 8);
}
