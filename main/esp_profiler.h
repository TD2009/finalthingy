#ifndef ESP_PROFILER_H
#define ESP_PROFILER_H

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Synchronization Telemetry Structure ---
typedef struct {
    volatile uint64_t total_wait_time_us;
    volatile uint64_t max_wait_time_us;
    volatile uint32_t acquisitions;
} sem_profiler_stats_t;

extern sem_profiler_stats_t g_sem_stats;

// Core Profiler Functions
void esp_profiler_init(void);
void esp_profiler_run_window(uint32_t window_ms);
BaseType_t xSemaphoreTakeProfiled(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);

#ifdef __cplusplus
}
#endif

#endif // ESP_PROFILER_H