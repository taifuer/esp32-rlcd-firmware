idf_component_register(
    SRCS "boot_recovery.c" "boot_recovery_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_system heap
    PRIV_REQUIRES freertos
)
