#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>

#define DEADLINE_US             10000   // 10 ms hard deadline
#define GUARD_TIME_US           200     // Context-switch guard band
#define SLACK_THRESHOLD_M2_US   1200    // Slack >= 1200 us -> Mode 2 (Full)
#define SLACK_THRESHOLD_M1_US   400     // Slack >= 400 us  -> Mode 1 (Standard)

#define UTIL_BASELINE           0.80f
#define UTIL_M0                 0.80f
#define UTIL_M1                 0.90f
#define UTIL_M2                 1.00f
#define PENALTY_DEADLINE_MISS   1.00f

typedef enum {
    PROFILER_MODE_M0_MINIMAL = 0,
    PROFILER_MODE_M1_STANDARD,
    PROFILER_MODE_M2_FULL
} ProfilerMode_t;

typedef enum {
    PHASE_CONTROL_NO_PROFILING = 0,
    PHASE_STATIC_MAX_TELEMETRY,
    PHASE_PROPOSED_ADAPTIVE
} ExperimentPhase_t;

typedef struct {
    uint32_t total_runs;
    uint32_t deadline_misses;
    uint32_t m0_count;
    uint32_t m1_count;
    uint32_t m2_count;
    float accrued_utility;
    uint64_t min_latency_us;
    uint64_t max_latency_us;
    uint64_t total_latency_us;
    uint64_t p95_latency_us;
    uint64_t p99_latency_us;
} ExperimentStats_t;

extern uint32_t measured_overhead_m0_us;
extern uint32_t measured_overhead_m1_us;
extern uint32_t measured_overhead_m2_us;

void calibrate_profiler_overheads(void);
void esp_profiler_execute_sample(ProfilerMode_t mode);
ProfilerMode_t select_profiler_mode(int64_t residual_slack_us);
uint32_t percentile_index(float p, uint32_t n);

#endif // PROFILER_H