#include <assert.h>
#include <stdio.h>

#include "voice_backend_policy.h"

int main(void)
{
    assert(app_voice_backend_choose(true, true, true, true, false, true) ==
           APP_VOICE_BACKEND_CLOUD);
    assert(app_voice_backend_choose(true, true, false, true, false, true) ==
           APP_VOICE_BACKEND_OFFLINE);
    assert(app_voice_backend_choose(true, false, true, true, false, true) ==
           APP_VOICE_BACKEND_OFFLINE);
    assert(app_voice_backend_choose(false, true, true, true, false, true) ==
           APP_VOICE_BACKEND_OFFLINE);
    assert(app_voice_backend_choose(true, true, true, false, false, true) ==
           APP_VOICE_BACKEND_OFFLINE);
    assert(app_voice_backend_choose(true, true, false, true, false, false) ==
           APP_VOICE_BACKEND_UNAVAILABLE);
    /* Cloud cleanup still owns the shared audio worker, so a quick retry must
     * not fall through to either backend until cleanup reaches a terminal
     * state. */
    assert(app_voice_backend_choose(true, true, true, true, true, true) ==
           APP_VOICE_BACKEND_UNAVAILABLE);
    puts("voice_backend_policy: OK");
    return 0;
}
