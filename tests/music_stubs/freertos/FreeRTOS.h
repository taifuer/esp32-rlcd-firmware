#pragma once
#include <stdint.h>
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
