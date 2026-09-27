#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_music.h"
#include "music_stream.h"

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(value) ((TickType_t)(value))

/* Only platform and neighboring subsystem boundaries are replaced. All music
 * entry points, worker callbacks and run_music below are production source. */
static struct {
    struct { bool initialized, speaker_ready; int state; } status;
    struct { bool running; } voice_status, conversation_status;
    bool music_requested, music_running, music_storage_change;
    bool diagnostic_requested, voice_requested, conversation_requested;
    bool alert_requested, alert_running;
    uint32_t music_generation;
    audio_music_status_t music_status;
    audio_music_policy_t music_policy;
    void *worker_task, *voice_cpu_lock, *speaker_device;
} s_audio;
static uint8_t s_playback_volume = 68U;
static uint64_t now_ms;
static unsigned locks, opens, closes, writes, pm_locks, notifications;
static unsigned delays;
static bool speaker_open;
static uint8_t track_count;
static void (*delay_hook)(void);
static void (*close_hook)(void);
static const char *const track_names[] = {"first.wav", "second.wav", "third.wav"};

typedef enum {
    STREAM_EOF,
    STREAM_ERROR,
    STREAM_EXPIRE,
    STREAM_PAUSE_EXPIRE,
    STREAM_REPLACE_ERROR,
    STREAM_PREEMPT,
    STREAM_EXPIRE_BEFORE_WRITE,
} stream_action_t;
static stream_action_t stream_action;

