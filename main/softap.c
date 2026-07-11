// ============================================================
// STERLING — EMERGENCY SOFTAP COMMAND CENTER
// ============================================================
// Simple web dashboard for:
//   1. WiFi scan + connect with live debug
//   2. Manage saved WiFi profiles (add/delete)
//   3. View device logs
//   4. System info + reboot
// Captive portal: redirects all requests to dashboard
// SSID: "Sterling"  Pass: "sterling123"
// ============================================================

#include "softap.h"
#include "config.h"
#include "network.h"
#include "storage.h"
#include "event_log.h"
#include "led.h"
#include "provision.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "softap";

static httpd_handle_t s_server = NULL;
static bool s_softap_active = false;
static bool s_connecting = false;

// Helper: send JSON response safely
static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

// ═══════════════════════════════════════════════════
// EMBEDDED DASHBOARD HTML — Light Theme, Sterling Blue
// ═══════════════════════════════════════════════════

static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Sterling</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:#f5f7fa;color:#1a1a2e;padding:16px;max-width:600px;margin:0 auto}"
"h1{color:#0066ff;font-size:22px;margin-bottom:4px}"
".sub{color:#666;font-size:13px;margin-bottom:16px}"
"h2{color:#0066ff;font-size:16px;margin:16px 0 8px}"
".card{background:#fff;border:1px solid #e0e4ea;border-radius:12px;padding:16px;margin:10px 0;"
"box-shadow:0 2px 8px rgba(0,0,0,0.05)}"
".status{display:flex;gap:12px;flex-wrap:wrap}"
".stat{flex:1;min-width:100px}"
".stat-label{color:#666;font-size:10px;text-transform:uppercase;letter-spacing:0.5px}"
".stat-value{color:#0066ff;font-size:16px;font-weight:bold}"
".stat-value.warn{color:#ff9800}"
".stat-value.crit{color:#f44336}"
".stat-value.ok{color:#4caf50}"
"button{background:#0066ff;color:#fff;border:none;padding:10px 20px;border-radius:8px;"
"font-weight:600;cursor:pointer;margin:4px;font-size:13px;transition:all 0.2s}"
"button:hover{background:#0052cc;transform:translateY(-1px)}"
"button:active{transform:translateY(0)}"
"button.danger{background:#f44336}"
"button.danger:hover{background:#d32f2f}"
"button.success{background:#4caf50}"
"button.success:hover{background:#388e3c}"
"button.warning{background:#ff9800}"
"button.warning:hover{background:#f57c00}"
"button:disabled{background:#ccc;cursor:not-allowed;transform:none}"
"input{background:#fff;color:#1a1a2e;border:1px solid #d0d5dd;padding:10px 12px;"
"border-radius:8px;font-size:14px;width:100%;margin:4px 0}"
"input:focus{outline:none;border-color:#0066ff;box-shadow:0 0 0 3px rgba(0,102,255,0.1)}"
"pre{background:#f0f2f5;padding:12px;border-radius:8px;overflow-x:auto;"
"font-size:11px;max-height:300px;overflow-y:auto;border:1px solid #e0e4ea}"
"table{width:100%;border-collapse:collapse;font-size:13px}"
"td,th{padding:8px 10px;border-bottom:1px solid #e0e4ea;text-align:left}"
"th{color:#666;text-transform:uppercase;font-size:10px;letter-spacing:0.5px}"
".btn-row{display:flex;gap:8px;flex-wrap:wrap}"
".rssi-bar{display:inline-block;width:8px;border-radius:2px;margin-left:4px;vertical-align:middle}"
".spinner{display:inline-block;width:14px;height:14px;border:2px solid #ccc;"
"border-top-color:#0066ff;border-radius:50%;animation:spin .6s linear infinite;margin-right:8px}"
"@keyframes spin{to{transform:rotate(360deg)}}"
".log-line{font-size:11px;padding:1px 0;border-bottom:1px solid #f0f2f5;font-family:monospace}"
"</style></head><body>"

