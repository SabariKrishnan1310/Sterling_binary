#include "console.h"
#include "device.h"
#include "network.h"
#include "storage.h"
#include "event_log.h"
#include "ota.h"
#include "led.h"
#include "mqtt.h"
#include "wireguard.h"
#include "provision.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "console";

static char s_console_pw[64];
static int s_failed_attempts = 0;
static TickType_t s_lockout_until = 0;

// ================================================================
// PASSWORD AUTHENTICATION
// ================================================================

static esp_err_t load_password(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CONSOLE_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        strncpy(s_console_pw, CONSOLE_DEFAULT_PASSWORD, sizeof(s_console_pw) - 1);
        return err;
    }
    size_t len = sizeof(s_console_pw);
    err = nvs_get_str(h, CONSOLE_NVS_KEY_PASSWORD, s_console_pw, &len);
    if (err != ESP_OK) {
        strncpy(s_console_pw, CONSOLE_DEFAULT_PASSWORD, sizeof(s_console_pw) - 1);
    }
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t save_password(const char *pw)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CONSOLE_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, CONSOLE_NVS_KEY_PASSWORD, pw);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void uart_read_no_echo(char *buf, size_t len)
{
    int pos = 0;
    while (pos < (int)len - 1) {
        char c;
        int r = uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY);
        if (r != 1) continue;
        if (c == '\r' || c == '\n') {
            printf("\r\n");
            break;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else {
            buf[pos++] = c;
            printf("*");
            fflush(stdout);
        }
    }
    buf[pos] = '\0';
}

static bool authenticate(void)
{
    load_password();

    while (1) {
        if (s_failed_attempts >= CONSOLE_MAX_FAILED_ATTEMPTS) {
            TickType_t now = xTaskGetTickCount();
            if (now < s_lockout_until) {
                int rem = (s_lockout_until - now) * portTICK_PERIOD_MS / 1000;
                printf("\r\nLocked out. %d seconds remaining.\r\n", rem);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            s_failed_attempts = 0;
        }

        printf("\r\nPassword: ");
        fflush(stdout);

        char pw[64];
        uart_read_no_echo(pw, sizeof(pw));

        if (strcmp(pw, s_console_pw) == 0) {
            printf("Access granted.\r\n");
            s_failed_attempts = 0;
            return true;
        }

        printf("Access denied.\r\n");
        s_failed_attempts++;

        if (s_failed_attempts >= CONSOLE_MAX_FAILED_ATTEMPTS) {
            event_log_write(EVT_CONSOLE_ATTACK);
            s_lockout_until = xTaskGetTickCount() + pdMS_TO_TICKS(CONSOLE_LOCKOUT_SECONDS * 1000);
            printf("Too many failed attempts. Locked out for %d seconds.\r\n",
                   CONSOLE_LOCKOUT_SECONDS);
        }
    }
}



// ================================================================
// HELPERS
// ================================================================

static const char *auth_mode_str(wifi_auth_mode_t mode)
{
    switch (mode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
        default: return "?";
    }
}

// ================================================================
// COMMAND: status
// ================================================================

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char mac[18];
    device_get_mac_str(mac, sizeof(mac));

    printf("========================================\r\n");
    printf("  Sterling Prod  v%s\r\n", FW_VERSION);
    printf("  Device ID:     %s\r\n", device_get_id());
    printf("  MAC:           %s\r\n", mac);
    printf("  Uptime:        %lld s\r\n", esp_timer_get_time() / 1000000);

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        printf("  WiFi SSID:     %s\r\n", (char *)ap.ssid);
        printf("  WiFi RSSI:     %d dBm\r\n", ap.rssi);
        printf("  WiFi CH:       %d\r\n", ap.primary);
        esp_netif_t *ni = esp_netif_get_handle_from_ifkey("STA_DEF");
        if (ni) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(ni, &ip) == ESP_OK) {
                printf("  IP:            " IPSTR "\r\n", IP2STR(&ip.ip));
            }
        }
    } else {
        printf("  WiFi:          not connected\r\n");
    }

    printf("  MQTT:          %s\r\n", mqtt_is_connected() ? "connected" : "disconnected");
    wg_state_t wg = wg_get_state();
    printf("  WG:            %s\r\n",
           wg == WG_STATE_UP ? "UP" :
           wg == WG_STATE_DISABLED ? "DISABLED" :
           wg == WG_STATE_CONNECTING ? "CONNECTING" :
           wg == WG_STATE_DOWN ? "DOWN" : "FAILED");
    printf("  Pending taps:  %lu\r\n", storage_get_pending_count());
    printf("  Total taps:    %lu\r\n", storage_get_total_count());
    printf("  Free heap:     %lu bytes\r\n", esp_get_free_heap_size());
    printf("========================================\r\n");
    return 0;
}