static void lock_context(void) { assert(locks == 0U); ++locks; }
static void unlock_context(void) { assert(locks == 1U); --locks; }
static bool audio_session_state_is_active(int state) { return state != 0; }
static int64_t esp_timer_get_time(void) { return (int64_t)(now_ms * 1000U); }
static TickType_t xTaskGetTickCount(void) { return (TickType_t)now_ms; }
static void xTaskNotifyGive(TaskHandle_t task) { assert(task != NULL); ++notifications; }
static void vTaskDelay(TickType_t ticks)
{
    assert(locks == 0U && ++delays < 1000U);
    now_ms += ticks;
    if (delay_hook != NULL) {
        void (*hook)(void) = delay_hook;
        delay_hook = NULL;
        hook();
    }
}
static int esp_pm_lock_acquire(void *handle)
{ assert(handle != NULL && pm_locks == 0U); ++pm_locks; return ESP_OK; }
static int esp_pm_lock_release(void *handle)
{ assert(handle != NULL && pm_locks == 1U); --pm_locks; return ESP_OK; }
static int esp_codec_dev_set_out_mute(void *device, bool muted)
{ (void)muted; assert(device != NULL && speaker_open); return ESP_OK; }
static int esp_codec_dev_set_out_vol(void *device, uint8_t volume)
{ assert(device != NULL && speaker_open && volume <= 100U); return ESP_OK; }
static int esp_codec_dev_write(void *device, void *pcm, int bytes)
{ assert(device != NULL && pcm != NULL && bytes > 0 && speaker_open); ++writes; return ESP_OK; }
static esp_err_t codec_error(int error) { return error; }
static esp_err_t open_speaker_at_rate(uint32_t rate, uint8_t volume)
{ assert(!speaker_open && rate == 16000U && volume <= 100U); speaker_open = true; ++opens; return ESP_OK; }
static esp_err_t close_speaker(void)
{
    assert(speaker_open);
    speaker_open = false;
    ++closes;
    if (close_hook != NULL) {
        void (*hook)(void) = close_hook;
        close_hook = NULL;
        hook();
    }
    return ESP_OK;
}
void music_library_get_status(music_library_status_t *status)
{ *status = (music_library_status_t){.count = track_count, .scanned = true, .card_available = track_count != 0U}; }
bool music_library_track(size_t index, music_track_t *track)
{
    if (index >= track_count) return false;
    *track = (music_track_t){0};
    snprintf(track->filename, sizeof(track->filename), "%s", track_names[index]);
    return true;
}
bool music_library_find(const char *name, size_t *index)
{
    for (size_t i = 0; name != NULL && i < track_count; ++i) {
        if (strcmp(name, track_names[i]) == 0) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}

#include "audio_music_under_test.inc"

static void jump_to_deadline(void) { now_ms = s_audio.music_policy.sleep_deadline_ms; }
static void next_on_delay(void) { assert(audio_music_next() == ESP_OK); }
static void pause_on_delay(void)
{
    assert(audio_music_toggle() == ESP_OK);
    assert(s_audio.music_status.state == AUDIO_MUSIC_PAUSED);
}
static void pause_and_expire_on_delay(void)
{
    pause_on_delay();
    jump_to_deadline();
}
static void worker_stops_on_delay(void) { s_audio.music_running = false; }

esp_err_t music_stream_run(const music_track_t *track, const music_stream_sink_t *sink)
{
    assert(track != NULL && sink != NULL);
    if (!sink->ready(sink->context)) return ESP_ERR_INVALID_STATE;
    if (stream_action == STREAM_ERROR) return ESP_FAIL;
    if (stream_action == STREAM_EXPIRE_BEFORE_WRITE) jump_to_deadline();
    const int16_t pcm[] = {12, -34};
    const esp_err_t written = sink->write(sink->context, 16000U, pcm, 2U);
    if (written != ESP_OK) return written;
    sink->progress(sink->context, 7U);
    if (stream_action == STREAM_EXPIRE || stream_action == STREAM_PAUSE_EXPIRE) {
        if (stream_action == STREAM_PAUSE_EXPIRE) {
            assert(audio_music_toggle() == ESP_OK);
            delay_hook = jump_to_deadline;
        } else jump_to_deadline();
        assert(!sink->ready(sink->context));
        return ESP_ERR_INVALID_STATE;
    }
    if (stream_action == STREAM_REPLACE_ERROR) {
        assert(audio_music_select("second.wav", true) == ESP_OK);
        assert(audio_music_set_sleep_timer(30U) == ESP_OK);
        sink->progress(sink->context, 99U);
        assert(!sink->ready(sink->context));
        return ESP_FAIL;
    }
    if (stream_action == STREAM_PREEMPT) {
        lock_context();
        music_stop_locked();
        s_audio.alert_requested = true;
        unlock_context();
        assert(!sink->ready(sink->context));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static audio_music_status_t snapshot(void)
{ audio_music_status_t result; audio_music_get_status(&result); return result; }
static void reset(void)
{
    assert(locks == 0U && !speaker_open && pm_locks == 0U);
    memset(&s_audio, 0, sizeof(s_audio));
    s_audio.status.initialized = true;
    s_audio.status.speaker_ready = true;
    s_audio.worker_task = &s_audio;
    s_audio.speaker_device = &s_audio;
    s_audio.voice_cpu_lock = &s_audio;
    now_ms = 1000U;
    opens = closes = writes = notifications = delays = 0U;
    delay_hook = close_hook = NULL;
    track_count = 3U;
    stream_action = STREAM_EOF;
}
static void run_one_requested(void)
{
    assert(s_audio.music_requested && !s_audio.music_running);
    s_audio.music_requested = false;
    s_audio.music_running = true;
    run_music(s_audio.music_generation, s_audio.music_status.selected_index);
    assert(!s_audio.music_running && !speaker_open && pm_locks == 0U);
}

static void test_modes_and_errors(void)
{
    for (unsigned mode = 0; mode <= 2U; ++mode) {
        reset();
        assert(audio_music_set_playback_mode((audio_music_playback_mode_t)mode) == ESP_OK);
        assert(audio_music_select("third.wav", true) == ESP_OK);
        assert(audio_music_set_sleep_timer(15U) == ESP_OK);
        const uint64_t deadline = s_audio.music_policy.sleep_deadline_ms;
        run_one_requested();
        audio_music_status_t status = snapshot();
        assert(status.playback_mode == (audio_music_playback_mode_t)mode);
        if (mode == 0U) {
            assert(status.state == AUDIO_MUSIC_STOPPED && !status.busy);
            assert(status.sleep_remaining_seconds == 0U);
        } else {
            assert(status.state == AUDIO_MUSIC_PLAYING && status.busy);
            assert(status.selected_index == (mode == 1U ? 2U : 0U));
            assert(s_audio.music_policy.sleep_deadline_ms == deadline);
            stream_action = STREAM_ERROR;
            run_one_requested();
            status = snapshot();
            assert(status.state == AUDIO_MUSIC_ERROR && status.error == ESP_FAIL);
            assert(!status.busy && status.sleep_remaining_seconds == 0U);
        }
        assert(opens == closes);
    }
    reset();
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(15U) == ESP_OK);
    const uint32_t generation = s_audio.music_generation;
    assert(audio_music_set_playback_mode(AUDIO_MUSIC_MODE_REPEAT_ONE) == ESP_OK);
    assert(s_audio.music_generation == generation);
    run_one_requested();
    assert(snapshot().selected_index == 0U);
    assert(audio_music_next() == ESP_OK && snapshot().selected_index == 1U);
    assert(snapshot().sleep_remaining_seconds > 0U);
    assert(audio_music_set_playback_mode((audio_music_playback_mode_t)3) == ESP_ERR_INVALID_ARG);
    audio_music_stop();
    assert(snapshot().sleep_remaining_seconds == 0U && !snapshot().busy);
}

static void test_pause_and_timers(void)
{
    reset();
    assert(audio_music_set_sleep_timer(15U) == ESP_ERR_INVALID_STATE);
    assert(audio_music_set_sleep_timer(0U) == ESP_OK);
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(90U) == ESP_OK);
    assert(snapshot().sleep_remaining_seconds == 5400U);
    ++now_ms;
    assert(snapshot().sleep_remaining_seconds == 5400U);
    now_ms += 999U;
    assert(snapshot().sleep_remaining_seconds == 5399U);
    const uint64_t deadline = s_audio.music_policy.sleep_deadline_ms;
    assert(audio_music_toggle() == ESP_OK && snapshot().state == AUDIO_MUSIC_PAUSED);
    assert(audio_music_next() == ESP_OK);
    assert(snapshot().state == AUDIO_MUSIC_PAUSED && snapshot().selected_index == 1U);
    assert(s_audio.music_policy.sleep_deadline_ms == deadline);
    assert(audio_music_set_sleep_timer(3U) == ESP_ERR_INVALID_ARG);
    assert(s_audio.music_policy.sleep_deadline_ms == deadline);
    delay_hook = jump_to_deadline;
    run_one_requested();
    assert(opens == 0U && writes == 0U);
    assert(snapshot().state == AUDIO_MUSIC_STOPPED && snapshot().sleep_remaining_seconds == 0U);
    assert(!snapshot().busy);

    const stream_action_t actions[] = {STREAM_EXPIRE, STREAM_PAUSE_EXPIRE,
                                       STREAM_EXPIRE_BEFORE_WRITE};
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        reset();
        assert(audio_music_toggle() == ESP_OK);
        assert(audio_music_set_sleep_timer(15U) == ESP_OK);
        stream_action = actions[i];
        run_one_requested();
        assert(snapshot().state == AUDIO_MUSIC_STOPPED && !snapshot().busy);
        assert(snapshot().sleep_remaining_seconds == 0U && opens == closes);
        assert(snapshot().error == ESP_OK); /* cancellation is not a decode error */
    }
    reset();
    assert(audio_music_set_playback_mode(AUDIO_MUSIC_MODE_REPEAT_ALL) == ESP_OK);
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(15U) == ESP_OK);
    close_hook = jump_to_deadline;
    run_one_requested();
    assert(snapshot().state == AUDIO_MUSIC_STOPPED && !snapshot().busy);
    assert(snapshot().sleep_remaining_seconds == 0U);
}

