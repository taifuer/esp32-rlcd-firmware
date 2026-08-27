#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    bool ipv4_ready;
} network_station_link_t;

void network_station_link_begin(network_station_link_t *link);
void network_station_link_stop(network_station_link_t *link);
bool network_station_link_got_ipv4(network_station_link_t *link);
bool network_station_link_lost_ipv4(network_station_link_t *link);
bool network_station_link_is_active(const network_station_link_t *link);
bool network_station_link_is_connected(const network_station_link_t *link);

#ifdef __cplusplus
}
#endif
