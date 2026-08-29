#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "network_retry_policy.h"

int main(void)
{
    assert(network_retry_delay_ms(0U) == 60000U);
    assert(network_retry_delay_ms(1U) == 60000U);
    assert(network_retry_delay_ms(2U) == 300000U);
    assert(network_retry_delay_ms(3U) == 900000U);
    assert(network_retry_delay_ms(4U) == 900000U);
    assert(network_retry_delay_ms(UINT32_MAX) == 900000U);

    puts("network retry policy tests passed");
    return 0;
}
