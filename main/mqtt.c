#include "mqtt.h"
#include "heartbeat.h"
#include "command_handler.h"
#include "network.h"
#include "config.h"
#include "device.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static bool s_initialized = false;

static char s_host[64] = MQTT_DEFAULT_HOST;
static uint16_t s_port = MQTT_DEFAULT_PORT;
static char s_username[64] = "";
static char s_password[128] = "";

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            s_connected = true;
            xEventGroupSetBits(wifi_event_group, MQTT_CONNECTED_BIT);
            {
                const char *id = device_get_id();
                char cmd_topic[64], cmd2_topic[64], prov_topic[64], fw_topic[64];
                snprintf(cmd_topic, sizeof(cmd_topic), "command/%s", id);
                snprintf(cmd2_topic, sizeof(cmd2_topic), "cmd/%s", id);
                snprintf(prov_topic, sizeof(prov_topic), "prov/%s", id);
                snprintf(fw_topic, sizeof(fw_topic), "firmware/%s", id);
                esp_mqtt_client_subscribe(s_client, cmd_topic, 2);
                esp_mqtt_client_subscribe(s_client, cmd2_topic, 1);
                esp_mqtt_client_subscribe(s_client, prov_topic, 1);
                esp_mqtt_client_subscribe(s_client, fw_topic, 1);
                ESP_LOGI(TAG, "Subscribed to command topics for %s", id);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected from broker");
            s_connected = false;
            xEventGroupClearBits(wifi_event_group, MQTT_CONNECTED_BIT);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data on topic=%.*s: %.*s",
                     (int)event->topic_len, event->topic,
                     (int)event->data_len, event->data);
            if (event->topic_len >= 5 && memcmp(event->topic, "pong/", 5) == 0) {
                heartbeat_handle_pong(event->data, event->data_len);
            }
            // Route binary commands from command/{id} or cmd/{id}
            if ((event->topic_len >= 8 && memcmp(event->topic, "command/", 8) == 0) ||
                (event->topic_len >= 4 && memcmp(event->topic, "cmd/", 4) == 0)) {
                if (event->data_len == 44) {
                    command_handler_process((const uint8_t*)event->data, event->data_len);
                }
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            break;

        default:
            break;
    }
}

esp_err_t mqtt_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MQTT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No MQTT config in NVS, using defaults");
    }

    if (err == ESP_OK) {
        size_t len = sizeof(s_host);
        if (nvs_get_str(handle, "host", s_host, &len) != ESP_OK) {
            strncpy(s_host, MQTT_DEFAULT_HOST, sizeof(s_host) - 1);
        }

        int32_t port_i32 = 0;
        len = sizeof(port_i32);
        if (nvs_get_i32(handle, "port", &port_i32) == ESP_OK) {
            s_port = (uint16_t)port_i32;
        } else {
            char port_str[8];
            len = sizeof(port_str);
            if (nvs_get_str(handle, "port", port_str, &len) == ESP_OK) {
                s_port = (uint16_t)atoi(port_str);
            } else {
                s_port = MQTT_DEFAULT_PORT;
            }
        }

        len = sizeof(s_username);
        if (nvs_get_str(handle, "username", s_username, &len) != ESP_OK) {
            s_username[0] = '\0';
        }

        len = sizeof(s_password);
        if (nvs_get_str(handle, "password", s_password, &len) != ESP_OK) {
            s_password[0] = '\0';
        }

        nvs_close(handle);
    }

    s_host[sizeof(s_host) - 1] = '\0';
    s_username[sizeof(s_username) - 1] = '\0';
    s_password[sizeof(s_password) - 1] = '\0';

    ESP_LOGI(TAG, "MQTT config: host=%s, port=%d, username=%s",
             s_host, s_port, s_username);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t mqtt_start(void)
{
    if (s_client) {
        ESP_LOGW(TAG, "MQTT client already running");
        return ESP_OK;
    }

    if (!s_initialized) {
        ESP_LOGE(TAG, "mqtt_init() must be called first");
        return ESP_ERR_INVALID_STATE;
    }

    const char *client_id = s_username[0] ? s_username : device_get_id();
    const char *username = s_username[0] ? s_username : NULL;
    const char *password = s_password[0] ? s_password : NULL;

    esp_mqtt_client_config_t cfg = {
        .broker.address.hostname = s_host,
        .broker.address.port = s_port,
        .credentials.username = username,
        .credentials.client_id = client_id,
        .credentials.authentication.password = password,
        .session.disable_clean_session = false,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 1000,
        .network.timeout_ms = 8000,
        .buffer.size = 1024,
        .buffer.out_size = 512,
        .task.priority = 5,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started: %s:%d", s_host, s_port);
    return ESP_OK;
}

esp_err_t mqtt_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    xEventGroupClearBits(wifi_event_group, MQTT_CONNECTED_BIT);
    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

bool mqtt_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_publish(const char *topic, const char *data, int len, int qos, int retain)
{
    if (!s_connected || !s_client) {
        return ESP_FAIL;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, data, len, qos, retain);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mqtt_subscribe(const char *topic, int qos)
{
    if (!s_connected || !s_client) {
        return ESP_FAIL;
    }
    int msg_id = esp_mqtt_client_subscribe_single(s_client, topic, qos);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void* mqtt_get_client(void)
{
    return (void*)s_client;
}

void mqtt_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT task started, waiting for provisioning...");
    xEventGroupWaitBits(wifi_event_group, PROVISION_DONE_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Provisioning done, initializing MQTT...");
    esp_err_t err = mqtt_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT init failed, restarting...");
        esp_restart();
    }

    ESP_LOGI(TAG, "Waiting for network (WG or WiFi)...");
    xEventGroupWaitBits(wifi_event_group, WG_UP_BIT | WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Network ready, starting MQTT connection...");
    err = mqtt_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed, will retry in task loop");
    }

    TickType_t last_connected = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MQTT_HEALTH_CHECK_INTERVAL_MS));

        if (mqtt_is_connected()) {
            last_connected = xTaskGetTickCount();
        } else {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_connected) > pdMS_TO_TICKS(MQTT_RESTART_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "Disconnected for >5 min, restarting MQTT client");
                mqtt_stop();
                vTaskDelay(pdMS_TO_TICKS(1000));
                mqtt_start();
                last_connected = xTaskGetTickCount();
            }
        }
    }
}
