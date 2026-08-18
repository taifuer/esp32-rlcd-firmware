#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns an approximate 18650 state of charge, clamped to 0..100%. */
uint8_t battery_level_from_voltage_mv(uint16_t voltage_mv);

#ifdef __cplusplus
}
#endif
