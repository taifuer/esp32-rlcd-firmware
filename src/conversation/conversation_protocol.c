#include "conversation_protocol.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0U;
    if (text == NULL) {
        return capacity;
    }
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool event_id_is_safe(const char *value)
{
    const size_t length = bounded_length(
        value, CONVERSATION_EVENT_ID_CAPACITY);
    if (length == 0U || length >= CONVERSATION_EVENT_ID_CAPACITY) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char byte = (unsigned char)value[index];
        const bool alpha = (byte >= (unsigned char)'A' &&
                            byte <= (unsigned char)'Z') ||
                           (byte >= (unsigned char)'a' &&
                            byte <= (unsigned char)'z');
        const bool digit = byte >= (unsigned char)'0' &&
                           byte <= (unsigned char)'9';
        if (!alpha && !digit && byte != (unsigned char)'-' &&
            byte != (unsigned char)'_') {
            return false;
        }
    }
    return true;
}

static bool finish_message(char *output, size_t capacity, int result,
                           size_t *written)
{
    if (result < 0 || (size_t)result >= capacity) {
        if (output != NULL && capacity > 0U) {
            output[0] = '\0';
        }
        if (written != NULL) {
            *written = 0U;
        }
        return false;
    }
    if (written != NULL) {
        *written = (size_t)result;
    }
    return true;
}

bool conversation_protocol_build_session_update(
    char *output, size_t capacity, const char *event_id,
    conversation_model_t model, size_t *written)
{
    const char *voice = NULL;
    const char *transcription = NULL;
    switch (model) {
    case CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME:
        voice = "Cherry";
        transcription =
            "\"input_audio_transcription\":{\"model\":"
            "\"qwen3-asr-flash-realtime\"},";
        break;
    case CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH:
        voice = "longanqian";
        transcription = "";
        break;
    case CONVERSATION_MODEL_COUNT:
    default:
        break;
    }
    if (output == NULL || capacity == 0U ||
        !event_id_is_safe(event_id) || voice == NULL ||
        transcription == NULL) {
        if (output != NULL && capacity > 0U) {
            output[0] = '\0';
        }
        return false;
    }

    /* Both selected model families use PCM16 mono at fixed 16 kHz input and
     * 24 kHz output. The legacy format fields are the most broadly compatible
     * representation for qwen3-omni-flash-realtime. */
    const int result = snprintf(
        output, capacity,
        "{\"event_id\":\"%s\",\"type\":\"session.update\","
        "\"session\":{\"modalities\":[\"text\",\"audio\"],"
        "\"voice\":\"%s\",\"instructions\":"
        "\"你是简洁友好的中英双语助手。跟随用户语言回答，必要时清楚朗读单词。\","
        "\"input_audio_format\":\"pcm\","
        "\"output_audio_format\":\"pcm\","
        "%s"
        "\"turn_detection\":null}}",
        event_id, voice, transcription);
    return finish_message(output, capacity, result, written);
}

static size_t base64_encoded_size(size_t input_size)
{
    return input_size > (SIZE_MAX - 2U) / 4U * 3U
               ? SIZE_MAX
               : ((input_size + 2U) / 3U) * 4U;
}

