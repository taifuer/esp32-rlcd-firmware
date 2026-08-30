#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_VOICE_BACKEND_UNAVAILABLE = 0,
    APP_VOICE_BACKEND_OFFLINE,
    APP_VOICE_BACKEND_CLOUD,
} app_voice_backend_t;

app_voice_backend_t app_voice_backend_choose(
    bool cloud_configured, bool normal_power,
    bool station_connected, bool cloud_audio_ready, bool cloud_audio_busy,
    bool offline_engine_ready);

#ifdef __cplusplus
}
#endif
