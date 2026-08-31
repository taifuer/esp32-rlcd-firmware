#include "voice_display_model.h"

#include <stdio.h>
#include <string.h>

static size_t utf8_sequence_bytes(unsigned char lead)
{
    if (lead < 0x80U) {
        return 1U;
    }
    if (lead >= 0xc2U && lead <= 0xdfU) {
        return 2U;
    }
    if (lead >= 0xe0U && lead <= 0xefU) {
        return 3U;
    }
    if (lead >= 0xf0U && lead <= 0xf4U) {
        return 4U;
    }
    return 1U;
}

static bool ascii_space(unsigned char value)
{
    return value == ' ' || value == '\t';
}

static const char *skip_spacing(const char *text)
{
    while (text != NULL &&
           (*text == ' ' || *text == '\t' || *text == '\n' ||
            *text == '\r')) {
        ++text;
    }
    return text;
}

static const char *build_line(
    const char *source, char *line, size_t capacity, int max_width,
    voice_display_text_measure_t measure, void *measure_context)
{
    source = skip_spacing(source);
    line[0] = '\0';
    if (source == NULL || *source == '\0') {
        return source;
    }

    size_t output_length = 0U;
    size_t break_length = 0U;
    const char *break_source = NULL;
    const char *cursor = source;
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
        const size_t sequence = utf8_sequence_bytes(
            (unsigned char)*cursor);
        if (output_length + sequence >= capacity) {
            break;
        }

        const bool space = sequence == 1U &&
                           ascii_space((unsigned char)*cursor);
        const size_t before_sequence = output_length;
        memcpy(line + output_length, cursor, sequence);
        output_length += sequence;
        line[output_length] = '\0';
        if (measure(line, measure_context) > max_width) {
            output_length = before_sequence;
            line[output_length] = '\0';
            if (space && output_length > 0U) {
                cursor += sequence;
            } else if (break_source != NULL && break_length > 0U) {
                output_length = break_length;
                line[output_length] = '\0';
                cursor = break_source;
            } else if (output_length == 0U) {
                /* Always consume one complete code point so an unexpectedly
                 * wide glyph cannot stall the rolling window. */
                memcpy(line, cursor, sequence);
                output_length = sequence;
                line[output_length] = '\0';
                cursor += sequence;
            }
            break;
        }
        cursor += sequence;
        if (space) {
            break_length = before_sequence;
            break_source = cursor;
        }
    }

    while (output_length > 0U &&
           ascii_space((unsigned char)line[output_length - 1U])) {
        line[--output_length] = '\0';
    }
    return skip_spacing(cursor);
}

const char *voice_display_mode_title(bool ai_mode)
{
    return ai_mode ? "AI VOICE" : "OFFLINE VOICE";
}

bool voice_display_format_turn(char *buffer, size_t capacity,
                               uint8_t turn_number, uint8_t max_turns)
{
    if (buffer == NULL || capacity == 0U) {
        return false;
    }
    buffer[0] = '\0';
    if (turn_number == 0U || max_turns == 0U) {
        return false;
    }

    const uint8_t normalized_turn = turn_number <= max_turns
                                        ? turn_number
                                        : max_turns;
    const int written = snprintf(buffer, capacity, "TURN %u/%u",
                                 normalized_turn, max_turns);
    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}

bool voice_display_build_text_window(
    const char *text, int max_width, uint8_t max_lines,
    voice_display_text_measure_t measure, void *measure_context,
    voice_display_text_window_t *window)
{
    if (window == NULL) {
        return false;
    }
    memset(window, 0, sizeof(*window));
    if (text == NULL || max_width <= 0 || max_lines == 0U ||
        max_lines > VOICE_DISPLAY_TEXT_MAX_LINES || measure == NULL) {
        return false;
    }

    const char *cursor = skip_spacing(text);
    size_t total_lines = 0U;
    while (cursor != NULL && *cursor != '\0') {
        char *line = window->lines[total_lines % max_lines];
        const char *next = build_line(
            cursor, line, VOICE_DISPLAY_TEXT_LINE_CAPACITY,
            max_width, measure, measure_context);
        if (line[0] == '\0' || next == NULL || next == cursor) {
            break;
        }
        ++total_lines;
        cursor = next;
    }

    window->count = total_lines < max_lines
                        ? (uint8_t)total_lines
                        : max_lines;
    window->first = total_lines > max_lines
                        ? (uint8_t)(total_lines % max_lines)
                        : 0U;
    return true;
}

const char *voice_display_text_window_line(
    const voice_display_text_window_t *window, uint8_t index)
{
    if (window == NULL || index >= window->count ||
        window->count == 0U || window->count > VOICE_DISPLAY_TEXT_MAX_LINES ||
        window->first >= window->count) {
        return NULL;
    }
    return window->lines[(window->first + index) % window->count];
}
