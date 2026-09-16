#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "market_model.h"

static void make_response(uint8_t id, const char *price,
                           const char *previous, const char *percent,
                           const char *date, const char *clock,
                           char *response, size_t capacity)
{
    const market_preset_t *preset = market_preset_by_id(id);
    const char *fields[40];
    for (size_t i = 0; i < 40; ++i) fields[i] = "0";
    /* GB18030 name bytes deliberately not valid UTF-8. */
    fields[0] = "\xc9\xcf\xd6\xa4";
    size_t count;
    char full_time[32];
    (void)snprintf(full_time, sizeof(full_time), "%s %s", date, clock);
    switch (preset->kind) {
    case MARKET_QUOTE_CN:
        count = 33; fields[3] = price; fields[2] = previous;
        fields[30] = date; fields[31] = clock; break;
    case MARKET_QUOTE_HK:
        count = 19; fields[6] = price; fields[3] = previous;
        fields[8] = percent; fields[17] = date; fields[18] = clock; break;
    case MARKET_QUOTE_US:
        count = 27; fields[1] = price; fields[26] = previous;
        fields[2] = percent; fields[3] = full_time; break;
    case MARKET_QUOTE_GLOBAL:
        count = 13; fields[1] = price; fields[2] = previous;
        fields[3] = percent; fields[6] = date; fields[7] = clock;
        fields[5] = "1759126320"; break;
    default: abort();
    }
    size_t used = (size_t)snprintf(response, capacity, "var hq_str_%s=\"", preset->symbol);
    for (size_t i = 0; i < count; ++i) {
        const int length = snprintf(response + used, capacity - used, "%s%s", i ? "," : "", fields[i]);
        assert(length >= 0 && (size_t)length < capacity - used);
        used += (size_t)length;
    }
    assert(used + 4 < capacity);
    memcpy(response + used, "\";\n", 4);
}

static size_t parse_one(uint8_t id, const char *price,
                         const char *previous, const char *percent,
                         const char *date, const char *clock,
                         market_row_t rows[MARKET_MAX_ROWS])
{
    char response[2048];
    make_response(id, price, previous, percent, date, clock, response, sizeof(response));
    const market_config_t config = {.enabled = true, .count = 1, .ids = {id}};
    return market_parse_response(response, strlen(response), &config, rows);
}

static void test_config(void)
{
    market_config_t config;
    market_config_defaults(&config);
    assert(!config.enabled && config.count == 5 && market_config_valid(&config));
    assert(market_preset_count() == 15 && market_preset_get(15) == NULL);
    assert(market_preset_by_id(255) == NULL);
    for (size_t i = 0; i < 15; ++i) assert(market_preset_get(i)->id == i);
    uint8_t encoded[MARKET_CONFIG_RECORD_SIZE];
    assert(market_config_encode(&config, encoded));
    market_config_t decoded = {0};
    assert(market_config_decode(encoded, sizeof(encoded), &decoded));
    assert(market_config_equal(&config, &decoded));
    for (size_t i = 0; i < sizeof(encoded); ++i) {
        encoded[i] ^= 1;
        assert(!market_config_decode(encoded, sizeof(encoded), &decoded));
        encoded[i] ^= 1;
    }
    assert(!market_config_decode(encoded, sizeof(encoded) - 1, &decoded));
    config.ids[4] = config.ids[0];
    assert(!market_config_valid(&config));
    config.ids[4] = 15;
    assert(!market_config_valid(&config));
    config.count = 0;
    assert(!market_config_valid(&config));
    config.count = 6;
    assert(!market_config_valid(&config));
    config.count = 1; config.ids[0] = 14; config.ids[4] = 255;
    assert(market_config_valid(&config));
    assert(market_config_encode(&config, encoded));
    assert(encoded[9] == 0);
}

static void test_quotes(void)
{
    market_row_t rows[MARKET_MAX_ROWS];
    for (uint8_t id = 0; id < MARKET_PRESET_COUNT; ++id) {
        const bool global = market_preset_by_id(id)->kind == MARKET_QUOTE_GLOBAL;
        assert(parse_one(id, "101.25", global ? "1.25" : "100", "1.25",
                         "2026-09-16", "15:35:32", rows) == 1);
        assert(rows[0].valid && rows[0].id == id && !rows[0].stale);
        assert(rows[0].price == 101.25 && rows[0].percent == 1.25);
        assert(strcmp(rows[0].quote_time, "2026-09-16 15:35:32") == 0);
    }
    assert(parse_one(7, "100", "101", "-0.99", "2026/09/16", "16:09", rows) == 1);
    assert(strcmp(rows[0].quote_time, "2026-09-16 16:09:00") == 0);
    const char *bad_number[] = {"", "NaN", "inf", "1e3", "1 2", "12x", "--1", ".", "-1", "0", "999999999999999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad_number)/sizeof(*bad_number); ++i) {
        assert(parse_one(0, bad_number[i], "100", "1", "2026-09-16", "15:35:32", rows) == 0);
    }
    assert(parse_one(0, "100", "0", "1", "2026-09-16", "15:35:32", rows) == 0);
    assert(parse_one(9, "101", "100", "99", "2026-09-16", "15:35:32", rows) == 0);
    const char *bad_dates[] = {"2026-02-29", "2026-13-01", "2026-00-01", "2026-04-31", "2026-09-00", "2026-9-16", "abcd-09-16"};
    for (size_t i = 0; i < sizeof(bad_dates)/sizeof(*bad_dates); ++i) {
        assert(parse_one(0, "100", "100", "0", bad_dates[i], "15:35:32", rows) == 0);
    }
    assert(parse_one(0, "100", "100", "0", "2024-02-29", "23:59:59", rows) == 1);
    assert(parse_one(0, "100", "100", "0", "2100-02-29", "23:59:59", rows) == 0);
    assert(parse_one(0, "100", "100", "0", "2026-09-16", "24:00:00", rows) == 0);
    assert(parse_one(0, "100", "100", "0", "2026-09-16", "23:60:00", rows) == 0);
    assert(parse_one(0, "100", "100", "0", "2026-09-16", "23:59:60", rows) == 0);
}

