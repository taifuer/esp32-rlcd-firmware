#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conversation_protocol.h"

static const char EVENT_ID[] =
    "f894c16f-f20e-4c1d-837e-89e0fbc63a43";

static void test_model_specific_session_update(void)
{
    char message[CONVERSATION_PROTOCOL_MESSAGE_CAPACITY];
    size_t written = 0U;
    assert(conversation_protocol_build_session_update(
        message, sizeof(message), EVENT_ID,
        CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME, &written));
    assert(written == strlen(message));
    assert(strstr(message, "\"type\":\"session.update\"") != NULL);
    assert(strstr(message, "\"modalities\":[\"text\",\"audio\"]") !=
           NULL);
    assert(strstr(message, "\"voice\":\"Cherry\"") != NULL);
    assert(strstr(message, "\"turn_detection\":null") != NULL);
    assert(strstr(message, "\"input_audio_format\":\"pcm\"") != NULL);
    assert(strstr(message, "\"output_audio_format\":\"pcm\"") != NULL);
    assert(strstr(
               message,
               "\"input_audio_transcription\":{\"model\":"
               "\"qwen3-asr-flash-realtime\"}") != NULL);
    assert(strstr(message, "longanqian") == NULL);
    assert(strstr(message, "multimodal-dialog") == NULL);
    assert(strstr(message, "workspace_id") == NULL);
    assert(strstr(message, "app_id") == NULL);

    assert(conversation_protocol_build_session_update(
        message, sizeof(message), EVENT_ID,
        CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH, &written));
    assert(strstr(message, "\"voice\":\"longanqian\"") != NULL);
    assert(strstr(message, "input_audio_transcription") == NULL);
    assert(strstr(message, "Cherry") == NULL);

    char too_small[32];
    assert(!conversation_protocol_build_session_update(
        too_small, sizeof(too_small), EVENT_ID,
        CONVERSATION_DEFAULT_MODEL, NULL));
    assert(too_small[0] == '\0');
    assert(!conversation_protocol_build_session_update(
        message, sizeof(message), "bad\"id",
        CONVERSATION_DEFAULT_MODEL, NULL));
    assert(!conversation_protocol_build_session_update(
        message, sizeof(message), EVENT_ID, CONVERSATION_MODEL_COUNT,
        NULL));
}

static void test_audio_append_and_client_events(void)
{
    static const uint8_t pcm[] = {0x00U, 0x01U, 0xfeU, 0xffU};
    char message[CONVERSATION_PROTOCOL_MESSAGE_CAPACITY];
    size_t written = 0U;
    assert(conversation_protocol_build_audio_append(
        message, sizeof(message), EVENT_ID, pcm, sizeof(pcm), &written));
    assert(written == strlen(message));
    assert(strstr(message,
                  "\"type\":\"input_audio_buffer.append\"") != NULL);
    assert(strstr(message, "\"audio\":\"AAH+/w==\"") != NULL);

    assert(!conversation_protocol_build_audio_append(
        message, sizeof(message), EVENT_ID, pcm, 3U, NULL));
    uint8_t too_large[CONVERSATION_PROTOCOL_MAX_UPSTREAM_PCM_BYTES + 2U];
    assert(!conversation_protocol_build_audio_append(
        message, sizeof(message), EVENT_ID, too_large,
        sizeof(too_large), NULL));

    const struct {
        conversation_client_event_t event;
        const char *name;
    } cases[] = {
        {CONVERSATION_CLIENT_EVENT_AUDIO_COMMIT,
         "input_audio_buffer.commit"},
        {CONVERSATION_CLIENT_EVENT_AUDIO_CLEAR,
         "input_audio_buffer.clear"},
        {CONVERSATION_CLIENT_EVENT_RESPONSE_CREATE, "response.create"},
        {CONVERSATION_CLIENT_EVENT_RESPONSE_CANCEL, "response.cancel"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]);
         ++index) {
        assert(conversation_protocol_build_client_event(
            message, sizeof(message), EVENT_ID, cases[index].event,
            &written));
        assert(strcmp(conversation_client_event_name(cases[index].event),
                      cases[index].name) == 0);
        char expected[80];
        assert(snprintf(expected, sizeof(expected), "\"type\":\"%s\"",
                        cases[index].name) > 0);
        assert(strstr(message, expected) != NULL);
    }
    assert(conversation_client_event_name(
               (conversation_client_event_t)99) == NULL);
}

static conversation_server_event_t parse(const char *json,
                                         uint8_t *audio,
                                         size_t audio_capacity)
{
    conversation_server_event_t event;
    assert(conversation_protocol_parse_server_event(
        json, strlen(json), &event, audio, audio_capacity));
    return event;
}

