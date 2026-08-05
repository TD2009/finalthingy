#include "workload.h"
#include "esp_timer.h"

static uint32_t lcg_state = 42;

void seed_lcg(uint32_t seed) {
    lcg_state = seed;
}

uint32_t lcg_rand(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return lcg_state;
}

void run_workload_kernel(uint32_t target_duration_us) {
    uint64_t start = esp_timer_get_time();
    volatile uint32_t a = 1, b = 2, c = 0;
    while ((esp_timer_get_time() - start) < target_duration_us) {
        c = a * b + c;
    }
}

uint32_t get_simulated_workload_duration(void) {
    uint32_t roll = lcg_rand() % 100;
    if (roll < 80) {
        return 4695; // Normal workload (~4.7 ms)
    } else if (roll < 90) {
        return 9200; // Medium workload spike (~9.2 ms)
    } else {
        return 9680; // Severe workload spike (~9.68 ms)
    }
}