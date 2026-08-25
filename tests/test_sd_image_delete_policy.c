#include <assert.h>
#include <stdio.h>

#include "image_delete_policy.h"

int main(void)
{
    size_t next = 99U;

    assert(sd_image_delete_next_selected(1U, 0U, 0U, &next));
    assert(next == 0U);

    assert(sd_image_delete_next_selected(4U, 1U, 1U, &next));
    assert(next == 1U); /* The old successor moves into the deleted slot. */
    assert(sd_image_delete_next_selected(4U, 3U, 3U, &next));
    assert(next == 0U); /* Deleting the selected tail wraps to the first. */

    assert(sd_image_delete_next_selected(4U, 2U, 0U, &next));
    assert(next == 1U); /* The same selected image shifts left. */
    assert(sd_image_delete_next_selected(4U, 1U, 3U, &next));
    assert(next == 1U); /* Deleting after selection does not move it. */

    assert(!sd_image_delete_next_selected(0U, 0U, 0U, &next));
    assert(!sd_image_delete_next_selected(2U, 2U, 0U, &next));
    assert(!sd_image_delete_next_selected(2U, 0U, 2U, &next));
    assert(!sd_image_delete_next_selected(2U, 0U, 1U, NULL));

    puts("SD image delete policy tests passed");
    return 0;
}