"<h1>&#9889; Sterling</h1>"
"<div class='sub'>v" FW_VERSION " &mdash; Emergency WiFi Setup</div>"

"<div class='card' id='statusCard'>"
"<h2>Device Status</h2>"
"<div class='status' id='status'><div class='stat'><div class='stat-label'>Loading...</div></div></div>"
"</div>"

"<div class='card'>"
"<h2>WiFi Scan</h2>"
"<button onclick='scanWifi()' id='scanBtn'>Scan Networks</button>"
"<div id='scanresults'><pre>Tap Scan to find networks</pre></div>"
"</div>"

"<div class='card'>"
"<h2>Connect to WiFi</h2>"
"<input id='ssid' placeholder='Network name (SSID)'>"
"<input id='pwd' type='password' placeholder='Password'>"
"<div class='btn-row'>"
"<button onclick='connectWifi()' class='success' id='connectBtn'>Connect</button>"
"</div>"
"<div id='connectResult' style='margin-top:8px'></div>"
"<div id='connectDebug' style='display:none;margin-top:8px'>"
"<pre id='debugLog' style='max-height:200px'>Connecting...</pre>"
"</div>"
"</div>"

"<div class='card'>"
"<h2>Saved Profiles</h2>"
"<button onclick='listProfiles()'>Refresh</button>"
"<div id='profiles'><pre>Loading...</pre></div>"
"</div>"

"<div class='card'>"
"<h2>Logs</h2>"
"<button onclick='refreshLogs()'>Refresh</button>"
"<pre id='logOutput' style='max-height:250px'>Loading...</pre>"
"</div>"

"<div class='card'>"
"<h2>System</h2>"
"<div class='btn-row'>"
"<button onclick='getDiag()'>Diagnostics</button>"
"<button onclick='sendCmd(\"reboot\")' class='warning'>Reboot</button>"
"<button onclick='sendCmd(\"factory_reset\")' class='danger'>Factory Reset</button>"
"</div>"
"<div id='sysResult' style='margin-top:8px'>"
"<pre id='diagOutput'>Tap Diagnostics to load</pre>"
"</div>"
"</div>"

"<script>"
"function scanWifi(){"
"document.getElementById('scanBtn').disabled=true;"
"document.getElementById('scanBtn').innerHTML='<span class=spinner></span>Scanning...';"
"fetch('/api/wifi/scan').then(function(r){return r.json()}).then(function(d){"
"document.getElementById('scanBtn').disabled=false;"
"document.getElementById('scanBtn').textContent='Scan Networks';"
"if(d.error){document.getElementById('scanresults').innerHTML='<pre>Error: '+d.error+'</pre>';return}"
"var h='<table><tr><th>Network</th><th>Signal</th><th>Security</th><th></th></tr>';"
"d.networks.forEach(function(n,i){"
"var bars=n.rssi>-50?4:n.rssi>-60?3:n.rssi>-70?2:1;"
"var color=bars>=3?'#4caf50':bars>=2?'#ff9800':'#f44336';"
"h+='<tr><td>'+n.ssid+'</td><td>'+n.rssi+' dBm <span class=rssi-bar style=\"background:'+color+';height:'+(bars*4+4)+'px\"></span></td>'"
"+'<td>'+n.auth+'</td><td><button onclick=\"pickSsid(\\''+n.ssid.replace(/'/g,'\\\\u0027')+'\\')\">Select</button></td></tr>';"
"});h+='</table>';document.getElementById('scanresults').innerHTML=h;"
"}).catch(function(){document.getElementById('scanBtn').disabled=false;"
"document.getElementById('scanBtn').textContent='Scan Networks'})}"
"function pickSsid(s){document.getElementById('ssid').value=s}"

