#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_backup.h"

static void reject_replacement(const char *json, const char *needle, const char *replacement,
                               const app_settings_t *base)
{
    char changed[APP_SETTINGS_BACKUP_MAX_LENGTH + 1U];
    const char *at = strstr(json, needle);
    assert(at != NULL);
    const size_t prefix = (size_t)(at - json);
    assert(prefix + strlen(replacement) + strlen(at + strlen(needle)) < sizeof(changed));
    memcpy(changed, json, prefix);
    snprintf(changed + prefix, sizeof(changed) - prefix, "%s%s", replacement, at + strlen(needle));
    app_settings_t parsed = *base;
    assert(!settings_backup_parse(changed, strlen(changed), base, &parsed));
    assert(memcmp(&parsed, base, sizeof(parsed)) == 0);
}

int main(void)
{
    app_settings_t saved, base, parsed;
    app_settings_defaults(&saved);
    saved.utc_offset_minutes = -345;
    saved.temperature_unit = APP_TEMPERATURE_UNIT_FAHRENHEIT;
    saved.audio_playback_volume = 0U;
    saved.alarm_enabled = true;
    saved.alarm_volume = 100U;
    saved.timer_minutes = 180U;
    saved.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    const uint8_t reverse[] = {128U, 64U, 32U, 16U, 8U, 4U, 2U, 1U};
    memcpy(saved.daily_pages_order, reverse, sizeof(reverse));
    saved.daily_pages_mask = 0U;
    char json[APP_SETTINGS_BACKUP_MAX_LENGTH + 1U];
    size_t length = 0U;
    assert(settings_backup_encode(&saved, json, sizeof(json), &length));
    assert(length == strlen(json) && length < 1024U);
    assert(strstr(json, "manual_saving") == NULL);
    assert(strstr(json, "update_channel") == NULL);
    assert(strstr(json, "environment_history") == NULL);
    assert(strstr(json, "environment_archive") == NULL);
    assert(strstr(json, "key") == NULL && strstr(json, "host") == NULL);
    app_settings_defaults(&base);
    base.manual_saving_requested = true;
    base.update_channel = APP_UPDATE_CHANNEL_BETA;
    base.environment_history_save_enabled = true;
    base.environment_archive_save_enabled = true;
    assert(settings_backup_parse(json, length, &base, &parsed));
    assert(parsed.manual_saving_requested && parsed.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(parsed.environment_history_save_enabled);
    assert(parsed.environment_archive_save_enabled);
    parsed.manual_saving_requested = saved.manual_saving_requested;
    parsed.update_channel = saved.update_channel;
    parsed.environment_history_save_enabled = saved.environment_history_save_enabled;
    parsed.environment_archive_save_enabled = saved.environment_archive_save_enabled;
    assert(memcmp(&parsed, &saved, sizeof(saved)) == 0);

    const char *const invalid[][2] = {
        {"\"version\":1", "\"version\":2"},
        {"\"version\":1", "\"version\":1.0"},
        {"\"version\":1", "\"version\":01"},
        {"\"version\":1", "\"version\":1e0"},
        {"\"version\":1", "\"version\":true"},
        {"\"version\":1", "\"version\":1,\"version\":1"},
        {"\"version\":1,", ""},
        {"esp32-rlcd-preferences", "another-device"},
        {"\"preferences\":{", "\"preferences\":{\"api_key\":\"secret\","},
        {"\"preferences\":{", "\"preferences\":{\"update_channel\":\"beta\","},
        {"\"preferences\":{", "\"preferences\":{\"manual_saving_requested\":true,"},
        {"\"preferences\":{", "\"preferences\":{\"environment_history_save_enabled\":true,"},
        {"\"preferences\":{", "\"preferences\":{\"environment_archive_save_enabled\":true,"},
        {"\"utc_offset_minutes\":-345", "\"utc_offset_minutes\":-344"},
        {"\"utc_offset_minutes\":-345", "\"utc_offset_minutes\":-721"},
        {"\"utc_offset_minutes\":-345", "\"utc_offset_minutes\":99999999999999"},
        {"\"temperature_unit\":\"f\"", "\"temperature_unit\":\"x\""},
        {"\"audio_playback_volume\":0", "\"audio_playback_volume\":101"},
        {"\"audio_playback_volume\":0", "\"audio_playback_volume\":-1"},
        {"\"alarm_volume\":100", "\"alarm_volume\":0.5"},
        {"\"alarm_enabled\":true", "\"alarm_enabled\":1"},
        {"\"alarm_enabled\":true", "\"alarm_enabled\":falsee"},
        {"\"alarm_hour\":7", "\"alarm_hour\":24"},
        {"\"alarm_minute\":30", "\"alarm_minute\":60"},
        {"\"alarm_weekdays\":62", "\"alarm_weekdays\":0"},
        {"\"alarm_weekdays\":62", "\"alarm_weekdays\":128"},
        {"\"daily_pages_mask\":0", "\"daily_pages_mask\":256"},
        {"[128,64,32,16,8,4,2,1]", "[128,64,32,16,8,4,2]"},
        {"[128,64,32,16,8,4,2,1]", "[128,64,32,16,8,4,2,2]"},
        {"[128,64,32,16,8,4,2,1]", "[128,64,32,16,8,4,2,1,1]"},
        {"[128,64,32,16,8,4,2,1]", "[128,64,32,16,8,4,2,3]"},
        {"\"music_mode\":\"repeat_all\"", "\"music_mode\":\"shuffle\""},
        {"\"timer_minutes\":180", "\"timer_minutes\":0"},
        {"\"timer_minutes\":180", "\"timer_minutes\":181"},
        {"\"timer_minutes\":180", "\"alarm_hour\":7"},
        {"\"timer_minutes\":180", "\"timer\\u005fminutes\":180"},
        {"\"timer_minutes\":180", "\"timer_minutes\":180,"},
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reject_replacement(json, invalid[i][0], invalid[i][1], &base);
    }
    for (size_t cut = 0U; cut + 1U < length; ++cut) {
        parsed = base;
        assert(!settings_backup_parse(json, cut, &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
    char oversized[APP_SETTINGS_BACKUP_MAX_LENGTH + 2U];
    memset(oversized, ' ', sizeof(oversized));
    memcpy(oversized, json, length);
    assert(!settings_backup_parse(oversized, sizeof(oversized), &base, &parsed));
    assert(settings_backup_parse(oversized, APP_SETTINGS_BACKUP_MAX_LENGTH, &base, &parsed));
    json[length] = 'x';
    assert(!settings_backup_parse(json, length + 1U, &base, &parsed));
    json[10] = '\0';
    assert(!settings_backup_parse(json, length, &base, &parsed));
    assert(!settings_backup_encode(&saved, json, 8U, &length) && json[0] == '\0');
    assert(!settings_backup_encode(NULL, json, sizeof(json), &length));
    assert(!settings_backup_parse(NULL, length, &base, &parsed));
    puts("settings backup strict JSON, safe merge, bounds and round trip tests passed");
}
