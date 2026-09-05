#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*entry)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