static void test_response_integrity(void)
{
    char one[2048], two[2048], response[8192];
    make_response(0, "101", "100", "1", "2026-09-16", "15:00:00", one, sizeof(one));
    make_response(7, "99", "100", "-1", "2026/09/16", "16:00:00", two, sizeof(two));
    (void)snprintf(response, sizeof(response), "%s%s", one, two);
    market_config_t config = {.enabled = true, .count = 3, .ids = {7, 0, 11}};
    market_row_t rows[MARKET_MAX_ROWS];
    assert(market_parse_response(response, strlen(response), &config, rows) == 2);
    assert(rows[0].id == 7 && rows[1].id == 0 && !rows[2].valid && rows[2].id == 11);
    (void)snprintf(response, sizeof(response), "%s%s%s", one, two, one);
    assert(market_parse_response(response, strlen(response), &config, rows) == 1);
    assert(rows[0].valid && !rows[1].valid);
    (void)snprintf(response, sizeof(response), "%svar hq_str_sh000001=\"\";\n%s", one, two);
    assert(market_parse_response(response, strlen(response), &config, rows) == 1);
    one[10] = '\0';
    assert(market_parse_response(one, 100, &config, rows) == 0);
    assert(market_parse_response("", MARKET_RESPONSE_LIMIT + 1, &config, rows) == 0);
    const char *injected = "var hq_str_sh000001=\"0\";evil();\n";
    assert(market_parse_response(injected, strlen(injected), &config, rows) == 0);
    config.count = 0;
    assert(market_parse_response(two, strlen(two), &config, rows) == 0);
}

static void test_cache_policy(void)
{
    market_row_t cache[MARKET_PRESET_COUNT] = {0};
    market_row_t rows[MARKET_MAX_ROWS];
    assert(parse_one(0, "101", "100", "1", "2026-09-16", "15:35:32", rows) == 1);
    assert(market_merge_rows(cache, rows, 1) == 1);
    assert(cache[0].valid && cache[0].price == 101);
    rows[0].valid = false;
    assert(market_merge_rows(cache, rows, 1) == 0);
    assert(cache[0].valid && cache[0].stale && cache[0].price == 101);
    assert(strcmp(cache[0].quote_time, "2026-09-16 15:35:32") == 0);
    assert(parse_one(0, "102", "100", "2", "2026-09-15", "15:35:32", rows) == 1);
    assert(market_merge_rows(cache, rows, 1) == 0);
    assert(cache[0].price == 101 && cache[0].stale);
    assert(parse_one(0, "103", "100", "3", "2026-09-17", "15:35:32", rows) == 1);
    assert(market_merge_rows(cache, rows, 1) == 1);
    assert(cache[0].price == 103 && !cache[0].stale);
    /* Sep16 00:00 UTC; old holiday quotes remain usable indefinitely. */
    const int64_t now = 1789516800;
    assert(!market_quote_time_plausible(&rows[0], now));
    assert(market_quote_time_plausible(&rows[0], 0));
    assert(parse_one(0, "100", "100", "0", "2026-09-16", "15:35:32", rows) == 1);
    assert(market_quote_time_plausible(&rows[0], now));
    assert(parse_one(0, "100", "100", "0", "2020-01-01", "15:35:32", rows) == 1);
    assert(market_quote_time_plausible(&rows[0], now));
    assert(!market_refresh_due(false, true, true, true, false, 0));
    assert(!market_refresh_due(true, false, true, true, false, 0));
    assert(!market_refresh_due(true, true, false, false, false, 0));
    assert(market_refresh_due(true, true, false, true, true, 0));
    assert(market_refresh_due(true, true, true, false, false, 0));
    assert(!market_refresh_due(true, true, true, false, true, 119999));
    assert(market_refresh_due(true, true, true, false, true, 120000));
}

static void test_live_fixture(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    char body[MARKET_RESPONSE_LIMIT + 1];
    const size_t size = fread(body, 1, sizeof(body), file);
    assert(!ferror(file) && size <= MARKET_RESPONSE_LIMIT);
    fclose(file);
    for (uint8_t start = 0; start < MARKET_PRESET_COUNT; start += MARKET_MAX_ROWS) {
        market_config_t config = {.enabled = true, .count = MARKET_MAX_ROWS};
        for (size_t i = 0; i < MARKET_MAX_ROWS; ++i) config.ids[i] = start + i;
        market_row_t rows[MARKET_MAX_ROWS];
        assert(market_parse_response(body, size, &config, rows) == MARKET_MAX_ROWS);
    }
    puts("market live fixture: all 15 indices parsed");
}

int main(int argc, char **argv)
{
    test_config();
    test_quotes();
    test_response_integrity();
    test_cache_policy();
    if (argc > 1) test_live_fixture(argv[1]);
    puts("market model tests passed");
    return 0;
}
