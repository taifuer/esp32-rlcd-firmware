#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "market_portal_model.h"

static void rejected(const char *form)
{
    market_config_t original = {0};
    market_config_defaults(&original);
    market_config_t parsed = original;
    assert(!market_portal_parse_form(form, strlen(form), &parsed));
    assert(memcmp(&original, &parsed, sizeof(parsed)) == 0);
}

int main(void)
{
    market_config_t parsed = {0};
    const char *full = "enabled=on&ids=0%2C3%2C7%2C9%2C11";
    assert(market_portal_parse_form(full, strlen(full), &parsed));
    assert(parsed.enabled && parsed.count == 5U);
    const uint8_t expected[] = {0, 3, 7, 9, 11};
    assert(memcmp(parsed.ids, expected, sizeof(expected)) == 0);
    const char *reordered = "ids=14,9,0&enabled=off";
    assert(market_portal_parse_form(reordered, strlen(reordered), &parsed));
    assert(!parsed.enabled && parsed.count == 3U && parsed.ids[0] == 14U &&
           parsed.ids[1] == 9U && parsed.ids[2] == 0U);
    const char *one = "enabled=on&ids=0";
    assert(market_portal_parse_form(one, strlen(one), &parsed));
    assert(parsed.count == 1U && parsed.ids[0] == 0U);
    const char *encoded = "%65nabled=on&ids=10%2c14";
    assert(market_portal_parse_form(encoded, strlen(encoded), &parsed));
    assert(parsed.count == 2U && parsed.ids[0] == 10U && parsed.ids[1] == 14U);
    static const char *const invalid[] = {
        "", "enabled=on", "ids=0", "enabled=&ids=0", "enabled=yes&ids=0",
        "enabled=off&ids=", "enabled=off&ids=0,1,2,3,4,5",
        "enabled=on&ids=0,0", "enabled=on&ids=15", "enabled=on&ids=255",
        "enabled=on&ids=-1", "enabled=on&ids=+1", "enabled=on&ids=01",
        "enabled=on&ids=0,", "enabled=on&ids=,0", "enabled=on&ids=0,,1",
        "enabled=on&ids=0&ids=1", "enabled=on&enabled=off&ids=0",
        "enabled=on&ids=0&url=https://example.com", "enabled=on&symbols=sh000001",
        "enabled=on&ids=0&", "enabled=on&&ids=0", "enabled=on&ids=0%00",
        "enabled=on&ids=0%2", "enabled=on&ids=0%ZZ", "enabled=on&ids=0%26ids%3D1",
        "enabled=on&ids=0\n", "enabled=on&ids=0%20", "enabled=on&ids=0=1",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        rejected(invalid[index]);
    }
    char huge[MARKET_PORTAL_FORM_MAX_LENGTH + 1U];
    memset(huge, '1', sizeof(huge));
    assert(!market_portal_parse_form(huge, sizeof(huge), &parsed));
    const char embedded_nul[] = "enabled=on&ids=0\0&url=evil";
    assert(!market_portal_parse_form(embedded_nul, sizeof(embedded_nul) - 1U, &parsed));
    assert(!market_portal_parse_form(NULL, 12U, &parsed));
    assert(!market_portal_parse_form(one, strlen(one), NULL));
    puts("Market portal: strict preset-only form, order, bounds and failure isolation passed.");
    return 0;
}
