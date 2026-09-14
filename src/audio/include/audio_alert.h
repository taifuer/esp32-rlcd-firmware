#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Plays the device alert through the audio component's single ES8311 owner.
 * A pending alert safely cancels an active diagnostic session before it opens
 * the speaker. The alert also has an internal safety timeout.
 */
esp_err_t audio_alert_start(uint8_t volume_percent);
/* Explicit, bounded speaker-only preview; never changes saved volume.
 * A real alarm may preempt it. */
esp_err_t audio_alert_preview(uint8_t volume_percent);
void audio_alert_cancel_preview(void);
esp_err_t audio_alert_stop(void);
bool audio_alert_is_active(void);

#ifdef __cplusplus
}
#endif
