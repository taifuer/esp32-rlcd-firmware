#include "weather_http_json.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "miniz.h"
#include "weather_gzip.h"

enum {
    WEATHER_HTTP_TIMEOUT_MS = 15000,
    WEATHER_HTTP_BUFFER_BYTES = 4096,
    WEATHER_HTTP_MAX_COMPRESSED_BYTES = 32768,
    WEATHER_HTTP_MAX_CONSECUTIVE_TIMEOUTS = 3,
};

static esp_err_t read_body(esp_http_client_handle_t client,
                           uint8_t **body, size_t *body_length)
{
    uint8_t *buffer = malloc(WEATHER_HTTP_MAX_COMPRESSED_BYTES);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = ESP_OK;
    size_t received = 0U;
    unsigned int timeouts = 0U;
    while (received < WEATHER_HTTP_MAX_COMPRESSED_BYTES) {
        const int count = esp_http_client_read(
            client, (char *)buffer + received,
            (int)(WEATHER_HTTP_MAX_COMPRESSED_BYTES - received));
        if (count == -ESP_ERR_HTTP_EAGAIN) {
            if (++timeouts > WEATHER_HTTP_MAX_CONSECUTIVE_TIMEOUTS) {
                error = ESP_ERR_TIMEOUT;
                break;
            }
            continue;
        }
        if (count < 0) {
            error = ESP_FAIL;
            break;
        }
        if (count == 0) {
            if (!esp_http_client_is_complete_data_received(client)) {
                error = ESP_ERR_INVALID_RESPONSE;
            }
            break;
        }
        timeouts = 0U;
        received += (size_t)count;
    }
    if (error == ESP_OK &&
        received == WEATHER_HTTP_MAX_COMPRESSED_BYTES) {
        uint8_t extra = 0U;
        const int count = esp_http_client_read(client, (char *)&extra, 1);
        if (count != 0 || !esp_http_client_is_complete_data_received(client)) {
            error = ESP_ERR_INVALID_SIZE;
        }
    }
    if (error == ESP_OK && received == 0U) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        free(buffer);
        return error;
    }

    *body = buffer;
    *body_length = received;
    return ESP_OK;
}

static esp_err_t decode_body(const uint8_t *body, size_t body_length,
                             size_t maximum_json_bytes, char **json,
                             size_t *json_length)
{
    const bool gzip = body_length >= 2U && body[0] == 0x1fU &&
                      body[1] == 0x8bU;
    if (!gzip) {
        if (body_length > maximum_json_bytes) {
            return ESP_ERR_INVALID_SIZE;
        }
        char *output = malloc(body_length + 1U);
        if (output == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(output, body, body_length);
        output[body_length] = '\0';
        *json = output;
        *json_length = body_length;
        return ESP_OK;
    }

    weather_gzip_frame_t frame = {0};
    if (!weather_gzip_parse_frame(body, body_length, &frame) ||
        frame.expected_size == 0U ||
        frame.expected_size > maximum_json_bytes) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    char *output = malloc((size_t)frame.expected_size + 1U);
    if (output == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* tinfl_decompress_mem_to_mem() places tinfl_decompressor (roughly
     * 11 KiB) on the caller's stack. Keep that state on the heap so a gzip
     * weather response cannot exhaust the weather task's stack budget. */
    tinfl_decompressor *decompressor = heap_caps_malloc(
        sizeof(*decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (decompressor == NULL) {
        decompressor = heap_caps_malloc(sizeof(*decompressor),
                                        MALLOC_CAP_8BIT);
    }
    if (decompressor == NULL) {
        free(output);
        return ESP_ERR_NO_MEM;
    }
    tinfl_init(decompressor);
    size_t consumed = frame.deflate_length;
    size_t inflated = frame.expected_size;
    const tinfl_status inflate_status = tinfl_decompress(
        decompressor, body + frame.deflate_offset, &consumed,
        (mz_uint8 *)output, (mz_uint8 *)output, &inflated,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(decompressor);
    if (inflate_status != TINFL_STATUS_DONE ||
        consumed != frame.deflate_length ||
        inflated != frame.expected_size ||
        weather_gzip_crc32((const uint8_t *)output, inflated) !=
            frame.expected_crc32) {
        free(output);
        return ESP_ERR_INVALID_RESPONSE;
    }
    output[inflated] = '\0';
    *json = output;
    *json_length = inflated;
    return ESP_OK;
}

esp_err_t weather_http_get_json(const char *url, const char *api_key,
                                size_t maximum_json_bytes, char **json,
                                size_t *json_length)
{
    if (url == NULL || api_key == NULL || api_key[0] == '\0' ||
        maximum_json_bytes == 0U || json == NULL || json_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *json = NULL;
    *json_length = 0U;

    const esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .buffer_size = WEATHER_HTTP_BUFFER_BYTES,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = esp_http_client_set_header(
        client, "X-QW-Api-Key", api_key);
    if (error == ESP_OK) {
        error = esp_http_client_set_header(client, "Accept-Encoding", "gzip");
    }
    if (error == ESP_OK) {
        error = esp_http_client_set_header(
            client, "Accept", "application/json");
    }
    if (error == ESP_OK) {
        error = esp_http_client_open(client, 0);
    }
    if (error != ESP_OK) {
        esp_http_client_cleanup(client);
        return error;
    }

    const int64_t announced_length = esp_http_client_fetch_headers(client);
    if (announced_length < 0) {
        error = announced_length == ESP_FAIL
                    ? ESP_FAIL
                    : (esp_err_t)(-announced_length);
        goto cleanup;
    }
    const bool chunked_response =
        esp_http_client_is_chunked_response(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        error = status_code == 401 || status_code == 403
                    ? ESP_ERR_NOT_ALLOWED
                    : (status_code == 404 ? ESP_ERR_NOT_FOUND
                                          : ESP_ERR_INVALID_RESPONSE);
        goto cleanup;
    }
    if (announced_length > WEATHER_HTTP_MAX_COMPRESSED_BYTES) {
        error = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    uint8_t *body = NULL;
    size_t body_length = 0U;
    error = read_body(client, &body, &body_length);
    if (error == ESP_OK && !chunked_response &&
        (uint64_t)announced_length != body_length) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        error = decode_body(body, body_length, maximum_json_bytes,
                            json, json_length);
    }
    free(body);

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return error;
}
