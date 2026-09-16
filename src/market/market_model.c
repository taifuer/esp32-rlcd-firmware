#include "market_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const market_preset_t s_presets[MARKET_PRESET_COUNT] = {
    {0, "上证指数", "sh000001", MARKET_QUOTE_CN},
    {1, "深证成指", "sz399001", MARKET_QUOTE_CN},
    {2, "创业板指", "sz399006", MARKET_QUOTE_CN},
    {3, "沪深300", "sh000300", MARKET_QUOTE_CN},
    {4, "中证500", "sh000905", MARKET_QUOTE_CN},
    {5, "上证50", "sh000016", MARKET_QUOTE_CN},
    {6, "科创50", "sh000688", MARKET_QUOTE_CN},
    {7, "恒生指数", "hkHSI", MARKET_QUOTE_HK},
    {8, "恒生科技", "hkHSTECH", MARKET_QUOTE_HK},
    {9, "纳斯达克综合", "gb_ixic", MARKET_QUOTE_US},
    {10, "纳斯达克100", "gb_ndx", MARKET_QUOTE_US},
    {11, "标普500", "gb_inx", MARKET_QUOTE_US},
    {12, "道琼斯", "gb_dji", MARKET_QUOTE_US},
    {13, "日经225", "znb_NKY", MARKET_QUOTE_GLOBAL},
    {14, "台湾加权", "znb_TWJQ", MARKET_QUOTE_GLOBAL},
};

size_t market_preset_count(void)
{
    return MARKET_PRESET_COUNT;
}

const market_preset_t *market_preset_get(size_t index)
{
    return index < MARKET_PRESET_COUNT ? &s_presets[index] : NULL;
}

const market_preset_t *market_preset_by_id(uint8_t id)
{
    return market_preset_get(id);
}

void market_config_defaults(market_config_t *config)
{
    if (config != NULL) {
        *config = (market_config_t){.enabled = false, .count = 5,
                                   .ids = {0, 3, 7, 9, 11}};
    }
}

bool market_config_valid(const market_config_t *config)
{
    if (config == NULL || config->count == 0 ||
        config->count > MARKET_MAX_ROWS) {
        return false;
    }
    uint16_t seen = 0;
    for (size_t i = 0; i < config->count; ++i) {
        if (config->ids[i] >= MARKET_PRESET_COUNT ||
            (seen & (1U << config->ids[i])) != 0) {
            return false;
        }
        seen |= (uint16_t)(1U << config->ids[i]);
    }
    return true;
}

bool market_config_equal(const market_config_t *left,
                         const market_config_t *right)
{
    return market_config_valid(left) && market_config_valid(right) &&
           left->enabled == right->enabled && left->count == right->count &&
           memcmp(left->ids, right->ids, left->count) == 0;
}

static uint16_t record_checksum(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
        }
    }
    return crc;
}

bool market_config_encode(const market_config_t *config,
                          uint8_t encoded[MARKET_CONFIG_RECORD_SIZE])
{
    if (encoded == NULL || !market_config_valid(config)) {
        return false;
    }
    memset(encoded, 0, MARKET_CONFIG_RECORD_SIZE);
    encoded[0] = 'M';
    encoded[1] = 'K';
    encoded[2] = 1;
    encoded[3] = config->enabled ? 1 : 0;
    encoded[4] = config->count;
    memcpy(encoded + 5, config->ids, config->count);
    const uint16_t crc = record_checksum(encoded, 10);
    encoded[10] = (uint8_t)crc;
    encoded[11] = (uint8_t)(crc >> 8);
    return true;
}

bool market_config_decode(const uint8_t *encoded, size_t size,
                          market_config_t *config)
{
    if (encoded == NULL || config == NULL ||
        size != MARKET_CONFIG_RECORD_SIZE || encoded[0] != 'M' ||
        encoded[1] != 'K' || encoded[2] != 1 || encoded[3] > 1 ||
        encoded[4] == 0 || encoded[4] > MARKET_MAX_ROWS ||
        record_checksum(encoded, 10) !=
            ((uint16_t)encoded[10] | (uint16_t)encoded[11] << 8)) {
        return false;
    }
    market_config_t candidate = {.enabled = encoded[3] != 0,
                                 .count = encoded[4]};
    memcpy(candidate.ids, encoded + 5, candidate.count);
    if (!market_config_valid(&candidate)) {
        return false;
    }
    *config = candidate;
    return true;
}

typedef struct {
    const char *data;
    size_t size;
} field_t;

