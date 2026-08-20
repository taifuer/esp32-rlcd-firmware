#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t user_button_init(void);
bool user_button_is_pressed(void);

#ifdef __cplusplus
}
#endif
