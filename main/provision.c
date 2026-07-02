#include "provision.h"
#include "config.h"
#include "network.h"
#include "device.h"
#include "led.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "provision";
static int s_last_http_status = 0;

static uint32_t provision_get_backoff_ms(int attempt)
{
    static const uint32_t delays[] = {1000, 2000, 4000, 8000, 16000, 30000, 60000};
    int idx = attempt < 7 ? attempt : 6;
    uint32_t base = delays[idx];
    float jitter = 0.5f + (float)(esp_random() % 1000) / 2000.0f;
    return (uint32_t)((float)base * jitter);
}

bool provision_is_done(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t provisioned = 0;
    err = nvs_get_u8(handle, "provisioned", &provisioned);
    nvs_close(handle);

    return (err == ESP_OK && provisioned == 1);
}

esp_err_t provision_do(void)
{
    char mac_str[18];
    esp_err_t err = device_get_mac_str(mac_str, sizeof(mac_str));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddStringToObject(root, "serial", "");
    cJSON_AddStringToObject(root, "hw_ver", "");
    cJSON_AddStringToObject(root, "fw_ver", FW_VERSION);
    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_body) {
        ESP_LOGE(TAG, "Failed to build JSON body");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Provisioning with: %s", json_body);

    // Try HTTPS first, fall back to HTTP on connection failure.
    // NOTE: We use open()/fetch_headers()/read() instead of perform() because
    // perform() consumes the response internally and read() returns 0 after.
    const char *urls[] = { PROVISION_URL_HTTPS, PROVISION_URL_HTTP };
    int status = 0;
    char response_buf[2048] = {0};
    bool request_ok = false;

    for (int attempt_proto = 0; attempt_proto < 2; attempt_proto++) {
        esp_http_client_config_t cfg = {
            .url = urls[attempt_proto],
            .method = HTTP_METHOD_POST,
            .timeout_ms = HTTP_TIMEOUT_MS,
            .crt_bundle_attach = (attempt_proto == 0) ? esp_crt_bundle_attach : NULL,
            .skip_cert_common_name_check = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            free(json_body);
            return ESP_FAIL;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_body, strlen(json_body));

        err = esp_http_client_open(client, strlen(json_body));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Provision open (%s) failed: %s",
                     attempt_proto == 0 ? "HTTPS" : "HTTP", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            continue;
        }

        // Send the POST body
        int written = esp_http_client_write(client, json_body, strlen(json_body));
        if (written < (int)strlen(json_body)) {
            ESP_LOGW(TAG, "Provision write (%s) failed: %d",
                     attempt_proto == 0 ? "HTTPS" : "HTTP", written);
            esp_http_client_cleanup(client);
            continue;
        }

        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);

        memset(response_buf, 0, sizeof(response_buf));
        int total = 0;
        while (total < (int)sizeof(response_buf) - 1) {
            int r = esp_http_client_read(client, response_buf + total,
                                         sizeof(response_buf) - total - 1);
            if (r <= 0) break;
            total += r;
        }
        response_buf[total] = '\0';

        ESP_LOGI(TAG, "Provision response (%s): HTTP %d (%d bytes)",
                 attempt_proto == 0 ? "HTTPS" : "HTTP", status, total);
        request_ok = true;

        esp_http_client_cleanup(client);
        break;  // got a response, stop trying
    }

    free(json_body);

    if (!request_ok) {
        ESP_LOGE(TAG, "Provisioning failed on both HTTPS and HTTP");
        return ESP_FAIL;
    }

    s_last_http_status = status;

    if (status == 400) {
        ESP_LOGE(TAG, "FATAL: HTTP 400 - permanent provisioning failure");
        if (response_buf[0]) {
            ESP_LOGE(TAG, "Response body: %s", response_buf);
        }
        return ESP_FAIL;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Unexpected HTTP status: %d, body: %s", status, response_buf);
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(response_buf);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse provisioning JSON response (len=%zu): %s",
                 strlen(response_buf), response_buf[0] ? response_buf : "(empty)");
        return ESP_FAIL;
    }

    cJSON *device_id_item = cJSON_GetObjectItemCaseSensitive(json, "device_id");
    if (cJSON_IsString(device_id_item) && device_id_item->valuestring) {
        nvs_handle_t nvs;
        if (nvs_open("device", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "device_id", device_id_item->valuestring);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        device_refresh();
        ESP_LOGI(TAG, "Device ID set to: %s", device_get_id());
    }

    cJSON *wg = cJSON_GetObjectItemCaseSensitive(json, "wireguard");
    if (cJSON_IsObject(wg)) {
        nvs_handle_t nvs;
        if (nvs_open("wg", NVS_READWRITE, &nvs) == ESP_OK) {
            cJSON *item;
            item = cJSON_GetObjectItemCaseSensitive(wg, "private_key");
            if (cJSON_IsString(item)) nvs_set_str(nvs, "private_key", item->valuestring);

            item = cJSON_GetObjectItemCaseSensitive(wg, "address");
            if (cJSON_IsString(item)) nvs_set_str(nvs, "address", item->valuestring);

            item = cJSON_GetObjectItemCaseSensitive(wg, "server_public_key");
            if (cJSON_IsString(item)) nvs_set_str(nvs, "server_pubkey", item->valuestring);

            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }

    cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(json, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        nvs_handle_t nvs;
        if (nvs_open("mqtt", NVS_READWRITE, &nvs) == ESP_OK) {
            cJSON *item;
            item = cJSON_GetObjectItemCaseSensitive(mqtt, "host");
            if (cJSON_IsString(item)) nvs_set_str(nvs, "host", item->valuestring);

            item = cJSON_GetObjectItemCaseSensitive(mqtt, "port");
            if (cJSON_IsNumber(item)) nvs_set_i32(nvs, "port", (int32_t)item->valuedouble);

            item = cJSON_GetObjectItemCaseSensitive(mqtt, "username");
            if (cJSON_IsString(item)) nvs_set_str(nvs, "username", item->valuestring);

            item = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
            if (cJSON_IsString(item)) nvs_set_str(nvs, "password", item->valuestring);

            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }

    cJSON *wifi_networks = cJSON_GetObjectItemCaseSensitive(json, "wifi_networks");
    if (cJSON_IsArray(wifi_networks)) {
        nvs_handle_t nvs;
        if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_all(nvs);

            int count = 0;
            cJSON *net;
            cJSON_ArrayForEach(net, wifi_networks) {
                if (count >= WIFI_MAX_PROFILES) break;

                cJSON *ssid = cJSON_GetObjectItemCaseSensitive(net, "ssid");
                cJSON *pwd = cJSON_GetObjectItemCaseSensitive(net, "password");

                if (cJSON_IsString(ssid) && cJSON_IsString(pwd)) {
                    char key[20];
                    snprintf(key, sizeof(key), "ssid_%d", count);
                    nvs_set_str(nvs, key, ssid->valuestring);

                    snprintf(key, sizeof(key), "pwd_%d", count);
                    nvs_set_str(nvs, key, pwd->valuestring);

                    count++;
                }
            }

            nvs_set_u8(nvs, "count", (uint8_t)count);
            nvs_commit(nvs);
            nvs_close(nvs);

            ESP_LOGI(TAG, "Stored %d WiFi profiles from provisioning", count);
        }
    }

    cJSON_Delete(json);

    err = provision_mark_done();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark provisioning done in NVS");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Provisioning completed successfully");
    return ESP_OK;
}

