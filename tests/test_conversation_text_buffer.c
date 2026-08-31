#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "conversation_text_buffer.h"

static void test_copy_keeps_latest_tail(void)
{
    char output[5];
    assert(conversation_text_copy_tail(output, sizeof(output),
                                       "abcdef"));
    assert(strcmp(output, "cdef") == 0);

    char chinese[8];
    assert(conversation_text_copy_tail(chinese, sizeof(chinese),
                                       "甲乙丙"));
    assert(strcmp(chinese, "乙丙") == 0);

    char too_small[3];
    assert(conversation_text_copy_tail(too_small, sizeof(too_small),
                                       "甲"));
    assert(too_small[0] == '\0');
}

static void test_append_rolls_at_code_point_boundary(void)
{
    char output[8] = "甲乙";
    assert(conversation_text_append_tail(output, sizeof(output), "丙"));
    assert(strcmp(output, "乙丙") == 0);

    char ascii[7] = "abcd";
    assert(conversation_text_append_tail(ascii, sizeof(ascii), "efgh"));
    assert(strcmp(ascii, "cdefgh") == 0);
}

static void test_long_stream_keeps_most_recent_characters(void)
{
    char output[513] = {0};
    for (size_t index = 0U; index < 200U; ++index) {
        assert(conversation_text_append_tail(output, sizeof(output),
                                             "你"));
    }
    assert(strlen(output) == 510U);
    assert(strlen(output) % strlen("你") == 0U);
    assert(strncmp(output, "你你", strlen("你你")) == 0);
}

static void test_invalid_input_does_not_replace_text(void)
{
    char output[16] = "keep";
    static const char invalid[] = "\xf0\x9f\x98";
    assert(!conversation_text_copy_tail(output, sizeof(output), invalid));
    assert(strcmp(output, "keep") == 0);
    assert(!conversation_text_append_tail(output, sizeof(output), invalid));
    assert(strcmp(output, "keep") == 0);

    assert(!conversation_text_copy_tail(NULL, sizeof(output), "text"));
    assert(!conversation_text_copy_tail(output, 0U, "text"));
    assert(!conversation_text_append_tail(output, sizeof(output), NULL));
}

int main(void)
{
    test_copy_keeps_latest_tail();
    test_append_rolls_at_code_point_boundary();
    test_long_stream_keeps_most_recent_characters();
    test_invalid_input_does_not_replace_text();
    puts("conversation text buffer tests passed");
    return 0;
}
