#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PORTAL_TOKEN_BYTES 16U
#define SETTINGS_PORTAL_TOKEN_LENGTH (SETTINGS_PORTAL_TOKEN_BYTES * 2U)
#define SETTINGS_PORTAL_TOKEN_CAPACITY (SETTINGS_PORTAL_TOKEN_LENGTH + 1U)
#define SETTINGS_PORTAL_IDLE_MS (5U * 60U * 1000U)
#define SETTINGS_PORTAL_MAX_MS (30U * 60U * 1000U)
#define SETTINGS_PORTAL_TRANSACTION_MS (10U * 60U * 1000U)

typedef struct {
    uint32_t started_ms;
    uint32_t activity_ms;
    uint32_t transaction_ms;
    bool transaction_active;
} settings_portal_clock_t;

uint32_t settings_portal_clock_remaining(const settings_portal_clock_t *clock,
                                         uint32_t now_ms, bool recovery);
bool settings_portal_lan_post_allowed(const char *uri);
bool settings_portal_host_matches(const char *url, const char *host);
bool settings_portal_pair_code_matches(const char *expected,
                                       const char *body, size_t length);

typedef enum {
    SETTINGS_PORTAL_TIMEOUT_EXPIRE = 0,
    SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_MUTATION,
    SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_UPLOAD,
    SETTINGS_PORTAL_TIMEOUT_RESTART,
} settings_portal_timeout_action_t;

bool settings_portal_token_encode(
    const uint8_t entropy[SETTINGS_PORTAL_TOKEN_BYTES], char *token,
    size_t capacity);
bool settings_portal_token_matches(const char *expected,
                                   const char *provided);
bool settings_portal_write_is_available(bool session_ready,
                                        bool upload_started,
                                        bool mutation_active,
                                        bool restart_requested);
/* Evaluate an expired, already-closed session. A regular mutation receives a
 * bounded completion grace period; an OTA upload remains deadline-bound and
 * waits only for its abort/failure cleanup. */
settings_portal_timeout_action_t settings_portal_timeout_action(
    bool upload_started, bool mutation_active, bool restart_requested);
uint32_t settings_portal_deadline_remaining(uint32_t started_at,
                                            uint32_t now,
                                            uint32_t timeout_ticks);
bool settings_portal_parse_unix_form(const char *body, size_t length,
                                     int64_t *unix_seconds);
bool settings_portal_parse_volume_form(const char *body, size_t length,
                                       uint8_t *volume);
bool settings_portal_confirmation_matches(const char *body, size_t length,
                                          const char *expected_word);
/* Escape a UTF-8 byte string for use between JSON quotes. */
bool settings_portal_json_escape(const char *text, char *escaped,
                                 size_t capacity);

#ifdef __cplusplus
}
#endif
