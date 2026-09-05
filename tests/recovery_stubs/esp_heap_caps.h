#pragma once

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U

size_t heap_caps_get_free_size(uint32_t capabilities);
size_t heap_caps_get_minimum_free_size(uint32_t capabilities);
