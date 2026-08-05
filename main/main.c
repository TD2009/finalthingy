#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "profiler.h"
#include "workload.h"

static const char *TAG = "RTOS_BENCHMARK";
#define NUM_ITERATIONS 1000

/* BSS buffer allocation prevents stack overflow on task stack */
static uint64_t frame_latencies[NUM_ITERATIONS];

static int compare_uint64(const void *a, const void *b) {
    uint64_t arg1 = *(const uint64_t *)a;
    uint64_t arg2 = *(const uint64_t *)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

static void run_experiment_phase(ExperimentPhase_t phase, uint32_t seed, ExperimentStats_t *stats) {
    memset(stats, 0, sizeof(ExperimentStats_t));
    stats->total_runs = NUM_ITERATIONS;
    stats->min_latency_us = UINT64_MAX;

    seed_lcg(seed);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint32_t workload_us = get_simulated_workload_duration();
        uint64_t t_start = esp_timer_get_time();
        
        run_workload_kernel(workload_us);
        
        uint64_t t_compute_end = esp_timer_get_time();
        uint64_t compute_duration_us = t_compute_end - t_start;
        ProfilerMode_t active_mode = PROFILER_MODE_M0_MINIMAL;

        if (phase == PHASE_CONTROL_NO_PROFILING) {
            active_mode = PROFILER_MODE_M0_MINIMAL; 
        } 
        else if (phase == PHASE_STATIC_MAX_TELEMETRY) {
            active_mode = PROFILER_MODE_M2_FULL;
            esp_profiler_execute_sample(active_mode);
        } 
        else if (phase == PHASE_PROPOSED_ADAPTIVE) {
            int64_t residual_slack_us = (int64_t)DEADLINE_US - (int64_t)compute_duration_us - (int64_t)GUARD_TIME_US;
            active_mode = select_profiler_mode(residual_slack_us);
            esp_profiler_execute_sample(active_mode);
        }

        uint64_t t_frame_end = esp_timer_get_time();
        uint64_t total_frame_duration_us = t_frame_end - t_start;

        frame_latencies[i] = total_frame_duration_us;
        if (total_frame_duration_us < stats->min_latency_us) stats->min_latency_us = total_frame_duration_us;
        if (total_frame_duration_us > stats->max_latency_us) stats->max_latency_us = total_frame_duration_us;
        stats->total_latency_us += total_frame_duration_us;

        bool missed = (total_frame_duration_us > DEADLINE_US);
        if (missed) {
            stats->deadline_misses++;
            stats->accrued_utility -= PENALTY_DEADLINE_MISS;
        } else {
            if (phase == PHASE_CONTROL_NO_PROFILING) {
                stats->accrued_utility += UTIL_BASELINE;
            } else {
                switch (active_mode) {
                    case PROFILER_MODE_M0_MINIMAL:  stats->m0_count++; stats->accrued_utility += UTIL_M0; break;
                    case PROFILER_MODE_M1_STANDARD: stats->m1_count++; stats->accrued_utility += UTIL_M1; break;
                    case PROFILER_MODE_M2_FULL:     stats->m2_count++; stats->accrued_utility += UTIL_M2; break;
                }
            }
        }

        /* Paced delay keeps watchdog active without polluting frame measurement window */
        if (i % 50 == 0) {
            vTaskDelay(1);
        }
    }

    qsort(frame_latencies, NUM_ITERATIONS, sizeof(uint64_t), compare_uint64);
    stats->p95_latency_us = frame_latencies[percentile_index(0.95f, NUM_ITERATIONS)];
    stats->p99_latency_us = frame_latencies[percentile_index(0.99f, NUM_ITERATIONS)];
}

static void print_benchmark_summary(const char *phase_name, const ExperimentStats_t *stats) {
    float dmr = ((float)stats->deadline_misses / (float)stats->total_runs) * 100.0f;
    float max_possible_utility = (float)stats->total_runs * UTIL_M2;
    float uar = (stats->accrued_utility / max_possible_utility) * 100.0f;
    float avg_latency = (float)stats->total_latency_us / (float)stats->total_runs;

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " PHASE: %s", phase_name);
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Total Frame Runs   : %lu", stats->total_runs);
    ESP_LOGI(TAG, " Deadline Misses    : %lu (DMR: %.2f%%)", stats->deadline_misses, dmr);
    ESP_LOGI(TAG, " Accrued Utility    : %.2f / %.2f", stats->accrued_utility, max_possible_utility);
    ESP_LOGI(TAG, " Utility Ratio (UAR): %.2f%%", uar);
    ESP_LOGI(TAG, " Mode Selection     : M0=%lu | M1=%lu | M2=%lu", stats->m0_count, stats->m1_count, stats->m2_count);
    ESP_LOGI(TAG, " Frame Latency (us) : Min=%llu | Avg=%.2f | Max=%llu", stats->min_latency_us, avg_latency, stats->max_latency_us);
    ESP_LOGI(TAG, " Tail Latency  (us) : p95=%llu | p99=%llu", stats->p95_latency_us, stats->p99_latency_us);
    ESP_LOGI(TAG, "==================================================\n");
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing Refined Real-Time Hardware Benchmark Environment...");
    
    calibrate_profiler_overheads();

    const uint32_t BENCHMARK_SEED = 42;
    ExperimentStats_t control_stats, static_stats, adaptive_stats;

    run_experiment_phase(PHASE_CONTROL_NO_PROFILING, BENCHMARK_SEED, &control_stats);
    print_benchmark_summary("Control (No Profiling)", &control_stats);

    run_experiment_phase(PHASE_STATIC_MAX_TELEMETRY, BENCHMARK_SEED, &static_stats);
    print_benchmark_summary("Static Max (Always-M2)", &static_stats);

    run_experiment_phase(PHASE_PROPOSED_ADAPTIVE, BENCHMARK_SEED, &adaptive_stats);
    print_benchmark_summary("Proposed Slack-Aware Adaptive", &adaptive_stats);
}