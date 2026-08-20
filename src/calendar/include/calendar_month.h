#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t days;
    /* 0 is Sunday and 6 is Saturday, matching PCF85063 and struct tm. */
    uint8_t first_weekday;
} calendar_month_info_t;

bool calendar_month_info(uint16_t year, uint8_t month,
                         calendar_month_info_t *result);

#ifdef __cplusplus
}
#endif
