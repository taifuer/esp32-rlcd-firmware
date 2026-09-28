#include "conversation_feedback.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *code;
    conversation_feedback_error_t error;
} error_code_t;

/* Alibaba Model Studio's documented machine codes, plus our own transport
 * labels. Deliberately no substring/case folding or service-message parsing:
 * permission failures do not prove an expired model or an overdue account.
 * https://help.aliyun.com/zh/model-studio/error-code */
static const error_code_t s_error_codes[] = {
    {"InvalidApiKey", CONVERSATION_FEEDBACK_ERROR_AUTH},
    {"invalid_api_key", CONVERSATION_FEEDBACK_ERROR_AUTH},
    {"Unauthorized", CONVERSATION_FEEDBACK_ERROR_AUTH},
    {"AccessDenied", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"access_denied", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"AccessDenied.Unpurchased", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"Model.AccessDenied", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"Workspace.AccessDenied", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"Endpoint.AccessDenied", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"Forbidden", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"NOT AUTHORIZED", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"WorkSpaceNotFound", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"Arrearage", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"AllocationQuota.FreeTierOnly", CONVERSATION_FEEDBACK_ERROR_ACCESS},
    {"ModelNotFound", CONVERSATION_FEEDBACK_ERROR_MODEL},
    {"model_not_found", CONVERSATION_FEEDBACK_ERROR_MODEL},
    {"model_not_supported", CONVERSATION_FEEDBACK_ERROR_MODEL},
    {"Throttling", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"Throttling.RateQuota", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"Throttling.BurstRate", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"Throttling.AllocationQuota", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"LimitRequests", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"limit_requests", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"limit_burst_rate", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"insufficient_quota", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"rate_limit_exceeded", CONVERSATION_FEEDBACK_ERROR_LIMIT},
    {"Throttling.ServiceOverloaded", CONVERSATION_FEEDBACK_ERROR_SERVICE},
    {"Throttling.ResourceExhausted", CONVERSATION_FEEDBACK_ERROR_SERVICE},
    {"InternalError", CONVERSATION_FEEDBACK_ERROR_SERVICE},
    {"ServiceUnavailable", CONVERSATION_FEEDBACK_ERROR_SERVICE},
    {"PongTimeout", CONVERSATION_FEEDBACK_ERROR_TIMEOUT},
    {"TransportTimeout", CONVERSATION_FEEDBACK_ERROR_TIMEOUT},
    {"TlsError", CONVERSATION_FEEDBACK_ERROR_TLS},
    {"TransportError", CONVERSATION_FEEDBACK_ERROR_NETWORK},
    {"TransportStart", CONVERSATION_FEEDBACK_ERROR_NETWORK},
    {"Disconnected", CONVERSATION_FEEDBACK_ERROR_NETWORK},
    {"ServerClose", CONVERSATION_FEEDBACK_ERROR_CLOSED},
    {"TransportStopped", CONVERSATION_FEEDBACK_ERROR_NETWORK},
    {"AudioBufferFull", CONVERSATION_FEEDBACK_ERROR_RESOURCE},
};

conversation_feedback_error_t conversation_feedback_classify(
    conversation_feedback_local_error_t local_error,
    int service_code, const char *service_name)
{
    const char *name = service_name != NULL ? service_name : "";
    if (strcmp(name, "HandshakeError") == 0 ||
        strcmp(name, "Unauthorized") == 0 || strcmp(name, "Forbidden") == 0) {
        switch (service_code) {
        case 401: return CONVERSATION_FEEDBACK_ERROR_AUTH;
        case 403: return CONVERSATION_FEEDBACK_ERROR_ACCESS;
        case 404: return CONVERSATION_FEEDBACK_ERROR_ACCESS;
        case 429: return CONVERSATION_FEEDBACK_ERROR_LIMIT;
        case 408: case 504: return CONVERSATION_FEEDBACK_ERROR_TIMEOUT;
        default:
            if (service_code >= 500 && service_code <= 599) {
                return CONVERSATION_FEEDBACK_ERROR_SERVICE;
            }
            break;
        }
    }
    /* A concrete resource/timeout error is more useful than a generic
     * transport label; explicit server rejection codes remain authoritative. */
    for (size_t i = 0U; i < sizeof(s_error_codes) / sizeof(s_error_codes[0]); ++i) {
        if (strcmp(name, s_error_codes[i].code) == 0) {
            if (s_error_codes[i].error == CONVERSATION_FEEDBACK_ERROR_NETWORK) {
                if (local_error == CONVERSATION_FEEDBACK_LOCAL_TIMEOUT) {
                    return CONVERSATION_FEEDBACK_ERROR_TIMEOUT;
                }
                if (local_error == CONVERSATION_FEEDBACK_LOCAL_RESOURCE) {
                    return CONVERSATION_FEEDBACK_ERROR_RESOURCE;
                }
            }
            return s_error_codes[i].error;
        }
    }
    switch (local_error) {
    case CONVERSATION_FEEDBACK_LOCAL_TIMEOUT:
        return CONVERSATION_FEEDBACK_ERROR_TIMEOUT;
    case CONVERSATION_FEEDBACK_LOCAL_RESOURCE:
        return CONVERSATION_FEEDBACK_ERROR_RESOURCE;
    case CONVERSATION_FEEDBACK_LOCAL_NONE:
        return name[0] == '\0' && service_code == 0
                   ? CONVERSATION_FEEDBACK_ERROR_NONE
                   : CONVERSATION_FEEDBACK_ERROR_UNKNOWN;
    case CONVERSATION_FEEDBACK_LOCAL_FAILURE:
    default:
        return CONVERSATION_FEEDBACK_ERROR_UNKNOWN;
    }
}

