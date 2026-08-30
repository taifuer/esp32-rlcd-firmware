#include "voice_backend_policy.h"

app_voice_backend_t app_voice_backend_choose(
    bool cloud_configured, bool normal_power,
    bool station_connected, bool cloud_audio_ready, bool cloud_audio_busy,
    bool offline_engine_ready)
{
    if (cloud_audio_busy) {
        return APP_VOICE_BACKEND_UNAVAILABLE;
    }
    if (cloud_configured && normal_power && station_connected &&
        cloud_audio_ready) {
        return APP_VOICE_BACKEND_CLOUD;
    }
    if (offline_engine_ready) {
        return APP_VOICE_BACKEND_OFFLINE;
    }
    return APP_VOICE_BACKEND_UNAVAILABLE;
}
