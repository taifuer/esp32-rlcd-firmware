#include <assert.h>
#include <stdio.h>
#include "u8g2.h"
#include "quick_settings.h"
#include "hold_interaction.h"
#include "display_interaction_model.h"

int main(void)
{
    u8g2_t screen = {0};
    u8g2_SetFont(&screen, u8g2_font_helvB24_tf);
    const char *titles[] = {
        app_hold_prompt_title(APP_PAGE_ACTION_OPEN_SETTINGS, APP_HOLD_UPDATE_CHECK, false),
        "OPEN WEB SETTINGS",
        "EDIT VOLUME", "SAVE VOLUME", "EDIT ALARM", "SAVE ALARM",
        "EDIT DISPLAY", "SAVE DISPLAY", "WEATHER", "100 %"};
    for (unsigned index = 0U; index < sizeof(titles) / sizeof(titles[0]); ++index) {
        const unsigned width = u8g2_GetStrWidth(&screen, titles[index]);
        assert(width <= 376U);
    }
    for (unsigned item = 0U; item < QUICK_SETTINGS_ITEM_COUNT; ++item) {
        for (unsigned editing = 0U; editing < 2U; ++editing) {
            const quick_settings_t menu = {
                .active = true, .item = (quick_settings_item_t)item,
                .editing = editing != 0U,
            };
            assert(u8g2_GetStrWidth(&screen,
                quick_settings_hold_title(&menu)) <= 376U);
        }
    }
    u8g2_SetFont(&screen, u8g2_font_helvB14_tf);
    const char *values[] = {"100 %", "OFF 23:59", "WEATHER", ">"};
    for (unsigned index = 0U; index < 4U; ++index) {
        unsigned label = u8g2_GetStrWidth(&screen, quick_settings_item_name(index));
        unsigned value = u8g2_GetStrWidth(&screen, values[index]);
        assert(label + 16U + value <= 358U);
    }
    u8g2_SetFont(&screen, u8g2_font_6x13_tf);
    const char *footers[] = {
        display_interaction_settings_action_footer(false),
        display_interaction_settings_action_footer(true),
        display_interaction_quick_navigation(false),
        display_interaction_quick_navigation(true),
        display_interaction_quick_action(false, false),
        display_interaction_quick_action(false, true),
        display_interaction_quick_action(true, false),
        "UNAVAILABLE PAGES FALL BACK TO CLOCK", "SAVE FAILED | TRY AGAIN OR CANCEL"};
    for (unsigned index = 0U; index < sizeof(footers) / sizeof(footers[0]); ++index) {
        unsigned width = u8g2_GetStrWidth(&screen, footers[index]);
        assert(width <= 376U);
    }
    puts("settings layout real-font width tests passed");
}
