#include "softap.h"
#include "config.h"
#include "network.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
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
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "softap";

static httpd_handle_t s_server = NULL;
static bool s_softap_active = false;

// OTA state
static volatile bool s_ota_in_progress = false;
static volatile bool s_ota_abort = false;
static volatile int s_ota_bytes_downloaded = 0;
static volatile int s_ota_total_bytes = 0;
static volatile esp_err_t s_ota_status = ESP_OK;

// ═══════════════════════════════════════════════════
// EMBEDDED DASHBOARD HTML — Light Theme, Sterling Blue
// ═══════════════════════════════════════════════════

static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Sterling Setup</title>"
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
"button:disabled{background:#ccc;cursor:not-allowed;transform:none}"
"input{background:#fff;color:#1a1a2e;border:1px solid #d0d5dd;padding:10px 12px;"
"border-radius:8px;font-size:14px;width:100%;margin:4px 0}"
"input:focus{outline:none;border-color:#0066ff;box-shadow:0 0 0 3px rgba(0,102,255,0.1)}"
"pre{background:#f0f2f5;padding:12px;border-radius:8px;overflow-x:auto;"
"font-size:11px;max-height:250px;overflow-y:auto;border:1px solid #e0e4ea}"
"table{width:100%;border-collapse:collapse;font-size:13px}"
"td,th{padding:8px 10px;border-bottom:1px solid #e0e4ea;text-align:left}"
"th{color:#666;text-transform:uppercase;font-size:10px;letter-spacing:0.5px}"
".progress-container{background:#e0e4ea;border-radius:8px;height:24px;margin:8px 0;overflow:hidden}"
".progress-bar{background:#0066ff;height:100%;border-radius:8px;transition:width 0.3s;"
"display:flex;align-items:center;justify-content:center;color:#fff;font-size:11px;font-weight:bold}"
".progress-bar.error{background:#f44336}"
".btn-row{display:flex;gap:8px;flex-wrap:wrap}"
".rssi-bar{display:inline-block;width:8px;border-radius:2px;margin-left:4px;vertical-align:middle}"
"</style></head><body>"

"<h1>&#9889; Sterling Setup</h1>"
"<div class='sub'>Bootstrap v" BOOTSTRAP_VERSION " &mdash; Configure WiFi &amp; install firmware</div>"

"<div class='card' id='statusCard'>"
"<h2>Device Status</h2>"
"<div class='status' id='status'><div class='stat'><div class='stat-label'>Loading...</div></div></div>"
"</div>"

"<div class='card'>"
"<h2>WiFi Networks</h2>"
"<button onclick='scanWifi()' id='scanBtn'>Scan Networks</button>"
"<div id='scanresults'><pre>Tap Scan to find networks</pre></div>"
"</div>"

"<div class='card'>"
"<h2>Add WiFi Profile</h2>"
"<input id='ssid' placeholder='Network name (SSID)'>"
"<input id='pwd' type='password' placeholder='Password'>"
"<button onclick='addWifi()' class='success'>Save Profile</button>"
"<div id='addresult'></div>"
"</div>"

"<div class='card'>"
"<h2>Saved Profiles</h2>"
"<button onclick='listProfiles()'>Refresh</button>"
"<div id='profiles'><pre>Loading...</pre></div>"
"<p id='connstatus'></p>"
"</div>"

"<div class='card'>"
"<h2>WiFi Event Log</h2>"
"<p style='font-size:12px;color:#666;margin:0 0 8px'>Live connect/disconnect events with reasons</p>"
"<div id='eventlog'><pre>Loading...</pre></div>"
"</div>"

"<div class='card'>"
"<h2>System Info</h2>"
"<button onclick='getDiag()'>Load Diagnostics</button>"
"<div id='diagnostics'><pre>Tap to load</pre></div>"
"</div>"

