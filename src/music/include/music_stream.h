#pragma once

#include "music_library.h"

typedef struct {
    /* Invoked before reads/writes; may wait while paused. False cancels. */
    bool (*ready)(void *context);
    esp_err_t (*write)(void *context, uint32_t rate, const int16_t *mono, size_t frames);
    void (*progress)(void *context, uint32_t elapsed_seconds);
    void *context;
} music_stream_sink_t;

/* Runs on the sole audio worker. All file/decoder/buffer leases are released
 * before return, including cancellation, corrupt input and missing card. */
esp_err_t music_stream_run(const music_track_t *track, const music_stream_sink_t *sink);
