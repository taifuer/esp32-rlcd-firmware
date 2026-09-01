#include "conversation_caption_sync.h"

#include "conversation_text_buffer.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

static size_t utf8_sequence_bytes(unsigned char lead)
{
    if (lead < 0x80U) {
        return 1U;
    }
    if (lead >= 0xc2U && lead <= 0xdfU) {
        return 2U;
    }
    if (lead >= 0xe0U && lead <= 0xefU) {
        return 3U;
    }
    if (lead >= 0xf0U && lead <= 0xf4U) {
        return 4U;
    }
    return 1U;
}

static uint32_t add_saturating(uint32_t value, size_t added)
{
    return added > UINT32_MAX - value ? UINT32_MAX
                                      : value + (uint32_t)added;
}

/* 24 kHz, 16-bit mono PCM is 48,000 bytes per second. These weights model
 * roughly 6.7 CJK glyphs or 18 Latin characters per second. They are only a
 * provisional pace; response.done later scales the same relative weights to
 * the exact received audio duration. */
static uint32_t speech_weight(const char *text, size_t sequence)
{
    if (sequence > 1U) {
        return 7200U;
    }
    const unsigned char value = (unsigned char)text[0];
    if (isspace(value) != 0) {
        return 900U;
    }
    if (isalnum(value) != 0) {
        return 2600U;
    }
    return 1800U;
}

static uint64_t total_weight(const char *text)
{
    uint64_t total = 0U;
    for (size_t offset = 0U; text[offset] != '\0';) {
        const size_t sequence = utf8_sequence_bytes(
            (unsigned char)text[offset]);
        total += speech_weight(text + offset, sequence);
        offset += sequence;
    }
    return total;
}

static const char *caption_source(const conversation_caption_sync_t *sync)
{
    return sync->stream_complete && sync->final_text[0] != '\0'
               ? sync->final_text
               : sync->streamed_text;
}

static size_t visible_prefix_bytes(
    const conversation_caption_sync_t *sync, const char *text)
{
    if (sync->played_audio_bytes == 0U || text[0] == '\0') {
        return 0U;
    }

    const uint64_t exact_total = sync->stream_complete
                                     ? total_weight(text)
                                     : 0U;
    uint64_t consumed_weight = 0U;
    size_t visible = 0U;
    for (size_t offset = 0U; text[offset] != '\0';) {
        const size_t sequence = utf8_sequence_bytes(
            (unsigned char)text[offset]);
        consumed_weight += speech_weight(text + offset, sequence);
        uint64_t target = consumed_weight;
        if (exact_total > 0U && sync->received_audio_bytes > 0U) {
            target = (uint64_t)sync->received_audio_bytes *
                     consumed_weight / exact_total;
        }
        if (target > sync->played_audio_bytes) {
            break;
        }
        visible = offset + sequence;
        offset += sequence;
    }
    return visible;
}

static size_t prefix_bytes_for_sequences(
    const char *text, size_t sequence_count)
{
    size_t offset = 0U;
    size_t count = 0U;
    while (text[offset] != '\0' && count < sequence_count) {
        offset += utf8_sequence_bytes((unsigned char)text[offset]);
        ++count;
    }
    return offset;
}

static size_t count_prefix_sequences(const char *text, size_t length)
{
    size_t offset = 0U;
    size_t count = 0U;
    while (offset < length) {
        offset += utf8_sequence_bytes((unsigned char)text[offset]);
        ++count;
    }
    return count;
}

static void copy_utf8_tail(char *destination, size_t capacity,
                           const char *source, size_t length)
{
    size_t offset = length > capacity - 1U
                        ? length - (capacity - 1U)
                        : 0U;
    while (offset < length &&
           (((unsigned char)source[offset] & 0xc0U) == 0x80U)) {
        ++offset;
    }
    const size_t retained = length - offset;
    memcpy(destination, source + offset, retained);
    destination[retained] = '\0';
}

void conversation_caption_sync_reset(conversation_caption_sync_t *sync)
{
    if (sync != NULL) {
        memset(sync, 0, sizeof(*sync));
    }
}

bool conversation_caption_sync_append(
    conversation_caption_sync_t *sync, const char *text)
{
    return sync != NULL && text != NULL &&
           conversation_text_append_tail(
               sync->streamed_text, sizeof(sync->streamed_text), text);
}

bool conversation_caption_sync_set_final(
    conversation_caption_sync_t *sync, const char *text)
{
    return sync != NULL && text != NULL &&
           conversation_text_copy_tail(
               sync->final_text, sizeof(sync->final_text), text);
}

void conversation_caption_sync_note_audio_received(
    conversation_caption_sync_t *sync, size_t bytes)
{
    if (sync != NULL) {
        sync->received_audio_bytes = add_saturating(
            sync->received_audio_bytes, bytes);
    }
}

void conversation_caption_sync_note_audio_played(
    conversation_caption_sync_t *sync, size_t bytes)
{
    if (sync != NULL) {
        sync->played_audio_bytes = add_saturating(
            sync->played_audio_bytes, bytes);
    }
}

void conversation_caption_sync_mark_complete(
    conversation_caption_sync_t *sync)
{
    if (sync != NULL) {
        sync->stream_complete = true;
    }
}

bool conversation_caption_sync_copy_visible(
    conversation_caption_sync_t *sync, char *destination,
    size_t capacity, bool force_complete)
{
    if (sync == NULL || destination == NULL || capacity == 0U) {
        return false;
    }
    const char *source = caption_source(sync);
    size_t length = force_complete
                        ? strlen(source)
                        : visible_prefix_bytes(sync, source);
    const size_t calculated_sequences =
        count_prefix_sequences(source, length);
    if (calculated_sequences < sync->revealed_sequences) {
        length = prefix_bytes_for_sequences(
            source, sync->revealed_sequences);
    } else {
        sync->revealed_sequences = calculated_sequences;
    }
    copy_utf8_tail(destination, capacity, source, length);
    return true;
}
