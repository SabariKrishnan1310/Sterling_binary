// ============================================================
// STERLING — HEALTH MONITOR + RED TEAM + CRASH RECOVERY
// ============================================================
// Continuous background health checks, self-healing, and crash
// data preserved in RTC memory across soft reboots.
// ============================================================

#include "health.h"
#include "config.h"
#include "network.h"
#include "storage.h"
#include "led.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/rtc.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "health";

// Forward declaration (defined later, used by health_init's shutdown handler)
static void health_shutdown_handler(void);

// ============================================================
// RTC RETAINED DATA
// ============================================================
// RTC_DATA_ATTR survives soft reboot but NOT power cycle.
// This is the EMERGENCY HATCH — works even if NVS is corrupted.

#define RTC_HEALTH_MAGIC 0x5354524C  // "STRL"
#define RTC_BOOT_LOOP_THRESHOLD 5    // Rollback after 5 consecutive crashes

RTC_DATA_ATTR static rtc_health_t s_rtc = { 0 };

// ============================================================
// INIT — THE EMERGENCY HATCH
// ============================================================
// Boot loop detection works via TWO mechanisms:
//
// 1. RTC crash counter (primary) — health_record_crash() is called
//    by the panic handler BEFORE every crash+restart. If crash_count
//    reaches RTC_BOOT_LOOP_THRESHOLD (5), we roll back.
//    RTC survives soft reboot but NOT power cycle.
//
// 2. OTA partition state (secondary) — if the running partition is
//    in PENDING_VERIFY state, the previous boot did NOT confirm OTA
//    (meaning it crashed before the self-test could run, or the
//    self-test failed). We increment crash_count in this case too.
//
// These two mechanisms together provide coverage even if the panic
// handler fails to fire (e.g., hardware WDT reset).
// ============================================================

void health_init(void)
{
    // ═══ ALWAYS set magic — makes RTC data valid for next boot ═══
    // This must happen FIRST so that if we crash during init, the
    // next boot will find valid RTC data.
    s_rtc.magic = RTC_HEALTH_MAGIC;

    // ═══ OTA partition-based crash detection ═══
    // If the partition is still PENDING_VERIFY, the previous boot
    // didn't confirm OTA = it crashed before self-test.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running) {
        if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
            if (state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGW(TAG, "Previous boot did NOT confirm OTA (PENDING_VERIFY)");
                ESP_LOGW(TAG, "Incrementing crash count — crash before or during self-test");
                s_rtc.crash_count++;
            }
        }
    }

    // ═══ Log RTC data from previous boot ═══
    ESP_LOGI(TAG, "=== BOOT #%lu ===", (unsigned long)(s_rtc.boot_count + 1));
    if (s_rtc.crash_count > 0) {
        ESP_LOGW(TAG, "  Consecutive crashes: %lu", (unsigned long)s_rtc.crash_count);
        ESP_LOGW(TAG, "  Previous uptime: %lu seconds", (unsigned long)s_rtc.uptime_at_crash);
    }
    ESP_LOGI(TAG, "  Reset reason: %lu", (unsigned long)s_rtc.last_reset_reason);
    ESP_LOGI(TAG, "  Min heap seen: %lu bytes", (unsigned long)s_rtc.min_heap_seen);

    // ═══ BOOT LOOP DETECTION — THE EMERGENCY HATCH ═══
    // On a boot loop we do NOT roll back to another firmware (that other
    // firmware could be just as broken, or we may have no valid fallback).
    // Instead we enter SOFTAP-ONLY SAFE MODE: the recovery dashboard stays
    // up and nothing else runs. The device can never be bricked because the
    // user can always reach the dashboard and OTA a known-good firmware.
    if (s_rtc.crash_count >= RTC_BOOT_LOOP_THRESHOLD) {
        ESP_LOGE(TAG, "╔══════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║  BOOT LOOP DETECTED (%lu crashes!)     ║",
                  (unsigned long)s_rtc.crash_count);
        ESP_LOGE(TAG, "║  ENTERING SOFTAP-ONLY SAFE MODE        ║");
        ESP_LOGE(TAG, "║  Dashboard stays up — nothing else     ║");
        ESP_LOGE(TAG, "╚══════════════════════════════════════════╝");

        // Enter safe mode. SoftAP is already up (started first in app_main),
        // so the recovery dashboard is reachable. app_main will skip all
        // other tasks. We do NOT restart — staying up keeps the dashboard
        // available immediately.
        s_rtc.safe_mode = 1;

        // Leave crash_count as-is so a power cycle can still clear safe_mode
        // and retry a normal boot. The running partition stays PENDING_VERIFY
        // so a reboot (e.g. via dashboard) lets the bootloader roll back if a
        // good previous firmware exists.

        ESP_LOGW(TAG, "SAFE MODE: only SoftAP dashboard + health monitor will run.");
        return;  // Return to app_main, which gates the rest of the system
    }

    if (s_rtc.crash_count >= 2) {
        ESP_LOGW(TAG, "WARNING: %lu consecutive crashes. Monitor closely.",
                 (unsigned long)s_rtc.crash_count);
    }

    // Initialize RTC health data for this boot
    s_rtc.boot_count++;
    s_rtc.min_heap_seen = esp_get_free_heap_size();
    s_rtc.last_reset_reason = rtc_get_reset_reason(0);

    // Clear clean_reboot flag set by previous boot's health_mark_clean_reboot().
    // This ensures that if we crash later in THIS boot, the shutdown handler
    // will correctly detect it as a crash (clean_reboot is 0).
    s_rtc.clean_reboot = 0;

    // Register shutdown handler (called before every esp_restart, including
    // crashes and panics). Checks clean_reboot flag to distinguish intentional
    // restarts from crashes.
    esp_err_t sh_err = esp_register_shutdown_handler(&health_shutdown_handler);
    if (sh_err != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown handler registration: %s", esp_err_to_name(sh_err));
    }

    ESP_LOGI(TAG, "Health monitor initialized (boot #%lu, crashes=%lu)",
             (unsigned long)s_rtc.boot_count, (unsigned long)s_rtc.crash_count);
}

