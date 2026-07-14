#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// HEALTH MONITOR — Red Team + Self-Healing + Crash Recovery
// ============================================================
// Runs continuous background checks. Finds problems, fixes them.
// Stores crash info in RTC memory for post-mortem analysis.
// ============================================================

// ============================================================
// RTC RETAINED DATA — Survives soft reboot, NOT power cycle
// ============================================================

typedef struct __attribute__((packed)) {
    uint32_t magic;           // 0xHEALTH if valid
    uint32_t boot_count;      // Total boots
    uint32_t crash_count;     // Consecutive crash count (resets on clean boot)
    uint32_t last_reset_reason;
    uint32_t min_heap_seen;   // Lowest heap observed
    uint32_t uptime_at_crash; // Uptime seconds when last crash occurred
    uint32_t clean_reboot;    // Set before intentional reboot, cleared on boot
    uint32_t safe_mode;       // 1 = boot loop detected, run SoftAP-only safe mode
    uint32_t ota_last_attempt_ver; // Parsed version last attempted via OTA (0 = none)
    uint32_t ota_blacklist_ver;   // Version that failed to confirm; skip re-install
} rtc_health_t;

// ============================================================
// API
// ============================================================

/**
 * Initialize health monitor. Call once from app_main.
 * Reads RTC data, logs crash info if available.
 */
void health_init(void);

/**
 * Health monitor task. Runs continuously.
 * - Stack high water marks for all tasks
 * - Heap monitoring (alerts below 20KB)
 * - Task alive checks (detects hung tasks)
 * - Self-healing: restarts hung tasks
 *
 * Run as FreeRTOS task, core 0, priority 1.
 */
void health_monitor_task(void *pvParameters);

/**
 * Record a crash in RTC memory before reboot.
 * Call from panic handler or before esp_restart() in error paths.
 */
void health_record_crash(void);

/**
 * Mark this shutdown as intentional (clean reboot).
 * Call before esp_restart() for OTA success, system reboot, etc.
 * The shutdown handler will NOT record a crash if this is set.
 */
void health_mark_clean_reboot(void);

/**
 * Get current RTC health data (for diagnostics endpoint).
 */
const rtc_health_t *health_get_rtc_data(void);

/**
 * Returns true if the device booted into SoftAP-only safe mode
 * (boot loop detected). In safe mode only the SoftAP dashboard
 * and health monitor run — no RFID/upload/OTA/WiFi-connect tasks.
 * This guarantees the device can never be bricked: the recovery
 * dashboard is always reachable.
 */
bool health_is_safe_mode(void);

/**
 * Reset the boot-loop counters (crash_count + safe_mode) to zero.
 * Call immediately before rebooting into a freshly-installed firmware
 * (successful OTA or factory reset) so the new firmware starts with a
 * clean slate and is not trapped in safe mode by the previous firmware's
 * crash history.
 */
void health_reset_boot_loop_state(void);

/**
 * Record the firmware version (parsed int, e.g. 10102 for v1.1.2) we are
 * about to install via OTA. Used by the broken-OTA guard: if we later boot
 * back into a DIFFERENT version, the attempted build failed to confirm (bad
 * self-test / crash) and is blacklisted so we don't re-install it forever.
 */
void health_set_ota_attempt(int ver);

/**
 * Get the version last attempted via OTA (0 if none).
 */
uint32_t health_get_ota_last_attempt(void);

/**
 * Blacklist a firmware version that failed to confirm, so OTA skips it.
 */
void health_set_ota_blacklist(uint32_t ver);

/**
 * Get the blacklisted OTA version (0 if none).
 */
uint32_t health_get_ota_blacklist(void);

/**
 * Clear OTA attempt + blacklist state. Call on factory reset so a fresh
 * start does not inherit a stale blacklist.
 */
void health_clear_ota_state(void);

/**
 * Register a task for stack monitoring.
 * Call after xTaskCreatePinnedToCore succeeds.
 */
void health_register_task(const char *name, TaskHandle_t handle, uint32_t stack_size);
