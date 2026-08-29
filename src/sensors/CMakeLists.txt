idf_component_register(
    SRCS "environment_comfort.c" "environment_observation.c" "shtc3.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
)
