#include <assert.h>
#include <stdio.h>
#include "music_display_model.h"

static int measure(const char *text, void *unused)
{
    (void)unused;
    int width = 0;
    while (*text != '\0') {
        if (((unsigned char)*text & 0xc0U) != 0x80U) ++width;
        ++text;
    }
    return width;
}

int main(void)
{
    char lines[2][MUSIC_TITLE_LINE_CAPACITY];
    assert(music_display_title_lines("歌曲", 4, measure, NULL, lines));
    assert(strcmp(lines[0], "歌曲") == 0 && lines[1][0] == '\0');
    assert(music_display_title_lines("一二三四五六七八九十", 4, measure, NULL, lines));
    assert(strcmp(lines[0], "一二三四") == 0 && strcmp(lines[1], "五...") == 0);
    assert(music_display_title_lines("abcdefgh", 4, measure, NULL, lines));
    assert(strcmp(lines[0], "abcd") == 0 && strcmp(lines[1], "efgh") == 0);
    assert(!music_display_title_lines("\xe4\xb8", 4, measure, NULL, lines));
    assert(!music_display_title_lines(NULL, 4, measure, NULL, lines));
    char status[48];
    assert(music_display_format_status(status, sizeof(status), "PLAYING", "REPEAT ALL"));
    assert(strcmp(status, "PLAYING | REPEAT ALL") == 0);
    assert(music_display_format_status(status, sizeof(status), NULL, NULL));
    assert(strcmp(status, "STOPPED | SEQUENTIAL") == 0);
    assert(!music_display_format_status(status, 4U, "PAUSED", "REPEAT ONE"));
    assert(music_display_sleep_minutes(0) == 0U);
    assert(music_display_sleep_minutes(1) == 1U);
    assert(music_display_sleep_minutes(60) == 1U);
    assert(music_display_sleep_minutes(61) == 2U);
    assert(music_display_sleep_minutes(UINT32_MAX) == 71582789U);
    assert(music_display_format_detail(status, sizeof(status), 68, 1740, false));
    assert(strcmp(status, "VOLUME 68% | STOP IN 29m") == 0);
    assert(music_display_format_detail(status, sizeof(status), 0, 0, false));
    assert(strcmp(status, "VOLUME 0%") == 0);
    assert(music_display_format_detail(status, sizeof(status), 100, 60, true));
    assert(strcmp(status, "CHECK FILE / CARD | KEY: RETRY") == 0);
    assert(!music_display_format_detail(status, 3U, 68, 0, false));
    assert(!music_display_format_detail(status, sizeof(status), 101, 0, false));
    puts("music title wrapping tests passed");
}
