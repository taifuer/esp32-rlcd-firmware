#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "network_screen_policy.h"

int main(void)
{
    assert(app_network_setup_should_overlay(true, false, false, 0U));
    assert(app_network_setup_should_overlay(
        true, false, false, APP_NETWORK_SETUP_SCREEN_MS - 1U));
    assert(!app_network_setup_should_overlay(
        true, false, false, APP_NETWORK_SETUP_SCREEN_MS));
    assert(!app_network_setup_should_overlay(false, false, false, 0U));
    assert(!app_network_setup_should_overlay(true, true, false, 0U));
    assert(!app_network_setup_should_overlay(true, false, true, 0U));
    assert(!app_network_setup_should_overlay(false, true, true, UINT32_MAX));

    puts("network screen policy tests passed");
    return 0;
}
