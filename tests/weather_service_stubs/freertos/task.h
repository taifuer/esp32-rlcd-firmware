#pragma once
#include "../../market_stubs/freertos/task.h"
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle);
