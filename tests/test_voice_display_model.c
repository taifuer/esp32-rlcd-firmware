#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    assert(strcmp(voice_display_mode_title(true), "AI VOICE") == 0);
    assert(strcmp(voice_display_mode_title(false), "OFFLINE VOICE") == 0);
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
    test_turn_label();
    test_missing_or_short_output();
    test_wrapped_text_uses_measured_width();
    test_latest_four_lines_replace_old_content();
    test_text_window_invalid_arguments();
    puts("voice display model tests passed");
    return 0;
}