"<div class='card'>"
"<h2>Install Firmware</h2>"
"<p style='font-size:13px;color:#666;margin-bottom:8px'>Downloads Sterling main firmware from GitHub and installs it.</p>"
"<div id='otaSection'>"
"<button onclick='startOta()' id='otaBtn' class='danger'>Install Firmware</button>"
"</div>"
"<div id='otaProgress' style='display:none'>"
"<div class='progress-container'>"
"<div class='progress-bar' id='progressBar' style='width:0%'>0%</div>"
"</div>"
"<div style='font-size:12px;color:#666' id='otaStatus'>Preparing...</div>"
"<div class='btn-row' id='otaButtons' style='display:none'>"
"<button onclick='cancelOta()' class='danger'>Cancel</button>"
"</div>"
"</div>"
"<div id='otaResult' style='display:none;margin-top:8px'></div>"
"</div>"

"<script>"
"function h(tag,cls,txt){var e=document.createElement(tag);if(cls)e.className=cls;if(txt)e.textContent=txt;return e}"
"function escAttr(s){return String(s).replace(/&/g,'&amp;').replace(/\"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}"
"function escHtml(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}"
"function selectNetwork(btn){document.getElementById('ssid').value=btn.getAttribute('data-ssid');document.getElementById('pwd').focus()}"

// Status auto-refresh
"function refreshStatus(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"var conn=d.wifi_connected;"
"var h='';"
"h+='<div class=stat><div class=stat-label>STA Link</div><div class=stat-value '+(conn?'ok':'crit')+'>'+(conn?'CONNECTED':'DISCONNECTED')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>Network</div><div class=stat-value>'+(d.sta_ssid||'--')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>STA IP</div><div class=stat-value>'+(d.sta_ip||'--')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>Channel</div><div class=stat-value>'+(d.channel||'--')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>RSSI</div><div class=stat-value '+(d.rssi<-75?'warn':d.rssi<-50?'':'ok')+'>'+(d.rssi?d.rssi+' dBm':'--')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>AP IP</div><div class=stat-value>'+(d.ip||'--')+'</div></div>';"
"h+='<div class=stat><div class=stat-label>Memory</div><div class=stat-value'+(d.free_heap<10000?' crit':'')+'>'+(d.free_heap/1024).toFixed(1)+' KB</div></div>';"
"h+='<div class=stat><div class=stat-label>Uptime</div><div class=stat-value>'+d.uptime+'</div></div>';"
"h+='<div class=stat><div class=stat-label>FW</div><div class=stat-value>'+d.version+'</div></div>';"
"document.getElementById('status').innerHTML=h;"
"}).catch(e=>{})}"
"setInterval(refreshStatus,3000);"
"refreshStatus();"

// Decode WiFi disconnect reason codes to human text
"function reasonText(r){"
"var m={1:'auth expired',2:'auth not valid / wrong password',3:'deauth leaving (AP kicked STA)',4:'disassoc due to inactivity',5:'AP busy',6:'class2 frame from non-auth',7:'class3 frame from non-assoc',8:'STA leaving / deauth',15:'4-way handshake timeout',201:'STA found no AP (scan failed)',205:'STA disconnected (no AP in range)'};"
"return m[r]?m[r]:('code '+r);"
"}"

// Live WiFi event log
"function refreshEvents(){"
"fetch('/api/events').then(r=>r.json()).then(d=>{"
"if(!d.events||d.events.length===0){document.getElementById('eventlog').innerHTML='<pre>No events yet</pre>';return}"
"var h='';"
"d.events.forEach(function(e){"
"var cls=e.msg.indexOf('Connected')===0?'ok':e.msg.indexOf('Disconnected')===0?'crit':'';"
"var color=cls==='ok'?'#4caf50':cls==='crit'?'#f44336':'#333';"
"var txt=e.msg;"
"var ri=txt.indexOf('reason ');"
"if(ri>=0){var num=parseInt(txt.substring(ri+7));txt=txt.substring(0,ri)+'reason '+num+' ('+reasonText(num)+')';}"
"h+='<div style=\"padding:4px 0;border-bottom:1px solid #eee;font-size:12px\"><span style=color:#999>['+fmtTime(e.t)+']</span> <span style=color:'+color+'>'+escHtml(txt)+'</span></div>';"
"});"
"document.getElementById('eventlog').innerHTML=h;"
"}).catch(e=>{})}"
"function fmtTime(s){var hh=Math.floor(s/3600),mm=Math.floor((s%3600)/60),ss=s%60;return (hh<10?'0':'')+hh+':'+(mm<10?'0':'')+mm+':'+(ss<10?'0':'')+ss;}"
"setInterval(refreshEvents,2000);"
"refreshEvents();"

