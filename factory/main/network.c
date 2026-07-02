#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "FACTORY_NETWORK";

// NVS namespace for WiFi profiles
#define WIFI_NVS_NAMESPACE "wifi_profiles"

// WiFi connection state
static bool wifi_connected = false;
static SemaphoreHandle_t wifi_connect_sem = NULL;

// WiFi initialization
void wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Initialize TCP/IP
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    // WiFi config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    // Event handler
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_register(ESP_EVENT_ANY_ID, ESP_EVENT_ANY_ID, &instance_any_id, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &instance_got_ip, wifi_event_handler, NULL);
    
    // Start WiFi
    esp_wifi_start();
    
    // Create semaphore for connection
    wifi_connect_sem = xSemaphoreCreateBinary();
    
    // Connect to stored WiFi profiles
    wifi_connect_to_stored_profiles();
}

// WiFi event handler
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: %s", ip4addr_ntoa(&event->ip_info.ip));
        wifi_connected = true;
        if (wifi_connect_sem) {
            xSemaphoreGive(wifi_connect_sem);
        }
    } else if (event_base == ESP_EVENT_ANY_ID && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected");
        wifi_connected = false;
        // Try to reconnect
        esp_wifi_connect();
    }
}

// Connect to stored WiFi profiles (up to 250)
void wifi_connect_to_stored_profiles(void)
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
        ESP_LOGI(TAG, "No stored WiFi profiles");
        nvs_close(nvs);
        return;
    }
    
    ESP_LOGI(TAG, "Found %d stored WiFi profiles", profile_count);
    
    // Try each profile
    for (int i = 0; i < profile_count && i < WIFI_MAX_PROFILES; i++) {
        char ssid_key[32];
        char pwd_key[32];
        snprintf(ssid_key, sizeof(ssid_key), "ssid_%d", i);
        snprintf(pwd_key, sizeof(pwd_key), "pwd_%d", i);
        
        char ssid[64] = {0};
        char pwd[64] = {0};
        
        size_t ssid_len = sizeof(ssid);
        size_t pwd_len = sizeof(pwd);
        
        err = nvs_get_str(nvs, ssid_key, ssid, &ssid_len);
        if (err != ESP_OK) continue;
        
        err = nvs_get_str(nvs, pwd_key, pwd, &pwd_len);
        if (err != ESP_OK) continue;
        
        // Configure WiFi
        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, pwd, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        
        // Set and connect
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_connect();
        
        // Wait for connection (with timeout)
        if (xSemaphoreTake(wifi_connect_sem, WIFI_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS)) {
            ESP_LOGI(TAG, "Connected to WiFi: %s", ssid);
            nvs_close(nvs);
            return;
        }
        
        // Disconnect and try next profile
        esp_wifi_disconnect();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    nvs_close(nvs);
    ESP_LOGE(TAG, "Failed to connect to any WiFi profile");
}

// Check if WiFi is connected
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
        ESP_LOGE(TAG, "Failed to open HTTP: %s", esp_err_to_name(err));
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
        if (total >= (int)sizeof(buf) - 2) break;
    }
    buf[total] = '\0';
    esp_http_client_cleanup(client);

    if (total == 0) {
        ESP_LOGE(TAG, "Empty response");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    // Extract timestamp and set time
    cJSON *ts_item = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    if (cJSON_IsNumber(ts_item)) {
        time_t server_ts = (time_t)ts_item->valuedouble;
        struct timeval tv = { server_ts, 0 };
        settimeofday(&tv, NULL);
        setenv("TZ", "IST-5:30", 1);
        tzset();
        ESP_LOGI(TAG, "✅ Time synced from API: %lld", (long long)server_ts);
    } else {
        ESP_LOGW(TAG, "No timestamp in API response");
    }

    // Extract WiFi networks
    cJSON *networks = cJSON_GetObjectItemCaseSensitive(json, "networks");
    if (cJSON_IsArray(networks)) {
        nvs_handle_t nvs;
        esp_err_t nvs_err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
        
        if (nvs_err == ESP_OK) {
            // Erase old profiles
            nvs_erase_all(nvs);
            
            int count = 0;
            cJSON *net;
            cJSON_ArrayForEach(net, networks) {
                if (count >= WIFI_MAX_PROFILES) {
                    ESP_LOGW(TAG, "Reached max %d profiles, stopping", WIFI_MAX_PROFILES);
                    break;
                }
                
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
            
            ESP_LOGI(TAG, "✅ Stored %d WiFi profiles from API", count);
        } else {
            ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(nvs_err));
        }
    } else {
        ESP_LOGW(TAG, "No networks array in API response");
    }

    cJSON_Delete(json);
    return ESP_OK;
}

// Store networks to NVS
void wifi_store_networks(cJSON *networks)
{
    if (!cJSON_IsArray(networks)) return;
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }
    
    // Erase old profiles
    nvs_erase_all(nvs);
    
    int count = 0;
    cJSON *net;
    cJSON_ArrayForEach(net, networks) {
        if (count >= WIFI_MAX_PROFILES) {
            ESP_LOGW(TAG, "Reached max %d profiles, stopping", WIFI_MAX_PROFILES);
            break;
        }
        
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
    
    ESP_LOGI(TAG, "✅ Stored %d WiFi profiles", count);
}

// Force OTA update
esp_err_t ota_force_update(void)
{
    ESP_LOGI(TAG, "Starting OTA update from %s", OTA_FIRMWARE_URL);
    
    // Download firmware
    esp_http_client_config_t cfg = {
        .url = OTA_FIRMWARE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP: %s", esp_err_to_name(err));
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

    // Get partition for OTA
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Erase partition
    esp_partition_erase_range(ota_partition, 0, ota_partition->size);
    
    // Write data
    size_t total = 0;
    int r;
    char buf[4096];
    FILE *f = fopen(ota_partition->label, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open partition for writing");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    while ((r = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, r, f);
        total += r;
    }
    fclose(f);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Downloaded %d bytes to OTA partition", total);

    // Verify with SHA256
    esp_ota_handle_t ota_handle;
    esp_err_t verify_err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (verify_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(verify_err));
        return verify_err;
    }

    // Read back and verify
    FILE *verify_f = fopen(ota_partition->label, "rb");
    if (!verify_f) {
        ESP_LOGE(TAG, "Failed to open partition for verification");
        esp_ota_abort(ota_handle);
        return ESP_FAIL;
    }

    char verify_buf[4096];
    while ((r = fread(verify_buf, 1, sizeof(verify_buf), verify_f)) > 0) {
        esp_ota_write(ota_handle, verify_buf, r);
    }
    fclose(verify_f);

    verify_err = esp_ota_end(ota_handle);
    if (verify_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(verify_err));
        return verify_err;
    }

    ESP_LOGI(TAG, "OTA verification successful");
    return ESP_OK;
}