static void test_session_and_transcript_events(void)
{
    conversation_server_event_t event = parse(
        "{\"type\":\"session.created\",\"session\":{\"id\":\"s\"}}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_SESSION_CREATED);
    event = parse("{\"type\":\"session.updated\",\"session\":{}}",
                  NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_SESSION_UPDATED);

    event = parse(
        "{\"type\":\"conversation.item.input_audio_transcription.delta\","
        "\"text\":\"今天\",\"stash\":\"天气\"}",
        NULL, 0U);
    assert(event.kind ==
           CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DELTA);
    assert(strcmp(event.text, "今天天气") == 0);
    event = parse(
        "{\"type\":"
        "\"conversation.item.input_audio_transcription.completed\","
        "\"transcript\":\"今天天气怎么样？\"}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DONE);
    assert(strcmp(event.text, "今天天气怎么样？") == 0);

    event = parse("{\"type\":\"response.created\",\"response\":{}}",
                  NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_RESPONSE_CREATED);
    event = parse(
        "{\"type\":\"response.audio_transcript.delta\","
        "\"delta\":\"Hello\"}",
        NULL, 0U);
    assert(event.kind ==
           CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DELTA);
    assert(strcmp(event.text, "Hello") == 0);
    event = parse(
        "{\"type\":\"response.audio_transcript.done\","
        "\"transcript\":\"Hello!\"}",
        NULL, 0U);
    assert(event.kind ==
           CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE);
    assert(strcmp(event.text, "Hello!") == 0);
}

static void test_audio_events_and_strict_base64(void)
{
    uint8_t audio[32] = {0};
    conversation_server_event_t event = parse(
        "{\"type\":\"response.audio.delta\","
        "\"delta\":\"AAH+/w==\"}",
        audio, sizeof(audio));
    assert(event.kind == CONVERSATION_SERVER_EVENT_AUDIO_DELTA);
    assert(event.audio_length == 4U);
    assert(audio[0] == 0x00U && audio[1] == 0x01U &&
           audio[2] == 0xfeU && audio[3] == 0xffU);
    event = parse("{\"type\":\"response.audio.done\"}", NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_AUDIO_DONE);

    assert(!conversation_protocol_parse_server_event(
        "{\"type\":\"response.audio.delta\",\"delta\":\"A===\"}",
        strlen("{\"type\":\"response.audio.delta\",\"delta\":\"A===\"}"),
        &event, audio, sizeof(audio)));
    assert(!conversation_protocol_parse_server_event(
        "{\"type\":\"response.audio.delta\",\"delta\":\"AA==\"}",
        strlen("{\"type\":\"response.audio.delta\",\"delta\":\"AA==\"}"),
        &event, audio, sizeof(audio))); /* odd decoded PCM byte */

    /* 12 KiB decoded audio validates that normal large deltas do not depend
     * on the WebSocket task stack. */
    const size_t encoded_size = 16U * 1024U;
    const size_t json_size = encoded_size + 64U;
    char *json = malloc(json_size);
    uint8_t *large_audio = malloc(12U * 1024U);
    assert(json != NULL && large_audio != NULL);
    const int prefix = snprintf(
        json, json_size,
        "{\"type\":\"response.audio.delta\",\"delta\":\"");
    assert(prefix > 0);
    memset(json + prefix, 'A', encoded_size);
    memcpy(json + prefix + encoded_size, "\"}", 3U);
    assert(conversation_protocol_parse_server_event(
        json, (size_t)prefix + encoded_size + 2U, &event, large_audio,
        12U * 1024U));
    assert(event.audio_length == 12U * 1024U);
    free(large_audio);
    free(json);
}

static void test_response_done_status_and_errors(void)
{
    conversation_server_event_t event = parse(
        "{\"type\":\"response.done\","
        "\"response\":{\"status\":\"completed\",\"output\":[]}}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_RESPONSE_DONE);

    event = parse(
        "{\"type\":\"response.done\","
        "\"response\":{\"status\":\"cancelled\"}}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_RESPONSE_CANCELLED);

    event = parse(
        "{\"type\":\"response.done\",\"response\":{"
        "\"status\":\"failed\",\"status_details\":{\"error\":{"
        "\"code\":\"Model.AccessDenied\",\"message\":\"secret body\"}}}}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_ERROR);
    assert(strcmp(event.error_name, "Model.AccessDenied") == 0);
    assert(strstr(event.error_name, "secret") == NULL);

    event = parse(
        "{\"type\":\"error\",\"error\":{"
        "\"type\":\"invalid_request_error\","
        "\"code\":\"invalid_value\","
        "\"message\":\"API key and transcript must not leak\"}}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_ERROR);
    assert(strcmp(event.error_name, "invalid_value") == 0);
    assert(event.text[0] == '\0');

    event = parse(
        "{\"type\":"
        "\"conversation.item.input_audio_transcription.failed\","
        "\"error\":{\"code\":\"asr_failed\",\"message\":\"body\"}}",
        NULL, 0U);
    assert(event.kind ==
           CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_FAILED);
    assert(strcmp(event.error_name, "asr_failed") == 0);

    event = parse(
        "{\"type\":\"error\",\"error\":{"
        "\"code\":\"unsafe code\\nsecret\",\"message\":\"body\"}}",
        NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_ERROR);
    assert(strcmp(event.error_name, "ServiceError") == 0);

    event = parse("{\"type\":\"session.finished\"}", NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_SESSION_FINISHED);
    event = parse("{\"type\":\"rate_limits.updated\"}", NULL, 0U);
    assert(event.kind == CONVERSATION_SERVER_EVENT_UNKNOWN);
}

static void test_invalid_and_bounded_messages(void)
{
    conversation_server_event_t event;
    assert(!conversation_protocol_parse_server_event("{}", 2U, &event,
                                                     NULL, 0U));
    assert(!conversation_protocol_parse_server_event("{", 1U, &event,
                                                     NULL, 0U));
    assert(!conversation_protocol_parse_server_event(NULL, 0U, &event,
                                                     NULL, 0U));
    assert(!conversation_protocol_parse_server_event(
        "{\"type\":\"response.done\",\"response\":{}}",
        strlen("{\"type\":\"response.done\",\"response\":{}}"),
        &event, NULL, 0U));

    static const char trailing_garbage[] =
        "{\"type\":\"session.created\"}garbage";
    assert(!conversation_protocol_parse_server_event(
        trailing_garbage, sizeof(trailing_garbage) - 1U, &event,
        NULL, 0U));
    static const char embedded_nul[] =
        "{\"type\":\"session.created\"}\0junk";
    assert(!conversation_protocol_parse_server_event(
        embedded_nul, sizeof(embedded_nul) - 1U, &event, NULL, 0U));
    static const char trailing_whitespace[] =
        "{\"type\":\"session.created\"} \t\r\n";
    assert(conversation_protocol_parse_server_event(
        trailing_whitespace, sizeof(trailing_whitespace) - 1U, &event,
        NULL, 0U));
    assert(event.kind == CONVERSATION_SERVER_EVENT_SESSION_CREATED);

    static const char truncated_utf8[] =
        "{\"type\":\"response.audio_transcript.delta\","
        "\"delta\":\"\xf0\x9f\x98\"}";
    assert(!conversation_protocol_parse_server_event(
        truncated_utf8, sizeof(truncated_utf8) - 1U, &event, NULL, 0U));

    char repeated[700] = {0};
    size_t used = 0U;
    for (size_t index = 0U; index < 200U; ++index) {
        memcpy(repeated + used, "你", strlen("你"));
        used += strlen("你");
    }
    char json[1024];
    const int written = snprintf(
        json, sizeof(json),
        "{\"type\":\"response.audio_transcript.done\","
        "\"transcript\":\"%s\"}",
        repeated);
    assert(written > 0 && (size_t)written < sizeof(json));
    const conversation_server_event_t parsed = parse(json, NULL, 0U);
    assert(parsed.kind ==
           CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE);
    assert(strlen(parsed.text) < CONVERSATION_EVENT_TEXT_CAPACITY);
    assert(strlen(parsed.text) % strlen("你") == 0U);

    char long_transcript[700];
    memset(long_transcript, 'x', sizeof(long_transcript));
    const char suffix[] = "TAIL-回答结束";
    const size_t suffix_length = strlen(suffix);
    const size_t transcript_length = 620U;
    memcpy(long_transcript + transcript_length - suffix_length,
           suffix, suffix_length);
    long_transcript[transcript_length] = '\0';
    const int transcript_written = snprintf(
        json, sizeof(json),
        "{\"type\":\"response.audio_transcript.done\","
        "\"transcript\":\"%s\"}",
        long_transcript);
    assert(transcript_written > 0 &&
           (size_t)transcript_written < sizeof(json));
    const conversation_server_event_t tail = parse(json, NULL, 0U);
    assert(tail.kind ==
           CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE);
    assert(strlen(tail.text) == CONVERSATION_EVENT_TEXT_CAPACITY - 1U);
    assert(strcmp(tail.text + strlen(tail.text) - suffix_length,
                  suffix) == 0);

    /* response.done may include a large complete output and usage object. */
    const size_t filler_size = 40U * 1024U;
    char *large = malloc(filler_size + 128U);
    assert(large != NULL);
    const int prefix = snprintf(
        large, filler_size + 128U,
        "{\"type\":\"response.done\",\"response\":{"
        "\"status\":\"completed\",\"ignored\":\"");
    assert(prefix > 0);
    memset(large + prefix, 'x', filler_size);
    memcpy(large + prefix + filler_size, "\"}}", 4U);
    assert(conversation_protocol_parse_server_event(
        large, (size_t)prefix + filler_size + 3U, &event, NULL, 0U));
    assert(event.kind == CONVERSATION_SERVER_EVENT_RESPONSE_DONE);
    free(large);
}

int main(void)
{
    test_model_specific_session_update();
    test_audio_append_and_client_events();
    test_session_and_transcript_events();
    test_audio_events_and_strict_base64();
    test_response_done_status_and_errors();
    test_invalid_and_bounded_messages();
    puts("conversation_protocol: OK");
    return 0;
}