static bool decimal(field_t field, double *value)
{
    if (field.size == 0 || field.size >= 32) {
        return false;
    }
    bool dot = false;
    size_t digits = 0;
    for (size_t i = 0; i < field.size; ++i) {
        const char ch = field.data[i];
        if (ch >= '0' && ch <= '9') {
            ++digits;
        } else if (ch == '.' && !dot) {
            dot = true;
        } else if ((ch != '-' && ch != '+') || i != 0) {
            return false;
        }
    }
    if (digits == 0) {
        return false;
    }
    char text[32];
    memcpy(text, field.data, field.size);
    text[field.size] = '\0';
    char *end;
    const double number = strtod(text, &end);
    if (!isfinite(number) || end != text + field.size) {
        return false;
    }
    *value = number;
    return true;
}

static int digit_pair(const char *text)
{
    if (text[0] < '0' || text[0] > '9' ||
        text[1] < '0' || text[1] > '9') {
        return -1;
    }
    return (text[0] - '0') * 10 + text[1] - '0';
}

static bool quote_datetime(field_t date, field_t clock,
                            char result[MARKET_QUOTE_TIME_CAPACITY])
{
    if (date.size != 10 || (clock.size != 5 && clock.size != 8)) {
        return false;
    }
    const char separator = date.data[4];
    if ((separator != '-' && separator != '/') || date.data[7] != separator ||
        clock.data[2] != ':' || (clock.size == 8 && clock.data[5] != ':')) {
        return false;
    }
    const int century = digit_pair(date.data);
    const int suffix = digit_pair(date.data + 2);
    const int month = digit_pair(date.data + 5);
    const int day = digit_pair(date.data + 8);
    const int hour = digit_pair(clock.data);
    const int minute = digit_pair(clock.data + 3);
    const int second = clock.size == 8 ? digit_pair(clock.data + 6) : 0;
    if (century < 20 || century > 21 || suffix < 0 || month < 1 || month > 12 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return false;
    }
    const int year = century * 100 + suffix;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day < 1 || day > days[month - 1] + (month == 2 && leap ? 1 : 0)) {
        return false;
    }
    memcpy(result, date.data, 10);
    result[4] = '-';
    result[7] = '-';
    result[10] = ' ';
    memcpy(result + 11, clock.data, clock.size);
    if (clock.size == 5) memcpy(result + 16, ":00", 3);
    result[19] = '\0';
    return true;
}

static bool parse_quote(const market_preset_t *preset,
                         const char *text, size_t length, market_row_t *row)
{
    field_t fields[64];
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || text[i] == ',') {
            if (count == sizeof(fields) / sizeof(fields[0])) {
                return false;
            }
            fields[count++] = (field_t){text + start, i - start};
            start = i + 1;
        }
    }
    size_t current_index, previous_index, percent_index = 0;
    field_t date, clock;
    double previous = 0;
    double supplied_percent = 0;
    switch (preset->kind) {
    case MARKET_QUOTE_CN:
        if (count < 32) return false;
        current_index = 3;
        previous_index = 2;
        date = fields[30];
        clock = fields[31];
        break;
    case MARKET_QUOTE_HK:
        if (count < 19) return false;
        current_index = 6;
        previous_index = 3;
        percent_index = 8;
        date = fields[17];
        clock = fields[18];
        break;
    case MARKET_QUOTE_US:
        if (count < 27 || (fields[3].size != 19 && fields[3].size != 16) ||
            fields[3].data[10] != ' ') return false;
        current_index = 1;
        previous_index = 26;
        percent_index = 2;
        date = (field_t){fields[3].data, 10};
        clock = (field_t){fields[3].data + 11, fields[3].size - 11};
        break;
    case MARKET_QUOTE_GLOBAL:
        if (count < 8) return false;
        current_index = 1;
        previous_index = 2; /* Delta, not previous close. */
        percent_index = 3;
        date = fields[6];
        clock = fields[7];
        break;
    default:
        return false;
    }
    if (!decimal(fields[current_index], &row->price) ||
        !decimal(fields[previous_index], &previous) ||
        !quote_datetime(date, clock, row->quote_time)) {
        return false;
    }
    if (preset->kind == MARKET_QUOTE_GLOBAL) {
        previous = row->price - previous;
    }
    if (row->price <= 0 || row->price > 10000000 ||
        previous <= 0 || previous > 10000000) {
        return false;
    }
    row->percent = (row->price - previous) / previous * 100.0;
    if (!isfinite(row->percent) || fabs(row->percent) > 100.0) {
        return false;
    }
    if (preset->kind != MARKET_QUOTE_CN &&
        (!decimal(fields[percent_index], &supplied_percent) ||
         fabs(supplied_percent - row->percent) > 0.15)) {
        return false;
    }
    row->valid = true;
    return true;
}

static bool whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