"function connectWifi(){"
"var ssid=document.getElementById('ssid').value.trim();"
"var pwd=document.getElementById('pwd').value;"
"if(!ssid){document.getElementById('connectResult').innerHTML="
"'<span style=color:#f44336>Enter SSID</span>';return}"
"document.getElementById('connectBtn').disabled=true;"
"document.getElementById('connectBtn').innerHTML='<span class=spinner></span>Connecting...';"
"document.getElementById('connectDebug').style.display='block';"
"document.getElementById('debugLog').textContent='Starting connection to '+ssid+'...\\n';"
"fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:ssid,password:pwd})})"
".then(function(r){return r.json()}).then(function(d){"
"document.getElementById('connectBtn').disabled=false;"
"document.getElementById('connectBtn').textContent='Connect';"
"if(d.error){document.getElementById('connectResult').innerHTML="
"'<span style=color:#f44336>'+d.error+'</span>';return}"
"document.getElementById('connectResult').innerHTML="
"'<span style=color:#4caf50>'+d.status+'</span>';"
"if(d.debug){document.getElementById('debugLog').textContent=d.debug}"
"}).catch(function(e){document.getElementById('connectBtn').disabled=false;"
"document.getElementById('connectBtn').textContent='Connect';"
"document.getElementById('connectResult').innerHTML="
"'<span style=color:#f44336>Request failed</span>'})}"

"function listProfiles(){"
"fetch('/api/wifi/profiles').then(function(r){return r.json()}).then(function(d){"
"if(!d.profiles||d.profiles.length===0){"
"document.getElementById('profiles').innerHTML='<pre>No profiles saved</pre>';return}"
"var h='<table><tr><th>#</th><th>SSID</th><th></th></tr>';"
"d.profiles.forEach(function(p,i){"
"h+='<tr><td>'+i+'</td><td>'+p.ssid+'</td><td>"
"+'<button class=danger onclick=\"delProfile('+i+')\">Remove</button></td></tr>';"
"});h+='</table>';document.getElementById('profiles').innerHTML=h;"
"}).catch(function(){})}"

"function delProfile(i){"
"if(!confirm('Remove profile '+i+'?'))return;"
"fetch('/api/wifi/profiles/'+i,{method:'DELETE'})"
".then(function(r){return r.json()}).then(function(){listProfiles()})}"

"function refreshStatus(){"
"fetch('/api/status').then(function(r){return r.json()}).then(function(d){"
"var h='';"
"h+='<div class=stat><div class=stat-label>WiFi</div><div class=stat-value '+(d.wifi==='CONNECTED'?'ok':'crit')+'>'+d.wifi+'</div></div>';"
"h+='<div class=stat><div class=stat-label>IP</div><div class=stat-value>'+(d.ip||'--')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>RSSI</div><div class=stat-value '+(d.rssi<-75?'crit':d.rssi<-50?'warn':'ok')+'>'+(d.rssi||0)+' dBm</div></div>';"
"h+='<div class=stat><div class=stat-label>Free RAM</div><div class=stat-value'+(d.free_heap<10000?' crit':'')+'>'+(d.free_heap/1024).toFixed(1)+' KB</div></div>';"
"h+='<div class=stat><div class=stat-label>Uptime</div><div class=stat-value>'+d.uptime+'</div></div>';"
"h+='<div class=stat><div class=stat-label>FW</div><div class=stat-value>'+d.version+'</div></div>';"
"h+='<div class=stat><div class=stat-label>Mode</div><div class=stat-value>'+d.wifi_mode+'</div></div>';"
"h+='<div class=stat><div class=stat-label>Clients</div><div class=stat-value>'+d.softap_clients+'</div></div>';"
"document.getElementById('status').innerHTML=h;"
"}).catch(function(){})}"
"setInterval(refreshStatus,3000);"
"refreshStatus();"

"function refreshLogs(){"
"fetch('/api/logs').then(function(r){return r.json()}).then(function(d){"
"document.getElementById('logOutput').textContent=d.logs||'(empty)';"
"}).catch(function(){document.getElementById('logOutput').textContent='Failed to load'})}"

