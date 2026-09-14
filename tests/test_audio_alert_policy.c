#include <assert.h>
#include <stdio.h>
#include "audio_alert_policy.h"

int main(void)
{
    assert(audio_alert_request_allowed(true, false, false, false, false, false));
    assert(!audio_alert_request_allowed(true, false, false, false, false, true));
    assert(audio_alert_request_allowed(false, false, false, false, false, true));
    for (unsigned busy = 0; busy < 2; ++busy) {
        assert(audio_alert_request_allowed(false, true, true, false, false, busy));
        assert(audio_alert_request_allowed(false, false, false, true, true, busy));
        assert(!audio_alert_request_allowed(false, true, false, true, true, busy));
        assert(!audio_alert_request_allowed(false, false, false, true, false, busy));
        assert(!audio_alert_request_allowed(true, true, true, false, false, busy));
        assert(!audio_alert_request_allowed(true, false, false, true, false, busy));
        assert(!audio_alert_request_allowed(true, false, false, true, true, busy));
    }
    puts("alarm preview admission and real-alarm priority tests passed");
}
