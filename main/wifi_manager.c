#include "wifi_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";

static wifi_network_t s_networks[WIFI_LIST_MAX];
static int s_network_count = 0;
static int s_current_index = 0;
static uint32_t s_list_version = 0;
static bool s_connected = false;

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_netif = NULL;
static bool s_init_done = false;

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        s_connected = true;
        ESP_LOGI(TAG, "Connected to AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGW(TAG, "Disconnected from AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t load_wifi_list(void)
{
    size_t len = 0;
    esp_err_t err = nvs_stl_get_blob(NVS_KEY_WIFI_LIST, NULL, &len);
    if (err != ESP_OK || len == 0) {
        s_network_count = 0;
        return ESP_OK;
    }

    char *json = malloc(len + 1);
    if (!json) return ESP_ERR_NO_MEM;

    err = nvs_stl_get_blob(NVS_KEY_WIFI_LIST, json, &len);
    if (err != ESP_OK) {
        free(json);
        return err;
    }
    json[len] = '\0';

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse wifi list JSON");
        return ESP_ERR_INVALID_ARG;
    }

    s_network_count = 0;
    cJSON *arr = cJSON_GetObjectItem(root, "networks");
    if (arr && cJSON_IsArray(arr)) {
        int count = cJSON_GetArraySize(arr);
        if (count > WIFI_LIST_MAX) count = WIFI_LIST_MAX;
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            if (!item) continue;
            cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
            cJSON *pwd = cJSON_GetObjectItem(item, "password");
            if (ssid && cJSON_IsString(ssid)) {
                strncpy(s_networks[s_network_count].ssid, ssid->valuestring,
                        sizeof(s_networks[s_network_count].ssid) - 1);
                if (pwd && cJSON_IsString(pwd)) {
                    strncpy(s_networks[s_network_count].password, pwd->valuestring,
                            sizeof(s_networks[s_network_count].password) - 1);
                }
                s_network_count++;
            }
        }
    }
    cJSON_Delete(root);

    uint32_t idx = 0;
    if (nvs_stl_get_u32(NVS_KEY_WIFI_CURRENT_IDX, &idx) == ESP_OK) {
        s_current_index = (int)idx;
    }
    uint32_t ver = 0;
    if (nvs_stl_get_u32(NVS_KEY_WIFI_LIST_VERSION, &ver) == ESP_OK) {
        s_list_version = ver;
    }

    ESP_LOGI(TAG, "Loaded %d networks, idx=%d, ver=%lu",
             s_network_count, s_current_index, (unsigned long)s_list_version);
    return ESP_OK;
}

static esp_err_t save_wifi_list(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < s_network_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", s_networks[i].ssid);
        cJSON_AddStringToObject(item, "password", s_networks[i].password);
        cJSON_AddItemToArray(arr, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    esp_err_t err = nvs_stl_set_blob(NVS_KEY_WIFI_LIST, json, strlen(json));
    free(json);
    return err;
}

esp_err_t wifi_manager_init(void)
{
    if (s_init_done) return ESP_OK;

    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    load_wifi_list();

    /* Fallback: if no networks configured, use hardcoded default for initial provisioning */
    if (s_network_count == 0) {
        ESP_LOGW(TAG, "No wifi networks in NVS, using default credentials for provisioning");
        strncpy(s_networks[0].ssid, WIFI_DEFAULT_SSID, sizeof(s_networks[0].ssid) - 1);
        strncpy(s_networks[0].password, WIFI_DEFAULT_PASSWORD, sizeof(s_networks[0].password) - 1);
        s_network_count = 1;
    }

    s_init_done = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void)
{
    if (s_network_count == 0) {
        ESP_LOGW(TAG, "No wifi networks configured");
        return ESP_ERR_NOT_FOUND;
    }

    int attempts = 0;
    while (attempts < s_network_count) {
        if (s_current_index >= s_network_count) s_current_index = 0;

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.sta.ssid, s_networks[s_current_index].ssid,
                sizeof(wifi_cfg.sta.ssid) - 1);
        strncpy((char *)wifi_cfg.sta.password, s_networks[s_current_index].password,
                sizeof(wifi_cfg.sta.password) - 1);
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_cfg.sta.pmf_cfg.capable = true;

        ESP_LOGI(TAG, "Connecting [%d]: %s", s_current_index, s_networks[s_current_index].ssid);

        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) return err;
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if (err != ESP_OK) return err;
        err = esp_wifi_start();
        if (err != ESP_OK) return err;
        err = esp_wifi_connect();
        if (err != ESP_OK) return err;

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to %s", s_networks[s_current_index].ssid);
            nvs_stl_set_u32(NVS_KEY_WIFI_CURRENT_IDX, (uint32_t)s_current_index);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Failed %s, trying next", s_networks[s_current_index].ssid);
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_current_index = (s_current_index + 1) % s_network_count;
        attempts++;
    }

    ESP_LOGE(TAG, "All wifi networks failed");
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_disconnect(void)
{
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK) {
        esp_wifi_stop();
        s_connected = false;
    }
    return err;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

