#include "wireguard.h"
#include "config.h"
#include "network.h"
#include "event_log.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "esp_wireguard.h"
#include <string.h>
#include <time.h>

static const char *TAG = "wireguard";

#define WG_NVS_NAMESPACE        "wg"
#define WG_LISTEN_PORT          51820
#define WG_ENDPOINT             "wg-sterling.sabarikrishnan.me"
#define WG_ENDPOINT_PORT        51820
#define WG_PERSISTENT_KEEPALIVE 25
#define WG_ALLOWED_IP_MASK      "255.255.255.0"
#define WG_CHECK_INTERVAL_MS    30000
#define WG_MAX_RETRIES          5
#define WG_RETRY_BACKOFF_MS     10000
#define WG_RECONNECT_DELAY_MS   1000
#define WG_TIME_SYNC_TIMEOUT_S  120
#define WG_TIME_SYNC_CHECK_MS   1000

static wireguard_ctx_t s_ctx = {0};
static bool s_initialized = false;
static bool s_running = false;
static wg_state_t s_state = WG_STATE_DISABLED;

static const char *wg_state_str(wg_state_t state)
{
    switch (state) {
        case WG_STATE_DISABLED:   return "DISABLED";
        case WG_STATE_CONNECTING: return "CONNECTING";
        case WG_STATE_UP:         return "UP";
        case WG_STATE_DOWN:       return "DOWN";
        case WG_STATE_FAILED:     return "FAILED";
        default:                  return "UNKNOWN";
    }
}

static esp_err_t read_nvs_string(nvs_handle_t handle, const char *key, char *buf, size_t buf_size)
{
    size_t len = buf_size;
    esp_err_t err = nvs_get_str(handle, key, buf, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS key '%s' not found: %s", key, esp_err_to_name(err));
    }
    return err;
}

static bool wg_config_exists(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;
    nvs_close(handle);
    return true;
}

static void wait_for_time_sync(void)
{
    time_t now;
    struct tm timeinfo;
    int attempts = 0;

    while (attempts < WG_TIME_SYNC_TIMEOUT_S) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            ESP_LOGI(TAG, "Time synced (RTC valid)");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(WG_TIME_SYNC_CHECK_MS));
        attempts++;
    }
    ESP_LOGW(TAG, "Time sync timeout after %ds — WG handshake may fail",
             WG_TIME_SYNC_TIMEOUT_S);
}

esp_err_t wg_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No WG config in NVS namespace '%s': %s",
                 WG_NVS_NAMESPACE, esp_err_to_name(err));
        s_state = WG_STATE_DISABLED;
        return ESP_ERR_NOT_FOUND;
    }

    char private_key[128] = {0};
    char address[64] = {0};
    char server_pubkey[128] = {0};

    bool ok = true;
    if (read_nvs_string(handle, "private_key", private_key, sizeof(private_key)) != ESP_OK) ok = false;
    if (read_nvs_string(handle, "address", address, sizeof(address)) != ESP_OK) ok = false;
    if (read_nvs_string(handle, "server_pubkey", server_pubkey, sizeof(server_pubkey)) != ESP_OK) ok = false;
    nvs_close(handle);

    if (!ok) {
        ESP_LOGE(TAG, "Incomplete WG config in NVS (need: private_key, address, server_pubkey)");
        s_state = WG_STATE_DISABLED;
        return ESP_ERR_INVALID_VERSION;
    }

    wireguard_config_t wg_config = ESP_WIREGUARD_CONFIG_DEFAULT();
    wg_config.private_key = private_key;
    wg_config.listen_port = WG_LISTEN_PORT;
    wg_config.public_key = server_pubkey;
    wg_config.allowed_ip = address;
    wg_config.allowed_ip_mask = WG_ALLOWED_IP_MASK;
    wg_config.endpoint = WG_ENDPOINT;
    wg_config.port = WG_ENDPOINT_PORT;
    wg_config.persistent_keepalive = WG_PERSISTENT_KEEPALIVE;

    err = esp_wireguard_init(&wg_config, &s_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_init failed: %s", esp_err_to_name(err));
        s_state = WG_STATE_FAILED;
        return err;
    }

    s_initialized = true;
    s_state = WG_STATE_DOWN;
    ESP_LOGI(TAG, "WG initialized: address=%s, peer=%s:%d, keepalive=%ds",
             address, WG_ENDPOINT, WG_ENDPOINT_PORT, WG_PERSISTENT_KEEPALIVE);
    return ESP_OK;
}