"function getDiag(){"
"fetch('/api/diagnostics').then(function(r){return r.json()}).then(function(d){"
"var s='';"
"s+='Chip: '+d.chip+' rev'+d.revision+' ('+d.cores+' cores)\\n';"
"s+='Flash: '+d.flash_mb+' MB\\n';"
"s+='IDF: '+d.idf_version+'\\n';"
"s+='Free heap: '+d.free_heap+' bytes\\n';"
"s+='Min heap: '+d.min_free_heap+' bytes\\n';"
"s+='Partition: '+d.partition+'\\n';"
"s+='Reset reason: '+d.reset_reason+'\\n';"
"s+='WiFi RSSI: '+d.rssi+' dBm\\n';"
"s+='Profiles: '+d.profiles+'\\n';"
"document.getElementById('diagOutput').textContent=s;"
"}).catch(function(){document.getElementById('diagOutput').textContent='Failed'})}"

"function sendCmd(cmd){"
"var msg='Execute '+cmd+'?';"
"if(cmd==='factory_reset')msg='FACTORY RESET? This erases ALL data.';"
"if(!confirm(msg))return;"
"fetch('/api/system/cmd',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({command:cmd})})"
".then(function(r){return r.json()}).then(function(d){"
"document.getElementById('sysResult').innerHTML="
"'<span style=color:'+(d.error?'#f44336':'#4caf50')+'>'"
"+(d.error||d.status)+'</span>';"
"}).catch(function(){document.getElementById('sysResult').innerHTML="
"'<span style=color:#f44336>Failed</span>'})}"

"refreshLogs();setInterval(refreshLogs,10000);"
"</script></body></html>";

// ═══════════════════════════════════════════════════
// HTTP HANDLERS
// ═══════════════════════════════════════════════════

static esp_err_t handle_dashboard(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1);
}

static esp_err_t handle_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    const char *mode_str = "UNKNOWN";
    if (mode == WIFI_MODE_STA) mode_str = "STA";
    else if (mode == WIFI_MODE_AP) mode_str = "AP";
    else if (mode == WIFI_MODE_APSTA) mode_str = "APSTA";
    cJSON_AddStringToObject(root, "wifi_mode", mode_str);

    wifi_sta_list_t sta_list;
    esp_err_t sta_err = esp_wifi_ap_get_sta_list(&sta_list);
    cJSON_AddNumberToObject(root, "softap_clients",
                            (sta_err == ESP_OK) ? sta_list.num : 0);

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddStringToObject(root, "wifi", "CONNECTED");
            cJSON_AddStringToObject(root, "ssid", (char *)ap_info.ssid);
            cJSON_AddNumberToObject(root, "rssi", ap_info.rssi);
        } else {
            cJSON_AddStringToObject(root, "wifi", "CONNECTING");
            cJSON_AddNumberToObject(root, "rssi", 0);
        }
    } else {
        cJSON_AddStringToObject(root, "wifi", "DISCONNECTED");
        cJSON_AddNumberToObject(root, "rssi", 0);
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            cJSON_AddStringToObject(root, "ip", ip_str);
        } else {
            cJSON_AddStringToObject(root, "ip", SOFTAP_IP_ADDR);
        }
    } else {
        cJSON_AddStringToObject(root, "ip", SOFTAP_IP_ADDR);
    }

    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

    int64_t us = esp_timer_get_time();
    int sec = us / 1000000;
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%dh %dm %ds", sec / 3600, (sec % 3600) / 60, sec % 60);
    cJSON_AddStringToObject(root, "uptime", uptime);

    cJSON_AddStringToObject(root, "version", FW_VERSION);
    cJSON_AddNumberToObject(root, "connecting", s_connecting ? 1 : 0);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