// ================================================================
// COMMAND: reboot
// ================================================================

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

// ================================================================
// COMMAND: factory-reset
// ================================================================

static int cmd_factory_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Are you sure? (yes/no): ");
    fflush(stdout);

    char buf[16];
    int pos = 0;
    while (pos < (int)sizeof(buf) - 1) {
        char c;
        int r = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(5000));
        if (r != 1) {
            printf("\r\nTimeout. Aborted.\r\n");
            return 1;
        }
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            printf("\r\n");
            break;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) { pos--; printf("\b \b"); fflush(stdout); }
        } else {
            buf[pos++] = c;
            printf("%c", c);
            fflush(stdout);
        }
    }

    if (strcmp(buf, "yes") != 0) {
        printf("Factory reset aborted.\r\n");
        return 1;
    }

    printf("Erasing NVS...\r\n");
    nvs_flash_erase();

    printf("Unmounting LittleFS...\r\n");
    esp_vfs_littlefs_unregister("littlefs");

    printf("Restarting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

// ================================================================
// COMMAND: uptime
// ================================================================

static int cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int64_t us = esp_timer_get_time();
    int64_t sec = us / 1000000;
    int64_t min = sec / 60;
    int64_t hr = min / 60;
    printf("Uptime: %lld:%02lld:%02lld (%lld seconds)\r\n",
           hr, min % 60, sec % 60, sec);
    return 0;
}

// ================================================================
// COMMAND: heap
// ================================================================

static int cmd_heap(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Free heap:       %lu bytes\r\n", esp_get_free_heap_size());
    printf("Min free heap:   %lu bytes\r\n", esp_get_minimum_free_heap_size());
    printf("Largest block:   %u bytes\r\n",
           (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    return 0;
}

// ================================================================
// COMMAND: task-list
// ================================================================

static int cmd_task_list(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = malloc(sizeof(TaskStatus_t) * n);
    if (!tasks) {
        printf("malloc failed\r\n");
        return 1;
    }
    n = uxTaskGetSystemState(tasks, n, NULL);
    printf("%-20s %-6s %-4s %s\r\n", "Name", "State", "Prio", "StkHWM");
    for (UBaseType_t i = 0; i < n; i++) {
        const char *st = "?";
        switch (tasks[i].eCurrentState) {
            case eRunning:   st = "RUN"; break;
            case eReady:     st = "RDY"; break;
            case eBlocked:   st = "BLK"; break;
            case eSuspended: st = "SUS"; break;
            case eDeleted:   st = "DEL"; break;
            case eInvalid:   st = "INV"; break;
        }
        printf("%-20s %-6s %-4u %u\r\n",
               tasks[i].pcTaskName, st,
               (unsigned int)tasks[i].uxCurrentPriority,
               (unsigned int)tasks[i].usStackHighWaterMark);
    }
    free(tasks);
    return 0;
}

// ================================================================
// COMMAND: mac
// ================================================================

static int cmd_mac(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char mac[18];
    device_get_mac_str(mac, sizeof(mac));
    printf("MAC:       %s\r\n", mac);
    printf("Device ID: %s\r\n", device_get_id());
    return 0;
}

// ================================================================
// COMMAND: version
// ================================================================

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Sterling Prod v%s\r\n", FW_VERSION);
    return 0;
}

// ================================================================
// COMMAND: ble
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_end *end;
} ble_args;

