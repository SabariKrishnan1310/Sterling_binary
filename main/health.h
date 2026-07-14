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
 * Register a task for stack monitoring.
 * Call after xTaskCreatePinnedToCore succeeds.
 */
void health_register_task(const char *name, TaskHandle_t handle, uint32_t stack_size);