static void test_generation_and_preemption(void)
{
    reset();
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(15U) == ESP_OK);
    stream_action = STREAM_REPLACE_ERROR;
    run_one_requested();
    assert(snapshot().state == AUDIO_MUSIC_PLAYING && snapshot().busy);
    assert(snapshot().selected_index == 1U && snapshot().elapsed_seconds == 0U);
    assert(snapshot().error == ESP_OK && snapshot().sleep_remaining_seconds == 1800U);
    audio_music_stop();

    reset();
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(15U) == ESP_OK);
    const uint64_t deadline = s_audio.music_policy.sleep_deadline_ms;
    delay_hook = next_on_delay;
    run_one_requested();
    assert(snapshot().state == AUDIO_MUSIC_PLAYING && snapshot().selected_index == 1U);
    assert(s_audio.music_policy.sleep_deadline_ms == deadline);
    audio_music_stop();

    reset();
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(15U) == ESP_OK);
    stream_action = STREAM_PREEMPT;
    run_one_requested();
    assert(snapshot().state == AUDIO_MUSIC_STOPPED && !snapshot().busy);
    assert(snapshot().sleep_remaining_seconds == 0U && s_audio.alert_requested);
    assert(audio_music_toggle() == ESP_ERR_INVALID_STATE);
    s_audio.alert_requested = false;
    assert(!snapshot().busy); /* no auto resume after the alarm */
}