// WiFi scan
"function scanWifi(){"
"document.getElementById('scanBtn').disabled=true;"
"document.getElementById('scanBtn').textContent='Scanning...';"
"fetch('/api/wifi/scan').then(function(r){return r.json()}).then(function(d){"
"document.getElementById('scanBtn').disabled=false;"
"document.getElementById('scanBtn').textContent='Scan Networks';"
"if(d.error){document.getElementById('scanresults').innerHTML='<pre>Error: '+d.error+'</pre>';return;}"
"var h='<table><tr><th>Network</th><th>Signal</th><th>Security</th><th></th></tr>';"
"d.networks.forEach(function(n){"
"var bars=n.rssi>-50?4:n.rssi>-60?3:n.rssi>-70?2:1;"
"var color=bars>=3?'#4caf50':bars>=2?'#ff9800':'#f44336';"
"h+='<tr><td>'+escHtml(n.ssid)+'</td><td>'+n.rssi+' dBm <span class=rssi-bar style=background:'+color+';height:'+(bars*4+4)+'px></span></td>'"
"+'<td>'+n.auth+'</td><td><button onclick=\"selectNetwork(this)\" data-ssid=\"'+escAttr(n.ssid)+'\">Select</button></td></tr>';"
"});"
"h+='</table>';"
"document.getElementById('scanresults').innerHTML=h;"
"}).catch(function(e){"
"document.getElementById('scanBtn').disabled=false;"
"document.getElementById('scanBtn').textContent='Scan Networks';"
"});"
"}"

// Add WiFi
"function addWifi(){"
"var ssid=document.getElementById('ssid').value.trim();"
"var pwd=document.getElementById('pwd').value;"
"if(!ssid){document.getElementById('addresult').innerHTML='<span style=color:#f44336>Enter SSID</span>';return}"
"fetch('/api/wifi/profiles',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:ssid,password:pwd})})"
".then(r=>r.json()).then(d=>{"
"if(d.error){document.getElementById('addresult').innerHTML='<span style=color:#f44336>'+d.error+'</span>';return}"
"document.getElementById('addresult').innerHTML='<span style=color:#4caf50>Added! Total: '+d.count+' profiles — connecting...</span>';"
"document.getElementById('ssid').value='';document.getElementById('pwd').value='';"
"listProfiles();"
"setTimeout(refreshConn,4000);"
"}).catch(e=>{document.getElementById('addresult').innerHTML='<span style=color:#f44336>Failed</span>'})"
"}"

// Connect to stored profiles now
"function connectWifi(){"
"fetch('/api/wifi/connect',{method:'POST'}).then(r=>r.json()).then(d=>{"
"document.getElementById('connstatus').innerHTML='<span style=color:#2196f3>Connecting...</span>';"
"setTimeout(refreshConn,4000);"
"}).catch(e=>{})}"
"function refreshConn(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"var s=d.wifi_connected?'<span style=color:#4caf50>Connected: '+(d.sta_ip||'')+'</span>':'<span style=color:#f44336>Not connected</span>';"
"document.getElementById('connstatus').innerHTML=s;"
"}).catch(e=>{})}"
"setInterval(refreshConn,8000);"

