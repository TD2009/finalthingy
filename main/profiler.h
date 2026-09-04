#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>

#define DEADLINE_US             10000
#define GUARD_TIME_US           0 // needed?
#define MAX_SYSTEM_TASKS        32

// Telemetry Utility 
#define UTIL_BASELINE           0.00f
#define UTIL_M0                 0.80f
#define UTIL_M1                 0.90f
#define UTIL_M2                 1.00f
#define PENALTY_DEADLINE_MISS   2.00f

typedef enum {
    PROFILER_MODE_OFF = 0,
    PROFILER_MODE_M0_MINIMAL,
    PROFILER_MODE_M1_STANDARD,
    PROFILER_MODE_M2_FULL
} ProfilerMode_t;

typedef enum {
    PHASE_CONTROL_NO_PROFILING = 0,
    PHASE_M0_TELEMETRY,
    PHASE_M1_TELEMETRY,
    PHASE_M2_TELEMETRY,
    PHASE_PROPOSED_ADAPTIVE
} ExperimentPhase_t;

typedef struct {
    uint32_t total_runs;
    uint32_t deadline_misses;
    uint32_t off_count;
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

extern uint32_t measured_overhead_m0_wcet_us;
extern uint32_t measured_overhead_m1_wcet_us;
extern uint32_t measured_overhead_m2_wcet_us;

extern uint32_t slack_threshold_m2_us;
extern uint32_t slack_threshold_m1_us;
extern uint32_t slack_threshold_m0_us;

void profiler_init(void);
void calibrate_profiler_overheads(void);
void esp_profiler_execute_sample(ProfilerMode_t mode);
ProfilerMode_t select_profiler_mode(int64_t raw_slack_us);
uint32_t percentile_index(float p, uint32_t n);

#endif