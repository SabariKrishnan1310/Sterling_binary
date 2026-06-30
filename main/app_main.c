#include "config.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "wg_manager.h"
#include "provisioning.h"
#include "mqtt_manager.h"
#include "command_handler.h"
#include "rfid_reader.h"
#include "tap_publisher.h"
#include "telemetry.h"
#include "timesync.h"
#include "diagnostics.h"
#include "bulk_upload.h"
#include "ota_manager.h"
#include "led_controller.h"
#include "event_log.h"
#include "hmac_utils.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "main";

/* Forward declarations */
static void main_loop_task(void *pvParameters);
static void rfid_task(void *pvParameters);
static void telemetry_timer_cb(void *arg);
static void on_timesync_msg(const char *topic, const uint8_t *payload, int len);
static void on_reconnect_timer_cb(void *arg);
static bool provision_device(void);

/* Handles */
static TaskHandle_t s_rfid_task_h = NULL;
static esp_timer_handle_t s_telemetry_timer = NULL;
static esp_timer_handle_t s_timesync_timer = NULL;
static esp_timer_handle_t s_reconnect_timer = NULL;
static esp_timer_handle_t s_wifi_sync_timer = NULL;
static bool s_provisioned = false;
static bool s_wg_up = false;
static bool s_mqtt_connected = false;

/* MAC address storage */
static char s_mac_str[18] = {0};

/* =========================================================
 *  Get MAC address as string "AA:BB:CC:DD:EE:FF"
 * ========================================================= */
static void get_mac_string(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_mac_str, sizeof(s_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* =========================================================
 *  Provisioning (first boot or re-provision)
 * ========================================================= */
static bool provision_device(void)
{
    ESP_LOGI(TAG, "Starting provisioning...");
    led_controller_play(LED_PATTERN_PROVISIONING);

    /* Connect WiFi first */
    esp_err_t ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed");
        return false;
    }

    ret = wifi_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed during provisioning");
        return false;
    }

    /* Call provisioning API */
    provisioning_data_t prov_data;
    char fw_ver_str[16];
    snprintf(fw_ver_str, sizeof(fw_ver_str), "%d.%d.%d",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);

    ret = provisioning_call(s_mac_str, "SN001", "2.0", fw_ver_str, &prov_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Provisioning API call failed");
        return false;
    }

    ESP_LOGI(TAG, "Provisioned! Device ID: %s", prov_data.device_id);

    /* Store in NVS */
    ret = provisioning_store(&prov_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store provisioning data");
        return false;
    }

    /* Store WiFi networks if provided */
    if (prov_data.wifi_network_count > 0) {
        wifi_network_t nets[32];
        int count = prov_data.wifi_network_count / 2;
        for (int i = 0; i < count && i < 32; i++) {
            strncpy(nets[i].ssid, prov_data.wifi_networks[i * 2], 63);
            strncpy(nets[i].password, prov_data.wifi_networks[i * 2 + 1], 63);
        }
        wifi_manager_set_list(nets, count);
        wifi_manager_connect();
    }

    event_log_write(EVT_PROVISIONING);
    return true;
}

/* =========================================================
 *  WireGuard startup
 * ========================================================= */
static bool start_wireguard(void)
{
    provisioning_data_t data;
    if (provisioning_load(&data) != ESP_OK) {
        ESP_LOGE(TAG, "No provisioning data to load");
        return false;
    }

    wg_manager_set_config(data.wg_private_key, data.wg_address,
                          data.wg_server_pubkey, data.wg_endpoint);

    esp_err_t ret = wg_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WireGuard start failed");
        return false;
    }

    s_wg_up = wg_manager_is_up();
    if (s_wg_up) {
        event_log_write(EVT_WG_UP);
        led_controller_play(LED_PATTERN_WG_CONNECTING);
        ESP_LOGI(TAG, "WireGuard tunnel UP at %s", data.wg_address);
    } else {
        event_log_write(EVT_WG_DOWN);
    }
    return s_wg_up;
}

