#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stdint.h>

void seed_lcg(uint32_t seed);
uint32_t lcg_rand(void);
void run_workload_kernel(uint32_t target_duration_us);
uint32_t get_simulated_workload_duration(void);

#endif // WORKLOAD_H