static esp_err_t handle_wifi_scan(httpd_req_t *req)
{
    // Must be in STA or APSTA mode to scan
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "error", "WiFi in AP-only mode, cannot scan");
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        return ESP_OK;
    }

    // Stop STA briefly for scan if connected
    bool was_connected = false;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) was_connected = true;

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 30) ap_count = 30;

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");

    for (int i = 0; i < ap_count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(obj, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(obj, "channel", ap_records[i].primary);

        const char *auth_str = "OPEN";
        switch (ap_records[i].authmode) {
            case WIFI_AUTH_OPEN:         auth_str = "OPEN"; break;
            case WIFI_AUTH_WEP:          auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK:      auth_str = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK:     auth_str = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth_str = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK:     auth_str = "WPA3"; break;
            default:                     auth_str = "OTHER"; break;
        }
        cJSON_AddStringToObject(obj, "auth", auth_str);
        cJSON_AddItemToArray(arr, obj);
    }

    free(ap_records);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

// Debug capture for WiFi connect
typedef struct {
    char log[2048];
    int pos;
} connect_debug_t;

static connect_debug_t s_debug;

static void debug_printf(const char *fmt, ...)
{
    if (s_debug.pos >= (int)sizeof(s_debug.log) - 128) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(s_debug.log + s_debug.pos, sizeof(s_debug.log) - s_debug.pos, fmt, args);
    va_end(args);
    if (n > 0) s_debug.pos += n;
}

static esp_err_t handle_wifi_connect(httpd_req_t *req)
{
    char buf[SOFTAP_MAX_POST_SIZE];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_FAIL; }

    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd_item = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = cJSON_IsString(pwd_item) ? pwd_item->valuestring : "";

    memset(&s_debug, 0, sizeof(s_debug));
    s_connecting = true;

    debug_printf("=== Connecting to: %s ===\n", ssid);
    debug_printf("Password: %s\n", strlen(password) > 0 ? "(set)" : "(open)");
    debug_printf("Step 1: Scanning for network...\n");

    // Scan for the target
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 3000,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        debug_printf("Scan FAILED: %s\n", esp_err_to_name(err));
        goto done;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    debug_printf("Found %d networks\n", ap_count);

    uint16_t fetch_count = ap_count;
    wifi_ap_record_t *records = NULL;
    wifi_auth_mode_t target_auth = WIFI_AUTH_OPEN;
    bool found = false;

    if (ap_count > 0) {
        records = calloc(ap_count, sizeof(wifi_ap_record_t));
        if (records) {
            esp_wifi_scan_get_ap_records(&fetch_count, records);
            for (int i = 0; i < fetch_count; i++) {
                if (strcmp((char *)records[i].ssid, ssid) == 0) {
                    found = true;
                    target_auth = records[i].authmode;
                    debug_printf("Found: %s RSSI=%d Auth=%d Ch=%u\n",
                                 ssid, records[i].rssi, records[i].authmode, records[i].primary);
                    break;
                }
            }
            free(records);
        }
    }

    if (!found) {
        debug_printf("Network '%s' NOT FOUND in scan!\n", ssid);
        debug_printf("Make sure you are within range.\n");
        goto done;
    }

    debug_printf("Step 2: Configuring STA (auto-detected auth=%d)...\n", target_auth);

    // Save profile to NVS
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        uint8_t count = 0;
        nvs_get_u8(nvs, "count", &count);

        // Check if profile already exists
        bool exists = false;
        for (int i = 0; i < count; i++) {
            char key[16];
            char existing_ssid[64] = {0};
            size_t len = sizeof(existing_ssid);
            snprintf(key, sizeof(key), "ssid_%u", i);
            if (nvs_get_str(nvs, key, existing_ssid, &len) == ESP_OK) {
                if (strcmp(existing_ssid, ssid) == 0) {
                    // Update password
                    snprintf(key, sizeof(key), "pwd_%u", i);
                    nvs_set_str(nvs, key, password);
                    exists = true;
                    debug_printf("Updated existing profile slot %d\n", i);
                    break;
                }
            }
        }

        if (!exists && count < WIFI_MAX_PROFILES) {
            char key[16];
            snprintf(key, sizeof(key), "ssid_%u", count);
            nvs_set_str(nvs, key, ssid);
            snprintf(key, sizeof(key), "pwd_%u", count);
            nvs_set_str(nvs, key, password);
            count++;
            nvs_set_u8(nvs, "count", count);
            debug_printf("Saved as profile slot %d (total: %d)\n", count - 1, count);
        }
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    debug_printf("Step 3: Connecting...\n");

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN; // accept any
    wifi_config.sta.failure_retry_cnt = 5;

    esp_wifi_disconnect();
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        debug_printf("set_config FAILED: %s\n", esp_err_to_name(err));
        goto done;
    }

    // Switch to APSTA so SoftAP stays up while STA connects
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        debug_printf("wifi_connect FAILED: %s\n", esp_err_to_name(err));
        goto done;
    }

    debug_printf("Connecting... waiting up to 15s for IP...\n");

    // Wait up to 15s for connection
    for (int i = 0; i < 15; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        EventBits_t b = xEventGroupGetBits(wifi_event_group);
        if (b & WIFI_CONNECTED_BIT) {
            debug_printf("CONNECTED! IP obtained.\n");
            goto done;
        }
    }

    debug_printf("Connection timed out after 15s.\n");
    debug_printf("Check password and signal strength.\n");

