#include "music_format.h"

#include <ctype.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool supported_rate(uint32_t rate)
{
    static const uint32_t rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        if (rates[i] == rate) return true;
    }
    return false;
}

music_format_t music_filename_format(const char *name)
{
    if (name == NULL || name[0] == '.' || name[0] == '\0') return MUSIC_FORMAT_NONE;
    const size_t length = strlen(name);
    if (length < 5U || length >= MUSIC_FILENAME_CAPACITY) return MUSIC_FORMAT_NONE;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 32U || c == 127U || c == '/' || c == '\\' || c == ':') return MUSIC_FORMAT_NONE;
        if (c >= 128U) {
            unsigned following;
            uint32_t codepoint, minimum;
            if (c >= 0xc2U && c <= 0xdfU) { following = 1; codepoint = c & 0x1fU; minimum = 0x80U; }
            else if (c >= 0xe0U && c <= 0xefU) { following = 2; codepoint = c & 0x0fU; minimum = 0x800U; }
            else if (c >= 0xf0U && c <= 0xf4U) { following = 3; codepoint = c & 7U; minimum = 0x10000U; }
            else return MUSIC_FORMAT_NONE;
            if (following >= length - i) return MUSIC_FORMAT_NONE;
            for (unsigned j = 0; j < following; ++j) {
                const unsigned char next = (unsigned char)name[++i];
                if ((next & 0xc0U) != 0x80U) return MUSIC_FORMAT_NONE;
                codepoint = (codepoint << 6) | (next & 0x3fU);
            }
            if (codepoint < minimum || codepoint > 0x10ffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return MUSIC_FORMAT_NONE;
        }
    }
    if (name[length - 4U] != '.') return MUSIC_FORMAT_NONE;
    char ext[4] = {0};
    for (size_t i = 0; i < 3U; ++i) ext[i] = (char)tolower((unsigned char)name[length - 3U + i]);
    if (strcmp(ext, "mp3") == 0) return MUSIC_FORMAT_MP3;
    if (strcmp(ext, "wav") == 0) return MUSIC_FORMAT_WAV;
    return MUSIC_FORMAT_NONE;
}

bool music_mp3_parse_header(const uint8_t h[4], music_mp3_frame_t *frame)
{
    if (h == NULL || frame == NULL || h[0] != 0xffU || (h[1] & 0xe0U) != 0xe0U) return false;
    const unsigned version = (h[1] >> 3) & 3U;
    const unsigned layer = (h[1] >> 1) & 3U;
    const unsigned rate_index = (h[2] >> 2) & 3U;
    const unsigned bitrate_index = h[2] >> 4;
    if (version == 1U || layer != 1U || rate_index == 3U ||
        bitrate_index == 0U || bitrate_index == 15U || (h[3] & 3U) == 2U) return false;
    static const uint16_t rates[] = {44100, 48000, 32000};
    static const uint16_t br1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const uint16_t br2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    const uint32_t rate = rates[rate_index] / (version == 3U ? 1U : version == 2U ? 2U : 4U);
    const uint32_t bitrate = (version == 3U ? br1 : br2)[bitrate_index] * 1000U;
    const uint32_t bytes = (version == 3U ? 144U : 72U) * bitrate / rate + ((h[2] >> 1) & 1U);
    if (bytes < 24U || bytes > MUSIC_MAX_FRAME_BYTES) return false;
    *frame = (music_mp3_frame_t){.sample_rate = rate, .frame_bytes = (uint16_t)bytes,
        .samples = version == 3U ? 1152U : 576U,
        .channels = (h[3] >> 6) == 3U ? 1U : 2U};
    return true;
}