/* =========================================================
 *  MQTT startup
 * ========================================================= */
static void start_mqtt(void)
{
    mqtt_manager_init();
    mqtt_manager_start();
}

/* =========================================================
 *  MQTT event handler callback (called from mqtt_manager event loop)
 * ========================================================= */
static void on_mqtt_event(bool connected)
{
    s_mqtt_connected = connected;
    if (connected) {
        event_log_write(EVT_MQTT_CONNECTED);
        led_controller_play(LED_PATTERN_MQTT_CONNECTED);

        /* Subscribe to command topics */
        provisioning_data_t data;
        if (provisioning_load(&data) == ESP_OK) {
            char topic[128];
            snprintf(topic, sizeof(topic), "cmd/%s", data.mqtt_username);
            mqtt_manager_subscribe(topic, MQTT_CMD_QOS);
            snprintf(topic, sizeof(topic), "prov/%s", data.mqtt_username);
            mqtt_manager_subscribe(topic, 1);
            snprintf(topic, sizeof(topic), "firmware/%s", data.mqtt_username);
            mqtt_manager_subscribe(topic, 1);
        }

        /* Register command handler */
        command_handler_init();

        /* Register timesync handler */
        mqtt_manager_register_cb("timesync/", on_timesync_msg);

        /* Send initial ping and timesync */
        char ping_topic[128];
        provisioning_data_t pd;
        if (provisioning_load(&pd) == ESP_OK) {
            snprintf(ping_topic, sizeof(ping_topic), "ping/%s", pd.mqtt_username);
            mqtt_manager_publish(ping_topic, NULL, 0, 1, 0);
        }
        timesync_request();

        /* Start telemetry timer */
        if (s_telemetry_timer) {
            esp_timer_start_periodic(s_telemetry_timer, telemetry_get_interval() * 1000);
        }

        /* Flush stored taps */
        tap_publisher_publish_from_storage();

        /* Cancle reconnect timer */
        if (s_reconnect_timer) {
            esp_timer_stop(s_reconnect_timer);
        }

    } else {
        event_log_write(EVT_MQTT_DISCONNECTED);
        led_controller_play(LED_PATTERN_WAVE);

        /* Stop telemetry timer */
        if (s_telemetry_timer) {
            esp_timer_stop(s_telemetry_timer);
        }

        /* Start reconnect timer */
        if (s_reconnect_timer) {
            esp_timer_start_once(s_reconnect_timer, 5000000); /* 5s */
        }
    }
}

/* =========================================================
 *  MQTT event interceptor — wrap mqtt_manager events
 * ========================================================= */
/* =========================================================
 *  Timesync response handler
 * ========================================================= */
static void on_timesync_msg(const char *topic, const uint8_t *payload, int len)
{
    timesync_process_response(payload, len);
}

/* =========================================================
 *  Reconnection timer
 * ========================================================= */
static void on_reconnect_timer_cb(void *arg)
{
    if (!s_wg_up) {
        ESP_LOGI(TAG, "Reconnecting WireGuard...");
        if (wg_manager_restart() == ESP_OK) {
            s_wg_up = wg_manager_is_up();
        }
    }
    if (s_wg_up && !s_mqtt_connected) {
        ESP_LOGI(TAG, "Reconnecting MQTT...");
        mqtt_manager_start();
    }
}

/* =========================================================
 *  Telemetry timer callback
 * ========================================================= */
static void telemetry_timer_cb(void *arg)
{
    telemetry_send();
}

/* =========================================================
 *  Timesync timer callback (every hour)
 * ========================================================= */
static void timesync_timer_cb(void *arg)
{
    timesync_request();
}

/* =========================================================
 *  WiFi list sync timer (every 6 hours)
 * ========================================================= */
static void wifi_sync_timer_cb(void *arg)
{
    wifi_manager_fetch_and_update_list();
}