esp_err_t provision_mark_done(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, "provisioned", 1);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t provision_clear(void)
{
    const char *namespaces[] = {"device", "wg", "mqtt"};
    for (int i = 0; i < 3; i++) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(namespaces[i], NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            nvs_erase_all(handle);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("device", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_u8(handle, "provisioned", 0);
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

void provision_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Provision task started, waiting for WiFi...");

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi connected, waiting for NTP time sync before provisioning...");
    network_wait_for_time_sync();

    if (provision_is_done()) {
        ESP_LOGI(TAG, "Already provisioned");
        xEventGroupSetBits(wifi_event_group, PROVISION_DONE_BIT);
        vTaskDelete(NULL);
    }

    int attempt = 0;
    while (1) {
        ESP_LOGI(TAG, "Starting provisioning attempt %d", attempt);

        esp_err_t err = provision_do();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Provisioning complete");
            xEventGroupSetBits(wifi_event_group, PROVISION_DONE_BIT);
            vTaskDelete(NULL);
        }

        if (s_last_http_status == 400) {
            ESP_LOGE(TAG, "Fatal provisioning error (HTTP 400), waiting 60s");
            led_send(LED_PATTERN_FAILURE);
            vTaskDelay(pdMS_TO_TICKS(60000));
        } else {
            uint32_t delay_ms = provision_get_backoff_ms(attempt);
            ESP_LOGW(TAG, "Provisioning failed, retrying in %lu ms", delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            attempt++;
        }
    }
}
