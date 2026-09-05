#pragma once

#include "esp_err.h"
#include "music_library.h"

typedef enum { AUDIO_MUSIC_STOPPED, AUDIO_MUSIC_PLAYING, AUDIO_MUSIC_PAUSED, AUDIO_MUSIC_ERROR } audio_music_state_t;
typedef struct {
    audio_music_state_t state;
    uint32_t revision;
    uint32_t elapsed_seconds;
    uint8_t selected_index;
    uint8_t volume;
    esp_err_t error;
    bool busy;
} audio_music_status_t;

void audio_music_get_status(audio_music_status_t *status);
esp_err_t audio_music_toggle(void);
esp_err_t audio_music_next(void);
void audio_music_stop(void);
/* Bounded maintenance handoff. Never force-close another task's SD mount. */
esp_err_t audio_music_stop_and_wait(uint32_t timeout_ms);