static int cmd_ble(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ble_args);
    if (nerrors) {
        arg_print_errors(stdout, ble_args.end, NULL);
        return 1;
    }
    if (ble_args.cmd->count == 0 || strcmp(ble_args.cmd->sval[0], "status") == 0) {
        printf("BLE: ESP32 BLE hardware present. Use esp_ble.h APIs to manage.\r\n");
        printf("  BLE controller: ESP32 supports BLE v4.2 BR/EDR BLE\r\n");
    } else {
        printf("Usage: ble [status]\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: ota
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_str *url;
    struct arg_end *end;
} ota_args;

static int cmd_ota(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ota_args);
    if (nerrors) {
        arg_print_errors(stdout, ota_args.end, NULL);
        return 1;
    }

    const char *sub = ota_args.cmd->sval[0];

    if (strcmp(sub, "status") == 0) {
        const esp_partition_t *run = esp_ota_get_running_partition();
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        esp_ota_img_states_t st;
        printf("Running:  %s\r\n", run ? run->label : "?");
        printf("Next:     %s\r\n", next ? next->label : "?");
        if (run && esp_ota_get_state_partition(run, &st) == ESP_OK) {
            printf("State:    %s\r\n",
                   st == ESP_OTA_IMG_NEW ? "NEW" :
                   st == ESP_OTA_IMG_PENDING_VERIFY ? "PENDING_VERIFY" :
                   st == ESP_OTA_IMG_VALID ? "VALID" :
                   st == ESP_OTA_IMG_INVALID ? "INVALID" :
                   st == ESP_OTA_IMG_ABORTED ? "ABORTED" : "?");
        }
    } else if (strcmp(sub, "force") == 0) {
        ota_trigger_flag = true;
        printf("OTA trigger set. Check will run on next ota_task cycle.\r\n");
    } else if (strcmp(sub, "rollback") == 0) {
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        if (!next) {
            printf("No next partition found\r\n");
            return 1;
        }
        esp_err_t err = esp_ota_set_boot_partition(next);
        if (err == ESP_OK) {
            printf("Rollback to %s scheduled. Restarting...\r\n", next->label);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else {
            printf("Rollback failed: %s\r\n", esp_err_to_name(err));
        }
    } else if (strcmp(sub, "url") == 0) {
        if (ota_args.url->count == 0) {
            printf("Usage: ota url <firmware_url>\r\n");
            return 1;
        }
        nvs_handle_t h;
        esp_err_t err = nvs_open("ota", NVS_READWRITE, &h);
        if (err != ESP_OK) {
            printf("NVS open failed: %s\r\n", esp_err_to_name(err));
            return 1;
        }
        err = nvs_set_str(h, "firmware_url", ota_args.url->sval[0]);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
        if (err == ESP_OK) {
            printf("Custom firmware URL stored.\r\n");
        } else {
            printf("Failed to store URL: %s\r\n", esp_err_to_name(err));
        }
    } else {
        printf("Usage: ota status|force|rollback|url <url>\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: wifi
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_args;

static int cmd_wifi(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_args);
    if (nerrors) {
        arg_print_errors(stdout, wifi_args.end, NULL);
        return 1;
    }

    const char *sub = wifi_args.cmd->sval[0];

    if (strcmp(sub, "scan") == 0) {
        printf("Scanning...\r\n");
        esp_err_t err = esp_wifi_scan_start(NULL, true);
        if (err != ESP_OK) {
            printf("WiFi scan failed: %s\r\n", esp_err_to_name(err));
            return 1;
        }
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n == 0) {
            printf("No APs found.\r\n");
            return 0;
        }
        wifi_ap_record_t *recs = malloc(sizeof(wifi_ap_record_t) * n);
        if (!recs) {
            printf("malloc failed\r\n");
            return 1;
        }
        esp_wifi_scan_get_ap_records(&n, recs);
        printf("%-30s %-6s %-4s\r\n", "SSID", "RSSI", "AUTH");
        for (uint16_t i = 0; i < n; i++) {
            printf("%-30s %-6d %-4s\r\n",
                   (char *)recs[i].ssid, recs[i].rssi,
                   auth_mode_str(recs[i].authmode));
        }
        free(recs);
    } else if (strcmp(sub, "status") == 0) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            printf("SSID:    %s\r\n", (char *)ap.ssid);
            printf("BSSID:   %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   ap.bssid[0], ap.bssid[1], ap.bssid[2],
                   ap.bssid[3], ap.bssid[4], ap.bssid[5]);
            printf("RSSI:    %d dBm\r\n", ap.rssi);
            printf("Channel: %d\r\n", ap.primary);
            printf("Auth:    %s\r\n", auth_mode_str(ap.authmode));
            esp_netif_t *ni = esp_netif_get_handle_from_ifkey("STA_DEF");
            if (ni) {
                esp_netif_ip_info_t ip;
                if (esp_netif_get_ip_info(ni, &ip) == ESP_OK) {
                    printf("IP:      " IPSTR "\r\n", IP2STR(&ip.ip));
                    printf("GW:      " IPSTR "\r\n", IP2STR(&ip.gw));
                    printf("MASK:    " IPSTR "\r\n", IP2STR(&ip.netmask));
                }
            }
        } else {
            printf("WiFi not connected.\r\n");
        }
    } else if (strcmp(sub, "list") == 0) {
        nvs_handle_t h;
        esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h);
        if (err != ESP_OK) {
            printf("No WiFi profiles.\r\n");
            return 0;
        }
        uint8_t cnt = 0;
        if (nvs_get_u8(h, "count", &cnt) != ESP_OK) cnt = 0;
        for (uint8_t i = 0; i < cnt; i++) {
            char key[16], ssid[64], pwd[64];
            snprintf(key, sizeof(key), "ssid_%u", i);
            size_t len = sizeof(ssid);
            if (nvs_get_str(h, key, ssid, &len) == ESP_OK) {
                snprintf(key, sizeof(key), "pwd_%u", i);
                len = sizeof(pwd);
                if (nvs_get_str(h, key, pwd, &len) == ESP_OK) {
                    printf("  %d: SSID=%s PWD=****\r\n", i, ssid);
                }
            }
        }
        nvs_close(h);
    } else if (strcmp(sub, "add") == 0) {
        if (wifi_args.ssid->count == 0 || wifi_args.password->count == 0) {
            printf("Usage: wifi add <ssid> <password>\r\n");
            return 1;
        }
        nvs_handle_t h;
        esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err != ESP_OK) {
            printf("NVS open failed: %s\r\n", esp_err_to_name(err));
            return 1;
        }
        uint8_t cnt = 0;
        nvs_get_u8(h, "count", &cnt);
        if (cnt >= WIFI_MAX_PROFILES) {
            printf("Max %d profiles reached.\r\n", WIFI_MAX_PROFILES);
            nvs_close(h);
            return 1;
        }
        char key[16];
        snprintf(key, sizeof(key), "ssid_%u", cnt);
        nvs_set_str(h, key, wifi_args.ssid->sval[0]);
        snprintf(key, sizeof(key), "pwd_%u", cnt);
        nvs_set_str(h, key, wifi_args.password->sval[0]);
        cnt++;
        nvs_set_u8(h, "count", cnt);
        err = nvs_commit(h);
        nvs_close(h);
        if (err == ESP_OK) {
            printf("Profile %d added: %s\r\n", cnt - 1, wifi_args.ssid->sval[0]);
        } else {
            printf("Failed to save: %s\r\n", esp_err_to_name(err));
        }
    } else if (strcmp(sub, "remove") == 0) {
        if (wifi_args.ssid->count == 0) {
            printf("Usage: wifi remove <ssid>\r\n");
            return 1;
        }
        nvs_handle_t h;
        esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err != ESP_OK) {
            printf("NVS open failed\r\n");
            return 1;
        }

        uint8_t cnt = 0;
        nvs_get_u8(h, "count", &cnt);

        char target[64];
        strncpy(target, wifi_args.ssid->sval[0], sizeof(target) - 1);

        int found = -1;
        for (uint8_t i = 0; i < cnt; i++) {
            char key[16], ssid[64];
            snprintf(key, sizeof(key), "ssid_%u", i);
            size_t len = sizeof(ssid);
            if (nvs_get_str(h, key, ssid, &len) == ESP_OK) {
                if (strcmp(ssid, target) == 0) {
                    found = i;
                    break;
                }
            }
        }

        if (found < 0) {
            printf("SSID '%s' not found.\r\n", target);
            nvs_close(h);
            return 1;
        }

        for (int i = found; i < cnt - 1; i++) {
            char k1[16], k2[16], ssid[64], pwd[64];
            size_t len;
            snprintf(k1, sizeof(k1), "ssid_%u", i + 1);
            snprintf(k2, sizeof(k2), "ssid_%u", i);
            len = sizeof(ssid);
            if (nvs_get_str(h, k1, ssid, &len) == ESP_OK) nvs_set_str(h, k2, ssid);
            snprintf(k1, sizeof(k1), "pwd_%u", i + 1);
            snprintf(k2, sizeof(k2), "pwd_%u", i);
            len = sizeof(pwd);
            if (nvs_get_str(h, k1, pwd, &len) == ESP_OK) nvs_set_str(h, k2, pwd);
        }

        char key[16];
        snprintf(key, sizeof(key), "ssid_%u", cnt - 1);
        nvs_erase_key(h, key);
        snprintf(key, sizeof(key), "pwd_%u", cnt - 1);
        nvs_erase_key(h, key);

        cnt--;
        nvs_set_u8(h, "count", cnt);
        err = nvs_commit(h);
        nvs_close(h);
        if (err == ESP_OK) {
            printf("Profile '%s' removed.\r\n", target);
        } else {
            printf("Failed to commit: %s\r\n", esp_err_to_name(err));
        }
    } else {
        printf("Usage: wifi scan|status|list|add <ssid> <pass>|remove <ssid>\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: mqtt
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_end *end;
} mqtt_args;

static int cmd_mqtt(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&mqtt_args);
    if (nerrors) {
        arg_print_errors(stdout, mqtt_args.end, NULL);
        return 1;
    }

    const char *sub = mqtt_args.cmd->sval[0];

    if (strcmp(sub, "status") == 0) {
        printf("MQTT: %s\r\n", mqtt_is_connected() ? "connected" : "disconnected");
    } else if (strcmp(sub, "reconnect") == 0) {
        printf("Reconnecting MQTT...\r\n");
        mqtt_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_err_t err = mqtt_start();
        if (err == ESP_OK) {
            printf("MQTT start initiated.\r\n");
        } else {
            printf("MQTT start failed: %s\r\n", esp_err_to_name(err));
        }
    } else {
        printf("Usage: mqtt status|reconnect\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: wg
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_end *end;
} wg_args;

static int cmd_wg(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wg_args);
    if (nerrors) {
        arg_print_errors(stdout, wg_args.end, NULL);
        return 1;
    }

    const char *sub = wg_args.cmd->sval[0];

    if (strcmp(sub, "status") == 0) {
        wg_state_t s = wg_get_state();
        printf("WG state: %s\r\n",
               s == WG_STATE_UP ? "UP" :
               s == WG_STATE_DOWN ? "DOWN" :
               s == WG_STATE_CONNECTING ? "CONNECTING" :
               s == WG_STATE_FAILED ? "FAILED" :
               s == WG_STATE_DISABLED ? "DISABLED" : "?");
        printf("WG tunnel: %s\r\n", wg_is_up() ? "up" : "down");
    } else if (strcmp(sub, "reconnect") == 0) {
        printf("Reconnecting WG...\r\n");
        wg_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_err_t err = wg_start();
        if (err == ESP_OK) {
            printf("WG start initiated.\r\n");
        } else {
            printf("WG start failed: %s\r\n", esp_err_to_name(err));
        }
    } else {
        printf("Usage: wg status|reconnect\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: storage
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_end *end;
} storage_args;

static int cmd_storage(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&storage_args);
    if (nerrors) {
        arg_print_errors(stdout, storage_args.end, NULL);
        return 1;
    }

    const char *sub = NULL;
    if (storage_args.cmd->count > 0) sub = storage_args.cmd->sval[0];

    if (!sub || strcmp(sub, "dump") == 0 || strcmp(sub, "stats") == 0) {
        printf("Pending: %lu\r\n", storage_get_pending_count());
        printf("Total:   %lu\r\n", storage_get_total_count());
        size_t total = 0, used = 0;
        if (esp_littlefs_info("littlefs", &total, &used) == ESP_OK) {
            printf("LittleFS: %lu/%lu bytes used\r\n", (unsigned long)used, (unsigned long)total);
        }
    } else if (strcmp(sub, "clear") == 0) {
        printf("Clear all stored taps? (yes/no): ");
        fflush(stdout);
        char buf[16];
        int pos = 0;
        while (pos < (int)sizeof(buf) - 1) {
            char c;
            int r = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(5000));
            if (r != 1) { printf("\r\nTimeout.\r\n"); return 1; }
            if (c == '\r' || c == '\n') { buf[pos] = '\0'; printf("\r\n"); break; }
            if (c == '\b' || c == 127) { if (pos > 0) { pos--; printf("\b \b"); fflush(stdout); } }
            else { buf[pos++] = c; printf("%c", c); fflush(stdout); }
        }
        if (strcmp(buf, "yes") != 0) {
            printf("Clear aborted.\r\n");
            return 1;
        }
        FILE *f = fopen(STORAGE_FILE_PATH, "wb");
        if (f) fclose(f);
        nvs_handle_t h;
        if (nvs_open("storage_ns", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_key(h, "upload_csr");
            nvs_commit(h);
            nvs_close(h);
        }
        printf("Storage cleared. Reboot recommended.\r\n");
    } else if (strcmp(sub, "flush") == 0) {
        upload_force_flag = true;
        printf("Upload flush triggered.\r\n");
    } else {
        printf("Usage: storage [dump|clear|flush]\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: telemetry
// ================================================================

static int cmd_telemetry(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Telemetry: not implemented\r\n");
    return 0;
}

// ================================================================
// COMMAND: config
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_str *key;
    struct arg_str *value;
    struct arg_end *end;
} config_args;

static int cmd_config(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&config_args);
    if (nerrors) {
        arg_print_errors(stdout, config_args.end, NULL);
        return 1;
    }

    const char *sub = config_args.cmd->sval[0];

    if (strcmp(sub, "show") == 0) {
        printf("=== Device config ===\r\n");
        nvs_handle_t h;
        if (nvs_open("device", NVS_READONLY, &h) == ESP_OK) {
            nvs_iterator_t it = NULL;
            esp_err_t res = nvs_entry_find("nvs", "device", NVS_TYPE_ANY, &it);
            while (res == ESP_OK) {
                nvs_entry_info_t info;
                nvs_entry_info(it, &info);
                char val[128] = {0};
                size_t len = sizeof(val);
                esp_err_t er = nvs_get_str(h, info.key, val, &len);
                if (er != ESP_OK) {
                    uint8_t u8;
                    if (nvs_get_u8(h, info.key, &u8) == ESP_OK)
                        snprintf(val, sizeof(val), "%u", u8);
                }
                int mask = (strstr(info.key, "pw") || strstr(info.key, "password") ||
                           strstr(info.key, "secret")) ? 1 : 0;
                printf("  device.%s = %s\r\n", info.key, mask ? "****" : val);
                res = nvs_entry_next(&it);
            }
            nvs_release_iterator(it);
            nvs_close(h);
        }
    } else if (strcmp(sub, "set") == 0) {
        if (config_args.key->count == 0 || config_args.value->count == 0) {
            printf("Usage: config set <key> <value>\r\n");
            return 1;
        }
        nvs_handle_t h;
        esp_err_t err = nvs_open("device", NVS_READWRITE, &h);
        if (err != ESP_OK) {
            printf("NVS open failed: %s\r\n", esp_err_to_name(err));
            return 1;
        }
        err = nvs_set_str(h, config_args.key->sval[0], config_args.value->sval[0]);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
        if (err == ESP_OK) {
            printf("device.%s = %s\r\n", config_args.key->sval[0], config_args.value->sval[0]);
        } else {
            printf("Set failed: %s\r\n", esp_err_to_name(err));
        }
    } else {
        printf("Usage: config show|set <key> <value>\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: test
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_str *value;
    struct arg_end *end;
} test_args;

static int cmd_test(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&test_args);
    if (nerrors) {
        arg_print_errors(stdout, test_args.end, NULL);
        return 1;
    }

    const char *sub = test_args.cmd->sval[0];

    if (strcmp(sub, "led") == 0) {
        if (test_args.value->count == 0) {
            printf("Usage: test led boot|tag|failure|wave|idle\r\n");
            return 1;
        }
        led_pattern_t p;
        if (strcmp(test_args.value->sval[0], "boot") == 0) p = LED_PATTERN_BOOT;
        else if (strcmp(test_args.value->sval[0], "tag") == 0) p = LED_PATTERN_TAG;
        else if (strcmp(test_args.value->sval[0], "failure") == 0) p = LED_PATTERN_FAILURE;
        else if (strcmp(test_args.value->sval[0], "wave") == 0) p = LED_PATTERN_WAVE;
        else if (strcmp(test_args.value->sval[0], "idle") == 0) p = LED_PATTERN_IDLE;
        else {
            printf("Unknown pattern: %s\r\n", test_args.value->sval[0]);
            return 1;
        }
        esp_err_t err = led_send(p);
        printf("LED pattern '%s': %s\r\n", test_args.value->sval[0],
               err == ESP_OK ? "sent" : "failed");
    } else if (strcmp(sub, "tap") == 0) {
        if (test_args.value->count == 0) {
            printf("Usage: test tap <hex>\r\n");
            return 1;
        }
        const char *uid = test_args.value->sval[0];
        uint32_t seq = 0;
        esp_err_t err = storage_append_tap(uid, &seq);
        if (err == ESP_OK) {
            printf("Tap appended: seq=%lu uid=%s\r\n", (unsigned long)seq, uid);
        } else {
            printf("Storage append failed: %s\r\n", esp_err_to_name(err));
            return 1;
        }
        err = network_send_tap_single(uid);
        if (err == ESP_OK) {
            printf("Tap sent: uid=%s\r\n", uid);
        } else {
            printf("Tap send failed (offline?): %s\r\n", esp_err_to_name(err));
        }
    } else if (strcmp(sub, "ping") == 0) {
        printf("ping: not implemented on embedded target\r\n");
    } else {
        printf("Usage: test led <pattern>|tap <hex>|ping <host>\r\n");
    }
    return 0;
}

// ================================================================
// COMMAND: provision
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_end *end;
} provision_args;

static int cmd_provision(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&provision_args);
    if (nerrors) {
        arg_print_errors(stdout, provision_args.end, NULL);
        return 1;
    }

    if (provision_args.cmd->count > 0 && strcmp(provision_args.cmd->sval[0], "status") == 0) {
        printf("Provision status: %s\r\n",
               provision_is_done() ? "done" : "pending");
    } else {
        printf("Starting provisioning...\r\n");
        esp_err_t err = provision_do();
        if (err == ESP_OK) {
            printf("Provisioning completed.\r\n");
        } else {
            printf("Provisioning failed: %s\r\n", esp_err_to_name(err));
        }
    }
    return 0;
}

// ================================================================
// COMMAND: secret
// ================================================================

static struct {
    struct arg_str *cmd;
    struct arg_str *type;
    struct arg_str *value;
    struct arg_end *end;
} secret_args;

static int cmd_secret(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&secret_args);
    if (nerrors) {
        arg_print_errors(stdout, secret_args.end, NULL);
        return 1;
    }

    if (secret_args.cmd->count == 0 || strcmp(secret_args.cmd->sval[0], "set") != 0) {
        printf("Usage: secret set hmac|api <value>\r\n");
        return 1;
    }
    if (secret_args.type->count == 0 || secret_args.value->count == 0) {
        printf("Usage: secret set hmac|api <value>\r\n");
        return 1;
    }

    const char *nvs_key = NULL;
    if (strcmp(secret_args.type->sval[0], "hmac") == 0) nvs_key = "hmac_secret";
    else if (strcmp(secret_args.type->sval[0], "api") == 0) nvs_key = "api_secret";
    else {
        printf("Unknown secret type: %s (use hmac or api)\r\n", secret_args.type->sval[0]);
        return 1;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("device", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        printf("NVS open failed: %s\r\n", esp_err_to_name(err));
        return 1;
    }
    err = nvs_set_str(h, nvs_key, secret_args.value->sval[0]);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        printf("%s stored.\r\n", nvs_key);
    } else {
        printf("Store failed: %s\r\n", esp_err_to_name(err));
    }
    return 0;
}

// ================================================================
// COMMAND: passwd
// ================================================================

static int cmd_passwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Enter current password: ");
    fflush(stdout);
    char cur[64];
    uart_read_no_echo(cur, sizeof(cur));

    if (strcmp(cur, s_console_pw) != 0) {
        printf("Incorrect password.\r\n");
        return 1;
    }

    printf("Enter new password: ");
    fflush(stdout);
    char new1[64];
    uart_read_no_echo(new1, sizeof(new1));

    printf("Confirm new password: ");
    fflush(stdout);
    char new2[64];
    uart_read_no_echo(new2, sizeof(new2));

    if (strcmp(new1, new2) != 0) {
        printf("Passwords don't match.\r\n");
        return 1;
    }

    if (strlen(new1) < 4) {
        printf("Password too short (min 4 chars).\r\n");
        return 1;
    }

    esp_err_t err = save_password(new1);
    if (err == ESP_OK) {
        strncpy(s_console_pw, new1, sizeof(s_console_pw) - 1);
        printf("Password changed.\r\n");
    } else {
        printf("Failed to save password: %s\r\n", esp_err_to_name(err));
    }
    return 0;
}

