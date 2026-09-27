#pragma once
#include "FreeRTOS.h"
typedef unsigned EventBits_t;
typedef void *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t events, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t events);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t events, EventBits_t bits,
                              BaseType_t clear, BaseType_t all, TickType_t timeout);