const char *conversation_feedback_error_name(conversation_feedback_error_t error)
{
    switch (error) {
    case CONVERSATION_FEEDBACK_ERROR_NONE: return "none";
    case CONVERSATION_FEEDBACK_ERROR_AUTH: return "authentication";
    case CONVERSATION_FEEDBACK_ERROR_ACCESS: return "access";
    case CONVERSATION_FEEDBACK_ERROR_MODEL: return "model";
    case CONVERSATION_FEEDBACK_ERROR_LIMIT: return "rate_limit";
    case CONVERSATION_FEEDBACK_ERROR_TIMEOUT: return "timeout";
    case CONVERSATION_FEEDBACK_ERROR_NETWORK: return "network";
    case CONVERSATION_FEEDBACK_ERROR_TLS: return "tls";
    case CONVERSATION_FEEDBACK_ERROR_SERVICE: return "service";
    case CONVERSATION_FEEDBACK_ERROR_RESOURCE: return "resource";
    case CONVERSATION_FEEDBACK_ERROR_CLOSED: return "connection_closed";
    default: return "unknown";
    }
}

const char *conversation_feedback_error_hint(conversation_feedback_error_t error)
{
    switch (error) {
    case CONVERSATION_FEEDBACK_ERROR_NONE: return "";
    case CONVERSATION_FEEDBACK_ERROR_AUTH: return "Check API Key and Host";
    case CONVERSATION_FEEDBACK_ERROR_ACCESS: return "Check access and API Host";
    case CONVERSATION_FEEDBACK_ERROR_MODEL: return "Check model availability";
    case CONVERSATION_FEEDBACK_ERROR_LIMIT: return "Too many requests; try later";
    case CONVERSATION_FEEDBACK_ERROR_TIMEOUT: return "Timed out; please try again";
    case CONVERSATION_FEEDBACK_ERROR_NETWORK: return "Check Wi-Fi and Internet";
    case CONVERSATION_FEEDBACK_ERROR_TLS: return "Check time and network";
    case CONVERSATION_FEEDBACK_ERROR_SERVICE: return "Service busy; try again later";
    case CONVERSATION_FEEDBACK_ERROR_RESOURCE: return "Device busy; please try again";
    case CONVERSATION_FEEDBACK_ERROR_CLOSED: return "Connection closed; try again";
    default: return "Please try again later";
    }
}

const char *conversation_feedback_error_hint_zh(conversation_feedback_error_t error)
{
    switch (error) {
    case CONVERSATION_FEEDBACK_ERROR_NONE: return "";
    case CONVERSATION_FEEDBACK_ERROR_AUTH: return "身份验证失败，请检查 API Key 及其对应地域的 Host。";
    case CONVERSATION_FEEDBACK_ERROR_ACCESS: return "访问被拒绝，请检查百炼控制台中的权限、账户状态和 API Host。";
    case CONVERSATION_FEEDBACK_ERROR_MODEL: return "模型不可用，请检查该地域的模型开放情况和访问权限。";
    case CONVERSATION_FEEDBACK_ERROR_LIMIT: return "请求受限，请稍后重试，并检查百炼控制台中的调用限额。";
    case CONVERSATION_FEEDBACK_ERROR_TIMEOUT: return "等待响应超时，请检查网络后重试。";
    case CONVERSATION_FEEDBACK_ERROR_NETWORK: return "连接中断或无法建立，请检查 Wi-Fi 和互联网连接。";
    case CONVERSATION_FEEDBACK_ERROR_TLS: return "安全连接未能建立，请检查设备时间和网络后重试。";
    case CONVERSATION_FEEDBACK_ERROR_SERVICE: return "服务暂不可用，请稍后重试。";
    case CONVERSATION_FEEDBACK_ERROR_RESOURCE: return "设备资源暂不可用，请结束其他操作后重试。";
    case CONVERSATION_FEEDBACK_ERROR_CLOSED: return "服务端关闭了会话，请稍后重试；若持续失败，请查看连接诊断。";
    default: return "对话未能完成，请稍后重试；若持续失败，请检查配置或更新固件。";
    }
}

const char *conversation_feedback_result_name(conversation_feedback_result_t result)
{
    switch (result) {
    case CONVERSATION_FEEDBACK_RESULT_SUCCESS: return "success";
    case CONVERSATION_FEEDBACK_RESULT_FAILED: return "failed";
    case CONVERSATION_FEEDBACK_RESULT_CANCELLED: return "cancelled";
    default: return "not_tested";
    }
}

void conversation_feedback_complete(
    conversation_feedback_status_t *status, uint32_t config_generation,
    uint8_t completed_turns, bool cancelled,
    conversation_feedback_error_t error)
{
    if (status == NULL) {
        return;
    }
    *status = (conversation_feedback_status_t){
        .config_generation = config_generation,
        .completed_turns = completed_turns,
    };
    if (cancelled) {
        status->result = CONVERSATION_FEEDBACK_RESULT_CANCELLED;
    } else if (error != CONVERSATION_FEEDBACK_ERROR_NONE) {
        status->result = CONVERSATION_FEEDBACK_RESULT_FAILED;
        status->error = error;
    } else if (completed_turns > 0U) {
        status->result = CONVERSATION_FEEDBACK_RESULT_SUCCESS;
    } else {
        /* A clean close before any complete answer (or skipping all replies)
         * must not claim that an answer was successfully played. */
        status->result = CONVERSATION_FEEDBACK_RESULT_CANCELLED;
    }
}

conversation_feedback_status_t conversation_feedback_for_generation(
    const conversation_feedback_status_t *status, uint32_t current_generation)
{
    if (status == NULL || current_generation == 0U ||
        status->config_generation != current_generation) {
        return (conversation_feedback_status_t){0};
    }
    return *status;
}