// ================================================================
// COMMAND REGISTRATION
// ================================================================

static void register_commands(void)
{
    // help — built-in
    esp_console_register_help_command();

    // status
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "status",
        .help = "Print full device summary",
        .func = cmd_status,
    });

    // reboot
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "reboot",
        .help = "Restart the device",
        .func = cmd_reboot,
    });

    // factory-reset
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "factory-reset",
        .help = "Wipe NVS + LittleFS and restart",
        .func = cmd_factory_reset,
    });

    // uptime
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "uptime",
        .help = "Print system uptime",
        .func = cmd_uptime,
    });

    // heap
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "heap",
        .help = "Print heap memory stats",
        .func = cmd_heap,
    });

    // task-list
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "task-list",
        .help = "List all FreeRTOS tasks",
        .func = cmd_task_list,
    });

    // mac
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "mac",
        .help = "Print MAC address and device ID",
        .func = cmd_mac,
    });

    // version
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "version",
        .help = "Print firmware version",
        .func = cmd_version,
    });

    // ble
    ble_args.cmd = arg_str0(NULL, NULL, "<cmd>", "status");
    ble_args.end = arg_end(2);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "ble",
        .help = "BLE status",
        .func = cmd_ble,
        .argtable = &ble_args,
    });

    // ota
    ota_args.cmd = arg_str1(NULL, NULL, "<cmd>", "status|force|rollback|url");
    ota_args.url = arg_str0(NULL, NULL, "<url>", "firmware URL (for url subcommand)");
    ota_args.end = arg_end(3);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "ota",
        .help = "OTA commands",
        .func = cmd_ota,
        .argtable = &ota_args,
    });

    // wifi
    wifi_args.cmd = arg_str1(NULL, NULL, "<cmd>", "scan|status|list|add|remove");
    wifi_args.ssid = arg_str0(NULL, NULL, "<ssid>", "SSID");
    wifi_args.password = arg_str0(NULL, NULL, "<password>", "password");
    wifi_args.end = arg_end(4);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "wifi",
        .help = "WiFi commands",
        .func = cmd_wifi,
        .argtable = &wifi_args,
    });

    // mqtt
    mqtt_args.cmd = arg_str1(NULL, NULL, "<cmd>", "status|reconnect");
    mqtt_args.end = arg_end(2);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "mqtt",
        .help = "MQTT commands",
        .func = cmd_mqtt,
        .argtable = &mqtt_args,
    });

    // wg
    wg_args.cmd = arg_str1(NULL, NULL, "<cmd>", "status|reconnect");
    wg_args.end = arg_end(2);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "wg",
        .help = "WireGuard commands",
        .func = cmd_wg,
        .argtable = &wg_args,
    });

    // storage
    storage_args.cmd = arg_str0(NULL, NULL, "<cmd>", "dump|clear|flush");
    storage_args.end = arg_end(2);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "storage",
        .help = "Storage management",
        .func = cmd_storage,
        .argtable = &storage_args,
    });

    // telemetry
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "telemetry",
        .help = "Print telemetry data",
        .func = cmd_telemetry,
    });

    // config
    config_args.cmd = arg_str1(NULL, NULL, "<cmd>", "show|set");
    config_args.key = arg_str0(NULL, NULL, "<key>", "NVS key");
    config_args.value = arg_str0(NULL, NULL, "<value>", "NVS value");
    config_args.end = arg_end(4);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "config",
        .help = "NVS config get/set",
        .func = cmd_config,
        .argtable = &config_args,
    });

    // test
    test_args.cmd = arg_str1(NULL, NULL, "<cmd>", "led|tap|ping");
    test_args.value = arg_str0(NULL, NULL, "<value>", "pattern/hex/host");
    test_args.end = arg_end(3);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "test",
        .help = "Test commands",
        .func = cmd_test,
        .argtable = &test_args,
    });

    // provision
    provision_args.cmd = arg_str0(NULL, NULL, "<cmd>", "status (omit to run provisioning)");
    provision_args.end = arg_end(2);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "provision",
        .help = "Run provisioning or check status",
        .func = cmd_provision,
        .argtable = &provision_args,
    });

    // secret
    secret_args.cmd = arg_str1(NULL, NULL, "<cmd>", "set");
    secret_args.type = arg_str0(NULL, NULL, "<type>", "hmac|api");
    secret_args.value = arg_str0(NULL, NULL, "<value>", "secret value");
    secret_args.end = arg_end(4);
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "secret",
        .help = "Manage secrets",
        .func = cmd_secret,
        .argtable = &secret_args,
    });

    // passwd
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "passwd",
        .help = "Change console password",
        .func = cmd_passwd,
    });
}