/* =========================================================
 *  WiFi event handler (from wifi_manager)
 * ========================================================= */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        event_log_write(EVT_WIFI_CONNECTED);
        diagnostics_set_status_bit(1, true); /* WiFi bit */
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        event_log_write(EVT_WIFI_DISCONNECTED);
        diagnostics_set_status_bit(1, false);
        led_controller_play(LED_PATTERN_WAVE);
        /* WiFi reconnection handled by wifi_manager internally */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP address");
    }
}

/* =========================================================
 *  RFID reader task
 * ========================================================= */
static void rfid_task(void *pvParameters)
{
    rfid_reader_init();
    diagnostics_set_status_bit(0, true); /* RFID OK */

    rfid_tag_t tag;

    while (1) {
        if (rfid_reader_read_tag(&tag)) {
            event_log_write(EVT_TAG_READ);
            led_controller_play(LED_PATTERN_TAG);
            tap_publisher_publish(tag.hex_uid, tag.rssi);
            vTaskDelay(pdMS_TO_TICKS(TAP_DEBOUNCE_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(50)); /* poll every 50ms */
    }
}

/* =========================================================
 *  Main loop task (watchdog feed, reconnection checks)
 * ========================================================= */
static void main_loop_task(void *pvParameters)
{
    while (1) {
        esp_task_wdt_reset();

        /* Check WireGuard status */
        if (s_wg_up && !wg_manager_is_up()) {
            ESP_LOGW(TAG, "WireGuard tunnel dropped, reconnecting...");
            s_wg_up = false;
            event_log_write(EVT_WG_DOWN);
            led_controller_play(LED_PATTERN_WG_CONNECTING);

            if (wg_manager_restart() == ESP_OK) {
                s_wg_up = wg_manager_is_up();
                if (s_wg_up) {
                    event_log_write(EVT_WG_UP);
                }
            }
        }

        /* Check MQTT status */
        if (s_wg_up && !mqtt_manager_is_connected() && s_mqtt_connected) {
            s_mqtt_connected = false;
            event_log_write(EVT_MQTT_DISCONNECTED);
        }
        if (s_wg_up && mqtt_manager_is_connected() && !s_mqtt_connected) {
            s_mqtt_connected = true;
            event_log_write(EVT_MQTT_CONNECTED);
            on_mqtt_event(true);
        }

        /* Update diagnostics flags */
        diagnostics_set_status_bit(2, s_wg_up);     /* WG */
        diagnostics_set_status_bit(3, mqtt_manager_is_connected()); /* MQTT */
        diagnostics_set_status_bit(4, true);         /* Storage */

        /* Check free heap */
        uint32_t free_heap = esp_get_free_heap_size();
        if (free_heap < 20480) {
            ESP_LOGW(TAG, "Low memory: %lu bytes, rebooting", free_heap);
            event_log_write(EVT_ERROR);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(WDT_FEED_INTERVAL_MS));
    }
}

/* =========================================================
 *  app_main — entry point
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " SterlingONE Firmware v" FW_VERSION_STR);
    ESP_LOGI(TAG, "========================================");

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupted, erasing...");
        esp_err_t err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }

    nvs_stl_init();

    /* Get MAC address */
    get_mac_string();
    ESP_LOGI(TAG, "MAC: %s", s_mac_str);

    /* Initialize subsystems */
    event_log_init();
    diagnostics_init();
    led_controller_init();
    ota_manager_init();
    bulk_upload_init();

    /* Read HMAC secret from NVS or use default */
    char hmac_key[HMAC_SECRET_MAX_LEN] = {0};
    size_t hmac_len = sizeof(hmac_key);
    if (nvs_stl_get_string(NVS_KEY_HMAC_SECRET, hmac_key, &hmac_len) != ESP_OK) {
        /* Use default for now */
        hmac_set_key((const uint8_t *)HMAC_SECRET_DEFAULT, strlen(HMAC_SECRET_DEFAULT));
        ESP_LOGW(TAG, "Using default HMAC secret (dev mode)");
    } else {
        hmac_set_key((const uint8_t *)hmac_key, hmac_len);
    }

    /* Create LED task */
    TaskHandle_t led_handle = NULL;
    xTaskCreatePinnedToCore(
        led_controller_task, "led_task", LED_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 1, &led_handle, 1);

    /* Create timers */
    esp_timer_create_args_t tel_args = {
        .callback = telemetry_timer_cb,
        .name = "telemetry"
    };
    esp_timer_create(&tel_args, &s_telemetry_timer);

    esp_timer_create_args_t ts_args = {
        .callback = timesync_timer_cb,
        .name = "timesync"
    };
    esp_timer_create(&ts_args, &s_timesync_timer);
    esp_timer_start_periodic(s_timesync_timer, (uint64_t)TIMESYNC_INTERVAL_MS * 1000);

    esp_timer_create_args_t rec_args = {
        .callback = on_reconnect_timer_cb,
        .name = "reconnect"
    };
    esp_timer_create(&rec_args, &s_reconnect_timer);

    esp_timer_create_args_t wifi_sync_args = {
        .callback = wifi_sync_timer_cb,
        .name = "wifi_sync"
    };
    esp_timer_create(&wifi_sync_args, &s_wifi_sync_timer);
    esp_timer_start_periodic(s_wifi_sync_timer, (uint64_t)WIFI_LIST_SYNC_INTERVAL_MS * 1000);

    /* Check if provisioned */
    s_provisioned = provisioning_is_provisioned();

    if (!s_provisioned) {
        ESP_LOGI(TAG, "Device not provisioned. Starting provisioning...");
        event_log_write(EVT_PROVISIONING);

        if (!provision_device()) {
            ESP_LOGE(TAG, "Provisioning failed. Entering retry loop.");
            /* Retry loop is handled inside provision_device with WiFi */
            /* For now, just restart after delay */
            vTaskDelay(pdMS_TO_TICKS(10000));
            esp_restart();
        }
        provisioning_mark_provisioned();
        s_provisioned = true;
        event_log_write(EVT_PROVISIONED);
    } else {
        ESP_LOGI(TAG, "Already provisioned, loading credentials...");
        /* Connect WiFi using stored list */
        wifi_manager_init();
        wifi_manager_connect();
    }

    /* Read telemetry interval from NVS */
    uint32_t tel_intvl = TELEMETRY_INTERVAL_MS;
    nvs_stl_get_u32(NVS_KEY_TELEMETRY_INTERVAL, &tel_intvl);
    telemetry_set_interval(tel_intvl);
    telemetry_init();

    /* Initialize tap publisher */
    tap_publisher_init();

    /* Start WireGuard */
    if (s_provisioned) {
        start_wireguard();
    }

    /* Start MQTT */
    if (s_wg_up) {
        start_mqtt();
    }

    /* Register WiFi event handler */
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         wifi_event_handler, NULL, &wifi_handler);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         wifi_event_handler, NULL, &wifi_handler);

    /* Create RFID reader task */
    xTaskCreatePinnedToCore(
        rfid_task, "rfid_task", RFID_STACK_SIZE, NULL,
        tskIDLE_PRIORITY + 3, &s_rfid_task_h, 1);

    /* Create main loop task */
    xTaskCreatePinnedToCore(
        main_loop_task, "main_loop", 4096, NULL,
        tskIDLE_PRIORITY + 1, NULL, 0);

    ESP_LOGI(TAG, "All systems initialized. System running.");
    ESP_LOGI(TAG, "  Core 0: main_loop, wifi, mqtt, timers");
    ESP_LOGI(TAG, "  Core 1: rfid, led");
    ESP_LOGI(TAG, "  Device ID: %s", provisioning_is_provisioned() ? "(provisioned)" : "(unprovisioned)");

    /* Main task exits — app_main must NOT return */
    vTaskDelete(NULL);
}
