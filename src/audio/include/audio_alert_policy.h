#pragma once

#include <stdbool.h>

/* Real alarms may replace a queued preview or interrupt a running one.
 * Preview requests never interrupt real alarms, speech or card writes. */
static inline bool audio_alert_request_allowed(
    bool preview, bool pending, bool pending_preview, bool running,
    bool running_preview, bool other_audio_busy)
{
    return !(pending && (preview || !pending_preview)) &&
        !(running && (preview || !running_preview)) &&
        !(preview && other_audio_busy);
}
