#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "profiler.h"
#include "workload.h"

static const char *TAG = "RTOS_BENCHMARK";
#define NUM_ITERATIONS 1000

static const uint32_t seeds[] = {
    1, 12, 123, 1234, 12345, 123456, 1234567, 12345678, 123456789, 1234567890
};
#define NUM_SEEDS (sizeof(seeds) / sizeof(seeds[0]))

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
        ProfilerMode_t active_mode = PROFILER_MODE_OFF;

        if (phase == PHASE_CONTROL_NO_PROFILING) {
            active_mode = PROFILER_MODE_OFF;
        }
        else if (phase == PHASE_STATIC_MAX_TELEMETRY) {
            active_mode = PROFILER_MODE_M2_FULL;
            esp_profiler_execute_sample(active_mode);
        }
        else if (phase == PHASE_PROPOSED_ADAPTIVE) {
            int64_t raw_slack_us = (int64_t)DEADLINE_US - (int64_t)compute_duration_us;
            active_mode = select_profiler_mode(raw_slack_us);
            if (active_mode != PROFILER_MODE_OFF) {
                esp_profiler_execute_sample(active_mode);
            }
        }

        uint64_t t_frame_end = esp_timer_get_time();
        uint64_t total_frame_duration_us = t_frame_end - t_start;

        frame_latencies[i] = total_frame_duration_us;
        if (total_frame_duration_us < stats->min_latency_us) stats->min_latency_us = total_frame_duration_us;
        if (total_frame_duration_us > stats->max_latency_us) stats->max_latency_us = total_frame_duration_us;
        stats->total_latency_us += total_frame_duration_us;

        switch (active_mode) {
            case PROFILER_MODE_M0_MINIMAL:  stats->m0_count++; break;
            case PROFILER_MODE_M1_STANDARD: stats->m1_count++; break;
            case PROFILER_MODE_M2_FULL:     stats->m2_count++; break;
            case PROFILER_MODE_OFF:         stats->off_count++; break;
        }

        bool missed = (total_frame_duration_us > DEADLINE_US);
        if (missed) {
            stats->deadline_misses++;
            stats->accrued_utility -= PENALTY_DEADLINE_MISS;
        } else {
            if (phase == PHASE_CONTROL_NO_PROFILING) {
                stats->accrued_utility += UTIL_BASELINE;
            } else {
                switch (active_mode) {
                    case PROFILER_MODE_M0_MINIMAL:  stats->accrued_utility += UTIL_M0; break;
                    case PROFILER_MODE_M1_STANDARD: stats->accrued_utility += UTIL_M1; break;
                    case PROFILER_MODE_M2_FULL:     stats->accrued_utility += UTIL_M2; break;
                    case PROFILER_MODE_OFF:         stats->accrued_utility += UTIL_BASELINE; break;
                }
            }
        }

        if (i % 50 == 0) {
            vTaskDelay(1);
        }
    }

    qsort(frame_latencies, NUM_ITERATIONS, sizeof(uint64_t), compare_uint64);
    stats->p95_latency_us = frame_latencies[percentile_index(0.95f, NUM_ITERATIONS)];
    stats->p99_latency_us = frame_latencies[percentile_index(0.99f, NUM_ITERATIONS)];
}

static void print_benchmark_summary(const char *phase_name, uint32_t seed, const ExperimentStats_t *stats) {
    float dmr = ((float)stats->deadline_misses / (float)stats->total_runs) * 100.0f;
    float max_possible_utility = (float)stats->total_runs * UTIL_M2;
    float uar = (stats->accrued_utility / max_possible_utility) * 100.0f;
    float avg_latency = (float)stats->total_latency_us / (float)stats->total_runs;

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " PHASE: %s [Seed: %lu]", phase_name, seed);
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Total Frame Runs   : %lu", stats->total_runs);
    ESP_LOGI(TAG, " Deadline Misses    : %lu (DMR: %.2f%%)", stats->deadline_misses, dmr);
    ESP_LOGI(TAG, " Accrued Utility    : %.2f / %.2f", stats->accrued_utility, max_possible_utility);
    ESP_LOGI(TAG, " Utility Ratio (UAR): %.2f%%", uar);
    ESP_LOGI(TAG, " Mode Selection     : M0=%lu | M1=%lu | M2=%lu | OFF=%lu",
             stats->m0_count, stats->m1_count, stats->m2_count, stats->off_count);
    ESP_LOGI(TAG, " Frame Latency (us) : Min=%llu | Avg=%.2f | Max=%llu", stats->min_latency_us, avg_latency, stats->max_latency_us);
    ESP_LOGI(TAG, " Tail Latency  (us) : p95=%llu | p99=%llu", stats->p95_latency_us, stats->p99_latency_us);
    ESP_LOGI(TAG, "==================================================\n");
}

