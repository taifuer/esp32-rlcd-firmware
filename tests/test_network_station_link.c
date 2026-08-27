#include <assert.h>
#include <stdio.h>

#include "network_station_link.h"

int main(void)
{
    network_station_link_t link = {0};
    assert(!network_station_link_is_active(&link));
    assert(!network_station_link_is_connected(&link));

    network_station_link_begin(&link);
    assert(network_station_link_is_active(&link));
    assert(!network_station_link_is_connected(&link));
    assert(network_station_link_got_ipv4(&link));
    assert(network_station_link_is_connected(&link));

    assert(network_station_link_lost_ipv4(&link));
    assert(network_station_link_is_active(&link));
    assert(!network_station_link_is_connected(&link));
    assert(network_station_link_got_ipv4(&link));
    assert(network_station_link_is_connected(&link));

    network_station_link_stop(&link);
    assert(!network_station_link_is_active(&link));
    assert(!network_station_link_is_connected(&link));
    assert(!network_station_link_got_ipv4(&link));
    assert(!network_station_link_is_connected(&link));

    network_station_link_begin(&link);
    assert(network_station_link_got_ipv4(&link));
    network_station_link_begin(&link);
    assert(network_station_link_is_active(&link));
    assert(!network_station_link_is_connected(&link));

    network_station_link_begin(NULL);
    network_station_link_stop(NULL);
    assert(!network_station_link_got_ipv4(NULL));
    assert(!network_station_link_lost_ipv4(NULL));
    assert(!network_station_link_is_active(NULL));
    assert(!network_station_link_is_connected(NULL));

    puts("network station link tests passed");
    return 0;
}