done:
    s_connecting = false;

    cJSON *resp = cJSON_CreateObject();
    EventBits_t final_bits = xEventGroupGetBits(wifi_event_group);
    if (final_bits & WIFI_CONNECTED_BIT) {
        cJSON_AddStringToObject(resp, "status", "Connected successfully!");
    } else {
        cJSON_AddStringToObject(resp, "status", "Connection attempted — check debug log");
    }
    cJSON_AddStringToObject(resp, "debug", s_debug.log);

    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t handle_profiles_get(httpd_req_t *req)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddArrayToObject(root, "profiles");
        cJSON_AddNumberToObject(root, "count", 0);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        return ESP_OK;
    }

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "profiles");

    for (int i = 0; i < count && i < WIFI_MAX_PROFILES; i++) {
        char key[16];
        char ssid[64] = {0};
        size_t len = sizeof(ssid);
        snprintf(key, sizeof(key), "ssid_%u", i);
        if (nvs_get_str(nvs, key, ssid, &len) == ESP_OK) {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "index", i);
            cJSON_AddStringToObject(obj, "ssid", ssid);
            cJSON_AddItemToArray(arr, obj);
        }
    }
    nvs_close(nvs);

    cJSON_AddNumberToObject(root, "count", count);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t handle_profile_delete(httpd_req_t *req)
{
    // Extract index from URI: /api/wifi/profiles/<idx>
    char id_str[16] = {0};
    const char *uri = req->uri;
    const char *last_slash = strrchr(uri, '/');
    if (!last_slash || !*(last_slash + 1)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing index");
        return ESP_FAIL;
    }
    strncpy(id_str, last_slash + 1, sizeof(id_str) - 1);
    int idx = atoi(id_str);

    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);
    if (idx < 0 || idx >= count) {
        nvs_close(nvs);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }

    // Shift profiles down
    for (int i = idx; i < count - 1; i++) {
        char ssid_buf[64] = {0};
        char pwd_buf[64] = {0};
        char k[16];
        size_t len;

        snprintf(k, sizeof(k), "ssid_%u", i + 1); len = sizeof(ssid_buf);
        nvs_get_str(nvs, k, ssid_buf, &len);
        snprintf(k, sizeof(k), "pwd_%u", i + 1); len = sizeof(pwd_buf);
        nvs_get_str(nvs, k, pwd_buf, &len);

        snprintf(k, sizeof(k), "ssid_%u", i); nvs_set_str(nvs, k, ssid_buf);
        snprintf(k, sizeof(k), "pwd_%u", i); nvs_set_str(nvs, k, pwd_buf);
    }

    // Erase last slot
    char k[16];
    snprintf(k, sizeof(k), "ssid_%u", count - 1); nvs_erase_key(nvs, k);
    snprintf(k, sizeof(k), "pwd_%u", count - 1); nvs_erase_key(nvs, k);

    count--;
    nvs_set_u8(nvs, "count", count);
    nvs_commit(nvs);
    nvs_close(nvs);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "count", count);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t handle_logs(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    char log_buf[2048];
    int pos = 0;
    #define SAFE_SNPRINTF(...) do { \
        int n = snprintf(log_buf + pos, sizeof(log_buf) - pos, __VA_ARGS__); \
        if (n > 0) pos += n; \
        if (pos >= (int)sizeof(log_buf)) { pos = sizeof(log_buf) - 1; } \
    } while(0)

    SAFE_SNPRINTF("=== Sterling v%s ===\n", FW_VERSION);

    int64_t us = esp_timer_get_time();
    int sec = us / 1000000;
    SAFE_SNPRINTF("Uptime: %dh %dm %ds\n", sec / 3600, (sec % 3600) / 60, sec % 60);
    SAFE_SNPRINTF("Free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    SAFE_SNPRINTF("WiFi: %s\n", (bits & WIFI_CONNECTED_BIT) ? "CONNECTED" : "DISCONNECTED");

    int rssi = network_get_rssi();
    if (rssi > -127) {
        SAFE_SNPRINTF("RSSI: %d dBm\n", rssi);
    }

    SAFE_SNPRINTF("Profiles: %d\n", network_get_profile_count());
    SAFE_SNPRINTF("SoftAP: %s\n", s_softap_active ? "active" : "inactive");
    SAFE_SNPRINTF("Connecting: %s\n", s_connecting ? "yes" : "no");

    #undef SAFE_SNPRINTF

    cJSON_AddStringToObject(root, "logs", log_buf);
    return send_json(req, root);
}

