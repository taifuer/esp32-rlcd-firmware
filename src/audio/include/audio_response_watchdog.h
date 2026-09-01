#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RESPONSE_FIRST_PROGRESS_TIMEOUT_MS 20000U
#define AUDIO_RESPONSE_STALL_TIMEOUT_MS 15000U

typedef struct {
    uint32_t idle_ms;
    bool progress_started;
} audio_response_watchdog_t;

void audio_response_watchdog_init(audio_response_watchdog_t *watchdog);
void audio_response_watchdog_note_progress(
    audio_response_watchdog_t *watchdog);
bool audio_response_watchdog_tick(
    audio_response_watchdog_t *watchdog, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif
