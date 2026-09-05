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
/* Prevent new playback while a portal file transaction owns the storage.
 * begin stops and waits; on failure it releases its own reservation. */
esp_err_t audio_music_begin_storage_change(uint32_t timeout_ms);
void audio_music_end_storage_change(void);
/* Select by stable filename, not a stale browser index. play=false is also
 * used after catalog edits; a missing old selection falls back to index zero. */
esp_err_t audio_music_select(const char *filename, bool play);
