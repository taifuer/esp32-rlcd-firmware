#pragma once

#include "market_model.h"

#define MARKET_PORTAL_FORM_MAX_LENGTH 128U

/* Accepts only enabled=on/off and 1–5 ordered built-in IDs. No URLs/symbols.
 * On any malformed, duplicate or unknown field, output is left unchanged. */
bool market_portal_parse_form(const char *body, size_t length,
                             market_config_t *config);