static void print_aggregated_summary(const char *phase_name, const ExperimentStats_t runs[], size_t num_seeds) {
    float dmr_sum = 0.0f, uar_sum = 0.0f;
    float avg_lat_sum = 0.0f, p95_sum = 0.0f, p99_sum = 0.0f;
    
    float dmrs[num_seeds];
    float uars[num_seeds];

    for (size_t i = 0; i < num_seeds; i++) {
        float dmr = ((float)runs[i].deadline_misses / (float)runs[i].total_runs) * 100.0f;
        float max_util = (float)runs[i].total_runs * UTIL_M2;
        float uar = (runs[i].accrued_utility / max_util) * 100.0f;
        float avg_lat = (float)runs[i].total_latency_us / (float)runs[i].total_runs;

        dmrs[i] = dmr;
        uars[i] = uar;

        dmr_sum += dmr;
        uar_sum += uar;
        avg_lat_sum += avg_lat;
        p95_sum += (float)runs[i].p95_latency_us;
        p99_sum += (float)runs[i].p99_latency_us;
    }

    float dmr_mean = dmr_sum / (float)num_seeds;
    float uar_mean = uar_sum / (float)num_seeds;

    float dmr_var_sum = 0.0f, uar_var_sum = 0.0f;
    for (size_t i = 0; i < num_seeds; i++) {
        dmr_var_sum += (dmrs[i] - dmr_mean) * (dmrs[i] - dmr_mean);
        uar_var_sum += (uars[i] - uar_mean) * (uars[i] - uar_mean);
    }

    float dmr_stddev = sqrtf(dmr_var_sum / (float)num_seeds);
    float uar_stddev = sqrtf(uar_var_sum / (float)num_seeds);

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " AGGREGATED METRICS (%zu SEEDS): %s", num_seeds, phase_name);
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " DMR (Mean +/- StdDev) : %.2f%% (+/- %.2f%%)", dmr_mean, dmr_stddev);
    ESP_LOGI(TAG, " UAR (Mean +/- StdDev) : %.2f%% (+/- %.2f%%)", uar_mean, uar_stddev);
    ESP_LOGI(TAG, " Mean Frame Latency    : %.2f us", avg_lat_sum / (float)num_seeds);
    ESP_LOGI(TAG, " Mean Tail Latency     : p95=%.1f us | p99=%.1f us", p95_sum / (float)num_seeds, p99_sum / (float)num_seeds);
    ESP_LOGI(TAG, "==================================================\n");
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing Dynamic WCET-Calibrated Real-Time Benchmark Environment...");
    profiler_init();
    calibrate_profiler_overheads();
    calibrate_workload_kernel();

    ExperimentStats_t control_runs[NUM_SEEDS];
    ExperimentStats_t static_runs[NUM_SEEDS];
    ExperimentStats_t adaptive_runs[NUM_SEEDS];

    for (size_t s = 0; s < NUM_SEEDS; s++) {
        uint32_t current_seed = seeds[s];
        ESP_LOGI(TAG, ">>> STARTING EXPERIMENTAL RUN FOR SEED %lu (%zu/%zu) <<<", current_seed, s + 1, NUM_SEEDS);

        run_experiment_phase(PHASE_CONTROL_NO_PROFILING, current_seed, &control_runs[s]);
        print_benchmark_summary("Control (No Profiling)", current_seed, &control_runs[s]);

        run_experiment_phase(PHASE_STATIC_MAX_TELEMETRY, current_seed, &static_runs[s]);
        print_benchmark_summary("Static Max (Always-M2)", current_seed, &static_runs[s]);

        run_experiment_phase(PHASE_PROPOSED_ADAPTIVE, current_seed, &adaptive_runs[s]);
        print_benchmark_summary("Proposed Slack-Aware Adaptive", current_seed, &adaptive_runs[s]);
    }

    ESP_LOGI(TAG, "**************************************************");
    ESP_LOGI(TAG, "           FINAL AGGREGATED BENCHMARK             ");
    ESP_LOGI(TAG, "**************************************************\n");

    print_aggregated_summary("Control (No Profiling)", control_runs, NUM_SEEDS);
    print_aggregated_summary("Static Max (Always-M2)", static_runs, NUM_SEEDS);
    print_aggregated_summary("Proposed Slack-Aware Adaptive", adaptive_runs, NUM_SEEDS);
}