// List profiles
"function listProfiles(){"
"fetch('/api/wifi/profiles').then(r=>r.json()).then(d=>{"
"if(d.profiles.length===0){document.getElementById('profiles').innerHTML='<pre>No profiles saved</pre>';return}"
"var h='<table><tr><th>#</th><th>SSID</th><th></th></tr>';"
"d.profiles.forEach(function(p,i){"
"h+='<tr><td>'+i+'</td><td>'+p.ssid+'</td><td><button onclick=\"connectWifi()\">Connect</button> <button class=danger onclick=\"delProfile('+i+')\">Remove</button></td></tr>';"
"});h+='</table>';document.getElementById('profiles').innerHTML=h;"
"}).catch(e=>{})}"

// Delete profile
"function delProfile(i){"
"fetch('/api/wifi/profiles?id='+i,{method:'DELETE'})"
".then(r=>r.json()).then(()=>listProfiles())"
"}"

// Diagnostics
"function getDiag(){"
"fetch('/api/diagnostics').then(r=>r.json()).then(d=>{"
"var s='';"
"s+='Chip: '+d.chip+' rev'+d.revision+' ('+d.cores+' cores)\\n';"
"s+='Flash: '+d.flash_mb+' MB\\n';"
"s+='IDF: '+d.idf_version+'\\n';"
"s+='Free heap: '+d.free_heap+' bytes\\n';"
"s+='Min free heap: '+d.min_free_heap+' bytes\\n';"
"s+='Running partition: '+d.partition+'\\n';"
"s+='Reset reason: '+d.reset_reason+'\\n';"
"document.getElementById('diagnostics').innerHTML='<pre>'+s+'</pre>';"
"}).catch(e=>{})}"

// OTA
"function startOta(){"
"if(!confirm('Install firmware from GitHub?\\n\\nThis will download and install the main Sterling firmware.\\nThe device will reboot automatically.'))return;"
"document.getElementById('otaBtn').disabled=true;"
"document.getElementById('otaProgress').style.display='block';"
"document.getElementById('otaResult').style.display='none';"
"document.getElementById('otaStatus').textContent='Starting download...';"
"document.getElementById('otaButtons').style.display='none';"
"fetch('/api/system/ota',{method:'POST'}).then(r=>r.json()).then(d=>{"
"if(d.error){showOtaError(d.error);return}"
"document.getElementById('otaButtons').style.display='block';"
"pollOtaProgress();"
"}).catch(e=>showOtaError('Failed to start OTA'))}"

"function pollOtaProgress(){"
"var iv=setInterval(function(){"
"fetch('/api/ota/progress').then(r=>r.json()).then(d=>{"
"if(d.total>0){"
"var pct=Math.round((d.downloaded/d.total)*100);"
"document.getElementById('progressBar').style.width=pct+'%';"
"document.getElementById('progressBar').textContent=pct+'%';"
"document.getElementById('otaStatus').textContent="
"'Downloading... '+(d.downloaded/1024).toFixed(0)+'KB / '+(d.total/1024).toFixed(0)+'KB';"
"}"
"if(d.complete){"
"clearInterval(iv);"
"document.getElementById('progressBar').style.width='100%';"
"document.getElementById('progressBar').textContent='100%';"
"document.getElementById('otaStatus').textContent='Install complete! Rebooting in 3s...';"
"document.getElementById('otaButtons').style.display='none';"
"showOtaSuccess('Firmware installed successfully! Device will reboot shortly.');"
"} else if(d.error){"
"clearInterval(iv);"
"showOtaError(d.error);"
"} else if(!d.in_progress && !d.complete && !d.error){"
"clearInterval(iv);"
"document.getElementById('otaStatus').textContent='Idle';"
"}"
"}).catch(e=>{})},500)"
"}"

"function cancelOta(){"
"fetch('/api/system/abort_ota',{method:'POST'}).then(()=>{"
"document.getElementById('otaStatus').textContent='Cancelled';"
"document.getElementById('progressBar').className='progress-bar error';"
"document.getElementById('progressBar').textContent='Cancelled';"
"document.getElementById('otaBtn').disabled=false;"
"document.getElementById('otaButtons').style.display='none';"
"})}"

