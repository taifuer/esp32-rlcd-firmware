#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copy or append text while retaining the newest complete UTF-8 code points
 * that fit in destination. Invalid UTF-8 leaves destination unchanged. */
bool conversation_text_copy_tail(char *destination, size_t capacity,
                                 const char *text);
bool conversation_text_append_tail(char *destination, size_t capacity,
                                   const char *text);

#ifdef __cplusplus
}
#endif
