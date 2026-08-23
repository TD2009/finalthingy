#include "workload.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "esp_log.h"

static const char *TAG = "WORKLOAD";

static uint32_t lcg_state = 123;
static uint32_t cycles_per_1000_iterations = 0;
static uint32_t cpu_freq_mhz = 0;


void seed_lcg(uint32_t seed) {
    lcg_state = seed;
}


uint32_t lcg_rand(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return lcg_state;
}


void calibrate_workload_kernel(void) {
    const uint32_t test_iterations = 200000;
    volatile uint32_t a = 1, b = 2, c = 0;


    cpu_freq_mhz = esp_clk_cpu_freq() / 1000000;
    ESP_LOGI(TAG, "Calibrating workload kernel at measured CPU freq: %lu MHz", cpu_freq_mhz);


    uint32_t start_cycles = esp_cpu_get_cycle_count();
    for (uint32_t i = 0; i < test_iterations; i++) {
        c = a * b + c + i;
        asm volatile("" ::: "memory");
    }
    uint32_t elapsed_cycles = esp_cpu_get_cycle_count() - start_cycles;


    cycles_per_1000_iterations = (elapsed_cycles * 1000) / test_iterations;
}


void run_workload_kernel(uint32_t target_duration_us) {
    if (cycles_per_1000_iterations == 0) {
        calibrate_workload_kernel();
    }


    uint64_t target_cycles = (uint64_t)target_duration_us * cpu_freq_mhz;
    uint32_t total_iterations = (uint32_t)((target_cycles * 1000) / cycles_per_1000_iterations);


    volatile uint32_t a = 1, b = 2, c = 0;
    for (uint32_t i = 0; i < total_iterations; i++) {
        c = a * b + c + i;
        asm volatile("" ::: "memory");
    }
}


uint32_t get_simulated_workload_duration(void) {
    uint32_t roll = lcg_rand() % 100;
    uint32_t base_duration = 0;


    if (roll < 80) {
        base_duration = 4695;
    } else if (roll < 90) {
        base_duration = 9200;
    } else {
        base_duration = 9680;
    }


    int32_t jitter = (int32_t)(lcg_rand() % 101) - 50;
    return (uint32_t)((int32_t)base_duration + jitter);
}