"function showOtaError(msg){"
"document.getElementById('otaStatus').textContent='Failed: '+msg;"
"document.getElementById('progressBar').className='progress-bar error';"
"document.getElementById('progressBar').textContent='Failed';"
"document.getElementById('otaBtn').disabled=false;"
"document.getElementById('otaButtons').style.display='none';"
"document.getElementById('otaResult').style.display='block';"
"document.getElementById('otaResult').innerHTML='<span style=color:#f44336>'+msg+'</span>';"
"}"

"function showOtaSuccess(msg){"
"document.getElementById('otaResult').style.display='block';"
"document.getElementById('otaResult').innerHTML='<span style=color:#4caf50>'+msg+'</span>';"
"}"
"</script></body></html>";

// ═══════════════════════════════════════════════════
// HTTP HANDLERS
// ═══════════════════════════════════════════════════

// GET /
static esp_err_t handle_dashboard(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1);
}

// GET /api/status
static esp_err_t handle_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    // Bootstrap is always AP — show AP status
    cJSON_AddStringToObject(root, "wifi", "AP_ACTIVE");

    // IP — always 192.168.4.1 for SoftAP
    cJSON_AddStringToObject(root, "ip", SOFTAP_IP_ADDR);

    // RSSI — report the STA's connected signal strength when linked
    int8_t sta_rssi = 0;
    uint8_t sta_chan = 0;
    char sta_ssid[64] = {0};
    char sta_ip[16] = {0};
    if (wifi_is_connected()) {
        wifi_ap_record_t ap_rec;
        if (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) {
            sta_rssi = ap_rec.rssi;
            sta_chan = ap_rec.primary;
        }
        snprintf(sta_ssid, sizeof(sta_ssid), "%s", wifi_get_cur_ssid());
        esp_netif_ip_info_t ip_info;
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&ip_info.ip));
        }
    }
    cJSON_AddNumberToObject(root, "wifi_connected", wifi_is_connected() ? 1 : 0);
    cJSON_AddStringToObject(root, "sta_ssid", sta_ssid);
    cJSON_AddStringToObject(root, "sta_ip", sta_ip);
    cJSON_AddNumberToObject(root, "rssi", sta_rssi);
    cJSON_AddNumberToObject(root, "channel", sta_chan);

    // Memory
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

    // Uptime
    int64_t us = esp_timer_get_time();
    int sec = us / 1000000;
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%dh %dm %ds", sec / 3600, (sec % 3600) / 60, sec % 60);
    cJSON_AddStringToObject(root, "uptime", uptime);

    // Version
    cJSON_AddStringToObject(root, "version", BOOTSTRAP_VERSION);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

// GET /api/events — rolling WiFi event log (connect/disconnect/reason history)
static esp_err_t handle_events(httpd_req_t *req)
{
    uint8_t count = 0;
    const wifi_evt_t *log = wifi_evt_get_log(&count);
    uint8_t head = wifi_evt_get_head();

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "events");
    /* Walk oldest→newest: start at (head - count + WIFI_EVT_MAX) % WIFI_EVT_MAX */
    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = (head + WIFI_EVT_MAX - count + i) % WIFI_EVT_MAX;
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "t", log[idx].t_sec);
        cJSON_AddStringToObject(obj, "msg", log[idx].msg);
        cJSON_AddItemToArray(arr, obj);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

