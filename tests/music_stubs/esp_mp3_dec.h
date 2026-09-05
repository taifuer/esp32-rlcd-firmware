#pragma once
/* Project-owned decoder stand-in: tests lifecycle and bounds, not MP3 DSP. */
#include <stdint.h>
typedef int esp_audio_err_t;
enum { ESP_AUDIO_ERR_OK, ESP_AUDIO_ERR_DATA_LACK, ESP_AUDIO_ERR_CONTINUE, ESP_AUDIO_ERR_FAIL };
typedef struct { uint8_t *buffer; uint32_t len, consumed; } esp_audio_dec_in_raw_t;
typedef struct { uint8_t *buffer; uint32_t len, needed_size, decoded_size; } esp_audio_dec_out_frame_t;
typedef struct { uint32_t sample_rate; uint8_t bits_per_sample, channel; } esp_audio_dec_info_t;
esp_audio_err_t esp_mp3_dec_open(void *config, uint32_t size, void **decoder);
esp_audio_err_t esp_mp3_dec_decode(void *decoder, esp_audio_dec_in_raw_t *raw,
                                  esp_audio_dec_out_frame_t *frame, esp_audio_dec_info_t *info);
esp_audio_err_t esp_mp3_dec_close(void *decoder);
