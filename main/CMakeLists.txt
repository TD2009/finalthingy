idf_component_register(
   SRCS "matrix1.c" "main.c" "profiler.c" "workload.c"
   INCLUDE_DIRS "."
   PRIV_REQUIRES nvs_flash esp_driver_tsens driver esp_hw_support
)