// GET /api/wifi/scan
static esp_err_t handle_wifi_scan(httpd_req_t *req)
{
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

// GET /api/wifi/profiles
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

    uint16_t count = 0;
    nvs_get_u16(nvs, "count", &count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "profiles");

    for (int i = 0; i < count && i < WIFI_MAX_PROFILES; i++) {
        char key[32];
        char ssid[64] = {0};
        size_t len = sizeof(ssid);
        snprintf(key, sizeof(key), "ssid_%d", i);
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
    return err;
}

// POST /api/wifi/profiles
static esp_err_t handle_profiles_post(httpd_req_t *req)
{
    char buf[HTTP_MAX_POST_SIZE];
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

    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint16_t count = 0;
    nvs_get_u16(nvs, "count", &count);
    if (count >= WIFI_MAX_PROFILES) {
        nvs_close(nvs);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Max profiles reached");
        return ESP_FAIL;
    }

    char key[32];
    snprintf(key, sizeof(key), "ssid_%d", count);
    nvs_set_str(nvs, key, ssid_item->valuestring);
    snprintf(key, sizeof(key), "pwd_%d", count);
    nvs_set_str(nvs, key, cJSON_IsString(pwd_item) ? pwd_item->valuestring : "");
    count++;
    nvs_set_u16(nvs, "count", count);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Added profile: %s (total: %d)", ssid_item->valuestring, count);

    /* Try to join the newly added network right away (index = count-1). */
    wifi_connect_profile((uint16_t)(count - 1));

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "count", count);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// POST /api/wifi/connect — (re)try connecting to stored WiFi profiles now
static esp_err_t handle_wifi_connect(httpd_req_t *req)
{
    wifi_reconnect();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "connecting");
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// DELETE /api/wifi/profiles?id=<index>
static esp_err_t handle_profile_delete(httpd_req_t *req)
{
    // Parse the 'id' query parameter: /api/wifi/profiles?id=0
    char id_str[16] = {0};
    const char *q = strchr(req->uri, '?');
    if (!q || sscanf(q, "?id=%15s", id_str) != 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id");
        return ESP_FAIL;
    }
    int idx = atoi(id_str);

    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint16_t count = 0;
    nvs_get_u16(nvs, "count", &count);
    if (idx < 0 || idx >= count) {
        nvs_close(nvs);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }

    // Compact: shift profiles down
    for (int i = idx; i < count - 1; i++) {
        char ssid_buf[64], pwd_buf[64];
        char k[32];
        size_t len;

        snprintf(k, sizeof(k), "ssid_%d", i + 1); len = sizeof(ssid_buf);
        nvs_get_str(nvs, k, ssid_buf, &len);
        snprintf(k, sizeof(k), "pwd_%d", i + 1); len = sizeof(pwd_buf);
        nvs_get_str(nvs, k, pwd_buf, &len);

        snprintf(k, sizeof(k), "ssid_%d", i); nvs_set_str(nvs, k, ssid_buf);
        snprintf(k, sizeof(k), "pwd_%d", i); nvs_set_str(nvs, k, pwd_buf);
    }

    // Erase last slot
    char k[32];
    snprintf(k, sizeof(k), "ssid_%d", count - 1); nvs_erase_key(nvs, k);
    snprintf(k, sizeof(k), "pwd_%d", count - 1); nvs_erase_key(nvs, k);

    count--;
    nvs_set_u16(nvs, "count", count);
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

// ═══════════════════════════════════════════════════
// OTA HANDLERS
// ═══════════════════════════════════════════════════

static void ota_task(void *pvParameters)
{
    s_ota_in_progress = true;
    s_ota_abort = false;
    s_ota_bytes_downloaded = 0;
    s_ota_total_bytes = 0;
    s_ota_status = ESP_OK;

    ESP_LOGI(TAG, "OTA: Starting download from %s", OTA_FIRMWARE_URL);

    esp_http_client_config_t cfg = {
        .url = OTA_FIRMWARE_URL,
        .timeout_ms = 30000,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "OTA: HTTP client init failed");
        s_ota_status = ESP_FAIL;
        s_ota_in_progress = false;
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: HTTP open failed: %s", esp_err_to_name(err));
        s_ota_status = err;
        s_ota_in_progress = false;
        // Guard the cleanup: a failed TLS/open can leave the handle in a
        // state where cleanup dereferences NULL and panics the chip.
        if (client) esp_http_client_cleanup(client);
        client = NULL;
        return;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "OTA: HTTP status %d", status);
        s_ota_status = ESP_FAIL;
        s_ota_in_progress = false;
        if (client) esp_http_client_cleanup(client);
        return;
    }

    s_ota_total_bytes = esp_http_client_get_content_length(client);
    if (s_ota_total_bytes <= 0) s_ota_total_bytes = 300000;
    ESP_LOGI(TAG, "OTA: Expected size %d bytes", s_ota_total_bytes);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "OTA: No update partition found");
        s_ota_status = ESP_FAIL;
        s_ota_in_progress = false;
        if (client) esp_http_client_cleanup(client);
        return;
    }
    ESP_LOGI(TAG, "OTA: Writing to partition '%s' at offset 0x%" PRIx32,
             update_partition->label, update_partition->address);

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: esp_ota_begin failed: %s", esp_err_to_name(err));
        s_ota_status = err;
        s_ota_in_progress = false;
        if (client) esp_http_client_cleanup(client);
        return;
    }

    char buf[1024];
    while (!s_ota_abort) {
        int r = esp_http_client_read(client, buf, sizeof(buf));
        if (r <= 0) break;

        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA: esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            s_ota_status = err;
            s_ota_in_progress = false;
            if (client) esp_http_client_cleanup(client);
            return;
        }

        s_ota_bytes_downloaded += r;
    }

    if (client) esp_http_client_cleanup(client);

    if (s_ota_abort) {
        ESP_LOGW(TAG, "OTA: Aborted by user");
        esp_ota_abort(ota_handle);
        s_ota_status = ESP_ERR_INVALID_STATE;
        s_ota_in_progress = false;
        return;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: esp_ota_end failed: %s", esp_err_to_name(err));
        s_ota_status = err;
        s_ota_in_progress = false;
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: set_boot_partition failed: %s", esp_err_to_name(err));
        s_ota_status = err;
        s_ota_in_progress = false;
        return;
    }

    ESP_LOGI(TAG, "OTA: Complete! %d bytes written. Rebooting in 3s...", s_ota_bytes_downloaded);
    s_ota_status = ESP_OK;
    s_ota_in_progress = false;

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

