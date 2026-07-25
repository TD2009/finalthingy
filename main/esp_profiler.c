#include "esp_profiler.h"
#include <string.h>
#include "esp_freertos_hooks.h"

#define MAX_TRACKED_TASKS 32

static volatile uint32_t g_tick_count = 0;
sem_profiler_stats_t g_sem_stats = {0, 0, 0};

static TaskStatus_t s_start_tasks[MAX_TRACKED_TASKS];
static TaskStatus_t s_end_tasks[MAX_TRACKED_TASKS];
static bool s_tick_hook_registered = false;

// Native ESP-IDF Tick Hook signature: returns void, takes no args
static void IRAM_ATTR profiler_tick_hook(void) {
    __atomic_fetch_add(&g_tick_count, 1, __ATOMIC_RELAXED);
}

void esp_profiler_init(void) {
    g_tick_count = 0;
    g_sem_stats.total_wait_time_us = 0;
    g_sem_stats.max_wait_time_us = 0;
    g_sem_stats.acquisitions = 0;

    if (!s_tick_hook_registered) {
        esp_register_freertos_tick_hook(profiler_tick_hook);
        s_tick_hook_registered = true;
    }
}

BaseType_t xSemaphoreTakeProfiled(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    uint64_t start_time = esp_timer_get_time();
    BaseType_t result = xSemaphoreTake(xSemaphore, xTicksToWait);
    
    if (result == pdTRUE) {
        uint64_t elapsed_us = esp_timer_get_time() - start_time;
        __atomic_fetch_add(&g_sem_stats.total_wait_time_us, elapsed_us, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_sem_stats.acquisitions, 1, __ATOMIC_RELAXED);
        
        uint64_t current_max = g_sem_stats.max_wait_time_us;
        while (elapsed_us > current_max) {
            if (__atomic_compare_exchange_n(&g_sem_stats.max_wait_time_us, &current_max, elapsed_us, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                break;
            }
        }
    }
    return result;
}

void esp_profiler_run_window(uint32_t window_ms) {
    uint32_t start_total_runtime = 0;
    uint32_t end_total_runtime = 0;

    // Take Start Snapshot
    UBaseType_t start_count = uxTaskGetSystemState(s_start_tasks, MAX_TRACKED_TASKS, &start_total_runtime);
    uint32_t start_ticks = __atomic_load_n(&g_tick_count, __ATOMIC_RELAXED);
    uint64_t start_wall_time = esp_timer_get_time();

    // Sampling Delay Window
    vTaskDelay(pdMS_TO_TICKS(window_ms));

    // Take End Snapshot
    uint64_t end_wall_time = esp_timer_get_time();
    uint32_t end_ticks = __atomic_load_n(&g_tick_count, __ATOMIC_RELAXED);
    UBaseType_t end_count = uxTaskGetSystemState(s_end_tasks, MAX_TRACKED_TASKS, &end_total_runtime);

    uint64_t wall_time_delta_us = end_wall_time - start_wall_time;
    uint32_t total_runtime_delta = end_total_runtime - start_total_runtime;
    uint32_t tick_delta = end_ticks - start_ticks;

    // Calculate scheduler tick rate (ticks/sec) over the sampling window
    float system_tick_rate = (wall_time_delta_us > 0) ? 
        ((float)tick_delta / ((float)wall_time_delta_us / 1000000.0f)) : 0.0f;

    TaskHandle_t idle0_handle = xTaskGetIdleTaskHandleForCore(0);
    TaskHandle_t idle1_handle = (portNUM_PROCESSORS > 1) ? xTaskGetIdleTaskHandleForCore(1) : NULL;

    float core0_idle_pct = 0.0f;
    float core1_idle_pct = 0.0f;

    printf("\n========================================================================\n");
    printf("           IEEE 1003.1 / ISO 24765 REAL-TIME METRICS REPORT            \n");
    printf("========================================================================\n");
    printf("%-16s %-5s %-5s %-12s %-12s %-8s\n", 
           "Task Name", "Core", "State", "CPU Time(us)", "Usage %", "HWM (B)");
    printf("------------------------------------------------------------------------\n");

    for (UBaseType_t i = 0; i < end_count; i++) {
        uint32_t task_runtime_delta = 0;

        for (UBaseType_t j = 0; j < start_count; j++) {
            if (s_end_tasks[i].xHandle == s_start_tasks[j].xHandle) {
                task_runtime_delta = s_end_tasks[i].ulRunTimeCounter - s_start_tasks[j].ulRunTimeCounter;
                break;
            }
        }

        float task_cpu_pct = (total_runtime_delta > 0) ? 
            (((float)task_runtime_delta / (float)total_runtime_delta) * 100.0f) : 0.0f;

        if (s_end_tasks[i].xHandle == idle0_handle) {
            core0_idle_pct = task_cpu_pct;
        } else if (idle1_handle != NULL && s_end_tasks[i].xHandle == idle1_handle) {
            core1_idle_pct = task_cpu_pct;
        }

        char state_char = 'U';
        switch (s_end_tasks[i].eCurrentState) {
            case eRunning:   state_char = 'R'; break;
            case eReady:     state_char = 'Y'; break;
            case eBlocked:   state_char = 'B'; break;
            case eSuspended: state_char = 'S'; break;
            case eDeleted:   state_char = 'D'; break;
            case eInvalid:   state_char = 'I'; break;
        }

        int core_id = (int)xTaskGetCoreID(s_end_tasks[i].xHandle);

        printf("%-16s %-5d %-5c %-12lu %-12.2f %-8lu\n",
               s_end_tasks[i].pcTaskName,
               core_id,
               state_char,
               (unsigned long)task_runtime_delta,
               task_cpu_pct,
               (unsigned long)s_end_tasks[i].usStackHighWaterMark);
    }

    printf("------------------------------------------------------------------------\n");
    printf("IEEE Memory Diagnostics:\n");
    printf("  Heap Free (Bytes)               : %lu Bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("  Heap Minimum Watermark (Bytes)  : %lu Bytes\n", (unsigned long)esp_get_minimum_free_heap_size());

    printf("\nIEEE OS Micro-Architecture Metrics:\n");
    printf("  System Scheduler Rate           : %.2f ticks/sec\n", system_tick_rate);
    printf("  Core 0 Idle Utilization         : %.2f%%\n", core0_idle_pct);
    printf("  Core 1 Idle Utilization         : %.2f%%\n", core1_idle_pct);

    printf("\nIEEE Synchronization Metrics (Mutex / Semaphore):\n");
    uint32_t acquisitions = __atomic_load_n(&g_sem_stats.acquisitions, __ATOMIC_RELAXED);
    if (acquisitions > 0) {
        uint64_t total_wait = __atomic_load_n(&g_sem_stats.total_wait_time_us, __ATOMIC_RELAXED);
        float avg_wait = (float)total_wait / (float)acquisitions;
        printf("  Total Acquisitions              : %lu\n", (unsigned long)acquisitions);
        printf("  Average Wait Time               : %.2f us\n", avg_wait);
        printf("  Maximum Wait Time               : %llu us\n", (unsigned long long)g_sem_stats.max_wait_time_us);
    } else {
        printf("  Synchronization Wait Time       : No Profiled Acquisitions Recorded\n");
    }
    printf("========================================================================\n");
}