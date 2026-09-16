#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MARKET_PRESET_COUNT 15U
#define MARKET_MAX_ROWS 5U
#define MARKET_QUOTE_TIME_CAPACITY 20U
#define MARKET_RESPONSE_LIMIT 16384U
#define MARKET_REFRESH_INTERVAL_MS 120000U
#define MARKET_CONFIG_RECORD_SIZE 12U

typedef enum {
    MARKET_QUOTE_CN = 0,
    MARKET_QUOTE_HK,
    MARKET_QUOTE_US,
    MARKET_QUOTE_GLOBAL,
} market_quote_kind_t;

typedef struct {
    uint8_t id;
    const char *name;
    const char *symbol;
    market_quote_kind_t kind;
} market_preset_t;

typedef struct {
    bool enabled;
    uint8_t count;
    uint8_t ids[MARKET_MAX_ROWS];
} market_config_t;

typedef struct {
    uint8_t id;
    bool valid;
    bool stale;
    double price;
    double percent;
    /* Original upstream quote time, in UTC+8, never the retrieval time. */
    char quote_time[MARKET_QUOTE_TIME_CAPACITY];
} market_row_t;

size_t market_preset_count(void);
const market_preset_t *market_preset_get(size_t index);
const market_preset_t *market_preset_by_id(uint8_t id);
void market_config_defaults(market_config_t *config);
bool market_config_valid(const market_config_t *config);
bool market_config_equal(const market_config_t *left,
                         const market_config_t *right);
bool market_config_encode(const market_config_t *config,
                          uint8_t encoded[MARKET_CONFIG_RECORD_SIZE]);
bool market_config_decode(const uint8_t *encoded, size_t size,
                          market_config_t *config);
/* Parses raw GB18030 ASCII numeric/date fields without decoding names or
 * executing upstream JavaScript. Each malformed/missing row stays invalid. */
size_t market_parse_response(const char *body, size_t length,
                             const market_config_t *config,
                             market_row_t rows[MARKET_MAX_ROWS]);
/* Cache merge preserves valid old rows on per-index failure and rejects
 * backwards quote timestamps. Returns the number of accepted new rows. */
size_t market_merge_rows(market_row_t cache[MARKET_PRESET_COUNT],
                         const market_row_t *rows, size_t count);
/* Unknown RTC epoch skips this guard. Holidays/old quotes are not expired. */
bool market_quote_time_plausible(const market_row_t *row,
                                 int64_t now_epoch_seconds);
bool market_refresh_due(bool enabled, bool visible, bool automatic,
                        bool requested, bool attempted,
                        uint64_t elapsed_ms);
