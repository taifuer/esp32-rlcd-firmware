#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_DISPLAY_TEXT_MAX_LINES 4U
#define VOICE_DISPLAY_TEXT_LINE_CAPACITY 192U

typedef int (*voice_display_text_measure_t)(const char *text,
                                           void *context);

typedef struct {
    uint8_t count;
    uint8_t first;
    char lines[VOICE_DISPLAY_TEXT_MAX_LINES]
              [VOICE_DISPLAY_TEXT_LINE_CAPACITY];
} voice_display_text_window_t;

const char *voice_display_mode_title(bool ai_mode);
bool voice_display_format_turn(char *buffer, size_t capacity,
                               uint8_t turn_number, uint8_t max_turns);
/* Wrap with the supplied font measurement and retain only the newest lines.
 * Lines are exposed in reading order through voice_display_text_window_line. */
bool voice_display_build_text_window(
    const char *text, int max_width, uint8_t max_lines,
    voice_display_text_measure_t measure, void *measure_context,
    voice_display_text_window_t *window);
const char *voice_display_text_window_line(
    const voice_display_text_window_t *window, uint8_t index);

#ifdef __cplusplus
}
#endif
