#include "network_screen_policy.h"

bool app_network_setup_should_overlay(bool provisioning, bool configured,
                                      bool dismissed,
                                      uint32_t provisioning_elapsed_ms)
{
    return provisioning && !configured && !dismissed &&
           provisioning_elapsed_ms < APP_NETWORK_SETUP_SCREEN_MS;
}
