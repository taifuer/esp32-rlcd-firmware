#include "image_delete_policy.h"

bool sd_image_delete_next_selected(size_t image_count,
                                   size_t selected_index,
                                   size_t deleted_index,
                                   size_t *next_selected_index)
{
    if (image_count == 0U || selected_index >= image_count ||
        deleted_index >= image_count || next_selected_index == NULL) {
        return false;
    }

    const size_t remaining_count = image_count - 1U;
    if (remaining_count == 0U) {
        *next_selected_index = 0U;
    } else if (deleted_index < selected_index) {
        *next_selected_index = selected_index - 1U;
    } else if (deleted_index > selected_index) {
        *next_selected_index = selected_index;
    } else {
        *next_selected_index = deleted_index < remaining_count
                                   ? deleted_index
                                   : 0U;
    }
    return true;
}
