#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_MARKET_ROW_LIMIT 5U

typedef struct {
    const char *name;
    bool valid;
    /* A newer fetch failed for this row; preserve the previous valid quote. */
    bool stale;
    double price;
    double percent;
    /* Provider quote time, normalized to YYYY-MM-DD HH:MM:SS in UTC+8.
     * This is never the local fetch time or the device's configured timezone. */
    const char *quote_time;
} display_market_row_t;

typedef struct {
    size_t count;
    display_market_row_t rows[DISPLAY_MARKET_ROW_LIMIT];
    /* Current device-local RTC date/time, independent of quote timestamps. */
    bool current_time_valid;
    uint16_t current_year;
    uint8_t current_month;
    uint8_t current_day;
    uint8_t current_hour;
    uint8_t current_minute;
    bool refreshing;
    /* Short user-facing ASCII status, not an upstream error or URL. */
    const char *status_detail;
    bool saving;
} display_market_t;

bool display_market_format_price(char *buffer, size_t capacity,
                                 bool valid, double price);
bool display_market_format_percent(char *buffer, size_t capacity,
                                   bool valid, double percent);
bool display_market_format_quote_time(char *buffer, size_t capacity,
                                      const char *quote_time);
bool display_market_format_current_time(char *buffer, size_t capacity,
                                        const display_market_t *market);
bool display_market_format_status(char *buffer, size_t capacity,
                                  const display_market_t *market);

#ifdef __cplusplus
}
#endif
