idf_component_register(
    SRCS "alarm_scheduler.c" "alarm_input_gate.c"
         "alarm_history_record.c" "alarm_history.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
