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

/**
 * Factory trigger monitor task — checks crash loop detection on boot.
 * If >= 5 crashes within 10 minutes, triggers factory recovery.
 */
void factory_trigger_monitor_task(void *pvParameters);

/**
 * Register a crash before reboot. Increments NVS crash counter.
 */
void factory_register_crash(void);