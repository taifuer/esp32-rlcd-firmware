#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t network_retry_delay_ms(uint32_t consecutive_failures);

#ifdef __cplusplus
}
#endif
