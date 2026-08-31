#include "conversation_text_buffer.h"

#include <stdint.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool continuation(uint8_t value)
{
    return (value & 0xc0U) == 0x80U;
}

static bool utf8_valid(const char *text, size_t length)
{
    size_t offset = 0U;
    while (offset < length) {
        const uint8_t lead = (uint8_t)text[offset];
        size_t sequence = 0U;
        if (lead < 0x80U) {
            sequence = 1U;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            sequence = 2U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            sequence = 3U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            sequence = 4U;
        } else {
            return false;
        }
        if (sequence > length - offset) {
            return false;
        }
        for (size_t index = 1U; index < sequence; ++index) {
            if (!continuation((uint8_t)text[offset + index])) {
                return false;
            }
        }
        if ((lead == 0xe0U &&
             (uint8_t)text[offset + 1U] < 0xa0U) ||
            (lead == 0xedU &&
             (uint8_t)text[offset + 1U] > 0x9fU) ||
            (lead == 0xf0U &&
             (uint8_t)text[offset + 1U] < 0x90U) ||
            (lead == 0xf4U &&
             (uint8_t)text[offset + 1U] > 0x8fU)) {
            return false;
        }
        offset += sequence;
    }
    return true;
}

static size_t tail_offset(const char *text, size_t length,
                          size_t maximum_bytes)
{
    if (length <= maximum_bytes) {
        return 0U;
    }
    size_t offset = length - maximum_bytes;
    while (offset < length && continuation((uint8_t)text[offset])) {
        ++offset;
    }
    return offset;
}

bool conversation_text_copy_tail(char *destination, size_t capacity,
                                 const char *text)
{
    if (destination == NULL || capacity == 0U || text == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    if (!utf8_valid(text, length)) {
        return false;
    }
    const size_t offset = tail_offset(text, length, capacity - 1U);
    const size_t retained = length - offset;
    memmove(destination, text + offset, retained);
    destination[retained] = '\0';
    return true;
}

bool conversation_text_append_tail(char *destination, size_t capacity,
                                   const char *text)
{
    if (destination == NULL || capacity == 0U || text == NULL) {
        return false;
    }
    const size_t used = bounded_length(destination, capacity);
    const size_t added = strlen(text);
    if (used >= capacity || !utf8_valid(destination, used) ||
        !utf8_valid(text, added)) {
        return false;
    }
    if (added >= capacity) {
        return conversation_text_copy_tail(destination, capacity, text);
    }
    if (used + added >= capacity) {
        size_t dropped = used + added - (capacity - 1U);
        while (dropped < used &&
               continuation((uint8_t)destination[dropped])) {
            ++dropped;
        }
        memmove(destination, destination + dropped,
                used - dropped + 1U);
    }
    const size_t retained = strlen(destination);
    memcpy(destination + retained, text, added + 1U);
    return true;
}
