idf_component_register(
    SRCS "main.c" "level_sensor.c" "telemetry.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi esp_event nvs_flash mqtt esp_timer
)
