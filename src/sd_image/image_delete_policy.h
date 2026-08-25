#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve the live selection after removing one entry from an ordered image
 * catalog. Removing the selected entry advances to its successor and wraps to
 * the first entry when the removed image was last.
 */
bool sd_image_delete_next_selected(size_t image_count,
                                   size_t selected_index,
                                   size_t deleted_index,
                                   size_t *next_selected_index);

#ifdef __cplusplus
}
#endif
