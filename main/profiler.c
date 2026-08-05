#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/temperature_sensor.h"
#include "profiler.h"

static const char *TAG = "PROFILER";

// Hardware & Queue handles
static temperature_sensor_handle_t temp_handle = NULL;
static QueueHandle_t example_queue = NULL;

// Calibrated overhead globals declared in profiler.h
uint32_t measured_overhead_m0_us = 0;
uint32_t measured_overhead_m1_us = 0;
uint32_t measured_overhead_m2_us = 0;

// Data payload structure for heavy M2 sampling
typedef struct {
    float temp_celsius;
    float heap_fragmentation;
    uint32_t task_count;
    char formatted_json[256];
} profiler_m2_data_t;

void profiler_init(void) {
    // Initialize temperature sensor peripheral
    temperature_sensor_config_t temp_sensor_config = {
        .range_min = -10,
        .range_max = 80,
    };
    if (temperature_sensor_install(&temp_sensor_config, &temp_handle) == ESP_OK) {
        temperature_sensor_enable(temp_handle);
    }

    // Initialize mock queue for IPC metric monitoring
    example_queue = xQueueCreate(10, sizeof(int));
}

// ------------------------------------------------------------------
// Mode Implementation Execution
// ------------------------------------------------------------------

static void execute_m0(void) {
    // 1. Hardware Cycle Counter Snapshot
    uint32_t cycles = esp_cpu_get_cycle_count();

    // 2. Minimum Heap Watermark Read (Atomic stored value read)
    size_t min_heap = esp_get_minimum_free_heap_size();

    // 3. Current Task Handle Snapshot
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();

    (void)cycles;
    (void)min_heap;
    (void)handle;
}

static void execute_m1(void) {
    // 1. Current Free Heap Check
    size_t free_heap = esp_get_free_heap_size();

    // 2. Heap Fragmentation Ratio
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    float fragmentation = 0.0f;
    if (free_heap > 0) {
        fragmentation = 1.0f - ((float)largest_block / (float)free_heap);
    }

    // 3. IPC Queue Occupancy Check
    UBaseType_t msgs_waiting = 0;
    UBaseType_t spaces_left = 0;
    if (example_queue != NULL) {
        msgs_waiting = uxQueueMessagesWaiting(example_queue);
        spaces_left = uxQueueSpacesAvailable(example_queue);
    }

    (void)free_heap;
    (void)fragmentation;
    (void)msgs_waiting;
    (void)spaces_left;
}

static void execute_m2(void) {
    profiler_m2_data_t data;
    memset(&data, 0, sizeof(profiler_m2_data_t));

    // 1. Peripheral Analog Hardware Read
    if (temp_handle != NULL) {
        temperature_sensor_get_celsius(temp_handle, &data.temp_celsius);
    }

    // 2. Heap Memory Heap Block Search & Fragmentation
    size_t free_heap = esp_get_free_heap_size();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    data.heap_fragmentation = 0.0f;
    if (free_heap > 0) {
        data.heap_fragmentation = 1.0f - ((float)largest_block / (float)free_heap);
    }

    // 3. Full Task Table Traversal and Stack Watermarking Scan
    UBaseType_t task_array_size = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_status_array = pvPortMalloc(task_array_size * sizeof(TaskStatus_t));

    if (task_status_array != NULL) {
        uint32_t total_run_time = 0;
        data.task_count = uxTaskGetSystemState(task_status_array, task_array_size, &total_run_time);

        // Linear memory scan across active task stack frames
        for (UBaseType_t i = 0; i < data.task_count; i++) {
            UBaseType_t stack_watermark = uxTaskGetStackHighWaterMark(task_status_array[i].xHandle);
            (void)stack_watermark;
        }

        vPortFree(task_status_array);
    }

    // 4. String Serialization & Formatting Overhead
    snprintf(data.formatted_json, sizeof(data.formatted_json),
             "{\"temp\":%.2f,\"frag\":%.2f,\"tasks\":%u,\"free_heap\":%u}",
             data.temp_celsius, data.heap_fragmentation, (unsigned int)data.task_count, (unsigned int)free_heap);
}

// ------------------------------------------------------------------
// Core Interface Functions
// ------------------------------------------------------------------

void esp_profiler_execute_sample(ProfilerMode_t mode) {
    switch (mode) {
        case PROFILER_MODE_M0_MINIMAL:
            execute_m0();
            break;
        case PROFILER_MODE_M1_STANDARD:
            execute_m1();
            break;
        case PROFILER_MODE_M2_FULL:
            execute_m2();
            break;
    }
}

ProfilerMode_t select_profiler_mode(int64_t residual_slack_us) {
    if (residual_slack_us >= SLACK_THRESHOLD_M2_US) {
        return PROFILER_MODE_M2_FULL;
    } else if (residual_slack_us >= SLACK_THRESHOLD_M1_US) {
        return PROFILER_MODE_M1_STANDARD;
    } else {
        return PROFILER_MODE_M0_MINIMAL;
    }
}

void calibrate_profiler_overheads(void) {
    uint64_t t_start, t_end;
    const int iterations = 10;

    // Calibrate M0
    uint64_t sum_m0 = 0;
    for (int i = 0; i < iterations; i++) {
        t_start = esp_timer_get_time();
        execute_m0();
        t_end = esp_timer_get_time();
        sum_m0 += (t_end - t_start);
    }
    measured_overhead_m0_us = (uint32_t)(sum_m0 / iterations);

    // Calibrate M1
    uint64_t sum_m1 = 0;
    for (int i = 0; i < iterations; i++) {
        t_start = esp_timer_get_time();
        execute_m1();
        t_end = esp_timer_get_time();
        sum_m1 += (t_end - t_start);
    }
    measured_overhead_m1_us = (uint32_t)(sum_m1 / iterations);

    // Calibrate M2
    uint64_t sum_m2 = 0;
    for (int i = 0; i < iterations; i++) {
        t_start = esp_timer_get_time();
        execute_m2();
        t_end = esp_timer_get_time();
        sum_m2 += (t_end - t_start);
    }
    measured_overhead_m2_us = (uint32_t)(sum_m2 / iterations);

    ESP_LOGI(TAG, "Calibration Results -> M0: %lu us | M1: %lu us | M2: %lu us",
             measured_overhead_m0_us, measured_overhead_m1_us, measured_overhead_m2_us);
}

uint32_t percentile_index(float p, uint32_t n) {
    if (n == 0) return 0;
    uint32_t idx = (uint32_t)(p * (float)n);
    return (idx >= n) ? (n - 1) : idx;
}