#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "conversation_caption_sync.h"

static void test_completed_caption_follows_playback(void)
{
    conversation_caption_sync_t sync;
    char visible[64];

    conversation_caption_sync_reset(&sync);
    assert(conversation_caption_sync_append(&sync, "你好世界"));
    assert(conversation_caption_sync_set_final(&sync, "你好世界"));
    conversation_caption_sync_note_audio_received(&sync, 40000U);
    conversation_caption_sync_mark_complete(&sync);

    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "") == 0);

    conversation_caption_sync_note_audio_played(&sync, 10000U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "你") == 0);

    conversation_caption_sync_note_audio_played(&sync, 10000U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "你好") == 0);

    conversation_caption_sync_note_audio_played(&sync, 20000U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "你好世界") == 0);
}

static void test_streaming_text_uses_bounded_speech_pace(void)
{
    conversation_caption_sync_t sync;
    char visible[16];

    conversation_caption_sync_reset(&sync);
    assert(conversation_caption_sync_append(&sync, "AB"));
    conversation_caption_sync_note_audio_played(&sync, 2599U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "") == 0);

    conversation_caption_sync_note_audio_played(&sync, 1U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "A") == 0);

    conversation_caption_sync_note_audio_played(&sync, 2600U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "AB") == 0);
}

static void test_final_text_does_not_jump_ahead(void)
{
    conversation_caption_sync_t sync;
    char visible[32];

    conversation_caption_sync_reset(&sync);
    assert(conversation_caption_sync_append(&sync, "旧"));
    assert(conversation_caption_sync_set_final(&sync, "最终文字"));
    conversation_caption_sync_note_audio_received(&sync, 48000U);
    conversation_caption_sync_mark_complete(&sync);
    conversation_caption_sync_note_audio_played(&sync, 12000U);

    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "最") == 0);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), true));
    assert(strcmp(visible, "最终文字") == 0);
}

static void test_completion_never_moves_caption_backwards(void)
{
    conversation_caption_sync_t sync;
    char visible[32];

    conversation_caption_sync_reset(&sync);
    assert(conversation_caption_sync_append(&sync, "你好世界"));
    conversation_caption_sync_note_audio_played(&sync, 14400U);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "你好") == 0);

    assert(conversation_caption_sync_set_final(
        &sync, "你好世界，欢迎回来"));
    conversation_caption_sync_note_audio_received(&sync, 144000U);
    conversation_caption_sync_mark_complete(&sync);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "你好") == 0);
}

static void test_playback_progress_survives_producer_backpressure(void)
{
    conversation_caption_sync_t sync;
    char visible[32];

    conversation_caption_sync_reset(&sync);
    assert(conversation_caption_sync_set_final(&sync, "一二三"));
    conversation_caption_sync_note_audio_received(&sync, 10000U);
    conversation_caption_sync_note_audio_played(&sync, 10000U);
    /* The consumer can drain another chunk while the producer is blocked
     * before accounting for that chunk as received. */
    conversation_caption_sync_note_audio_played(&sync, 20000U);
    conversation_caption_sync_note_audio_received(&sync, 20000U);
    conversation_caption_sync_mark_complete(&sync);

    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), false));
    assert(strcmp(visible, "一二三") == 0);
}

static void test_small_output_keeps_valid_utf8_tail(void)
{
    conversation_caption_sync_t sync;
    char visible[7];

    conversation_caption_sync_reset(&sync);
    assert(conversation_caption_sync_set_final(&sync, "你好世界"));
    conversation_caption_sync_mark_complete(&sync);
    assert(conversation_caption_sync_copy_visible(
        &sync, visible, sizeof(visible), true));
    assert(strcmp(visible, "世界") == 0);
}

static void test_invalid_arguments(void)
{
    conversation_caption_sync_t sync;
    char visible[8];

    conversation_caption_sync_reset(&sync);
    assert(!conversation_caption_sync_append(NULL, "text"));
    assert(!conversation_caption_sync_append(&sync, NULL));
    assert(!conversation_caption_sync_set_final(NULL, "text"));
    assert(!conversation_caption_sync_copy_visible(
        NULL, visible, sizeof(visible), false));
    assert(!conversation_caption_sync_copy_visible(
        &sync, NULL, sizeof(visible), false));
    assert(!conversation_caption_sync_copy_visible(
        &sync, visible, 0U, false));
}

int main(void)
{
    test_completed_caption_follows_playback();
    test_streaming_text_uses_bounded_speech_pace();
    test_final_text_does_not_jump_ahead();
    test_completion_never_moves_caption_backwards();
    test_playback_progress_survives_producer_backpressure();
    test_small_output_keeps_valid_utf8_tail();
    test_invalid_arguments();
    puts("conversation caption sync tests passed");
    return 0;
}
