#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHINESE_LUNAR_GREGORIAN_FIRST_YEAR 2000U
#define CHINESE_LUNAR_GREGORIAN_LAST_YEAR 2099U

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    bool leap_month;
} chinese_lunar_date_t;

bool chinese_lunar_from_gregorian(uint16_t year, uint8_t month, uint8_t day,
                                  chinese_lunar_date_t *result);

/* Formats a date as UTF-8 month/day text such as "七月初六". */
bool chinese_lunar_format(const chinese_lunar_date_t *date, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