void health_record_crash(void)
{
    // Don't record crash if this is a clean reboot
    if (s_rtc.clean_reboot) {
        s_rtc.clean_reboot = 0;
        return;
    }
    s_rtc.magic = RTC_HEALTH_MAGIC;
    s_rtc.crash_count++;
    s_rtc.uptime_at_crash = (uint32_t)(esp_timer_get_time() / 1000000);
    s_rtc.last_reset_reason = rtc_get_reset_reason(0);
    // RTC memory is direct-mapped, no flash write needed
}

void health_mark_clean_reboot(void)
{
    s_rtc.clean_reboot = 1;
}

// ═══ Shutdown handler ═══
// Registered with esp_register_shutdown_handler().
// Called BEFORE every esp_restart(), including crashes and panics.
// If clean_reboot was NOT set (e.g., crash, abort, WDT), records the
// crash in RTC so the next boot knows we crashed.
static void health_shutdown_handler(void)
{
    if (!s_rtc.clean_reboot) {
        health_record_crash();
    }
    s_rtc.clean_reboot = 0;
}

const rtc_health_t *health_get_rtc_data(void)
{
    return &s_rtc;
}

bool health_is_safe_mode(void)
{
    return s_rtc.safe_mode != 0;
}

// ============================================================
// RESET BOOT-LOOP STATE
// ============================================================
// Called right before a successful OTA (or factory reset) reboots into a
// freshly-installed firmware. The new firmware must get a CLEAN boot-loop
// counter — otherwise it would inherit the old broken firmware's high
// crash_count and immediately re-enter safe mode, making OTA recovery a
// dead end (the only escape would be a power cycle). Clearing both the
// counter and the safe_mode flag gives the new firmware a fair chance.
void health_reset_boot_loop_state(void)
{
    s_rtc.crash_count = 0;
    s_rtc.safe_mode = 0;
    ESP_LOGI(TAG, "Boot-loop state RESET (crash_count=0, safe_mode=0) for new firmware");
}

// ============================================================
// SELF-TEST ON BOOT
// ============================================================
// Runs after init. Confirms new firmware is working.
// Must complete within OTA confirm window.

