#include "wg_manager.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wireguard.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wg";

static wireguard_ctx_t s_wg_ctx;
static wireguard_config_t s_wg_cfg;
static wg_state_t s_state = WG_STATE_UNPROVISIONED;
static bool s_configured = false;

static char s_private_key[128];
static char s_address[32];
static char s_server_pubkey[128];
static char s_endpoint_host[256];
static int  s_endpoint_port = 51820;

static void parse_endpoint(const char *endpoint, char *host, int host_size, int *port)
{
    const char *colon = strrchr(endpoint, ':');
    if (colon) {
        size_t host_len = colon - endpoint;
        if (host_len >= (size_t)host_size) host_len = host_size - 1;
        strncpy(host, endpoint, host_len);
        host[host_len] = '\0';
        *port = atoi(colon + 1);
        if (*port <= 0) *port = 51820;
    } else {
        strncpy(host, endpoint, host_size - 1);
        host[host_size - 1] = '\0';
    }
}

void wg_manager_set_config(const char *private_key, const char *address,
                           const char *server_pubkey, const char *endpoint)
{
    if (private_key) {
        strncpy(s_private_key, private_key, sizeof(s_private_key) - 1);
        s_private_key[sizeof(s_private_key) - 1] = '\0';
    }
    if (address) {
        strncpy(s_address, address, sizeof(s_address) - 1);
        s_address[sizeof(s_address) - 1] = '\0';
    }
    if (server_pubkey) {
        strncpy(s_server_pubkey, server_pubkey, sizeof(s_server_pubkey) - 1);
        s_server_pubkey[sizeof(s_server_pubkey) - 1] = '\0';
    }
    if (endpoint) {
        parse_endpoint(endpoint, s_endpoint_host, sizeof(s_endpoint_host), &s_endpoint_port);
    }

    s_state = WG_STATE_KEYS_STORED;
    s_configured = true;
    ESP_LOGI(TAG, "WireGuard config stored");
}

esp_err_t wg_manager_init(void)
{
    s_state = WG_STATE_UNPROVISIONED;
    s_configured = false;
    memset(&s_wg_ctx, 0, sizeof(s_wg_ctx));
    ESP_LOGI(TAG, "WireGuard manager initialized");
    return ESP_OK;
}

esp_err_t wg_manager_start(void)
{
    if (!s_configured) {
        ESP_LOGE(TAG, "Cannot start: not configured");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_wg_cfg, 0, sizeof(s_wg_cfg));
    s_wg_cfg.private_key = s_private_key;
    s_wg_cfg.listen_port = 0;
    s_wg_cfg.fw_mark = 0;
    s_wg_cfg.public_key = s_server_pubkey;
    s_wg_cfg.preshared_key = NULL;
    s_wg_cfg.allowed_ip = s_address;
    s_wg_cfg.allowed_ip_mask = "255.255.255.0";
    s_wg_cfg.endpoint = s_endpoint_host;
    s_wg_cfg.port = s_endpoint_port;
    s_wg_cfg.persistent_keepalive = WG_KEEPALIVE_S;

    esp_err_t err = esp_wireguard_init(&s_wg_cfg, &s_wg_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_init failed: %s", esp_err_to_name(err));
        s_state = WG_STATE_ERROR;
        return err;
    }

    err = esp_wireguard_connect(&s_wg_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_connect failed: %s", esp_err_to_name(err));
        s_state = WG_STATE_TUNNEL_DOWN;
        return err;
    }

    s_state = WG_STATE_TUNNEL_UP;
    ESP_LOGI(TAG, "WireGuard tunnel is up");
    return ESP_OK;
}

esp_err_t wg_manager_stop(void)
{
    esp_err_t err = esp_wireguard_disconnect(&s_wg_ctx);
    s_state = WG_STATE_TUNNEL_DOWN;
    ESP_LOGI(TAG, "WireGuard tunnel stopped");
    return err;
}

bool wg_manager_is_up(void)
{
    return s_state == WG_STATE_TUNNEL_UP;
}

wg_state_t wg_manager_get_state(void)
{
    return s_state;
}

esp_err_t wg_manager_restart(void)
{
    esp_err_t err = wg_manager_stop();
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(500));
    return wg_manager_start();
}
