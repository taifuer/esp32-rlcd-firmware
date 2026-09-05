#include "music_stream.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_mp3_dec.h"
#include "sd_media.h"

esp_err_t music_stream_run(const music_track_t *track, const music_stream_sink_t *sink)
{
    if (track == NULL || sink == NULL || sink->ready == NULL || sink->write == NULL ||
        music_filename_format(track->filename) == MUSIC_FORMAT_NONE) return ESP_ERR_INVALID_ARG;
    esp_err_t error;
    sd_media_read_t *lease = NULL;
    FILE *file = NULL;
    void *decoder = NULL;
    uint8_t *input = NULL;
    int16_t *pcm = NULL;
    if (!sink->ready(sink->context)) return ESP_ERR_INVALID_STATE;
    error = sd_media_begin_read(&lease, 0);
    if (error != ESP_OK) return error;
    char path[192];
    const int length = snprintf(path, sizeof(path), "%s%s/%s", SD_MEDIA_MOUNT_PATH,
                                MUSIC_CARD_DIRECTORY, track->filename);
    struct stat st;
    error = ESP_ERR_NOT_FOUND;
    if (length < 0 || (size_t)length >= sizeof(path) || stat(path, &st) != 0 ||
        !S_ISREG(st.st_mode) || st.st_size != track->file_bytes) goto done;
    file = fopen(path, "rb");
    if (file == NULL) goto done;
    music_file_info_t info;
    error = ESP_ERR_INVALID_RESPONSE;
    if (!music_file_probe(file, track->file_bytes, track->info.format, &info)) goto done;
    pcm = heap_caps_malloc(MUSIC_PCM_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    input = heap_caps_malloc(MUSIC_MAX_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    error = ESP_ERR_NO_MEM;
    if (pcm == NULL || input == NULL) goto done;
    if (info.format == MUSIC_FORMAT_MP3 && esp_mp3_dec_open(NULL, 0, &decoder) != ESP_AUDIO_ERR_OK) goto done;
    uint32_t remaining = info.data_bytes;
    uint64_t played_frames = 0;
    unsigned empty_frames = 0;
    error = ESP_OK;
    while (remaining > 0U) {
        if (!sink->ready(sink->context)) { error = ESP_ERR_INVALID_STATE; break; }
        size_t decoded_bytes;
        if (info.format == MUSIC_FORMAT_WAV) {
            const size_t wanted = remaining < MUSIC_PCM_BUFFER_BYTES ? remaining : MUSIC_PCM_BUFFER_BYTES;
            decoded_bytes = fread(pcm, 1, wanted, file);
            if (decoded_bytes != wanted) { error = ESP_FAIL; break; }
            remaining -= (uint32_t)decoded_bytes;
        } else {
            if (remaining < 4U || fread(input, 1, 4, file) != 4U) { error = ESP_ERR_INVALID_RESPONSE; break; }
            /* ID3v1 and trailing ID3v2 tags carry no playable samples. */
            if ((remaining == 128U && memcmp(input, "TAG", 3) == 0) ||
                memcmp(input, "ID3", 3) == 0) break;
            music_mp3_frame_t frame;
            if (!music_mp3_parse_header(input, &frame) || frame.frame_bytes > remaining ||
                frame.sample_rate != info.sample_rate || frame.channels != info.channels ||
                fread(input + 4, 1, frame.frame_bytes - 4U, file) != frame.frame_bytes - 4U) {
                error = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            remaining -= frame.frame_bytes;
            esp_audio_dec_in_raw_t raw = {.buffer = input, .len = frame.frame_bytes};
            esp_audio_dec_out_frame_t out = {.buffer = (uint8_t *)pcm, .len = MUSIC_PCM_BUFFER_BYTES};
            esp_audio_dec_info_t decoded = {0};
            const esp_audio_err_t result = esp_mp3_dec_decode(decoder, &raw, &out, &decoded);
            if (raw.consumed > raw.len || out.decoded_size > MUSIC_PCM_BUFFER_BYTES) {
                error = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (result == ESP_AUDIO_ERR_DATA_LACK || result == ESP_AUDIO_ERR_CONTINUE ||
                (result == ESP_AUDIO_ERR_OK && out.decoded_size == 0U)) {
                if (++empty_frames <= 8U) continue;
            }
            if (result != ESP_AUDIO_ERR_OK || decoded.bits_per_sample != 16U ||
                decoded.channel != info.channels || decoded.sample_rate != info.sample_rate ||
                out.decoded_size == 0U) { error = ESP_ERR_INVALID_RESPONSE; break; }
            empty_frames = 0;
            decoded_bytes = out.decoded_size;
        }
        if (decoded_bytes % (info.channels * sizeof(int16_t)) != 0U) {
            error = ESP_ERR_INVALID_SIZE;
            break;
        }
        const size_t frames = decoded_bytes / (info.channels * sizeof(int16_t));
        if (info.channels == 2U) {
            for (size_t i = 0; i < frames; ++i) pcm[i] = (int16_t)(((int32_t)pcm[i * 2U] + pcm[i * 2U + 1U]) / 2);
        }
        for (size_t offset = 0; offset < frames; ) {
            if (!sink->ready(sink->context)) { error = ESP_ERR_INVALID_STATE; break; }
            const size_t chunk = frames - offset > 480U ? 480U : frames - offset;
            error = sink->write(sink->context, info.sample_rate, pcm + offset, chunk);
            if (error != ESP_OK) break;
            offset += chunk;
            played_frames += chunk;
        }
        if (error != ESP_OK) break;
        if (sink->progress != NULL) sink->progress(sink->context, (uint32_t)(played_frames / info.sample_rate));
    }
    if (error == ESP_OK && played_frames == 0U) error = ESP_ERR_INVALID_RESPONSE;
done:
    if (decoder != NULL) (void)esp_mp3_dec_close(decoder);
    heap_caps_free(input);
    heap_caps_free(pcm);
    if (file != NULL) fclose(file);
    if (lease != NULL) {
        const esp_err_t unmount_error = sd_media_end_read(lease);
        if (error == ESP_OK) error = unmount_error;
    }
    return error;
}
