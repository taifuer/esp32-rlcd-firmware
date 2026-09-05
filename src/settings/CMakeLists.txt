idf_component_register(
    SRCS "app_settings.c" "settings_model.c" "settings_power_policy.c"
         "settings_record.c" "quick_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash storage
)
