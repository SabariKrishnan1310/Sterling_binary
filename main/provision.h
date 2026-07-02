#pragma once
#include "esp_err.h"

/**
 * Fetch WiFi config + IST time from server API.
 * Called ONCE after WiFi connects.
 * Sets system time via settimeofday(), stores WiFi networks in NVS.
 * On failure: returns ESP_FAIL, caller should fall back to NTP.
 */
esp_err_t wifi_fetch_global_config(void);

/**
 * Factory reset: erase NVS + LittleFS, set boot to factory partition, restart.
 * This is the ULTIMATE self-heal. Device goes to factory recovery firmware
 * which re-provisions everything fresh from GitHub.
 */
void factory_reset(void);