static esp_err_t handle_diagnostics(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "chip", CONFIG_IDF_TARGET);
    cJSON_AddNumberToObject(root, "revision", chip_info.revision);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    cJSON_AddNumberToObject(root, "flash_mb", flash_size / (1024 * 1024));

    cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());

    const esp_partition_t *running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "partition", running ? running->label : "unknown");

    cJSON_AddNumberToObject(root, "reset_reason", esp_rom_get_reset_reason(0));
    cJSON_AddNumberToObject(root, "rssi", network_get_rssi());
    cJSON_AddNumberToObject(root, "profiles", network_get_profile_count());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

static esp_err_t handle_system_cmd(httpd_req_t *req)
{
    char buf[SOFTAP_MAX_POST_SIZE];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_FAIL; }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(cmd_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing command");
        return ESP_FAIL;
    }

    const char *cmd = cmd_item->valuestring;
    cJSON *resp = cJSON_CreateObject();

    if (strcmp(cmd, "reboot") == 0) {
        cJSON_AddStringToObject(resp, "status", "Rebooting in 1s...");
        char *json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

    } else if (strcmp(cmd, "factory_reset") == 0) {
        event_log_write(EVT_FACTORY_TRIGGERED);
        cJSON_AddStringToObject(resp, "status", "Factory reset in 3s...");
        char *json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        vTaskDelay(pdMS_TO_TICKS(1000));
        factory_reset();

    } else {
        cJSON_AddStringToObject(resp, "error", "Unknown command");
        char *json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
    }

    return ESP_OK;
}

