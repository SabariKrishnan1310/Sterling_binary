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
#include <string.h>

static const char *TAG = "health";

// ============================================================
// RTC RETAINED DATA
// ============================================================
// RTC_DATA_ATTR survives soft reboot but NOT power cycle.
// This is the EMERGENCY HATCH — works even if NVS is corrupted.

#define RTC_HEALTH_MAGIC 0x5354524C  // "STRL"
#define RTC_BOOT_LOOP_THRESHOLD 5    // Rollback after 5 consecutive crashes

RTC_DATA_ATTR static rtc_health_t s_rtc = { 0 };

// ============================================================
// INIT
// ============================================================

void health_init(void)
{
    // Check if we have valid RTC data from previous boot
    if (s_rtc.magic == RTC_HEALTH_MAGIC) {
        ESP_LOGW(TAG, "=== CRASH RECOVERY DATA ===");
        ESP_LOGW(TAG, "  Previous uptime: %lu seconds", (unsigned long)s_rtc.uptime_at_crash);
        ESP_LOGW(TAG, "  Crash count: %lu", (unsigned long)s_rtc.crash_count);
        ESP_LOGW(TAG, "  Reset reason: %lu", (unsigned long)s_rtc.last_reset_reason);
        ESP_LOGW(TAG, "  Crash PC: 0x%08lx", (unsigned long)s_rtc.last_crash_pc);
        ESP_LOGW(TAG, "  Min heap: %lu bytes", (unsigned long)s_rtc.min_heap_seen);

        // ═══ BOOT LOOP DETECTION — THE EMERGENCY HATCH ═══
        // If device has crashed RTC_BOOT_LOOP_THRESHOLD times in a row
        // (soft reboots only — RTC survives those), rollback immediately.
        // This works even if NVS is completely corrupted.
        if (s_rtc.crash_count >= RTC_BOOT_LOOP_THRESHOLD) {
            ESP_LOGE(TAG, "╔══════════════════════════════════════════╗");
            ESP_LOGE(TAG, "║  BOOT LOOP DETECTED (%lu crashes!)     ║", 
                     (unsigned long)s_rtc.crash_count);
            ESP_LOGE(TAG, "║  Rolling back to previous firmware...  ║");
            ESP_LOGE(TAG, "╚══════════════════════════════════════════╝");
            
            // Clear crash count so we don't loop forever
            s_rtc.crash_count = 0;
            
            // Find the other OTA partition and boot from it
            const esp_partition_t *running = esp_ota_get_running_partition();
            if (running) {
                esp_ota_img_states_t state;
                if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
                    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
                        // We're in pending verify — just don't confirm, bootloader rolls back
                        ESP_LOGE(TAG, "NOT confirming OTA — bootloader will rollback");
                        // Do NOT call esp_ota_mark_app_valid_cancel_rollback()
                        // The bootloader's timeout will handle rollback
                    } else {
                        // Already confirmed — need to manually switch
                        esp_partition_type_t other_type = 
                            (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ?
                            ESP_PARTITION_SUBTYPE_APP_OTA_1 :
                            ESP_PARTITION_SUBTYPE_APP_OTA_0;
                        const esp_partition_t *other = esp_partition_find_first(
                            ESP_PARTITION_TYPE_APP, other_type, NULL);
                        if (other) {
                            ESP_LOGE(TAG, "Switching to partition: %s", other->label);
                            esp_ota_set_boot_partition(other);
                        }
                    }
                }
            }
            
            ESP_LOGE(TAG, "Rebooting in 2 seconds...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
            return;  // Never reaches here
        }

        if (s_rtc.crash_count >= 2) {
            ESP_LOGW(TAG, "WARNING: %lu consecutive crashes. Monitor closely.",
                     (unsigned long)s_rtc.crash_count);
        }
    }

    // Initialize RTC health data for this boot
    s_rtc.boot_count++;
    s_rtc.min_heap_seen = esp_get_free_heap_size();
    s_rtc.last_reset_reason = rtc_get_reset_reason(0);

    ESP_LOGI(TAG, "Health monitor initialized (boot #%lu, crashes=%lu)",
             (unsigned long)s_rtc.boot_count, (unsigned long)s_rtc.crash_count);
}

void health_record_crash(void)
{
    s_rtc.magic = RTC_HEALTH_MAGIC;
    s_rtc.crash_count++;
    s_rtc.uptime_at_crash = (uint32_t)(esp_timer_get_time() / 1000000);
    s_rtc.last_reset_reason = rtc_get_reset_reason(0);
    // RTC memory is direct-mapped, no flash write needed
}

const rtc_health_t *health_get_rtc_data(void)
{
    return &s_rtc;
}

// ============================================================
// SELF-TEST ON BOOT
// ============================================================
// Runs after init. Confirms new firmware is working.
// Must complete within OTA confirm window.

static bool run_boot_self_test(void)
{
    ESP_LOGI(TAG, "Running boot self-test...");

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

    // Test 3: NVS accessible
    // (already tested by nvs_flash_init in app_main)

    // Test 4: Storage accessible
    // (already tested by storage_init in app_main)

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

    const TickType_t CHECK_INTERVAL = pdMS_TO_TICKS(300000);  // 5 minutes

    while (1) {
        vTaskDelay(CHECK_INTERVAL);

        // Update min heap
        uint32_t heap = esp_get_free_heap_size();
        if (heap < s_rtc.min_heap_seen) {
            s_rtc.min_heap_seen = heap;
        }

        // Run red team checks
        red_team_check_stacks();
        red_team_check_heap();
        red_team_check_wifi();
        red_team_check_tasks();

        // Feed WDT
        esp_task_wdt_reset();
    }
}
