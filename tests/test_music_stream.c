#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "music_stream.h"
#include "sd_media.h"
#include "esp_mp3_dec.h"

struct sd_media_read { int unused; };
static struct sd_media_read lease;
static unsigned leases, allocations, decoders, writes, frames, polls;
static unsigned stop_at, fail_allocation, allocation_attempts;
static bool mount_fails, write_fails, decoder_fails, decoder_oversize;
void *heap_caps_malloc(size_t bytes, unsigned caps)
{
    (void)caps;
    if (++allocation_attempts == fail_allocation) return NULL;
    assert(bytes <= MUSIC_PCM_BUFFER_BYTES);
    void *p = malloc(bytes);
    if (p != NULL) ++allocations;
    return p;
}
void heap_caps_free(void *p) { if (p != NULL) { assert(allocations > 0); --allocations; free(p); } }
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait)
{
    assert(wait == 0 && leases == 0);
    if (mount_fails) return ESP_FAIL;
    ++leases; *session = &lease; return ESP_OK;
}
esp_err_t sd_media_end_read(sd_media_read_t *session)
{
    assert(session == &lease && leases == 1); --leases; return ESP_OK;
}
esp_audio_err_t esp_mp3_dec_open(void *config, uint32_t size, void **decoder)
{
    assert(config == NULL && size == 0 && decoders == 0);
    ++decoders; *decoder = &decoders; return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_mp3_dec_close(void *decoder)
{
    assert(decoder == &decoders && decoders == 1); --decoders; return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_mp3_dec_decode(void *decoder, esp_audio_dec_in_raw_t *raw,
                                  esp_audio_dec_out_frame_t *out, esp_audio_dec_info_t *info)
{
    assert(decoder == &decoders && raw->len == 417 && out->len == MUSIC_PCM_BUFFER_BYTES);
    if (decoder_fails) return ESP_AUDIO_ERR_FAIL;
    memset(out->buffer, 0, out->len);
    raw->consumed = raw->len;
    out->decoded_size = decoder_oversize ? out->len + 1U : out->len;
    *info = (esp_audio_dec_info_t){.sample_rate = 44100, .bits_per_sample = 16, .channel = 2};
    return ESP_AUDIO_ERR_OK;
}
static bool ready(void *context)
{
    (void)context;
    return ++polls != stop_at;
}
static esp_err_t write_pcm(void *context, uint32_t rate, const int16_t *pcm, size_t count)
{
    (void)context;
    assert(rate == 16000 || rate == 44100);
    assert(count > 0 && count <= 480);
    for (size_t i = 0; i < count; ++i) assert(pcm[i] == (rate == 16000 ? 200 : 0));
    ++writes; frames += (unsigned)count;
    return write_fails ? ESP_FAIL : ESP_OK;
}
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
static music_track_t fixture(const char *name, const uint8_t *data, size_t bytes)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/rlcd/music/%s", SD_MEDIA_MOUNT_PATH, name);
    FILE *file = fopen(path, "wb");
    assert(file != NULL && fwrite(data, 1, bytes, file) == bytes);
    fclose(file);
    music_track_t track = {.file_bytes = (uint32_t)bytes, .info.format = music_filename_format(name)};
    strcpy(track.filename, name);
    return track;
}
static esp_err_t run(const music_track_t *track)
{
    writes = frames = polls = allocation_attempts = 0;
    const music_stream_sink_t sink = {.ready = ready, .write = write_pcm};
    const esp_err_t result = music_stream_run(track, &sink);
    assert(leases == 0 && allocations == 0 && decoders == 0);
    return result;
}
int main(void)
{
    assert(mkdir(SD_MEDIA_MOUNT_PATH "/rlcd", 0700) == 0);
    assert(mkdir(SD_MEDIA_MOUNT_PATH "/rlcd/music", 0700) == 0);
    uint8_t wav[6444] = {0};
    memcpy(wav, "RIFF", 4); put32(wav + 4, sizeof(wav) - 8);
    memcpy(wav + 8, "WAVEfmt ", 8); put32(wav + 16, 16);
    wav[20] = 1; wav[22] = 2; put32(wav + 24, 16000);
    put32(wav + 28, 64000); wav[32] = 4; wav[34] = 16;
    memcpy(wav + 36, "data", 4); put32(wav + 40, sizeof(wav) - 44);
    for (size_t i = 44; i < sizeof(wav); i += 4) { wav[i] = 100; wav[i + 2] = 44; wav[i + 3] = 1; }
    music_track_t track = fixture("test.wav", wav, sizeof(wav));
    assert(run(&track) == ESP_OK && frames == 1600 && writes > 1);
    for (stop_at = 1; stop_at < 7; ++stop_at) assert(run(&track) == ESP_ERR_INVALID_STATE);
    stop_at = 0;
    for (fail_allocation = 1; fail_allocation <= 2; ++fail_allocation) assert(run(&track) == ESP_ERR_NO_MEM);
    fail_allocation = 0;
    mount_fails = true; assert(run(&track) == ESP_FAIL); mount_fails = false;
    write_fails = true; assert(run(&track) == ESP_FAIL); write_fails = false;
    ++track.file_bytes; assert(run(&track) == ESP_ERR_NOT_FOUND);
    uint8_t mp3[834] = {0};
    memcpy(mp3, "\xff\xfb\x90\x00", 4); memcpy(mp3 + 417, mp3, 4);
    track = fixture("test.mp3", mp3, sizeof(mp3));
    assert(run(&track) == ESP_OK && frames == 2304);
    decoder_fails = true; assert(run(&track) == ESP_ERR_INVALID_RESPONSE); decoder_fails = false;
    decoder_oversize = true; assert(run(&track) == ESP_ERR_INVALID_SIZE);
    puts("music stream: PCM/downmix/chunks, cancellation, bounded allocation and decoder cleanup passed");
    return 0;
}