static void test_pause_during_eof_drain(void)
{
    const struct {
        audio_music_playback_mode_t mode;
        size_t index;
        size_t next;
        bool continues;
    } scenarios[] = {
        {AUDIO_MUSIC_MODE_SEQUENTIAL, 0U, 1U, true},
        {AUDIO_MUSIC_MODE_SEQUENTIAL, 2U, 2U, false},
        {AUDIO_MUSIC_MODE_REPEAT_ONE, 1U, 1U, true},
        {AUDIO_MUSIC_MODE_REPEAT_ALL, 2U, 0U, true},
    };
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        reset();
        assert(audio_music_set_playback_mode(scenarios[i].mode) == ESP_OK);
        assert(audio_music_select(track_names[scenarios[i].index], true) == ESP_OK);
        assert(audio_music_set_sleep_timer(15U) == ESP_OK);
        const uint64_t deadline = s_audio.music_policy.sleep_deadline_ms;
        delay_hook = pause_on_delay;
        run_one_requested();
        assert(opens == 1U && closes == 1U && writes == 1U);
        assert(snapshot().selected_index == scenarios[i].next);
        if (scenarios[i].continues) {
            assert(snapshot().state == AUDIO_MUSIC_PAUSED && snapshot().busy);
            assert(s_audio.music_policy.sleep_deadline_ms == deadline);
            /* The queued next/repeated track waits before opening a file or
             * speaker, and its countdown can expire without any UI polling. */
            delay_hook = jump_to_deadline;
            run_one_requested();
            assert(opens == 1U && closes == 1U && writes == 1U);
        }
        assert(snapshot().state == AUDIO_MUSIC_STOPPED && !snapshot().busy);
        assert(snapshot().sleep_remaining_seconds == 0U);
        assert(snapshot().error == ESP_OK);

        reset();
        assert(audio_music_set_playback_mode(scenarios[i].mode) == ESP_OK);
        assert(audio_music_select(track_names[scenarios[i].index], true) == ESP_OK);
        assert(audio_music_set_sleep_timer(15U) == ESP_OK);
        delay_hook = pause_and_expire_on_delay;
        run_one_requested();
        assert(snapshot().state == AUDIO_MUSIC_STOPPED && !snapshot().busy);
        assert(snapshot().selected_index == scenarios[i].index);
        assert(snapshot().sleep_remaining_seconds == 0U);
        assert(snapshot().error == ESP_OK && opens == closes);
    }
}

static void test_storage_and_boot(void)
{
    reset();
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(15U) == ESP_OK);
    assert(audio_music_begin_storage_change(100U) == ESP_OK);
    assert(snapshot().sleep_remaining_seconds == 0U && !snapshot().busy);
    assert(audio_music_toggle() == ESP_ERR_INVALID_STATE);
    assert(audio_music_next() == ESP_ERR_INVALID_STATE);
    assert(audio_music_set_sleep_timer(15U) == ESP_ERR_INVALID_STATE);
    assert(audio_music_select("second.wav", false) == ESP_OK);
    audio_music_end_storage_change();
    assert(audio_music_toggle() == ESP_OK);
    assert(audio_music_set_sleep_timer(30U) == ESP_OK);
    /* Actual stop-and-wait must retain ownership until the worker exits. */
    s_audio.music_requested = false;
    s_audio.music_running = true;
    delay_hook = worker_stops_on_delay;
    assert(audio_music_stop_and_wait(100U) == ESP_OK);
    assert(snapshot().sleep_remaining_seconds == 0U && !snapshot().busy);
    reset();
    assert(snapshot().playback_mode == AUDIO_MUSIC_MODE_SEQUENTIAL);
    assert(snapshot().sleep_remaining_seconds == 0U && !snapshot().busy);
    track_count = 0U;
    assert(audio_music_toggle() == ESP_ERR_NOT_FOUND);
    assert(audio_music_next() == ESP_ERR_NOT_FOUND);
    assert(audio_music_set_sleep_timer(15U) == ESP_ERR_INVALID_STATE);
}

int main(void)
{
    test_modes_and_errors();
    test_pause_and_timers();
    test_generation_and_preemption();
    test_pause_during_eof_drain();
    test_storage_and_boot();
    puts("audio music runtime tests passed (production controls, stubbed platform/stream)");
    return 0;
}
