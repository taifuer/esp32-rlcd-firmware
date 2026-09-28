#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define BIT0 (1U << 0U)
#define BIT1 (1U << 1U)
#define BIT2 (1U << 2U)
#define BIT3 (1U << 3U)
#define BIT4 (1U << 4U)
#define BIT5 (1U << 5U)
