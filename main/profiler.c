#include "profiler.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_system.h"

#define RING_BUFFER_SIZE 128

static TelemetryPacket_t g_telemetry_ring_buffer[RING_BUFFER_SIZE];
static uint32_t g_buffer_head = 0;

void profiler_init(void) {
    memset(g_telemetry_ring_buffer, 0, sizeof(g_telemetry_ring_buffer));
    g_buffer_head = 0;
}

/**
 * @brief O(1) Gatekeeper Engine
 * Evaluates remaining slack time against pre-calculated schedulability thresholds.
 */
inline profiler_mode_t profiler_evaluate_mode(uint64_t slack_us) {
    if (slack_us >= SLACK_HIGH_THRESHOLD_US) {
        return MODE_M2_FULL;
    } else if (slack_us >= SLACK_LOW_THRESHOLD_US) {
        return MODE_M1_STANDARD;
    } else {
        return MODE_M0_MINIMAL;
    }
}

/**
 * @brief Real Profiler Payload Execution
 * Performs actual memory writes, string formatting, and system hardware checks.
 */
void profiler_log_event(event_tier_t tier, uint32_t event_id, const char *tag, profiler_mode_t active_mode, bool is_adaptive) {
    // Check if event tier is permitted under active mode
    if (is_adaptive && (tier > (event_tier_t)active_mode)) {
        return; // Proactively shed telemetry to preserve hard deadline
    }

    uint32_t idx = g_buffer_head % RING_BUFFER_SIZE;
    g_buffer_head++;

    TelemetryPacket_t *pkt = &g_telemetry_ring_buffer[idx];
    pkt->timestamp_us = esp_timer_get_time();
    pkt->event_id = event_id;
    pkt->core_id = (uint8_t)xPortGetCoreID();
    
    snprintf(pkt->tag, sizeof(pkt->tag), "%s", tag);
    pkt->free_heap_bytes = esp_get_free_heap_size();
    pkt->stack_hwm_bytes = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}