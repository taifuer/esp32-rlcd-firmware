#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(void (*task)(void *), const char *name,
                      unsigned stack, void *argument, unsigned priority,
                      TaskHandle_t *handle);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout);