static bool base64_encode(const uint8_t *input, size_t input_size,
                          char *output, size_t output_capacity,
                          size_t *output_size)
{
    const size_t required = base64_encoded_size(input_size);
    if ((input == NULL && input_size > 0U) || output == NULL ||
        required == SIZE_MAX || required > output_capacity) {
        return false;
    }
    size_t source = 0U;
    size_t destination = 0U;
    while (source + 3U <= input_size) {
        const uint32_t value = ((uint32_t)input[source] << 16U) |
                               ((uint32_t)input[source + 1U] << 8U) |
                               (uint32_t)input[source + 2U];
        output[destination++] = BASE64_ALPHABET[(value >> 18U) & 0x3fU];
        output[destination++] = BASE64_ALPHABET[(value >> 12U) & 0x3fU];
        output[destination++] = BASE64_ALPHABET[(value >> 6U) & 0x3fU];
        output[destination++] = BASE64_ALPHABET[value & 0x3fU];
        source += 3U;
    }
    const size_t remaining = input_size - source;
    if (remaining == 1U) {
        const uint32_t value = (uint32_t)input[source] << 16U;
        output[destination++] = BASE64_ALPHABET[(value >> 18U) & 0x3fU];
        output[destination++] = BASE64_ALPHABET[(value >> 12U) & 0x3fU];
        output[destination++] = '=';
        output[destination++] = '=';
    } else if (remaining == 2U) {
        const uint32_t value = ((uint32_t)input[source] << 16U) |
                               ((uint32_t)input[source + 1U] << 8U);
        output[destination++] = BASE64_ALPHABET[(value >> 18U) & 0x3fU];
        output[destination++] = BASE64_ALPHABET[(value >> 12U) & 0x3fU];
        output[destination++] = BASE64_ALPHABET[(value >> 6U) & 0x3fU];
        output[destination++] = '=';
    }
    if (output_size != NULL) {
        *output_size = destination;
    }
    return destination == required;
}

bool conversation_protocol_build_audio_append(
    char *output, size_t capacity, const char *event_id,
    const uint8_t *pcm, size_t pcm_length, size_t *written)
{
    static const char prefix_format[] =
        "{\"event_id\":\"%s\",\"type\":"
        "\"input_audio_buffer.append\",\"audio\":\"";
    static const char suffix[] = "\"}";
    if (output == NULL || capacity == 0U ||
        !event_id_is_safe(event_id) || pcm == NULL || pcm_length == 0U ||
        pcm_length > CONVERSATION_PROTOCOL_MAX_UPSTREAM_PCM_BYTES ||
        (pcm_length & 1U) != 0U) {
        if (output != NULL && capacity > 0U) {
            output[0] = '\0';
        }
        return false;
    }
    const int prefix_length = snprintf(output, capacity, prefix_format,
                                       event_id);
    if (prefix_length < 0 || (size_t)prefix_length >= capacity) {
        output[0] = '\0';
        return false;
    }
    size_t encoded_length = 0U;
    const size_t prefix_size = (size_t)prefix_length;
    if (!base64_encode(pcm, pcm_length, output + prefix_size,
                       capacity - prefix_size, &encoded_length) ||
        sizeof(suffix) > capacity - prefix_size - encoded_length) {
        output[0] = '\0';
        return false;
    }
    memcpy(output + prefix_size + encoded_length, suffix, sizeof(suffix));
    const size_t total = prefix_size + encoded_length + sizeof(suffix) - 1U;
    if (written != NULL) {
        *written = total;
    }
    return true;
}

const char *conversation_client_event_name(
    conversation_client_event_t event)
{
    switch (event) {
    case CONVERSATION_CLIENT_EVENT_AUDIO_COMMIT:
        return "input_audio_buffer.commit";
    case CONVERSATION_CLIENT_EVENT_AUDIO_CLEAR:
        return "input_audio_buffer.clear";
    case CONVERSATION_CLIENT_EVENT_RESPONSE_CREATE:
        return "response.create";
    case CONVERSATION_CLIENT_EVENT_RESPONSE_CANCEL:
        return "response.cancel";
    default:
        return NULL;
    }
}

bool conversation_protocol_build_client_event(
    char *output, size_t capacity, const char *event_id,
    conversation_client_event_t event, size_t *written)
{
    const char *name = conversation_client_event_name(event);
    if (output == NULL || capacity == 0U || name == NULL ||
        !event_id_is_safe(event_id)) {
        if (output != NULL && capacity > 0U) {
            output[0] = '\0';
        }
        return false;
    }
    const int result = snprintf(
        output, capacity, "{\"event_id\":\"%s\",\"type\":\"%s\"}",
        event_id, name);
    return finish_message(output, capacity, result, written);
}

static size_t utf8_sequence_length(unsigned char lead)
{
    if (lead < 0x80U) {
        return 1U;
    }
    if (lead >= 0xc2U && lead <= 0xdfU) {
        return 2U;
    }
    if (lead >= 0xe0U && lead <= 0xefU) {
        return 3U;
    }
    if (lead >= 0xf0U && lead <= 0xf4U) {
        return 4U;
    }
    return 0U;
}

