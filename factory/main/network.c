#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "FACTORY_NETWORK";

#define WIFI_NVS_NAMESPACE "wifi_profiles"

static bool wifi_connected = false;
static SemaphoreHandle_t wifi_connect_sem = NULL;

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
static void wifi_connect_to_stored_profiles(void);
static void wifi_store_networks(cJSON *networks);

// WiFi initialization
// ============================================================
// CONNECT STA  (called AFTER softap_init has brought up APSTA + started WiFi)
// ============================================================
// softap_init() has already called esp_netif_init(), created the STA+AP
// netifs, esp_wifi_init(), set WIFI_MODE_APSTA, and esp_wifi_start(). Calling
// any of those again here would error, and switching to WIFI_MODE_STA would
// tear down the SoftAP (which hosts the dashboard). So this function ONLY
// registers the STA event handlers and connects to the stored/default WiFi
// profile. That gives the bootstrap internet access so it can download the
// main firmware from GitHub — while the SoftAP keeps running for the dashboard.
void wifi_connect_sta(void)
{
    ESP_LOGI(TAG, "Bringing up STA to reach the internet...");

    wifi_connect_sem = xSemaphoreCreateBinary();

    // Register STA event handlers (softap_init registers none of these)
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    // Connect to the stored/default WiFi profile (e.g. "JaayM34")
    wifi_connect_to_stored_profiles();
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        if (wifi_connect_sem) {
            xSemaphoreGive(wifi_connect_sem);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected");
        wifi_connected = false;
        esp_wifi_connect();
    }
}

// Connect to stored WiFi profiles (up to 250)
static void seed_default_profiles(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for seeding");
        return;
    }

    uint16_t count = 1;
    nvs_set_u16(handle, "count", count);
    nvs_set_str(handle, "ssid_0", "JaayM34");
    nvs_set_str(handle, "pwd_0", "manju@2809");
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Seeded default WiFi profile: JaayM34");
}

static void wifi_connect_to_stored_profiles(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }
    
    uint16_t profile_count = 0;
    err = nvs_get_u16(nvs, "count", &profile_count);
    if (err != ESP_OK || profile_count == 0) {
        ESP_LOGI(TAG, "No stored WiFi profiles — seeding defaults");
        nvs_close(nvs);
        seed_default_profiles();
        // Retry after seeding
        err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
        if (err != ESP_OK) return;
        if (nvs_get_u16(nvs, "count", &profile_count) != ESP_OK || profile_count == 0) {
            nvs_close(nvs);
            return;
        }
    }
    
    ESP_LOGI(TAG, "Found %d stored WiFi profiles", profile_count);
    
    for (int i = 0; i < profile_count && i < WIFI_MAX_PROFILES; i++) {
        char ssid_key[32], pwd_key[32];
        snprintf(ssid_key, sizeof(ssid_key), "ssid_%d", i);
        snprintf(pwd_key, sizeof(pwd_key), "pwd_%d", i);
        
        char ssid[64] = {0};
        char pwd[64] = {0};
        
        size_t ssid_len = sizeof(ssid);
        size_t pwd_len = sizeof(pwd);
        
        if (nvs_get_str(nvs, ssid_key, ssid, &ssid_len) != ESP_OK) continue;
        if (nvs_get_str(nvs, pwd_key, pwd, &pwd_len) != ESP_OK) continue;
        
        wifi_config_t wifi_config = {0};
        snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
        snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", pwd);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
        
        if (xSemaphoreTake(wifi_connect_sem, WIFI_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS)) {
            ESP_LOGI(TAG, "Connected: %s", ssid);
            nvs_close(nvs);
            return;
        }
        
        esp_wifi_disconnect();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    nvs_close(nvs);
    ESP_LOGE(TAG, "Failed to connect to any WiFi profile");
}

bool wifi_is_connected(void)
{
    return wifi_connected;
}

// Fetch WiFi config from API
esp_err_t wifi_fetch_config(void)
{
    ESP_LOGI(TAG, "Fetching config from %s", WIFI_API_URL);
    
    esp_http_client_config_t cfg = {
        .url = WIFI_API_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Bad HTTP status: %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char buf[4096] = {0};
    int total = 0;
    int r;
    while ((r = esp_http_client_read(client, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += r;
        esp_task_wdt_reset();
        if (total >= (int)sizeof(buf) - 2) break;
    }
    buf[total] = '\0';
    esp_http_client_cleanup(client);

    if (total == 0) return ESP_FAIL;

    cJSON *json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    // Extract timestamp and set IST time
    cJSON *ts_item = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    if (cJSON_IsNumber(ts_item)) {
        time_t server_ts = (time_t)ts_item->valuedouble;
        struct timeval tv = { server_ts, 0 };
        settimeofday(&tv, NULL);
        setenv("TZ", "IST-5:30", 1);
        tzset();
        ESP_LOGI(TAG, "Time synced from API: %lld", (long long)server_ts);
    }

    // Extract and store WiFi networks
    cJSON *networks = cJSON_GetObjectItemCaseSensitive(json, "networks");
    if (cJSON_IsArray(networks)) {
        wifi_store_networks(networks);
    }

    cJSON_Delete(json);
    return ESP_OK;
}

// Store networks to NVS
static void wifi_store_networks(cJSON *networks)
{
    if (!cJSON_IsArray(networks)) return;
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open: %s", esp_err_to_name(err));
        return;
    }
    
    nvs_erase_all(nvs);
    
    int count = 0;
    cJSON *net;
    cJSON_ArrayForEach(net, networks) {
        if (count >= WIFI_MAX_PROFILES) break;
        
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(net, "ssid");
        cJSON *pwd  = cJSON_GetObjectItemCaseSensitive(net, "password");
        
        if (cJSON_IsString(ssid) && cJSON_IsString(pwd)) {
            char key[32];
            snprintf(key, sizeof(key), "ssid_%d", count);
            nvs_set_str(nvs, key, ssid->valuestring);
            snprintf(key, sizeof(key), "pwd_%d", count);
            nvs_set_str(nvs, key, pwd->valuestring);
            count++;
        }
    }
    
    nvs_set_u16(nvs, "count", (uint16_t)count);
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Stored %d WiFi profiles", count);
}

// Force OTA update using manual HTTP + esp_ota_write with WDT feeding
esp_err_t ota_force_update(void)
{
    ESP_LOGI(TAG, "Starting OTA from %s", OTA_FIRMWARE_URL);

    esp_http_client_config_t cfg = {
        .url = OTA_FIRMWARE_URL,
        .timeout_ms = 30000,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Bad OTA HTTP status: %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Prepare OTA partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    char buf[1024];
    int total = 0;
    int r;
    while ((r = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            return err;
        }
        total += r;
        esp_task_wdt_reset();
    }

    esp_http_client_cleanup(client);

    if (r < 0) {
        ESP_LOGE(TAG, "HTTP read error during OTA");
        esp_ota_abort(ota_handle);
        return ESP_FAIL;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA successful (%d bytes), restarting...", total);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}