size_t market_parse_response(const char *body, size_t length,
                             const market_config_t *config,
                             market_row_t rows[MARKET_MAX_ROWS])
{
    if (rows == NULL) return 0;
    memset(rows, 0, sizeof(*rows) * MARKET_MAX_ROWS);
    if (!market_config_valid(config)) return 0;
    for (size_t i = 0; i < config->count; ++i) rows[i].id = config->ids[i];
    if (body == NULL || length == 0 || length > MARKET_RESPONSE_LIMIT ||
        memchr(body, '\0', length) != NULL) return 0;

    bool seen[MARKET_MAX_ROWS] = {false};
    bool duplicate[MARKET_MAX_ROWS] = {false};
    static const char prefix[] = "var hq_str_";
    size_t position = 0;
    while (position < length) {
        while (position < length && whitespace(body[position])) ++position;
        if (position == length) break;
        const size_t line_start = position;
        while (position < length && body[position] != '\n') ++position;
        size_t line_end = position;
        while (line_end > line_start && whitespace(body[line_end - 1])) --line_end;
        if (line_end - line_start < sizeof(prefix) + 3 ||
            memcmp(body + line_start, prefix, sizeof(prefix) - 1) != 0) continue;
        const size_t symbol_start = line_start + sizeof(prefix) - 1;
        size_t symbol_end = symbol_start;
        while (symbol_end < line_end && body[symbol_end] != '=') ++symbol_end;
        if (symbol_end >= line_end || symbol_end + 1 >= line_end ||
            body[symbol_end + 1] != '"') continue;
        for (size_t i = 0; i < config->count; ++i) {
            const market_preset_t *preset = market_preset_by_id(config->ids[i]);
            if (strlen(preset->symbol) != symbol_end - symbol_start ||
                memcmp(preset->symbol, body + symbol_start, symbol_end - symbol_start) != 0) continue;
            if (seen[i]) duplicate[i] = true;
            seen[i] = true;
            if (line_end < symbol_end + 5 || body[line_end - 1] != ';' ||
                body[line_end - 2] != '"') break;
            const size_t content_start = symbol_end + 2;
            const size_t content_length = line_end - 2 - content_start;
            if (memchr(body + content_start, '"', content_length) != NULL) break;
            market_row_t candidate = {.id = preset->id};
            if (parse_quote(preset, body + content_start, content_length, &candidate)) {
                rows[i] = candidate;
            }
            break;
        }
    }
    size_t accepted = 0;
    for (size_t i = 0; i < config->count; ++i) {
        if (duplicate[i]) rows[i].valid = false;
        if (rows[i].valid) ++accepted;
    }
    return accepted;
}

size_t market_merge_rows(market_row_t cache[MARKET_PRESET_COUNT],
                         const market_row_t *rows, size_t count)
{
    if (cache == NULL || rows == NULL || count > MARKET_MAX_ROWS) return 0;
    size_t accepted = 0;
    for (size_t i = 0; i < count; ++i) {
        const market_row_t *next = &rows[i];
        if (next->id >= MARKET_PRESET_COUNT) continue;
        market_row_t *previous = &cache[next->id];
        if (next->valid && (!previous->valid ||
            strcmp(next->quote_time, previous->quote_time) >= 0)) {
            *previous = *next;
            previous->stale = false;
            ++accepted;
        } else {
            previous->stale = previous->valid;
        }
    }
    return accepted;
}

bool market_refresh_due(bool enabled, bool visible, bool automatic,
                        bool requested, bool attempted, uint64_t elapsed_ms)
{
    return enabled && visible && (requested ||
        (automatic && (!attempted || elapsed_ms >= MARKET_REFRESH_INTERVAL_MS)));
}

bool market_quote_time_plausible(const market_row_t *row,
                                 int64_t now_epoch_seconds)
{
    if (row == NULL || !row->valid) return false;
    if (now_epoch_seconds < 1577836800) return true;
    char normalized[MARKET_QUOTE_TIME_CAPACITY];
    if (strlen(row->quote_time) != 19 || !quote_datetime(
            (field_t){row->quote_time, 10},
            (field_t){row->quote_time + 11, 8}, normalized)) return false;
    int year = digit_pair(row->quote_time) * 100 +
               digit_pair(row->quote_time + 2);
    const int month = digit_pair(row->quote_time + 5);
    const int day = digit_pair(row->quote_time + 8);
    /* Gregorian civil date to days since 1970, independent of libc TZ. */
    year -= month <= 2;
    const int era = year / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned mp = (unsigned)(month + (month > 2 ? -3 : 9));
    const unsigned doy = (153 * mp + 2) / 5 + (unsigned)day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + doe - 719468;
    const int64_t quote_epoch = days * 86400 +
        digit_pair(row->quote_time + 11) * 3600 +
        digit_pair(row->quote_time + 14) * 60 +
        digit_pair(row->quote_time + 17) - 8 * 3600;
    /* Small RTC drift is tolerated; a future-dated response must not poison
     * the monotonic per-index cache for months or years. */
    return quote_epoch <= now_epoch_seconds + 24 * 3600;
}
