#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_NETWORK_SETUP_SCREEN_MS = 60000U,
};

bool app_network_setup_should_overlay(bool provisioning, bool configured,
                                      bool dismissed,
                                      uint32_t provisioning_elapsed_ms);

#ifdef __cplusplus
}
#endif
