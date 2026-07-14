#pragma once

// ======================================================
// STERLING BOOTSTRAP v2.0.0
// ======================================================

#define BOOTSTRAP_VERSION              "2.0.0"

// ======================================================
// DEVICE
// ======================================================

#define STATUS_LED                     27

// ======================================================
// SOFTAP — ALWAYS ACTIVE
// ======================================================

#define SOFTAP_SSID                    "Sterling"
#define SOFTAP_PASSWORD                "sterling123"
#define SOFTAP_CHANNEL                 1
#define SOFTAP_MAX_CONN                2
#define SOFTAP_IP_ADDR                "192.168.4.1"
#define SOFTAP_IP_GW                  "192.168.4.1"
#define SOFTAP_IP_NETMASK             "255.255.255.0"

// ======================================================
// HTTP SERVER
// ======================================================

#define HTTP_PORT                      80
#define HTTP_MAX_POST_SIZE             512
#define HTTP_TIMEOUT_MS                8000

// ======================================================
// OTA — PULLS FROM NEW REPO (Sterling_Prod)
// ======================================================

#define OTA_FIRMWARE_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/firmware.bin"

#define OTA_VERSION_URL \
"https://raw.githubusercontent.com/SabariKrishnan1310/Sterling_binary/main/version.txt"

// WiFi config API (used by wifi_fetch_config in network.c; kept defined so
// the recovery build compiles even though that helper is currently unused).
#define WIFI_API_URL "http://api.sabarikrishnan.me/api/v1/wifi"

// STA connect timeout (used by wifi_connect_to_stored_profiles in network.c).
#define WIFI_CONNECT_TIMEOUT_MS        15000

// ======================================================
// TX POWER — MAX FOR PCB TRACE ANTENNA
// ======================================================

#define WIFI_TX_POWER_MAX              78   // 19.5 dBm

// ======================================================
// WIFI PROFILES
// ======================================================

#define WIFI_MAX_PROFILES              10
#define WIFI_NVS_NAMESPACE             "wifi_profiles"