// ═══════════════════════════════════════════════════
// CAPTIVE PORTAL — redirect all to dashboard
// ═══════════════════════════════════════════════════

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to Sterling", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════
// SERVER START
// ═══════════════════════════════════════════════════

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    const httpd_uri_t uri_dashboard   = { .uri = "/",                       .method = HTTP_GET,  .handler = handle_dashboard };
    const httpd_uri_t uri_status      = { .uri = "/api/status",             .method = HTTP_GET,  .handler = handle_status };
    const httpd_uri_t uri_scan        = { .uri = "/api/wifi/scan",          .method = HTTP_GET,  .handler = handle_wifi_scan };
    const httpd_uri_t uri_connect     = { .uri = "/api/wifi/connect",       .method = HTTP_POST, .handler = handle_wifi_connect };
    const httpd_uri_t uri_profiles_g  = { .uri = "/api/wifi/profiles",      .method = HTTP_GET,  .handler = handle_profiles_get };
    const httpd_uri_t uri_profile_del = { .uri = "/api/wifi/profiles/<id>", .method = HTTP_DELETE,.handler = handle_profile_delete };
    const httpd_uri_t uri_logs        = { .uri = "/api/logs",               .method = HTTP_GET,  .handler = handle_logs };
    const httpd_uri_t uri_diag        = { .uri = "/api/diagnostics",        .method = HTTP_GET,  .handler = handle_diagnostics };
    const httpd_uri_t uri_cmd         = { .uri = "/api/system/cmd",         .method = HTTP_POST, .handler = handle_system_cmd };

    httpd_register_uri_handler(s_server, &uri_dashboard);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_connect);
    httpd_register_uri_handler(s_server, &uri_profiles_g);
    httpd_register_uri_handler(s_server, &uri_profile_del);
    httpd_register_uri_handler(s_server, &uri_logs);
    httpd_register_uri_handler(s_server, &uri_diag);
    httpd_register_uri_handler(s_server, &uri_cmd);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════

esp_err_t softap_init(void)
{
    ESP_LOGI(TAG, "Initializing SoftAP...");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_wifi_set_ps(WIFI_PS_NONE);

    return ESP_OK;
}

esp_err_t softap_start(void)
{
    if (s_softap_active) return ESP_OK;

    ESP_LOGI(TAG, "Starting SoftAP: SSID=%s", SOFTAP_SSID);

    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t ap_config = { 0 };
    strncpy((char *)ap_config.ap.ssid, SOFTAP_SSID, 32);
    strncpy((char *)ap_config.ap.password, SOFTAP_PASSWORD, 64);
    ap_config.ap.ssid_len = strlen(SOFTAP_SSID);
    ap_config.ap.channel = SOFTAP_CHANNEL;
    ap_config.ap.max_connection = SOFTAP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Set static IP for AP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_ip_info_t ip_info = {
            .ip.addr = ipaddr_addr(SOFTAP_IP_ADDR),
            .gw.addr = ipaddr_addr(SOFTAP_IP_GW),
            .netmask.addr = ipaddr_addr(SOFTAP_IP_NETMASK),
        };
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
    }

    // Also load existing STA config from NVS if available
    wifi_config_t sta_config = { 0 };
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t count = 0;
        nvs_get_u8(nvs, "count", &count);
        if (count > 0) {
            char ssid[64] = {0};
            char pwd[64] = {0};
            size_t len = sizeof(ssid);
            if (nvs_get_str(nvs, "ssid_0", ssid, &len) == ESP_OK) {
                len = sizeof(pwd);
                nvs_get_str(nvs, "pwd_0", pwd, &len);
                strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
                sta_config.sta.ssid[sizeof(sta_config.sta.ssid) - 1] = '\0';
                strncpy((char *)sta_config.sta.password, pwd, sizeof(sta_config.sta.password) - 1);
                sta_config.sta.password[sizeof(sta_config.sta.password) - 1] = '\0';
                ESP_LOGI(TAG, "STA pre-configured with: %s", ssid);
            }
        }
        nvs_close(nvs);
    }
    sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi started: SSID=%s", SOFTAP_SSID);

    esp_err_t http_err = start_server();
    if (http_err != ESP_OK) return http_err;

    s_softap_active = true;
    network_set_softap_active(true);

    ESP_LOGI(TAG, "Command Center active at http://%s", SOFTAP_IP_ADDR);
    return ESP_OK;
}

esp_err_t softap_stop(void)
{
    if (!s_softap_active) return ESP_OK;

    ESP_LOGI(TAG, "Stopping SoftAP...");

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    s_softap_active = false;
    network_set_softap_active(false);

    ESP_LOGI(TAG, "SoftAP stopped");
    return ESP_OK;
}

bool softap_is_active(void)
{
    return s_softap_active;
}
