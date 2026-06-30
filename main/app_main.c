/*
 * SterlingONE OTA Recovery v2.0.3
 *
 * TINY firmware — does ONE thing: WiFi + OTA to latest full firmware.
 * No RFID, MQTT, WireGuard, LED, anything else.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "recovery";

#define WIFI_SSID       "JaayM34"
#define WIFI_PASS       "manju@2809"
#define OTA_FW_URL      "https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/firmware-full.bin"
#define LOCAL_VERSION   "2.0.3"

#define WIFI_CONNECTED_BIT  BIT0
static EventGroupHandle_t s_wifi_group;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        xEventGroupClearBits(s_wifi_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_connect(void)
{
    s_wifi_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t handler_any;
    esp_event_handler_instance_t handler_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &handler_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &handler_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi %s...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_group, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi timeout");
        esp_restart();
    }
}

static void perform_ota(void)
{
    ESP_LOGI(TAG, "Starting OTA from %s", OTA_FW_URL);

    /* Remove from WDT supervision so download won't get killed */
    esp_task_wdt_delete(NULL);

    esp_http_client_config_t http_cfg = {
        .url = OTA_FW_URL,
        .timeout_ms = 120000,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA success, rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        /* Re-add WDT and retry */
        esp_task_wdt_add(NULL);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " SterlingONE OTA Recovery v" LOCAL_VERSION);
    ESP_LOGI(TAG, "========================================");

    ESP_ERROR_CHECK(nvs_flash_init());

    /* Register current task for WDT so we can feed it */
    esp_task_wdt_add(NULL);

    wifi_connect();

    /* Feed WDT before OTA */
    esp_task_wdt_reset();
    perform_ota();

    /* If OTA failed, reboot and retry */
    ESP_LOGE(TAG, "OTA failed, rebooting in 5s...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
