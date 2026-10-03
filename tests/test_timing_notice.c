#include <assert.h>
#include <stdio.h>
#include "timing_notice.h"

int main(void)
{
    timing_notice_t notice = {0};
    timing_notice_offer(&notice, 100U);
    assert(!timing_notice_update(&notice, 100U, true, false, false).started);
    assert(notice.waiting && !notice.visible);
    timing_notice_result_t r = timing_notice_update(&notice, 200U, false, true, true);
    assert(r.started && r.consume_input && notice.visible);
    r = timing_notice_update(&notice, 210U, false, false, true);
    assert(!r.stopped && r.consume_input); /* old press release is swallowed */
    r = timing_notice_update(&notice, 220U, false, false, true);
    assert(r.stopped && r.consume_input && !notice.visible);
    assert(!timing_notice_update(&notice, 230U, false, false, true).consume_input);
    timing_notice_offer(&notice, 300U);
    assert(timing_notice_update(&notice, 300U, false, false, true).started);
    assert(timing_notice_update(&notice, 301U, true, false, false).stopped);
    assert(!timing_notice_update(&notice, 302U, false, false, false).started);
    timing_notice_offer(&notice, 1000U);
    assert(!timing_notice_update(&notice, 61000U, false, false, false).started);
    assert(!notice.waiting);
    timing_notice_offer(&notice, UINT64_MAX - 20000U);
    assert(timing_notice_update(&notice, UINT64_MAX - 20000U, false, false, false).started);
    assert(!timing_notice_update(&notice, 0U, false, false, false).stopped);
    assert(timing_notice_update(&notice, UINT64_MAX - 10000U, false, true, false).stopped);
    puts("timing notice tests passed");
}