static bool append_utf8(const char *source, char *output, size_t capacity,
                        size_t *output_length)
{
    if (source == NULL || output == NULL || capacity == 0U ||
        output_length == NULL || *output_length >= capacity) {
        return false;
    }
    const size_t source_length = bounded_length(
        source, CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES + 1U);
    if (source_length >
        CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES) {
        return false;
    }
    size_t source_offset = 0U;
    while (source_offset < source_length) {
        const unsigned char lead = (unsigned char)source[source_offset];
        const size_t sequence = utf8_sequence_length(lead);
        if (sequence == 0U ||
            sequence > source_length - source_offset) {
            return false;
        }
        for (size_t index = 1U; index < sequence; ++index) {
            const unsigned char byte =
                (unsigned char)source[source_offset + index];
            if (byte < 0x80U || byte > 0xbfU) {
                return false;
            }
        }
        if ((lead == 0xe0U &&
             (unsigned char)source[source_offset + 1U] < 0xa0U) ||
            (lead == 0xedU &&
             (unsigned char)source[source_offset + 1U] > 0x9fU) ||
            (lead == 0xf0U &&
             (unsigned char)source[source_offset + 1U] < 0x90U) ||
            (lead == 0xf4U &&
             (unsigned char)source[source_offset + 1U] > 0x8fU)) {
            return false;
        }
        if (*output_length + sequence >= capacity) {
            break;
        }
        memcpy(output + *output_length, source + source_offset, sequence);
        *output_length += sequence;
        source_offset += sequence;
    }
    output[*output_length] = '\0';
    return true;
}

static bool copy_json_text(const cJSON *object, const char *name,
                           char *output, size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    size_t length = 0U;
    output[0] = '\0';
    return append_utf8(item->valuestring, output, capacity, &length);
}

static bool copy_json_text_pair(const cJSON *object, const char *first,
                                const char *second, char *output,
                                size_t capacity)
{
    const cJSON *left = cJSON_GetObjectItemCaseSensitive(object, first);
    const cJSON *right = cJSON_GetObjectItemCaseSensitive(object, second);
    if (!cJSON_IsString(left) || left->valuestring == NULL ||
        !cJSON_IsString(right) || right->valuestring == NULL) {
        return false;
    }
    size_t length = 0U;
    output[0] = '\0';
    return append_utf8(left->valuestring, output, capacity, &length) &&
           append_utf8(right->valuestring, output, capacity, &length);
}

static int base64_value(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
        return value - (unsigned char)'A';
    }
    if (value >= (unsigned char)'a' && value <= (unsigned char)'z') {
        return value - (unsigned char)'a' + 26;
    }
    if (value >= (unsigned char)'0' && value <= (unsigned char)'9') {
        return value - (unsigned char)'0' + 52;
    }
    if (value == (unsigned char)'+') {
        return 62;
    }
    if (value == (unsigned char)'/') {
        return 63;
    }
    return -1;
}

static bool base64_decode_strict(const char *encoded, uint8_t *output,
                                 size_t capacity, size_t *written)
{
    const size_t length = bounded_length(
        encoded, CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES + 1U);
    if (encoded == NULL || output == NULL || written == NULL ||
        length == 0U ||
        length > CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES ||
        (length & 3U) != 0U) {
        return false;
    }
    size_t output_length = 0U;
    for (size_t index = 0U; index < length; index += 4U) {
        const bool final_group = index + 4U == length;
        const unsigned char c0 = (unsigned char)encoded[index];
        const unsigned char c1 = (unsigned char)encoded[index + 1U];
        const unsigned char c2 = (unsigned char)encoded[index + 2U];
        const unsigned char c3 = (unsigned char)encoded[index + 3U];
        const int v0 = base64_value(c0);
        const int v1 = base64_value(c1);
        const int v2 = c2 == '=' ? 0 : base64_value(c2);
        const int v3 = c3 == '=' ? 0 : base64_value(c3);
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0 ||
            (!final_group && (c2 == '=' || c3 == '=')) ||
            (c2 == '=' && c3 != '=') ||
            (c2 == '=' && (v1 & 0x0f) != 0) ||
            (c3 == '=' && c2 != '=' && (v2 & 0x03) != 0)) {
            return false;
        }
        const size_t produced = c2 == '=' ? 1U : (c3 == '=' ? 2U : 3U);
        if (produced > capacity - output_length) {
            return false;
        }
        output[output_length++] = (uint8_t)((v0 << 2U) | (v1 >> 4U));
        if (produced >= 2U) {
            output[output_length++] =
                (uint8_t)((v1 << 4U) | (v2 >> 2U));
        }
        if (produced == 3U) {
            output[output_length++] = (uint8_t)((v2 << 6U) | v3);
        }
    }
    *written = output_length;
    return true;
}

