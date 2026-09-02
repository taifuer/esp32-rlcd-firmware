#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "display_interaction_model.h"
#include "voice_display_model.h"

static int measured_width(const char *text, void *context)
{
    const int ascii_width = *(const int *)context;
    int width = 0;
    for (size_t offset = 0U; text[offset] != '\0';) {
        const unsigned char lead = (unsigned char)text[offset];
        if (lead < 0x80U) {
            width += ascii_width;
            ++offset;
        } else {
            width += 2 * ascii_width;
            offset += lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U);
        }
    }
    return width;
}

static void test_mode_title(void)
{
    assert(strcmp(voice_display_mode_title(true), "AI CHAT") == 0);
    assert(strcmp(voice_display_mode_title(false),
                  "OFFLINE COMMANDS") == 0);
}

static void test_ready_prompt_names_the_chat_action(void)
{
    assert(strcmp(voice_display_ready_prompt(true, true),
                  "Hold KEY 2s to ask") == 0);
    assert(strcmp(voice_display_ready_prompt(false, true),
                  "Hold KEY 2s for a command") == 0);
    assert(strcmp(voice_display_ready_prompt(true, false),
                  "Commands are not ready") == 0);
    assert(strstr(voice_display_ready_prompt(true, true), "VOICE") == NULL);
    assert(strstr(voice_display_ready_prompt(false, true), "VOICE") == NULL);
    assert(strcmp(voice_display_feedback_footer(),
                  "RETURNING TO CHAT...") == 0);
}

static void assert_footer_fits(const char *footer)
{
    assert(footer != NULL);
    assert(strlen(footer) <= DISPLAY_INTERACTION_FOOTER_MAX_CHARS);
}

static void assert_footer_uses_concrete_targets(const char *footer)
{
    assert_footer_fits(footer);
    assert(strstr(footer, "BOOT: PAGE") == NULL);
    assert(strstr(footer, "KEY: SYSTEM") == NULL);
    assert(strstr(footer, "KEY: NEXT |") == NULL);
    assert(strstr(footer, ": PORTAL") == NULL);
}

static void test_targeted_navigation_footers(void)
{
    assert(strcmp(display_interaction_weather_footer(),
                  "BOOT: CALENDAR | KEY: STATUS") == 0);
    assert(strcmp(display_interaction_calendar_footer(true),
                  "BOOT: IMAGE | KEY: STATUS") == 0);
    assert(strcmp(display_interaction_calendar_footer(false),
                  "BOOT: HOME | KEY: STATUS") == 0);
    assert(strcmp(display_interaction_status_footer(),
                  "BOOT: HOME | KEY: CHAT | HOLD KEY 2s: SYNC TIME") == 0);
    assert(strcmp(display_interaction_chat_footer(),
                  "BOOT: HOME | KEY: SETTINGS") == 0);
    assert(strcmp(display_interaction_chat_next_turn_footer(),
                  "KEY: NEXT TURN | BOOT: CANCEL") == 0);
    assert(strcmp(display_interaction_settings_navigation_footer(),
                  "BOOT: HOME | KEY: ONLINE UPDATE") == 0);
    assert(strcmp(display_interaction_online_update_footer(false),
                  "BOOT: HOME | KEY: STATUS | HOLD KEY 2s: CHECK UPDATE") ==
           0);
    assert(strcmp(display_interaction_online_update_footer(true),
                  "BOOT: HOME | KEY: STATUS | HOLD KEY 2s: REVIEW UPDATE") ==
           0);

    assert_footer_uses_concrete_targets(
        display_interaction_weather_footer());
    assert_footer_uses_concrete_targets(
        display_interaction_calendar_footer(true));
    assert_footer_uses_concrete_targets(
        display_interaction_calendar_footer(false));
    assert_footer_uses_concrete_targets(display_interaction_status_footer());
    assert_footer_uses_concrete_targets(display_interaction_chat_footer());
    assert_footer_uses_concrete_targets(
        display_interaction_chat_next_turn_footer());
    assert_footer_uses_concrete_targets(
        display_interaction_settings_navigation_footer());
    assert_footer_uses_concrete_targets(
        display_interaction_online_update_footer(false));
    assert_footer_uses_concrete_targets(
        display_interaction_online_update_footer(true));
}

static void test_image_navigation_footer(void)
{
    char footer[64];
    char short_footer[12] = "stale";

    assert(display_interaction_format_image_navigation(
        footer, sizeof(footer), 1U, 6U));
    assert(strcmp(footer,
                  "BOOT: HOME | KEY: NEXT IMAGE | 2/6") == 0);
    assert_footer_uses_concrete_targets(footer);

    assert(display_interaction_format_image_navigation(
        footer, sizeof(footer), 99U, 6U));
    assert(strcmp(footer,
                  "BOOT: HOME | KEY: NEXT IMAGE | 1/6") == 0);

    assert(display_interaction_format_image_navigation(
        footer, sizeof(footer), 0U, 1U));
    assert(strcmp(footer, "BOOT: HOME | KEY: STATUS") == 0);
    assert_footer_uses_concrete_targets(footer);

    assert(!display_interaction_format_image_navigation(
        short_footer, sizeof(short_footer), 0U, 32U));
    assert(short_footer[0] == '\0');
    assert(!display_interaction_format_image_navigation(
        NULL, sizeof(footer), 0U, 1U));
    assert(!display_interaction_format_image_navigation(
        footer, 0U, 0U, 1U));
    assert_footer_uses_concrete_targets(
        display_interaction_image_action_footer());
}

