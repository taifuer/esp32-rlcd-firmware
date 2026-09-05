#pragma once
#include <stddef.h>
#include <stdint.h>
/* Test double only: checks digest/read-back control flow, not SHA-256. The
 * actual firmware links ESP-IDF's pinned mbedTLS implementation. */
typedef struct { uint32_t value; } mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *);
void mbedtls_sha256_free(mbedtls_sha256_context *);
int mbedtls_sha256_starts(mbedtls_sha256_context *, int);
int mbedtls_sha256_update(mbedtls_sha256_context *, const unsigned char *, size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context *, unsigned char *);