static bool copy_error_code(const cJSON *error,
                            char *output, size_t capacity,
                            const char *fallback)
{
    const cJSON *code = cJSON_IsObject(error)
                            ? cJSON_GetObjectItemCaseSensitive(error,
                                                               "code")
                            : NULL;
    const char *source = cJSON_IsString(code) && code->valuestring != NULL
                             ? code->valuestring
                             : NULL;
    if (output == NULL || capacity == 0U || fallback == NULL) {
        return false;
    }
    size_t length = source != NULL ? bounded_length(source, capacity) : 0U;
    bool safe = length > 0U && length < capacity;
    for (size_t index = 0U; safe && index < length; ++index) {
        const unsigned char value = (unsigned char)source[index];
        const bool alpha = (value >= (unsigned char)'A' &&
                            value <= (unsigned char)'Z') ||
                           (value >= (unsigned char)'a' &&
                            value <= (unsigned char)'z');
        const bool digit = value >= (unsigned char)'0' &&
                           value <= (unsigned char)'9';
        safe = alpha || digit || value == (unsigned char)'.' ||
               value == (unsigned char)'_' || value == (unsigned char)'-';
    }
    if (!safe) {
        source = fallback;
        length = bounded_length(source, capacity);
        if (length == 0U || length >= capacity) {
            return false;
        }
    }
    memcpy(output, source, length);
    output[length] = '\0';
    return true;
}

static bool parse_response_done(const cJSON *root,
                                conversation_server_event_t *event)
{
    const cJSON *response =
        cJSON_GetObjectItemCaseSensitive(root, "response");
    const cJSON *status = cJSON_IsObject(response)
                              ? cJSON_GetObjectItemCaseSensitive(response,
                                                                 "status")
                              : NULL;
    if (!cJSON_IsString(status) || status->valuestring == NULL) {
        return false;
    }
    if (strcmp(status->valuestring, "completed") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_RESPONSE_DONE;
        return true;
    }
    if (strcmp(status->valuestring, "cancelled") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_RESPONSE_CANCELLED;
        return true;
    }

    event->kind = CONVERSATION_SERVER_EVENT_ERROR;
    const cJSON *details =
        cJSON_GetObjectItemCaseSensitive(response, "status_details");
    const cJSON *error = cJSON_IsObject(details)
                             ? cJSON_GetObjectItemCaseSensitive(details,
                                                                "error")
                             : NULL;
    const char *fallback = strcmp(status->valuestring, "failed") == 0
                               ? "ResponseFailed"
                               : "ResponseIncomplete";
    return copy_error_code(error, event->error_name,
                           sizeof(event->error_name), fallback);
}