static bool probe_wav(FILE *file, uint32_t bytes, music_file_info_t *info)
{
    uint8_t header[16];
    if (fread(header, 1, 12, file) != 12 || memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0) return false;
    const uint32_t riff_bytes = le32(header + 4);
    if (riff_bytes < 36U || riff_bytes > bytes - 8U) return false;
    const uint32_t end = riff_bytes + 8U;
    bool have_format = false;
    uint32_t offset = 12U;
    for (unsigned chunks = 0; chunks < 64U && offset <= MUSIC_MAX_METADATA_BYTES; ++chunks) {
        if (offset > end || end - offset < 8U || fseek(file, (long)offset, SEEK_SET) != 0 ||
            fread(header, 1, 8, file) != 8) return false;
        const uint32_t size = le32(header + 4);
        offset += 8U;
        if (size > end - offset) return false;
        if (memcmp(header, "fmt ", 4) == 0) {
            if (have_format || size < 16U || fread(header, 1, 16, file) != 16) return false;
            info->channels = (uint8_t)le16(header + 2);
            info->sample_rate = le32(header + 4);
            if (le16(header) != 1U || le16(header + 2) < 1U || le16(header + 2) > 2U ||
                le16(header + 14) != 16U || !supported_rate(info->sample_rate) ||
                le16(header + 12) != info->channels * 2U ||
                le32(header + 8) != info->sample_rate * info->channels * 2U) return false;
            have_format = true;
        } else if (memcmp(header, "data", 4) == 0) {
            if (!have_format || size == 0U || size % (info->channels * 2U) != 0U) return false;
            info->data_offset = offset;
            info->data_bytes = size;
            return fseek(file, (long)offset, SEEK_SET) == 0;
        }
        offset += size + (size & 1U);
    }
    return false;
}

static bool probe_mp3(FILE *file, uint32_t bytes, music_file_info_t *info)
{
    uint8_t h[10];
    if (fread(h, 1, sizeof(h), file) != sizeof(h)) return false;
    uint32_t offset = 0;
    if (memcmp(h, "ID3", 3) == 0) {
        if (h[3] < 2U || h[3] > 4U || h[4] == 0xffU ||
            ((h[6] | h[7] | h[8] | h[9]) & 0x80U) != 0U) return false;
        offset = 10U + ((uint32_t)h[6] << 21) + ((uint32_t)h[7] << 14) +
                 ((uint32_t)h[8] << 7) + h[9];
        if (h[3] == 4U && (h[5] & 0x10U)) offset += 10U;
        if (offset > MUSIC_MAX_METADATA_BYTES || offset >= bytes) return false;
    }
    /* Two matching frames avoid treating an embedded album cover as audio. */
    if (bytes - offset < 4U || fseek(file, (long)offset, SEEK_SET) != 0 || fread(h, 1, 4, file) != 4) return false;
    for (unsigned skipped = 0; skipped < 4096U; ++skipped) {
        music_mp3_frame_t first, second;
        if (music_mp3_parse_header(h, &first) && first.frame_bytes <= bytes - offset &&
            bytes - offset - first.frame_bytes >= 4U) {
            uint8_t next[4];
            if (fseek(file, (long)(offset + first.frame_bytes), SEEK_SET) != 0 ||
                fread(next, 1, 4, file) != 4) return false;
            if (music_mp3_parse_header(next, &second) && second.sample_rate == first.sample_rate &&
                second.channels == first.channels && second.frame_bytes <= bytes - offset - first.frame_bytes) {
                info->data_offset = offset;
                info->data_bytes = bytes - offset;
                info->sample_rate = first.sample_rate;
                info->channels = first.channels;
                return fseek(file, (long)offset, SEEK_SET) == 0;
            }
            if (fseek(file, (long)(offset + 4U), SEEK_SET) != 0) return false;
        }
        if (bytes - offset <= 4U) break;
        const int next_byte = fgetc(file);
        if (next_byte == EOF) return false;
        memmove(h, h + 1, 3);
        h[3] = (uint8_t)next_byte;
        ++offset;
    }
    return false;
}

bool music_file_probe(FILE *file, uint32_t bytes, music_format_t format, music_file_info_t *info)
{
    if (file == NULL || info == NULL || bytes < 44U || bytes > MUSIC_MAX_FILE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) return false;
    *info = (music_file_info_t){.format = format};
    if (format == MUSIC_FORMAT_WAV) return probe_wav(file, bytes, info);
    if (format == MUSIC_FORMAT_MP3) return probe_mp3(file, bytes, info);
    return false;
}