int32_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

typedef struct {
    char *buffer;
    size_t size;
    size_t len;
} http_buf_t;

static esp_err_t http_get_handler(esp_http_client_event_t *evt)
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

esp_err_t wifi_manager_fetch_and_update_list(void)
{
    char device_id[64] = {0};
    size_t len = sizeof(device_id);
    esp_err_t err = nvs_stl_get_string(NVS_KEY_DEVICE_MAC, device_id, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

    char url[256];
    snprintf(url, sizeof(url), WIFI_CONFIG_URL_FMT, device_id);

    http_buf_t buf = {0};
    buf.size = 4096;
    buf.buffer = malloc(buf.size);
    if (!buf.buffer) return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_get_handler,
        .user_data = &buf,
        .timeout_ms = WIFI_CONNECT_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(buf.buffer);
        return ESP_FAIL;
    }

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buf.buffer);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Fetch wifi list: HTTP %d, %d bytes", status, buf.len);
    esp_http_client_cleanup(client);

    if (status != 200) {
        free(buf.buffer);
        return ESP_FAIL;
    }

    buf.buffer[buf.len] = '\0';
    cJSON *root = cJSON_Parse(buf.buffer);
    free(buf.buffer);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse wifi list response");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *networks = cJSON_GetObjectItem(root, "networks");
    if (!networks || !cJSON_IsArray(networks)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int count = cJSON_GetArraySize(networks);
    if (count > WIFI_LIST_MAX) count = WIFI_LIST_MAX;

    s_network_count = 0;
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(networks, i);
        if (!item) continue;
        cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
        cJSON *pwd = cJSON_GetObjectItem(item, "password");
        if (ssid && cJSON_IsString(ssid)) {
            strncpy(s_networks[s_network_count].ssid, ssid->valuestring,
                    sizeof(s_networks[s_network_count].ssid) - 1);
            if (pwd && cJSON_IsString(pwd)) {
                strncpy(s_networks[s_network_count].password, pwd->valuestring,
                        sizeof(s_networks[s_network_count].password) - 1);
            }
            s_network_count++;
        }
    }
    cJSON_Delete(root);

    s_list_version++;
    save_wifi_list();
    nvs_stl_set_u32(NVS_KEY_WIFI_LIST_VERSION, s_list_version);

    ESP_LOGI(TAG, "Fetched %d networks, version=%lu", s_network_count, (unsigned long)s_list_version);
    return ESP_OK;
}

esp_err_t wifi_manager_get_list(wifi_network_t *networks, int *count)
{
    if (!networks || !count) return ESP_ERR_INVALID_ARG;
    int copy_count = s_network_count;
    if (*count < copy_count) copy_count = *count;
    memcpy(networks, s_networks, copy_count * sizeof(wifi_network_t));
    *count = s_network_count;
    return ESP_OK;
}

esp_err_t wifi_manager_set_list(const wifi_network_t *networks, int count)
{
    if (!networks || count < 0 || count > WIFI_LIST_MAX) return ESP_ERR_INVALID_ARG;
    s_network_count = count;
    memcpy(s_networks, networks, count * sizeof(wifi_network_t));
    s_list_version++;
    save_wifi_list();
    nvs_stl_set_u32(NVS_KEY_WIFI_LIST_VERSION, s_list_version);
    return ESP_OK;
}

int wifi_manager_get_current_index(void)
{
    return s_current_index;
}

void wifi_manager_set_current_index(int idx)
{
    s_current_index = idx;
}

uint32_t wifi_manager_get_list_version(void)
{
    return s_list_version;
}

void wifi_manager_set_list_version(uint32_t ver)
{
    s_list_version = ver;
}
