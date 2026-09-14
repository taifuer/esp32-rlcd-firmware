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
    puts("music title wrapping tests passed");
}
