#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONVERSATION_CAPTION_SOURCE_CAPACITY 1025U

typedef struct {
    char streamed_text[CONVERSATION_CAPTION_SOURCE_CAPACITY];
    char final_text[CONVERSATION_CAPTION_SOURCE_CAPACITY];
    uint32_t received_audio_bytes;
    uint32_t played_audio_bytes;
    size_t revealed_sequences;
    bool stream_complete;
} conversation_caption_sync_t;

void conversation_caption_sync_reset(conversation_caption_sync_t *sync);
bool conversation_caption_sync_append(
    conversation_caption_sync_t *sync, const char *text);
bool conversation_caption_sync_set_final(
    conversation_caption_sync_t *sync, const char *text);
void conversation_caption_sync_note_audio_received(
    conversation_caption_sync_t *sync, size_t bytes);
void conversation_caption_sync_note_audio_played(
    conversation_caption_sync_t *sync, size_t bytes);
void conversation_caption_sync_mark_complete(
    conversation_caption_sync_t *sync);

/* Copies the subtitle that should be visible at the current local playback
 * position. Before response completion an estimated speech pace prevents
 * generated text from racing ahead of the speaker. Once the complete audio
 * length is known, the text is distributed over that exact duration. */
bool conversation_caption_sync_copy_visible(
    conversation_caption_sync_t *sync, char *destination,
    size_t capacity, bool force_complete);

#ifdef __cplusplus
}
#endif
