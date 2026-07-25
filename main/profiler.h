#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Telemetry Operational Modes
typedef enum {
    MODE_M0_MINIMAL  = 0, // Tier 1 only (Safety-critical bounds)
    MODE_M1_STANDARD = 1, // Tier 1 + Tier 2 (State & execution checkpoints)
    MODE_M2_FULL     = 2  // Tier 1 + Tier 2 + Tier 3 (High-frequency trace)
} profiler_mode_t;

// Telemetry Event Tiers
typedef enum {
    TIER_1_CRITICAL = 0,
    TIER_2_STATE    = 1,
    TIER_3_TRACE    = 2
} event_tier_t;

// Real Hardware Telemetry Ring Buffer Packet
typedef struct {
    uint64_t timestamp_us;
    uint32_t event_id;
    uint8_t  core_id;
    char     tag[16];
    uint32_t free_heap_bytes;
    uint32_t stack_hwm_bytes;
} TelemetryPacket_t;

// Schedulability Thresholds for Gatekeeper Engine (Microseconds)
#define SLACK_HIGH_THRESHOLD_US  1200   // >= 1.2ms slack -> Mode M2
#define SLACK_LOW_THRESHOLD_US   400    // >= 0.4ms slack -> Mode M1

// Profiler Engine API
void profiler_init(void);
profiler_mode_t profiler_evaluate_mode(uint64_t slack_us);
void profiler_log_event(event_tier_t tier, uint32_t event_id, const char *tag, profiler_mode_t active_mode, bool is_adaptive);

#endif // PROFILER_H