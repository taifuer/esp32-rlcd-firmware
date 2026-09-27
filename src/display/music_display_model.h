#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MUSIC_TITLE_LINE_CAPACITY 132U

static inline uint32_t music_display_sleep_minutes(uint32_t seconds)
{
    return seconds / 60U + (seconds % 60U != 0U ? 1U : 0U);
}

static inline bool music_display_format_status(char *buffer, size_t capacity,
                                               const char *state, const char *mode)
{
    if (buffer == NULL || capacity == 0U) return false;
    const int written = snprintf(buffer, capacity, "%s | %s",
        state != NULL ? state : "STOPPED", mode != NULL ? mode : "SEQUENTIAL");
    return written > 0 && (size_t)written < capacity;
}

static inline bool music_display_format_detail(char *buffer, size_t capacity,
                                               unsigned volume, uint32_t seconds,
                                               bool failed)
{
    if (buffer == NULL || capacity == 0U || volume > 100U) return false;
    const int written = failed
        ? snprintf(buffer, capacity, "CHECK FILE / CARD | KEY: RETRY")
        : seconds != 0U
        ? snprintf(buffer, capacity, "VOLUME %u%% | STOP IN %lum", volume,
                   (unsigned long)music_display_sleep_minutes(seconds))
        : snprintf(buffer, capacity, "VOLUME %u%%", volume);
    return written > 0 && (size_t)written < capacity;
}

/* Input has already been sanitized against the display font. Keep the start
 * of a title, never split a UTF-8 code point or scroll to its final words. */
static inline bool music_display_title_lines(
    const char *title, int width, int (*measure)(const char *, void *),
    void *context, char lines[2][MUSIC_TITLE_LINE_CAPACITY])
{
    if (title == NULL || width <= 0 || measure == NULL || lines == NULL) return false;
    memset(lines, 0, 2U * MUSIC_TITLE_LINE_CAPACITY);
    size_t offset = 0U;
    for (unsigned row = 0; row < 2U && title[offset] != '\0'; ++row) {
        size_t used = 0U;
        while (title[offset] != '\0') {
            const unsigned char lead = (unsigned char)title[offset];
            const size_t count = lead < 0x80U ? 1U : lead < 0xe0U ? 2U : lead < 0xf0U ? 3U : 4U;
            for (size_t i = 1U; i < count; ++i) {
                if (title[offset + i] == '\0' || ((unsigned char)title[offset + i] & 0xc0U) != 0x80U) return false;
            }
            if (used + count >= MUSIC_TITLE_LINE_CAPACITY) break;
            memcpy(lines[row] + used, title + offset, count);
            lines[row][used + count] = '\0';
            if (measure(lines[row], context) > width) {
                lines[row][used] = '\0';
                break;
            }
            used += count;
            offset += count;
        }
        if (row == 1U && title[offset] != '\0') {
            for (;;) {
                if (used + 3U < MUSIC_TITLE_LINE_CAPACITY) {
                    memcpy(lines[row] + used, "...", 4U);
                    if (measure(lines[row], context) <= width) break;
                }
                if (used == 0U) { lines[row][0] = '\0'; break; }
                do { --used; } while (used > 0U && ((unsigned char)lines[row][used] & 0xc0U) == 0x80U);
                lines[row][used] = '\0';
            }
        }
    }
    return true;
}
