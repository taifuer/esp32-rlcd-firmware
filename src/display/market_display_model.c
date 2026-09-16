#include "market_display_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool checked_result(char *buffer, size_t capacity, int written)
{
    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}

static bool valid_date(unsigned year, unsigned month, unsigned day)
{
    static const unsigned DAYS[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
    if (year < 1970U || year > 9999U || month < 1U || month > 12U || day < 1U)
        return false;
    unsigned maximum = DAYS[month - 1U];
    if (month == 2U && year % 4U == 0U &&
        (year % 100U != 0U || year % 400U == 0U)) ++maximum;
    return day <= maximum;
}

bool display_market_format_price(char *buffer, size_t capacity,
                                 bool valid, double price)
{
    if (buffer == NULL || capacity == 0U) return false;
    if (!valid || !isfinite(price) || price <= 0.0 || price >= 1000000000.0)
        return checked_result(buffer, capacity, snprintf(buffer, capacity, "--"));

    char plain[24];
    const int length = snprintf(plain, sizeof(plain), "%.2f", price);
    if (length < 0 || (size_t)length >= sizeof(plain)) {
        buffer[0] = '\0';
        return false;
    }
    const char *point = strchr(plain, '.');
    if (point == NULL) {
        buffer[0] = '\0';
        return false;
    }
    const size_t integer_length = (size_t)(point - plain);
    const size_t required = (size_t)length + (integer_length - 1U) / 3U;
    if (required >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    size_t output = 0U;
    for (size_t i = 0U; i < (size_t)length; ++i) {
        if (i > 0U && i < integer_length && (integer_length - i) % 3U == 0U)
            buffer[output++] = ',';
        buffer[output++] = plain[i];
    }
    buffer[output] = '\0';
    return true;
}

bool display_market_format_percent(char *buffer, size_t capacity,
                                   bool valid, double percent)
{
    if (buffer == NULL || capacity == 0U) return false;
    if (!valid || !isfinite(percent))
        return checked_result(buffer, capacity, snprintf(buffer, capacity, "--"));
    if (fabs(percent) < 0.005)
        return checked_result(buffer, capacity, snprintf(buffer, capacity, "0.00%%"));
    return checked_result(buffer, capacity,
                          snprintf(buffer, capacity, "%+.2f%%", percent));
}

bool display_market_format_quote_time(char *buffer, size_t capacity,
                                      const char *quote_time)
{
    if (buffer == NULL || capacity == 0U) return false;
    size_t length = 0U;
    if (quote_time != NULL) {
        while (length < 20U && quote_time[length] != '\0') ++length;
    }
    bool valid = length == 19U;
    for (size_t i = 0U; valid && i < length; ++i) {
        const char expected = i == 4U || i == 7U ? '-'
                              : i == 10U ? ' '
                              : i == 13U || i == 16U ? ':' : '\0';
        if (expected != '\0') valid = quote_time[i] == expected;
        else valid = quote_time[i] >= '0' && quote_time[i] <= '9';
    }
    if (valid) {
        const unsigned year = (unsigned)((quote_time[0] - '0') * 1000 +
            (quote_time[1] - '0') * 100 + (quote_time[2] - '0') * 10 + quote_time[3] - '0');
        const unsigned month = (unsigned)((quote_time[5] - '0') * 10 + quote_time[6] - '0');
        const unsigned day = (unsigned)((quote_time[8] - '0') * 10 + quote_time[9] - '0');
        const unsigned hour = (unsigned)((quote_time[11] - '0') * 10 + quote_time[12] - '0');
        const unsigned minute = (unsigned)((quote_time[14] - '0') * 10 + quote_time[15] - '0');
        const unsigned second = (unsigned)((quote_time[17] - '0') * 10 + quote_time[18] - '0');
        valid = valid_date(year, month, day) && hour < 24U && minute < 60U && second < 60U;
    }
    return checked_result(buffer, capacity,
        valid ? snprintf(buffer, capacity, "%.2s/%.2s %.5s UTC+8",
                          quote_time + 5, quote_time + 8, quote_time + 11)
              : snprintf(buffer, capacity, "--/-- --:-- UTC+8"));
}

bool display_market_format_current_time(char *buffer, size_t capacity,
                                        const display_market_t *market)
{
    if (buffer == NULL || capacity == 0U || market == NULL) return false;
    if (!market->current_time_valid ||
        !valid_date(market->current_year, market->current_month, market->current_day) ||
        market->current_hour > 23U || market->current_minute > 59U)
        return checked_result(buffer, capacity,
                              snprintf(buffer, capacity, "----/--/-- --:--"));
    return checked_result(buffer, capacity, snprintf(buffer, capacity,
        "%04u/%02u/%02u %02u:%02u", market->current_year, market->current_month,
        market->current_day, market->current_hour, market->current_minute));
}

bool display_market_format_status(char *buffer, size_t capacity,
                                  const display_market_t *market)
{
    if (buffer == NULL || capacity == 0U || market == NULL) return false;
    const char *detail = market->refreshing ? "REFRESHING"
        : market->status_detail != NULL && market->status_detail[0] != '\0'
            ? market->status_detail : "WAITING";
    return checked_result(buffer, capacity, snprintf(buffer, capacity,
        "SINA | %s%.42s", market->saving ? "SAVING | " : "", detail));
}
