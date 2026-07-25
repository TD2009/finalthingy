#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_profiler.h"

static SemaphoreHandle_t s_resource_mutex = NULL;

void ResearchTask_1(void *pvParameters) {
    while (1) {
        if (xSemaphoreTakeProfiled(s_resource_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            double x = 0.0;
            for (int i = 0; i < 50000; i++) {
                x += 0.001 * i;
            }
            (void)x;
            
            xSemaphoreGive(s_resource_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    esp_profiler_init();

    s_resource_mutex = xSemaphoreCreateMutex();
    
    xTaskCreatePinnedToCore(
        ResearchTask_1,
        "ResearchTask_1",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    while (1) {
        esp_profiler_run_window(2000);
    }
}