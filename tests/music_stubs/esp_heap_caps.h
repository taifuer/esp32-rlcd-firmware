#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t bytes, unsigned caps);
void *heap_caps_calloc(size_t count, size_t bytes, unsigned caps);
void heap_caps_free(void *pointer);