static void test_settings_footer_names_next_saving_action(void)
{
    const char *turn_on =
        display_interaction_settings_action_footer(false);
    const char *turn_off =
        display_interaction_settings_action_footer(true);

    assert(strcmp(
               turn_on,
               "HOLD BOOT 2s: MANUAL SAVING ON | HOLD KEY 3s: WEB SETTINGS") ==
           0);
    assert(strcmp(
               turn_off,
               "HOLD BOOT 2s: MANUAL SAVING OFF | HOLD KEY 3s: WEB SETTINGS") ==
           0);
    assert(strstr(turn_on, "WEB SETTINGS") != NULL);
    assert(strstr(turn_off, "WEB SETTINGS") != NULL);
    assert_footer_uses_concrete_targets(turn_on);
    assert_footer_uses_concrete_targets(turn_off);
}

static void test_turn_label(void)
{
    char label[16];

    assert(voice_display_format_turn(label, sizeof(label), 1U, 5U));
    assert(strcmp(label, "TURN 1/5") == 0);

    assert(voice_display_format_turn(label, sizeof(label), 5U, 5U));
    assert(strcmp(label, "TURN 5/5") == 0);

    assert(voice_display_format_turn(label, sizeof(label), 6U, 5U));
    assert(strcmp(label, "TURN 5/5") == 0);
}

static void test_missing_or_short_output(void)
{
    char label[16] = "stale";
    char short_label[8] = "stale";

    assert(!voice_display_format_turn(label, sizeof(label), 0U, 5U));
    assert(label[0] == '\0');
    assert(!voice_display_format_turn(label, sizeof(label), 1U, 0U));
    assert(label[0] == '\0');
    assert(!voice_display_format_turn(short_label, sizeof(short_label),
                                      1U, 5U));
    assert(short_label[0] == '\0');
    assert(!voice_display_format_turn(NULL, sizeof(label), 1U, 5U));
    assert(!voice_display_format_turn(label, 0U, 1U, 5U));
}

static void test_wrapped_text_uses_measured_width(void)
{
    const int unit = 1;
    voice_display_text_window_t window;
    assert(voice_display_build_text_window(
        "甲乙丙丁戊", 4, VOICE_DISPLAY_TEXT_MAX_LINES,
        measured_width, (void *)&unit, &window));
    assert(window.count == 3U);
    assert(strcmp(voice_display_text_window_line(&window, 0U),
                  "甲乙") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 1U),
                  "丙丁") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 2U),
                  "戊") == 0);

    assert(voice_display_build_text_window(
        "one two three", 7, VOICE_DISPLAY_TEXT_MAX_LINES,
        measured_width, (void *)&unit, &window));
    assert(window.count == 2U);
    assert(strcmp(voice_display_text_window_line(&window, 0U),
                  "one two") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 1U),
                  "three") == 0);
}

static void test_latest_four_lines_replace_old_content(void)
{
    const int unit = 1;
    voice_display_text_window_t window;
    assert(voice_display_build_text_window(
        "first\nsecond\nthird\nfourth\nfifth", 20,
        VOICE_DISPLAY_TEXT_MAX_LINES, measured_width, (void *)&unit,
        &window));
    assert(window.count == VOICE_DISPLAY_TEXT_MAX_LINES);
    assert(strcmp(voice_display_text_window_line(&window, 0U),
                  "second") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 1U),
                  "third") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 2U),
                  "fourth") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 3U),
                  "fifth") == 0);
    for (uint8_t index = 0U; index < window.count; ++index) {
        assert(strstr(voice_display_text_window_line(&window, index),
                      "...") == NULL);
    }

    assert(voice_display_build_text_window(
        "one\ntwo\nthree", 20, 2U, measured_width, (void *)&unit,
        &window));
    assert(window.count == 2U);
    assert(strcmp(voice_display_text_window_line(&window, 0U),
                  "two") == 0);
    assert(strcmp(voice_display_text_window_line(&window, 1U),
                  "three") == 0);
}

static void test_text_window_invalid_arguments(void)
{
    const int unit = 1;
    voice_display_text_window_t window;
    assert(!voice_display_build_text_window(
        NULL, 20, 4U, measured_width, (void *)&unit, &window));
    assert(window.count == 0U);
    assert(!voice_display_build_text_window(
        "text", 0, 4U, measured_width, (void *)&unit, &window));
    assert(!voice_display_build_text_window(
        "text", 20, 5U, measured_width, (void *)&unit, &window));
    assert(!voice_display_build_text_window(
        "text", 20, 4U, NULL, (void *)&unit, &window));
    assert(!voice_display_build_text_window(
        "text", 20, 4U, measured_width, (void *)&unit, NULL));
    assert(voice_display_text_window_line(&window, 0U) == NULL);
    assert(voice_display_text_window_line(NULL, 0U) == NULL);
}

int main(void)
{
    test_mode_title();
    test_ready_prompt_names_the_chat_action();
    test_targeted_navigation_footers();
    test_image_navigation_footer();
    test_settings_footer_names_next_saving_action();
    test_turn_label();
    test_missing_or_short_output();
    test_wrapped_text_uses_measured_width();
    test_latest_four_lines_replace_old_content();
    test_text_window_invalid_arguments();
    puts("voice display model tests passed");
    return 0;
}
