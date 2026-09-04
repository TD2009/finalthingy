#include "workload.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "esp_log.h"

static const char *TAG = "WORKLOAD";

/* TACLeBench matrix1 exports */
extern void matrix1_init(void);
extern void matrix1_main(void);
extern int matrix1_return(void);

static uint32_t lcg_state = 123;
static uint32_t cycles_per_matrix1_run = 0;
static uint32_t cpu_freq_mhz = 0;

void seed_lcg(uint32_t seed) {
    lcg_state = seed;
}

/*
static uint32_t lcg_rand(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return lcg_state;
}
*/

static double lcg_rand(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return (double)lcg_state / 4294967295.0;
}

uint32_t get_simulated_workload_duration(void){
    uint32_t nominal_duration = (BASE_EXECUTION_PERCENT * DEADLINE_US) / 100;
    uint32_t variation_us = (VARIATION * DEADLINE_US) / 100;
    double duration_roll = lcg_rand() - 0.5;
    uint32_t base_duration = (uint32_t)(nominal_duration - 2.0 * variation_us * duration_roll);

    uint16_t workload_roll = (uint16_t)(lcg_rand() * 100.0);

    if (workload_roll < 80) {
        return base_duration;
    }

    if (workload_roll < 90) {
        return (uint32_t)((2.0 - VARIATION / 100.0) * base_duration);
    }

    return (uint32_t)((2.0 + VARIATION / 100.0) * base_duration);
}


void calibrate_workload_kernel(void) {
    const uint32_t test_runs = 500;
    matrix1_init();

    cpu_freq_mhz = esp_clk_cpu_freq() / 1000000;

    // Measure average CPU cycle count for matrix1_main execution
    uint32_t start_cycles = esp_cpu_get_cycle_count();
    for (uint32_t i = 0; i < test_runs; i++) {
        matrix1_main();
    }
    uint32_t elapsed_cycles = esp_cpu_get_cycle_count() - start_cycles;

    cycles_per_matrix1_run = elapsed_cycles / test_runs;
    ESP_LOGI(TAG, "Calibrated matrix1: %lu CPU cycles per run @ %lu MHz", 
             cycles_per_matrix1_run, cpu_freq_mhz);
}

void run_workload_kernel(uint32_t target_duration_us) {
    if (cycles_per_matrix1_run == 0U) {
        calibrate_workload_kernel();
    }

    if (cycles_per_matrix1_run == 0U) {
        ESP_LOGE(TAG, "Matrix calibration failed");
        return;
    }

    // Convert requested duration (us) into total required iterations of matrix1_main
    uint64_t target_cycles = (uint64_t)target_duration_us * cpu_freq_mhz;
    uint32_t iterations = (uint32_t)((target_cycles + cycles_per_matrix1_run - 1U) / cycles_per_matrix1_run);
    for (uint32_t i = 0; i < iterations; i++) {
        matrix1_main();
    }
}