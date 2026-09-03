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

static temperature_sensor_handle_t temp_handle = NULL;
static QueueHandle_t example_queue = NULL;

static TaskStatus_t static_task_status_array[MAX_SYSTEM_TASKS];

uint32_t measured_overhead_m0_wcet_us = 0;
uint32_t measured_overhead_m1_wcet_us = 0;
uint32_t measured_overhead_m2_wcet_us = 0;

uint32_t measured_overhead_m0_avg_us = 0;
uint32_t measured_overhead_m1_avg_us = 0;
uint32_t measured_overhead_m2_avg_us = 0;

uint32_t slack_threshold_m2_us = 0;
uint32_t slack_threshold_m1_us = 0;
uint32_t slack_threshold_m0_us = 0;

typedef struct {
    float temp_celsius;
    size_t free_heap;
    float heap_fragmentation;
    UBaseType_t task_count;
    char formatted_json[256];
} profiler_data_t;

static profiler_data_t data;


void profiler_init(void) {
    temperature_sensor_config_t temp_sensor_config = {
        .range_min = -10,
        .range_max = 80,
    };
    if (temperature_sensor_install(&temp_sensor_config, &temp_handle) == ESP_OK) {
        temperature_sensor_enable(temp_handle);
    }

    example_queue = xQueueCreate(10, sizeof(int));
    memset(static_task_status_array, 0, sizeof(static_task_status_array));
    memset(&data, 0, sizeof(profiler_data_t));

}

static void execute_m0(void) {
    uint32_t cycles = esp_cpu_get_cycle_count();
    size_t min_heap = esp_get_minimum_free_heap_size();
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();

    (void)cycles;
    (void)min_heap;
    (void)handle;
}

static void execute_m1(void) {
    execute_m0();
    data.free_heap = esp_get_free_heap_size();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (data.free_heap > 0) {
        data.heap_fragmentation = 1.0f - ((float)largest_block / (float)data.free_heap);
    }

    if (example_queue != NULL) {
        UBaseType_t msgs_waiting = uxQueueMessagesWaiting(example_queue);
        UBaseType_t spaces_left = uxQueueSpacesAvailable(example_queue);
        (void)msgs_waiting;
        (void)spaces_left;
    }

    uint32_t total_run_time = 0;
    data.task_count = uxTaskGetSystemState(static_task_status_array, MAX_SYSTEM_TASKS, &total_run_time);
}

static void execute_m2(void) {
    execute_m1();

    if (temp_handle != NULL) {
        temperature_sensor_get_celsius(temp_handle, &data.temp_celsius);
    }

    for (UBaseType_t i = 0; i < data.task_count; i++) {
        UBaseType_t stack_watermark = uxTaskGetStackHighWaterMark(static_task_status_array[i].xHandle);
        (void)stack_watermark;
    }

    snprintf(data.formatted_json, sizeof(data.formatted_json),
             "{\"temp\":%.2f,\"frag\":%.2f,\"tasks\":%u,\"free_heap\":%u}",
             data.temp_celsius,
             data.heap_fragmentation,
             (unsigned int)data.task_count,
             (unsigned int)data.free_heap);
}

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
        case PROFILER_MODE_OFF:
        default:
            break;
    }
}

ProfilerMode_t select_profiler_mode(int64_t raw_slack_us) {
    if (raw_slack_us >= (int64_t)slack_threshold_m2_us) {
        return PROFILER_MODE_M2_FULL;
    } else if (raw_slack_us >= (int64_t)slack_threshold_m1_us) {
        return PROFILER_MODE_M1_STANDARD;
    } else if (raw_slack_us >= (int64_t)slack_threshold_m0_us) {
        return PROFILER_MODE_M0_MINIMAL;
    } else {
        return PROFILER_MODE_OFF;
    }
}

void calibrate_profiler_overheads(void) {
    uint64_t t_start;
    const int iterations = 10000;

    uint64_t max_m0 = 0;
    uint64_t avg_m0 = 0;
    for (int i = 0; i < iterations; i++) {
        t_start = esp_timer_get_time();
        execute_m0();
        uint64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed > max_m0) max_m0 = elapsed;
        avg_m0 += elapsed;
        if (i % 20 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    measured_overhead_m0_wcet_us = (uint32_t)max_m0;
    measured_overhead_m0_avg_us = (uint32_t)(avg_m0 / iterations);

    uint64_t max_m1 = 0;
    uint64_t avg_m1 = 0;
    for (int i = 0; i < iterations; i++) {
        t_start = esp_timer_get_time();
        execute_m1();
        uint64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed > max_m1) max_m1 = elapsed;
        avg_m1 += elapsed;
        if (i % 20 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    measured_overhead_m1_wcet_us = (uint32_t)max_m1;
    measured_overhead_m1_avg_us = (uint32_t)(avg_m1 / iterations);

    uint64_t max_m2 = 0;
    uint64_t avg_m2 = 0;
    for (int i = 0; i < iterations; i++) {
        t_start = esp_timer_get_time();
        execute_m2();
        uint64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed > max_m2) max_m2 = elapsed;
        avg_m2 += elapsed;
        if (i % 20 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    measured_overhead_m2_wcet_us = (uint32_t)max_m2;
    measured_overhead_m2_avg_us = (uint32_t)(avg_m2 / iterations);

    /* is guard time needed?*/
    slack_threshold_m2_us = measured_overhead_m2_wcet_us + GUARD_TIME_US;
    slack_threshold_m1_us = measured_overhead_m1_wcet_us + GUARD_TIME_US;
    slack_threshold_m0_us = measured_overhead_m0_wcet_us + GUARD_TIME_US;

    ESP_LOGI(TAG, "Empirical WCET Calibration -> M0: %lu us | M1: %lu us | M2: %lu us",
             measured_overhead_m0_wcet_us, measured_overhead_m1_wcet_us, measured_overhead_m2_wcet_us);
    ESP_LOGI(TAG, "Derived Decision Thresholds -> M0: %lu us | M1: %lu us | M2: %lu us",
             slack_threshold_m0_us, slack_threshold_m1_us, slack_threshold_m2_us);
    ESP_LOGI(TAG, "Empirical Average Overheads -> M0: %lu us | M1: %lu us | M2: %lu us",
             measured_overhead_m0_avg_us, measured_overhead_m1_avg_us, measured_overhead_m2_avg_us);
}

uint32_t percentile_index(float p, uint32_t n) {
    if (n == 0) return 0;
    uint32_t idx = (uint32_t)(p * (float)n);
    return (idx >= n) ? (n - 1) : idx;
}