#ifndef WORKLOAD_H
#define WORKLOAD_H

#define DEADLINE_US 10000
#define VARIATION 3
#define BASE_EXECUTION_PERCENT 45

#include <stdint.h>

void seed_lcg(uint32_t seed);
uint32_t get_simulated_workload_duration(void);
void calibrate_workload_kernel(void);
void run_workload_kernel(uint32_t workload_us);

#endif /* WORKLOAD_H */