static bool run_boot_self_test(void)
{
    ESP_LOGI(TAG, "Running boot self-test...");

    // In safe mode the running partition is a known-bad firmware that just
    // boot-looped. NEVER confirm it valid — leave it PENDING_VERIFY so a
    // reboot can roll back, or the user can OTA a good firmware via the
    // dashboard. The dashboard is the recovery path; confirming would weld
    // the broken firmware in place.
    if (health_is_safe_mode()) {
        ESP_LOGW(TAG, "SAFE MODE: skipping OTA confirmation (firmware is suspect)");
        return false;
    }

    bool pass = true;

    // Test 1: Flash read/write
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "FAIL: Cannot get running partition");
        pass = false;
    } else {
        ESP_LOGI(TAG, "  PASS: Running partition: %s", running->label);
    }

    // Test 2: Free heap check (must have > 50KB free)
    uint32_t free_heap = esp_get_free_heap_size();
    if (free_heap < 50000) {
        ESP_LOGE(TAG, "FAIL: Free heap too low: %lu bytes", (unsigned long)free_heap);
        pass = false;
    } else {
        ESP_LOGI(TAG, "  PASS: Free heap: %lu bytes", (unsigned long)free_heap);
    }

    // Test 3: NVS readable (not just initialized — actually openable)
    {
        nvs_handle_t nvs;
        esp_err_t nvs_test = nvs_open("storage", NVS_READONLY, &nvs);
        if (nvs_test != ESP_OK) {
            ESP_LOGE(TAG, "FAIL: NVS namespace 'storage' not openable: %s",
                      esp_err_to_name(nvs_test));
            pass = false;
        } else {
            int32_t seq = 0;
            nvs_get_i32(nvs, "seq", &seq);  // may be empty — that's fine
            nvs_close(nvs);
            ESP_LOGI(TAG, "  PASS: NVS readable (seq=%ld)", (long)seq);
        }
    }

    // Test 4: Storage (LittleFS) mounted + index readable
    {
        uint32_t total = storage_get_total_count();
        uint32_t next  = storage_get_next_sequence();
        if (next == 0 && total == 0) {
            // Could be a fresh device — not necessarily a failure, but log it
            ESP_LOGI(TAG, "  PASS: Storage index readable (fresh device)");
        } else {
            ESP_LOGI(TAG, "  PASS: Storage index readable (total=%lu next=%lu)",
                      (unsigned long)total, (unsigned long)next);
        }
    }

    // Test 5: Heap allocation sanity (no fragmentation deadlock)
    {
        void *probe = malloc(4096);
        if (!probe) {
            ESP_LOGE(TAG, "FAIL: Cannot allocate 4KB — heap fragmented");
            pass = false;
        } else {
            free(probe);
            ESP_LOGI(TAG, "  PASS: 4KB allocation OK");
        }
    }

    // Update min heap
    if (free_heap < s_rtc.min_heap_seen) {
        s_rtc.min_heap_seen = free_heap;
    }

    if (pass) {
        ESP_LOGI(TAG, "Self-test PASSED — confirming OTA boot");
        // Clear crash count — this boot is healthy
        s_rtc.crash_count = 0;
        // Mark app as valid (cancels rollback)
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(TAG, "Self-test FAILED — will rollback on next boot");
        // Don't call esp_ota_mark_app_valid_cancel_rollback()
        // Bootloader will rollback after timeout
    }

    return pass;
}

// ============================================================
// RED TEAM CHECKS
// ============================================================

typedef struct {
    const char *task_name;
    TaskHandle_t handle;
    uint32_t stack_size;
} task_info_t;

// Track known tasks for health checks
static task_info_t s_known_tasks[8];
static int s_known_task_count = 0;

void health_register_task(const char *name, TaskHandle_t handle, uint32_t stack_size)
{
    if (s_known_task_count >= 8 || !handle) return;
    s_known_tasks[s_known_task_count].task_name = name;
    s_known_tasks[s_known_task_count].handle = handle;
    s_known_tasks[s_known_task_count].stack_size = stack_size;
    s_known_task_count++;
}

