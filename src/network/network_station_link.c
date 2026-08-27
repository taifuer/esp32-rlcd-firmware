#include "network_station_link.h"

#include <stddef.h>

void network_station_link_begin(network_station_link_t *link)
{
    if (link == NULL) {
        return;
    }
    link->active = true;
    link->ipv4_ready = false;
}

void network_station_link_stop(network_station_link_t *link)
{
    if (link == NULL) {
        return;
    }
    link->active = false;
    link->ipv4_ready = false;
}

bool network_station_link_got_ipv4(network_station_link_t *link)
{
    if (link == NULL || !link->active) {
        return false;
    }
    link->ipv4_ready = true;
    return true;
}

bool network_station_link_lost_ipv4(network_station_link_t *link)
{
    if (link == NULL) {
        return false;
    }
    link->ipv4_ready = false;
    return link->active;
}

bool network_station_link_is_active(const network_station_link_t *link)
{
    return link != NULL && link->active;
}

bool network_station_link_is_connected(const network_station_link_t *link)
{
    return link != NULL && link->active && link->ipv4_ready;
}
