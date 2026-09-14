#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Keep steady-state navigation copy in one host-testable place.  The display
 * normally uses u8g2_font_6x13_tf for these ASCII footers, so 62 characters
 * remain inside the 12-pixel safe margins on a 400-pixel panel. The shorter
 * settings footer uses Helvetica bold 10, measured by real-font layout tests.
 */
#define DISPLAY_INTERACTION_FOOTER_MAX_CHARS 62U

static inline const char *display_interaction_weather_footer(void)
{
    return "BOOT: CALENDAR | KEY: SETTINGS | HOLD KEY 2s: REFRESH";
}

static inline const char *display_interaction_calendar_footer(
    bool image_available, bool music_available)
{
    return image_available ? "BOOT: IMAGE | KEY: SETTINGS"
         : music_available ? "BOOT: MUSIC | KEY: SETTINGS"
                           : "BOOT: CHAT | KEY: SETTINGS";
}

static inline bool display_interaction_format_image_navigation(
    char *buffer, size_t capacity, size_t selected_index, size_t image_count, bool music_available)
{
    if (buffer == NULL || capacity == 0U) {
        return false;
    }

    int written;
    if (image_count > 1U) {
        if (selected_index >= image_count) {
            selected_index = 0U;
        }
        written = snprintf(buffer, capacity,
                           "BOOT: %s | KEY: NEXT IMAGE | %u/%u",
                           music_available ? "MUSIC" : "CHAT",
                           (unsigned)(selected_index + 1U),
                           (unsigned)image_count);
    } else {
        written = snprintf(buffer, capacity,
                           "BOOT: %s | KEY: SETTINGS", music_available ? "MUSIC" : "CHAT");
    }

    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}

static inline const char *display_interaction_image_action_footer(void)
{
    return "HOLD KEY 2s: DELETE IMAGE";
}

static inline const char *display_interaction_status_footer(void)
{
    return "BOOT: HOME | KEY: SETTINGS | HOLD KEY 2s: SYNC TIME";
}

static inline const char *display_interaction_chat_footer(void)
{
    return "BOOT: HOME | KEY: SETTINGS";
}

static inline const char *display_interaction_chat_next_turn_footer(void)
{
    return "KEY: NEXT TURN | BOOT: CANCEL";
}

static inline const char *display_interaction_settings_navigation_footer(void)
{
    return "BOOT: HOME | KEY: ONLINE UPDATE";
}

static inline const char *display_interaction_settings_action_footer(
    bool manual_saving_requested)
{
    return manual_saving_requested
               ? "HOLD BOOT 2s: MANUAL SAVING OFF"
               : "HOLD BOOT 2s: MANUAL SAVING ON";
}

static inline const char *display_interaction_volume_navigation(void)
{
    return "BOOT: CANCEL | KEY: CHANGE VALUE";
}

static inline const char *display_interaction_volume_action(void)
{
    return "HOLD KEY 2s: SAVE";
}

static inline const char *display_interaction_online_update_footer(
    bool review_available)
{
    return review_available
               ? "BOOT: HOME | KEY: STATUS | HOLD KEY 2s: REVIEW UPDATE"
               : "BOOT: HOME | KEY: STATUS | HOLD KEY 2s: CHECK UPDATE";
}

static inline const char *display_interaction_recovery_settings_footer(void)
{
    return "KEY: ONLINE UPDATE | HOLD KEY 3s: WEB SETTINGS";
}

static inline const char *display_interaction_recovery_update_footer(
    bool review_available)
{
    return review_available
               ? "KEY: SETTINGS | HOLD KEY 2s: REVIEW UPDATE"
               : "KEY: SETTINGS | HOLD KEY 2s: CHECK UPDATE";
}

static inline const char *display_interaction_recovery_restart_hint(void)
{
    return "PWR OFF / ON";
}

#ifdef __cplusplus
}
#endif