bool conversation_protocol_parse_server_event(
    const char *json, size_t length, conversation_server_event_t *event,
    uint8_t *audio, size_t audio_capacity)
{
    if (json == NULL || length == 0U || event == NULL ||
        length > CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES) {
        return false;
    }
    memset(event, 0, sizeof(*event));

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        json, length, &parse_end, false);
    bool trailing_valid = parse_end != NULL && parse_end >= json &&
                          parse_end <= json + length;
    for (const char *position = parse_end;
         trailing_valid && position < json + length; ++position) {
        const unsigned char value = (unsigned char)*position;
        trailing_valid = value == 0x20U || value == 0x09U ||
                         value == 0x0aU || value == 0x0dU;
    }
    if (!cJSON_IsObject(root) || !trailing_valid) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }

    bool valid = true;
    if (strcmp(type->valuestring, "session.created") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_SESSION_CREATED;
    } else if (strcmp(type->valuestring, "session.updated") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_SESSION_UPDATED;
    } else if (strcmp(
                   type->valuestring,
                   "conversation.item.input_audio_transcription.delta") ==
               0) {
        event->kind = CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DELTA;
        valid = copy_json_text_pair(root, "text", "stash", event->text,
                                    sizeof(event->text));
    } else if (strcmp(
                   type->valuestring,
                   "conversation.item.input_audio_transcription.completed") ==
               0) {
        event->kind = CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DONE;
        valid = copy_json_text(root, "transcript", event->text,
                               sizeof(event->text));
    } else if (strcmp(
                   type->valuestring,
                   "conversation.item.input_audio_transcription.failed") ==
               0) {
        event->kind = CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_FAILED;
        valid = copy_error_code(
            cJSON_GetObjectItemCaseSensitive(root, "error"),
            event->error_name, sizeof(event->error_name),
            "InputTranscriptionFailed");
    } else if (strcmp(type->valuestring, "response.created") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_RESPONSE_CREATED;
    } else if (strcmp(type->valuestring,
                      "response.audio_transcript.delta") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DELTA;
        valid = copy_json_text(root, "delta", event->text,
                               sizeof(event->text));
    } else if (strcmp(type->valuestring,
                      "response.audio_transcript.done") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE;
        valid = copy_json_text(root, "transcript", event->text,
                               sizeof(event->text));
    } else if (strcmp(type->valuestring, "response.audio.delta") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_AUDIO_DELTA;
        const cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "delta");
        valid = cJSON_IsString(delta) && delta->valuestring != NULL &&
                audio != NULL &&
                base64_decode_strict(delta->valuestring, audio,
                                     audio_capacity,
                                     &event->audio_length) &&
                event->audio_length > 0U &&
                (event->audio_length & 1U) == 0U;
    } else if (strcmp(type->valuestring, "response.audio.done") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_AUDIO_DONE;
    } else if (strcmp(type->valuestring, "response.done") == 0) {
        valid = parse_response_done(root, event);
    } else if (strcmp(type->valuestring, "session.finished") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_SESSION_FINISHED;
    } else if (strcmp(type->valuestring, "error") == 0) {
        event->kind = CONVERSATION_SERVER_EVENT_ERROR;
        valid = copy_error_code(
            cJSON_GetObjectItemCaseSensitive(root, "error"),
            event->error_name, sizeof(event->error_name), "ServiceError");
    } else {
        event->kind = CONVERSATION_SERVER_EVENT_UNKNOWN;
    }

    cJSON_Delete(root);
    return valid;
}

const char *conversation_server_event_name(
    conversation_server_event_kind_t kind)
{
    switch (kind) {
    case CONVERSATION_SERVER_EVENT_SESSION_CREATED:
        return "session_created";
    case CONVERSATION_SERVER_EVENT_SESSION_UPDATED:
        return "session_updated";
    case CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DELTA:
        return "input_transcript_delta";
    case CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DONE:
        return "input_transcript_done";
    case CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_FAILED:
        return "input_transcript_failed";
    case CONVERSATION_SERVER_EVENT_RESPONSE_CREATED:
        return "response_created";
    case CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DELTA:
        return "response_transcript_delta";
    case CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE:
        return "response_transcript_done";
    case CONVERSATION_SERVER_EVENT_AUDIO_DELTA:
        return "audio_delta";
    case CONVERSATION_SERVER_EVENT_AUDIO_DONE:
        return "audio_done";
    case CONVERSATION_SERVER_EVENT_RESPONSE_DONE:
        return "response_done";
    case CONVERSATION_SERVER_EVENT_RESPONSE_CANCELLED:
        return "response_cancelled";
    case CONVERSATION_SERVER_EVENT_SESSION_FINISHED:
        return "session_finished";
    case CONVERSATION_SERVER_EVENT_ERROR:
        return "error";
    case CONVERSATION_SERVER_EVENT_UNKNOWN:
    default:
        return "unknown";
    }
}