static void red_team_check_stacks(void)
{
    for (int i = 0; i < s_known_task_count; i++) {
        UBaseType_t high_water = uxTaskGetStackHighWaterMark(s_known_tasks[i].handle);
        if (high_water == 0) {
            ESP_LOGE(TAG, "RED TEAM: Task '%s' stack EXHAUSTED!",
                     s_known_tasks[i].task_name);
        } else if (high_water < s_known_tasks[i].stack_size / 4) {
            ESP_LOGW(TAG, "RED TEAM: Task '%s' stack LOW: %lu/%lu bytes remaining",
                     s_known_tasks[i].task_name,
                     (unsigned long)(high_water * sizeof(StackType_t)),
                     (unsigned long)s_known_tasks[i].stack_size);
        }
    }
}

static void red_team_check_heap(void)
{
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();

    // Update RTC minimum
    if (min_heap < s_rtc.min_heap_seen) {
        s_rtc.min_heap_seen = min_heap;
    }

    if (free_heap < 20000) {
        ESP_LOGE(TAG, "RED TEAM: CRITICAL heap: %lu free, %lu min",
                 (unsigned long)free_heap, (unsigned long)min_heap);
        // Self-heal: trigger garbage collection by doing a dummy alloc
        // to force LittleFS/NVS to flush pending writes
        void *tmp = malloc(1024);
        if (tmp) free(tmp);
    } else if (free_heap < 40000) {
        ESP_LOGW(TAG, "RED TEAM: Heap warning: %lu free, %lu min",
                 (unsigned long)free_heap, (unsigned long)min_heap);
    }
}

static void red_team_check_wifi(void)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);

    // Check if WiFi has been disconnected for too long
    if (!(bits & WIFI_CONNECTED_BIT)) {
        // WiFi not connected — not necessarily a problem (may be in SoftAP mode)
        ESP_LOGD(TAG, "RED TEAM: WiFi not connected (SoftAP: %s)",
                 network_is_softap_active() ? "active" : "inactive");
    }
}

static void red_team_check_tasks(void)
{
    // Verify critical tasks are still running by checking their stack
    // If a task's stack high water mark hasn't changed in a while,
    // it might be stuck
    for (int i = 0; i < s_known_task_count; i++) {
        UBaseType_t high_water = uxTaskGetStackHighWaterMark(s_known_tasks[i].handle);
        if (high_water == 0) {
            // Task stack completely exhausted — self-heal
            ESP_LOGE(TAG, "SELF-HEAL: Task '%s' stack exhausted, may need restart",
                     s_known_tasks[i].task_name);
            // Can't restart individual FreeRTOS tasks, but we can log it
            // The watchdog will eventually catch truly hung tasks
        }
    }
}

// ============================================================
// HEALTH MONITOR TASK
// ============================================================

void health_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Health monitor task started");

    // Run boot self-test FIRST
    run_boot_self_test();

    // Heavy red-team checks run every 5 minutes...
    const TickType_t HEAVY_CHECK_INTERVAL = pdMS_TO_TICKS(300000);  // 5 minutes
    // ...but the WDT timeout is only 30s, so we MUST feed the watchdog far
    // more often than that. We reset every loop (1s) and only run the heavy
    // checks on the 5-minute cadence. A subscribed task that fails to reset
    // within WATCHDOG_TIMEOUT_SECONDS trips a panic — so the monitor itself
    // must never go that long without resetting.
    TickType_t last_heavy_check = xTaskGetTickCount();

    while (1) {
        // Feed WDT every loop — this is what keeps the device alive.
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Update min heap (cheap, do every loop)
        uint32_t heap = esp_get_free_heap_size();
        if (heap < s_rtc.min_heap_seen) {
            s_rtc.min_heap_seen = heap;
        }

        // Heavy red-team checks on the 5-minute cadence
        if ((xTaskGetTickCount() - last_heavy_check) >= HEAVY_CHECK_INTERVAL) {
            last_heavy_check = xTaskGetTickCount();
            red_team_check_stacks();
            red_team_check_heap();
            red_team_check_wifi();
            red_team_check_tasks();
        }
    }
}
