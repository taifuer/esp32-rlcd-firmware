#include "network_retry_policy.h"

uint32_t network_retry_delay_ms(uint32_t consecutive_failures)
{
    if (consecutive_failures <= 1U) {
        return 60000U;
    }
    if (consecutive_failures == 2U) {
        return 300000U;
    }
    return 900000U;
}