esp_err_t wg_start(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "wg_start: not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running) {
        return ESP_OK;
    }

    s_state = WG_STATE_CONNECTING;
    esp_err_t err = esp_wireguard_connect(&s_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_connect failed: %s", esp_err_to_name(err));
        s_state = WG_STATE_FAILED;
        return err;
    }

    s_running = true;
    ESP_LOGI(TAG, "WG connecting to %s:%d", WG_ENDPOINT, WG_ENDPOINT_PORT);
    return ESP_OK;
}

esp_err_t wg_stop(void)
{
    if (!s_running) return ESP_OK;

    esp_err_t err = esp_wireguard_disconnect(&s_ctx);
    s_running = false;
    s_state = WG_STATE_DOWN;
    xEventGroupClearBits(wifi_event_group, WG_UP_BIT);
    ESP_LOGI(TAG, "WG stopped");
    return err;
}

bool wg_is_up(void)
{
    if (!s_initialized || !s_running) return false;
    return esp_wireguardif_peer_is_up(&s_ctx) == ESP_OK;
}

wg_state_t wg_get_state(void)
{
    return s_state;
}

static void on_tunnel_up(void)
{
    if (s_state != WG_STATE_UP) {
        ESP_LOGI(TAG, "WG tunnel UP");
        s_state = WG_STATE_UP;
        event_log_write(EVT_WG_UP);
        xEventGroupSetBits(wifi_event_group, WG_UP_BIT);
    }
}

static void on_tunnel_down(void)
{
    if (s_state == WG_STATE_UP) {
        ESP_LOGW(TAG, "WG tunnel DOWN");
        event_log_write(EVT_WG_DOWN);
        xEventGroupClearBits(wifi_event_group, WG_UP_BIT);
    }
    s_state = WG_STATE_DOWN;
}

static void reconnect_wg(void)
{
    esp_wireguard_disconnect(&s_ctx);
    vTaskDelay(pdMS_TO_TICKS(WG_RECONNECT_DELAY_MS));
    esp_wireguard_connect(&s_ctx);
    s_state = WG_STATE_CONNECTING;
}

void wg_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WG task started, waiting for WiFi...");

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    while (!wg_config_exists()) {
        ESP_LOGI(TAG, "No WG config yet, waiting for provisioning...");
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group, PROVISION_DONE_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
        if (bits & PROVISION_DONE_BIT) break;

        EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
        if (!(wifi_bits & WIFI_CONNECTED_BIT)) {
            xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                pdFALSE, pdTRUE, portMAX_DELAY);
        }
    }

    wait_for_time_sync();

    esp_err_t err = wg_init();
    if (err != ESP_OK) {
        if (s_state == WG_STATE_DISABLED) {
            ESP_LOGI(TAG, "WG not configured — running without tunnel");
        } else {
            ESP_LOGE(TAG, "WG init failed: %s", esp_err_to_name(err));
        }
        while (1) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    err = wg_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WG start failed: %s", esp_err_to_name(err));
        while (1) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    int retry_count = 0;
    TickType_t last_check = xTaskGetTickCount();

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(5000));

        EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
        if (!(wifi_bits & WIFI_CONNECTED_BIT)) {
            if (s_state == WG_STATE_UP) {
                ESP_LOGW(TAG, "WiFi lost, WG down");
                on_tunnel_down();
            }
            retry_count = 0;
            xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                pdFALSE, pdTRUE, portMAX_DELAY);
            ESP_LOGI(TAG, "WiFi reconnected, restarting WG");
            reconnect_wg();
            last_check = xTaskGetTickCount();
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_check) < pdMS_TO_TICKS(WG_CHECK_INTERVAL_MS)) {
            continue;
        }
        last_check = now;

        bool up = wg_is_up();
        if (up) {
            on_tunnel_up();
            retry_count = 0;
        } else {
            on_tunnel_down();
            retry_count++;

            if (retry_count >= WG_MAX_RETRIES) {
                ESP_LOGE(TAG, "WG max retries (%d) exceeded, entering FAILED state",
                         WG_MAX_RETRIES);
                s_state = WG_STATE_FAILED;
                event_log_write(EVT_WG_FAILED);
                vTaskDelay(pdMS_TO_TICKS(WG_RETRY_BACKOFF_MS * 3));
                retry_count = 0;
            } else {
                ESP_LOGI(TAG, "WG reconnect attempt %d/%d (state=%s)",
                         retry_count, WG_MAX_RETRIES, wg_state_str(s_state));
            }
            reconnect_wg();
        }
    }
}
