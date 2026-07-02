#include "provision.h"
#include "config.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <string.h>

static const char *TAG = "PROVISION";

// NVS namespace for WiFi profiles
#define WIFI_NVS_NAMESPACE "wifi_profiles"

esp_err_t wifi_fetch_global_config(void)
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

    // ── Extract and set IST time (PRIMARY time source, replaces NTP) ──
    cJSON *ts_item = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    if (cJSON_IsNumber(ts_item)) {
        time_t server_ts = (time_t)ts_item->valuedouble;
        struct timeval tv = { server_ts, 0 };
        settimeofday(&tv, NULL);
        setenv("TZ", "IST-5:30", 1);
        tzset();
        ESP_LOGI(TAG, "✅ Time synced from API: %lld", (long long)server_ts);
    } else {
        ESP_LOGW(TAG, "No timestamp in API response — NTP fallback needed");
    }

    // ── Extract WiFi networks (UNLIMITED — store ALL of them) ──
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

void factory_reset(void)
{
    ESP_LOGW(TAG, "⚠️  FACTORY RESET INITIATED ⚠️");
    
    // ── Log the event ──
    // event_log_write(EVT_FACTORY_RESET);  // uncomment when event type added
    
    // ── Erase NVS (WiFi creds, sequence numbers, everything) ──
    nvs_flash_erase();
    ESP_LOGI(TAG, "NVS erased");
    
    // ── Erase LittleFS (buffered taps) ──
    // Call storage_deinit() or directly erase the partition
    const esp_partition_t *littlefs = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "littlefs");
    if (littlefs) {
        esp_partition_erase_range(littlefs, 0, littlefs->size);
        ESP_LOGI(TAG, "LittleFS erased");
    }
    
    // ── Set boot partition to FACTORY ──
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    
    if (factory) {
        esp_err_t e = esp_ota_set_boot_partition(factory);
        if (e == ESP_OK) {
            ESP_LOGI(TAG, "✅ Boot set to factory partition. Rebooting...");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Failed to set factory boot: %s", esp_err_to_name(e));
        }
    } else {
        ESP_LOGE(TAG, "FACTORY PARTITION NOT FOUND — cannot recover");
        // Last resort: try ota_0
        const esp_partition_t *ota0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        if (ota0) {
            esp_ota_set_boot_partition(ota0);
            esp_restart();
        }
    }
    
    // If we reach here, something is very wrong — infinite SOS
    while (1) {
        // hardware SOS blink
        for (int i = 0; i < 3; i++) {
            gpio_set_level(STATUS_LED, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        vTaskDelay(700 / portTICK_PERIOD_MS);
    }
}

// ── FACTORY TRIGGER DETECTION TASK ──
// This task monitors for conditions that should trigger factory recovery

#define CRASH_COUNTER_KEY "crash_cnt"
#define CRASH_TIME_KEY    "crash_ts"
#define MAX_CRASHES       5
#define CRASH_WINDOW_MS   600000  // 10 minutes

void factory_trigger_monitor_task(void *pvParameters)
{
    nvs_handle_t nvs;
    esp_err_t e = nvs_open("device", NVS_READWRITE, &nvs);
    if (e != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    
    // Read crash counter
    uint32_t crash_count = 0;
    uint32_t crash_time = 0;
    nvs_get_u32(nvs, CRASH_COUNTER_KEY, &crash_count);
    nvs_get_u32(nvs, CRASH_TIME_KEY, &crash_time);
    
    uint32_t now = esp_timer_get_time() / 1000;  // ms since boot
    
    // If first boot after crash or this is a fresh boot
    if (crash_count == 0) {
        // Normal boot — nothing to check
        nvs_close(nvs);
        vTaskDelete(NULL);
        return;
    }
    
    // Check if crashes happened within the window
    if (now < CRASH_WINDOW_MS) {
        // Within the window — check count
        if (crash_count >= MAX_CRASHES) {
            ESP_LOGE(TAG, "⚠️  %lu crashes in %lums — triggering factory recovery!",
                     (unsigned long)crash_count, (unsigned long)now);
            nvs_close(nvs);
            factory_reset();
            return;  // never reaches here
        }
    }
    
    // Reset crash counter — this boot succeeded
    nvs_set_u32(nvs, CRASH_COUNTER_KEY, 0);
    nvs_set_u32(nvs, CRASH_TIME_KEY, 0);
    nvs_commit(nvs);
    nvs_close(nvs);
    
    vTaskDelete(NULL);
}

// Call this when a crash is detected before reboot
void factory_register_crash(void)
{
    nvs_handle_t nvs;
    if (nvs_open("device", NVS_READWRITE, &nvs) != ESP_OK) return;
    
    uint32_t crash_count = 0;
    nvs_get_u32(nvs, CRASH_COUNTER_KEY, &crash_count);
    crash_count++;
    
    nvs_set_u32(nvs, CRASH_COUNTER_KEY, crash_count);
    nvs_set_u32(nvs, CRASH_TIME_KEY, esp_timer_get_time() / 1000);
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGE(TAG, "Crash #%lu registered", (unsigned long)crash_count);
}