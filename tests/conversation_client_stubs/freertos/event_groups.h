#pragma once
#include "FreeRTOS.h"
typedef uint32_t EventBits_t;
typedef struct test_client_events *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t events, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t events, EventBits_t bits);
void vEventGroupDelete(EventGroupHandle_t events);
