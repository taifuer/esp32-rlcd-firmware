#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_buttons_init(void);
bool board_boot_is_pressed(void);
bool board_key_is_pressed(void);

#ifdef __cplusplus
}
#endif