// POST /api/system/ota
static esp_err_t handle_ota_start(httpd_req_t *req)
{
    if (s_ota_in_progress) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "error", "OTA already in progress");
        char *json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        return ESP_OK;
    }

    // Start OTA in background task
    BaseType_t ret = xTaskCreate(ota_task, "ota_task", 16384, NULL, 5, NULL);
    if (ret != pdPASS) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "error", "Failed to create OTA task");
        char *json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        free(json);
        return ESP_OK;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "started");
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// GET /api/ota/progress
static esp_err_t handle_ota_progress(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "in_progress", s_ota_in_progress);
    cJSON_AddNumberToObject(root, "downloaded", s_ota_bytes_downloaded);
    cJSON_AddNumberToObject(root, "total", s_ota_total_bytes);
    cJSON_AddBoolToObject(root, "complete", s_ota_status == ESP_OK && !s_ota_in_progress && s_ota_bytes_downloaded > 0);

    if (s_ota_status != ESP_OK && !s_ota_in_progress && s_ota_bytes_downloaded > 0) {
        cJSON_AddStringToObject(root, "error", esp_err_to_name(s_ota_status));
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

// POST /api/system/abort_ota
static esp_err_t handle_ota_abort(httpd_req_t *req)
{
    if (s_ota_in_progress) {
        s_ota_abort = true;
        ESP_LOGW(TAG, "OTA abort requested");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "aborting");
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// GET /api/diagnostics
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

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

// ═══════════════════════════════════════════════════
// CAPTIVE PORTAL — 404 redirect to dashboard
// ═══════════════════════════════════════════════════

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to Sterling Setup", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════
// SERVER MANAGEMENT
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

    // Register 404 handler for captive portal
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    // URI handlers
    const httpd_uri_t uri_dashboard  = { .uri = "/",                        .method = HTTP_GET,  .handler = handle_dashboard };
    const httpd_uri_t uri_status     = { .uri = "/api/status",              .method = HTTP_GET,  .handler = handle_status };
    const httpd_uri_t uri_events     = { .uri = "/api/events",              .method = HTTP_GET,  .handler = handle_events };
    const httpd_uri_t uri_scan       = { .uri = "/api/wifi/scan",           .method = HTTP_GET,  .handler = handle_wifi_scan };
    const httpd_uri_t uri_profiles_g = { .uri = "/api/wifi/profiles",       .method = HTTP_GET,  .handler = handle_profiles_get };
    const httpd_uri_t uri_profiles_p = { .uri = "/api/wifi/profiles",       .method = HTTP_POST, .handler = handle_profiles_post };
    const httpd_uri_t uri_wifi_conn  = { .uri = "/api/wifi/connect",        .method = HTTP_POST, .handler = handle_wifi_connect };
    const httpd_uri_t uri_profile_del= { .uri = "/api/wifi/profiles",  .method = HTTP_DELETE,.handler = handle_profile_delete };
    const httpd_uri_t uri_ota_start  = { .uri = "/api/system/ota",          .method = HTTP_POST, .handler = handle_ota_start };
    const httpd_uri_t uri_ota_prog   = { .uri = "/api/ota/progress",        .method = HTTP_GET,  .handler = handle_ota_progress };
    const httpd_uri_t uri_ota_abort  = { .uri = "/api/system/abort_ota",    .method = HTTP_POST, .handler = handle_ota_abort };
    const httpd_uri_t uri_diag       = { .uri = "/api/diagnostics",         .method = HTTP_GET,  .handler = handle_diagnostics };

    httpd_register_uri_handler(s_server, &uri_dashboard);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_events);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_profiles_g);
    httpd_register_uri_handler(s_server, &uri_profiles_p);
    httpd_register_uri_handler(s_server, &uri_wifi_conn);
    httpd_register_uri_handler(s_server, &uri_profile_del);
    httpd_register_uri_handler(s_server, &uri_ota_start);
    httpd_register_uri_handler(s_server, &uri_ota_prog);
    httpd_register_uri_handler(s_server, &uri_ota_abort);
    httpd_register_uri_handler(s_server, &uri_diag);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════

