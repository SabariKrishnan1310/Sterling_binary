#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    char device_id[64];
    char wg_private_key[128];
    char wg_address[32];
    char wg_server_pubkey[128];
    char wg_endpoint[128];
    char mqtt_username[64];
    char mqtt_password[128];
    int  wifi_network_count;
    char wifi_networks[32][64];  /* alternating ssid,password */
} provisioning_data_t;

esp_err_t provisioning_call(const char *mac, const char *serial,
                            const char *hw_ver, const char *fw_ver,
                            provisioning_data_t *out);
esp_err_t provisioning_store(const provisioning_data_t *data);
esp_err_t provisioning_load(provisioning_data_t *data);
bool      provisioning_is_provisioned(void);
void      provisioning_mark_provisioned(void);
void      provisioning_clear(void);
