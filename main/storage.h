#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

// =====================================================
// STORAGE FILES
// =====================================================

// NVS namespace used by storage.c for persistent state.
// Shared here so other modules (e.g. health self-test) reference the
// same namespace instead of a hardcoded, mismatched string.
#define NVS_NAMESPACE             "storage_ns"

#define STORAGE_PENDING_FILE      "/littlefs/pending.bin"

// =====================================================
// RECORD STATUS
// =====================================================

typedef enum {
    TAP_PENDING = 0,
    TAP_UPLOADED = 1
} tap_status_t;

// =====================================================
// TAP RECORD
// =====================================================

typedef struct __attribute__((packed))
{
    uint32_t seq;
    uint64_t timestamp;

    uint8_t uid_len;

    uint8_t status;

    char uid[32];

} tap_record_t;

// =====================================================
// STORAGE LIFECYCLE
// =====================================================

esp_err_t storage_init(void);

// =====================================================
// WRITE OPERATIONS
// =====================================================

esp_err_t storage_append_tap(
    const char *uid,
    uint32_t *out_seq
);

// =====================================================
// READ OPERATIONS
// =====================================================

esp_err_t storage_get_next_pending(
    tap_record_t *record
);

esp_err_t storage_read_at(
    uint32_t seq,
    tap_record_t *record
);

uint32_t storage_first_pending_seq(void);

// =====================================================
// STATE MANAGEMENT
// =====================================================

esp_err_t storage_mark_uploaded(
    uint32_t seq
);

// =====================================================
// METRICS
// =====================================================

uint32_t storage_get_next_sequence(void);

uint32_t storage_get_pending_count(void);

uint32_t storage_get_total_count(void);

// =====================================================
// DEBUG
// =====================================================

void storage_dump_stats(void);