esp_err_t softap_init(void)
{
    ESP_LOGI(TAG, "Initializing Sterling Bootstrap...");

    // Init network stack
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Max TX power for PCB trace antenna
    esp_wifi_set_max_tx_power(WIFI_TX_POWER_MAX);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Set AP mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Configure AP
    wifi_config_t ap_config = { 0 };
    strncpy((char *)ap_config.ap.ssid, SOFTAP_SSID, 32);
    strncpy((char *)ap_config.ap.password, SOFTAP_PASSWORD, 64);
    ap_config.ap.ssid_len = strlen(SOFTAP_SSID);
    ap_config.ap.channel = SOFTAP_CHANNEL;
    ap_config.ap.max_connection = SOFTAP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Configure STA (for WiFi scan)
    wifi_config_t sta_config = { 0 };
    strncpy((char *)sta_config.sta.ssid, "", 32);
    sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    // Set AP IP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info = {
            .ip.addr = ipaddr_addr(SOFTAP_IP_ADDR),
            .gw.addr = ipaddr_addr(SOFTAP_IP_GW),
            .netmask.addr = ipaddr_addr(SOFTAP_IP_NETMASK),
        };
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
    }

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    s_softap_active = true;

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s", SOFTAP_SSID);

    // Start HTTP server
    esp_err_t http_err = start_server();
    if (http_err != ESP_OK) return http_err;

    ESP_LOGI(TAG, "HTTP server running on http://%s", SOFTAP_IP_ADDR);
    return ESP_OK;
}

esp_err_t softap_start(void)
{
    if (s_softap_active) return ESP_OK;
    return softap_init();
}

bool softap_is_active(void)
{
    return s_softap_active;
}
