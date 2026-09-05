#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "music_format.h"

static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
static bool probe(const uint8_t *data, size_t size, music_format_t format, music_file_info_t *info)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(fwrite(data, 1, size, file) == size);
    const bool valid = music_file_probe(file, (uint32_t)size, format, info);
    if (valid) assert(ftell(file) == (long)info->data_offset);
    fclose(file);
    return valid;
}

int main(void)
{
    assert(music_filename_format("01-音乐.MP3") == MUSIC_FORMAT_MP3);
    assert(music_filename_format("test.Wav") == MUSIC_FORMAT_WAV);
    assert(music_filename_format("song\xed\xa0\x80.mp3") == MUSIC_FORMAT_NONE);
    assert(music_filename_format("song\xc0\xaf.mp3") == MUSIC_FORMAT_NONE);
    assert(music_filename_format("song\xf4\x90\x80\x80.mp3") == MUSIC_FORMAT_NONE);
    const char *bad[] = {".song.mp3", "../song.mp3", "a/b.mp3", "a\\b.wav", "C:song.mp3", "a\n.mp3", "song.aac", "", ".mp3"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) assert(music_filename_format(bad[i]) == MUSIC_FORMAT_NONE);
    music_mp3_frame_t frame;
    const uint8_t header[] = {0xff, 0xfb, 0x90, 0};
    assert(music_mp3_parse_header(header, &frame));
    assert(frame.frame_bytes == 417 && frame.sample_rate == 44100 && frame.channels == 2 && frame.samples == 1152);
    uint8_t mp3[844] = {0};
    memcpy(mp3, "ID3\x04\x00\x00\x00\x00\x00\x00", 10);
    memcpy(mp3 + 10, header, 4); memcpy(mp3 + 427, header, 4);
    music_file_info_t info;
    assert(probe(mp3, sizeof(mp3), MUSIC_FORMAT_MP3, &info));
    assert(info.data_offset == 10 && info.data_bytes == 834);
    mp3[6] = 0x80;
    assert(!probe(mp3, sizeof(mp3), MUSIC_FORMAT_MP3, &info));
    mp3[6] = 0x7f; mp3[7] = 0x7f; mp3[8] = 0x7f; mp3[9] = 0x7f;
    assert(!probe(mp3, sizeof(mp3), MUSIC_FORMAT_MP3, &info));
    memset(mp3, 0, 10);
    mp3[427] = 0;
    memcpy(mp3, header, 4);
    assert(!probe(mp3, sizeof(mp3), MUSIC_FORMAT_MP3, &info));
    for (unsigned v = 0; v < 256; ++v) {
        uint8_t fuzz[] = {0xff, (uint8_t)v, 0, 0};
        assert(!music_mp3_parse_header(fuzz, &frame)); /* free bitrate unsupported */
    }
    uint8_t wav[48] = {0};
    memcpy(wav, "RIFF", 4); put32(wav + 4, 40);
    memcpy(wav + 8, "WAVEfmt ", 8); put32(wav + 16, 16);
    wav[20] = 1; wav[22] = 1; put32(wav + 24, 16000);
    put32(wav + 28, 32000); wav[32] = 2; wav[34] = 16;
    memcpy(wav + 36, "data", 4); put32(wav + 40, 4);
    assert(probe(wav, sizeof(wav), MUSIC_FORMAT_WAV, &info));
    assert(info.data_offset == 44 && info.data_bytes == 4 && info.sample_rate == 16000);
    for (size_t i = 0; i < sizeof(wav); ++i) assert(!probe(wav, i, MUSIC_FORMAT_WAV, &info));
    wav[20] = 3; assert(!probe(wav, sizeof(wav), MUSIC_FORMAT_WAV, &info));
    wav[20] = 1; wav[34] = 24; assert(!probe(wav, sizeof(wav), MUSIC_FORMAT_WAV, &info));
    wav[34] = 16; put32(wav + 40, UINT32_MAX); assert(!probe(wav, sizeof(wav), MUSIC_FORMAT_WAV, &info));
    assert(!music_file_probe(NULL, 128, MUSIC_FORMAT_MP3, &info));
    puts("music format tests passed");
    return 0;
}