// ================================================================
// PUBLIC API
// ================================================================

esp_err_t console_init(void)
{
    esp_console_config_t cfg = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 32,
        .hint_color = 39,
    };
    esp_err_t err = esp_console_init(&cfg);
    if (err != ESP_OK) return err;

    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "UART driver already installed");
    } else {
        ESP_ERROR_CHECK(err);
        ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_cfg));
        ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, -1, -1));
    }

    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(10);
    linenoiseSetMaxLineLen(cfg.max_cmdline_length);

    register_commands();

    ESP_LOGI(TAG, "Console initialized");
    return ESP_OK;
}

void console_task(void *pvParameters)
{
    (void)pvParameters;

    esp_err_t err = console_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Console init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Console task started");
    load_password();

    // 10-second boot window: show prompt, accept password
    printf("\r\n========================================\r\n");
    printf("  Sterling Edge v2.0  Console Access\r\n");
    printf("  Password: ");
    fflush(stdout);

    char pw[64] = {0};
    int pos = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10000);

    while (xTaskGetTickCount() < deadline) {
        char c;
        int r = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100));
        if (r == 1) {
            if (c == '\r' || c == '\n') {
                pw[pos] = '\0';
                if (pos > 0 && strcmp(pw, s_console_pw) == 0) {
                    printf("\r\nAccess granted.\r\n");
                    goto command_loop;
                }
                printf("\r\nWrong. Password: ");
                pos = 0;
                memset(pw, 0, sizeof(pw));
                fflush(stdout);
            } else if (c == '\b' || c == 127) {
                if (pos > 0) { pos--; pw[pos] = 0; printf("\b \b"); fflush(stdout); }
            } else if (pos < (int)sizeof(pw) - 1) {
                pw[pos++] = c;
                printf("*");
                fflush(stdout);
            }
        }
    }
    printf("\r\nContinuing silently. Type AT+Enter anytime.\r\n");
    printf("========================================\r\n");

    // Silent mode: AT+Enter triggers password prompt
    while (1) {
        int match = 0;
        while (1) {
            char c;
            int r = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(200));
            if (r != 1) { match = 0; continue; }
            if (c == 'A' && match == 0) match = 1;
            else if (c == 'T' && match == 1) match = 2;
            else if ((c == '\r' || c == '\n') && match == 2) {
                printf("\r\n>>> Console activated <<<\r\n");
                if (authenticate())
                    goto command_loop;
                printf("Returning to hatch...\r\n");
                break;
            }
            else match = (c == 'A') ? 1 : 0;
        }
    }

command_loop:
    printf("\r\nType 'help' for available commands.\r\n");
    while (1) {
        char *line = linenoise("> ");
        if (line == NULL) {
            printf("\r\nType AT+Enter to re-enter console.\r\n");
            break;
        }
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            int ret;
            esp_err_t e = esp_console_run(line, &ret);
            if (e == ESP_ERR_NOT_FOUND)
                printf("Unknown: %s\r\n", line);
            else if (e != ESP_OK)
                printf("Error: %s\r\n", esp_err_to_name(e));
        }
        linenoiseFree(line);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


