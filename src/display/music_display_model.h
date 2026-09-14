#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define MUSIC_TITLE_LINE_CAPACITY 132U

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
