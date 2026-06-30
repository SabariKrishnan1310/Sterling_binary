#include "provisioning.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "provision";

typedef struct {
    char *buffer;
    size_t size;
    size_t len;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (buf->len + evt->data_len <= buf->size) {
                memcpy(buf->buffer + buf->len, evt->data, evt->data_len);
                buf->len += evt->data_len;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t provisioning_call(const char *mac, const char *serial,
                            const char *hw_ver, const char *fw_ver,
                            provisioning_data_t *out)
{
    if (!mac || !serial || !hw_ver || !fw_ver || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(body, "mac", mac);
    cJSON_AddStringToObject(body, "serial", serial);
    cJSON_AddStringToObject(body, "hw_ver", hw_ver);
    cJSON_AddStringToObject(body, "fw_ver", fw_ver);

    char *json_body = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_body) return ESP_ERR_NO_MEM;

    http_buf_t buf = {0};
    buf.size = 8192;
    buf.buffer = malloc(buf.size);
    if (!buf.buffer) {
        free(json_body);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t cfg = {
        .url = PROVISIONING_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .timeout_ms = PROVISIONING_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(json_body);
        free(buf.buffer);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(json_body);
        free(buf.buffer);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Provisioning response: HTTP %d, %d bytes", status, buf.len);
    esp_http_client_cleanup(client);
    free(json_body);

    if (status != 200 && status != 201) {
        free(buf.buffer);
        return ESP_FAIL;
    }

    buf.buffer[buf.len] = '\0';
    cJSON *root = cJSON_Parse(buf.buffer);
    free(buf.buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse provisioning response JSON");
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(provisioning_data_t));

    cJSON *item = cJSON_GetObjectItem(root, "device_id");
    if (item && cJSON_IsString(item)) {
        strncpy(out->device_id, item->valuestring, sizeof(out->device_id) - 1);
    }

    cJSON *wg = cJSON_GetObjectItem(root, "wireguard");
    if (wg && cJSON_IsObject(wg)) {
        item = cJSON_GetObjectItem(wg, "private_key");
        if (item && cJSON_IsString(item))
            strncpy(out->wg_private_key, item->valuestring, sizeof(out->wg_private_key) - 1);
        item = cJSON_GetObjectItem(wg, "address");
        if (item && cJSON_IsString(item))
            strncpy(out->wg_address, item->valuestring, sizeof(out->wg_address) - 1);
        item = cJSON_GetObjectItem(wg, "server_public_key");
        if (item && cJSON_IsString(item))
            strncpy(out->wg_server_pubkey, item->valuestring, sizeof(out->wg_server_pubkey) - 1);
        item = cJSON_GetObjectItem(wg, "endpoint");
        if (item && cJSON_IsString(item))
            strncpy(out->wg_endpoint, item->valuestring, sizeof(out->wg_endpoint) - 1);
    }

    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (mqtt && cJSON_IsObject(mqtt)) {
        item = cJSON_GetObjectItem(mqtt, "username");
        if (item && cJSON_IsString(item))
            strncpy(out->mqtt_username, item->valuestring, sizeof(out->mqtt_username) - 1);
        item = cJSON_GetObjectItem(mqtt, "password");
        if (item && cJSON_IsString(item))
            strncpy(out->mqtt_password, item->valuestring, sizeof(out->mqtt_password) - 1);
    }

    cJSON *wifi_arr = cJSON_GetObjectItem(root, "wifi_networks");
    if (wifi_arr && cJSON_IsArray(wifi_arr)) {
        int count = cJSON_GetArraySize(wifi_arr);
        if (count > 32) count = 32;
        for (int i = 0; i < count; i++) {
            item = cJSON_GetArrayItem(wifi_arr, i);
            if (item && cJSON_IsString(item)) {
                strncpy(out->wifi_networks[i], item->valuestring,
                        sizeof(out->wifi_networks[i]) - 1);
            }
        }
        out->wifi_network_count = count;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Provisioning successful: device=%s", out->device_id);
    return ESP_OK;
}

esp_err_t provisioning_store(const provisioning_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;

    nvs_stl_set_string(NVS_KEY_WG_PRIV_KEY, data->wg_private_key);
    nvs_stl_set_string(NVS_KEY_WG_ADDRESS, data->wg_address);
    nvs_stl_set_string(NVS_KEY_WG_SERVER_PUBKEY, data->wg_server_pubkey);
    nvs_stl_set_string(NVS_KEY_WG_ENDPOINT, data->wg_endpoint);
    nvs_stl_set_string(NVS_KEY_MQTT_USERNAME, data->mqtt_username);
    nvs_stl_set_string(NVS_KEY_MQTT_PASSWORD, data->mqtt_password);

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    int n = data->wifi_network_count / 2;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", data->wifi_networks[i * 2]);
        cJSON_AddStringToObject(item, "password", data->wifi_networks[i * 2 + 1]);
        cJSON_AddItemToArray(arr, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) {
        nvs_stl_set_blob(NVS_KEY_WIFI_LIST, json, strlen(json));
        free(json);
    }

    nvs_stl_set_u8(NVS_KEY_PROVISIONED, 1);
    ESP_LOGI(TAG, "Provisioning data stored to NVS");
    return ESP_OK;
}

esp_err_t provisioning_load(provisioning_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memset(data, 0, sizeof(provisioning_data_t));

    size_t len = sizeof(data->wg_private_key);
    nvs_stl_get_string(NVS_KEY_WG_PRIV_KEY, data->wg_private_key, &len);
    len = sizeof(data->wg_address);
    nvs_stl_get_string(NVS_KEY_WG_ADDRESS, data->wg_address, &len);
    len = sizeof(data->wg_server_pubkey);
    nvs_stl_get_string(NVS_KEY_WG_SERVER_PUBKEY, data->wg_server_pubkey, &len);
    len = sizeof(data->wg_endpoint);
    nvs_stl_get_string(NVS_KEY_WG_ENDPOINT, data->wg_endpoint, &len);
    len = sizeof(data->mqtt_username);
    nvs_stl_get_string(NVS_KEY_MQTT_USERNAME, data->mqtt_username, &len);
    len = sizeof(data->mqtt_password);
    nvs_stl_get_string(NVS_KEY_MQTT_PASSWORD, data->mqtt_password, &len);

    size_t blob_len = 0;
    nvs_stl_get_blob(NVS_KEY_WIFI_LIST, NULL, &blob_len);
    if (blob_len > 0) {
        char *json = malloc(blob_len + 1);
        if (json) {
            nvs_stl_get_blob(NVS_KEY_WIFI_LIST, json, &blob_len);
            json[blob_len] = '\0';
            cJSON *root = cJSON_Parse(json);
            free(json);
            if (root) {
                cJSON *arr = cJSON_GetObjectItem(root, "networks");
                if (arr && cJSON_IsArray(arr)) {
                    int count = cJSON_GetArraySize(arr);
                    int idx = 0;
                    for (int i = 0; i < count && idx < 32; i++) {
                        cJSON *item = cJSON_GetArrayItem(arr, i);
                        if (!item) continue;
                        cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
                        cJSON *pwd = cJSON_GetObjectItem(item, "password");
                        if (ssid && cJSON_IsString(ssid) && idx < 32) {
                            strncpy(data->wifi_networks[idx++], ssid->valuestring,
                                    sizeof(data->wifi_networks[idx - 1]) - 1);
                        }
                        if (pwd && cJSON_IsString(pwd) && idx < 32) {
                            strncpy(data->wifi_networks[idx++], pwd->valuestring,
                                    sizeof(data->wifi_networks[idx - 1]) - 1);
                        }
                    }
                    data->wifi_network_count = idx;
                }
                cJSON_Delete(root);
            }
        }
    }

    return ESP_OK;
}

bool provisioning_is_provisioned(void)
{
    uint8_t val = 0;
    if (nvs_stl_get_u8(NVS_KEY_PROVISIONED, &val) == ESP_OK) {
        return val == 1;
    }
    return false;
}

void provisioning_mark_provisioned(void)
{
    nvs_stl_set_u8(NVS_KEY_PROVISIONED, 1);
    ESP_LOGI(TAG, "Marked as provisioned");
}

void provisioning_clear(void)
{
    nvs_stl_erase_all();
    ESP_LOGW(TAG, "Provisioning data cleared");
}
