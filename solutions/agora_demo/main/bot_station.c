#include "bot_station.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "settings.h"

#define TAG "BOT_STATION"
#define NVS_NAMESPACE "bot_station"
#define NVS_DEVICE_TOKEN_KEY "device_token"
#define DEVICE_ID_SIZE 65
#define DEVICE_TOKEN_SIZE 1536
#define PAIR_TOKEN_SIZE 1536
#define PAIR_CODE_SIZE 7
#define HTTP_TIMEOUT_MS 15000
#define HTTP_BUFFER_SIZE 2048
#define MAX_HTTP_BODY_SIZE (16 * 1024)
#define DEFAULT_PAIR_POLL_SECONDS 3
#define MIN_RUNTIME_POLL_SECONDS 30
#define MAX_BACKOFF_SECONDS 30

typedef struct {
    char *body;
    size_t body_size;
    size_t body_capacity;
    esp_err_t callback_error;
    int status;
} http_response_t;

typedef enum {
    BINDING_BOUND,
    BINDING_PENDING,
    BINDING_REPAIR,
    BINDING_TRANSIENT,
    BINDING_FATAL,
} binding_result_t;

static char device_id[DEVICE_ID_SIZE];
static char device_token[DEVICE_TOKEN_SIZE];
static char pair_token[PAIR_TOKEN_SIZE];
static char bound_agent_id[BOT_STATION_AGENT_ID_SIZE];
static char active_conversation_id[BOT_STATION_CONVERSATION_ID_SIZE];
static uint32_t pair_poll_seconds = DEFAULT_PAIR_POLL_SECONDS;
static uint32_t runtime_poll_seconds = MIN_RUNTIME_POLL_SECONDS;
static int64_t next_binding_check_us;
static volatile bool cancelled;
static bool initialized;
static SemaphoreHandle_t state_mutex;

static bool lock_state(void)
{
    return state_mutex != NULL &&
        xSemaphoreTakeRecursive(state_mutex, portMAX_DELAY) == pdTRUE;
}

static void unlock_state(void)
{
    xSemaphoreGiveRecursive(state_mutex);
}

static void clear_secret(char *value, size_t size)
{
    volatile char *p = value;
    while (size-- > 0) {
        *p++ = 0;
    }
}

static bool copy_string(char *destination, size_t destination_size,
                        const char *source)
{
    if (destination == NULL || destination_size == 0 || source == NULL ||
        source[0] == '\0') {
        return false;
    }
    size_t size = strlen(source);
    if (size >= destination_size) {
        return false;
    }
    memcpy(destination, source, size + 1);
    return true;
}

static bool is_pair_code(const char *value)
{
    if (value == NULL || strlen(value) != 6) {
        return false;
    }
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

static bool is_rtc_uid(const char *value)
{
    if (value == NULL || value[0] == '\0' ||
        strlen(value) >= BOT_STATION_UID_SIZE) {
        return false;
    }
    for (const char *p = value; *p != '\0'; ++p) {
        bool valid = (*p >= 'A' && *p <= 'Z') ||
                     (*p >= 'a' && *p <= 'z') ||
                     (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
                     *p == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

static uint32_t poll_seconds_from_json(const cJSON *data, bool runtime)
{
    uint32_t fallback = runtime ? MIN_RUNTIME_POLL_SECONDS :
                                  DEFAULT_PAIR_POLL_SECONDS;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(
        data, "poll_after_seconds");
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0 ||
        item->valuedouble > UINT32_MAX) {
        return fallback;
    }
    uint32_t seconds = (uint32_t)item->valuedouble;
    if (runtime && seconds < MIN_RUNTIME_POLL_SECONDS) {
        seconds = MIN_RUNTIME_POLL_SECONDS;
    }
    return seconds;
}

static bool sleep_cancellable(uint32_t seconds)
{
    for (uint32_t elapsed = 0; elapsed < seconds && !cancelled; ++elapsed) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return !cancelled;
}

static void free_http_response(http_response_t *response)
{
    if (response->body != NULL) {
        clear_secret(response->body, response->body_capacity);
    }
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    size_t needed = response->body_size + (size_t)event->data_len + 1;
    if (needed > MAX_HTTP_BODY_SIZE) {
        response->callback_error = ESP_ERR_INVALID_SIZE;
        return ESP_FAIL;
    }
    if (needed > response->body_capacity) {
        size_t capacity = response->body_capacity ?
                          response->body_capacity * 2 : HTTP_BUFFER_SIZE;
        while (capacity < needed) {
            capacity *= 2;
        }
        if (capacity > MAX_HTTP_BODY_SIZE) {
            capacity = MAX_HTTP_BODY_SIZE;
        }
        char *body = realloc(response->body, capacity);
        if (body == NULL) {
            response->callback_error = ESP_ERR_NO_MEM;
            return ESP_FAIL;
        }
        response->body = body;
        response->body_capacity = capacity;
    }
    memcpy(response->body + response->body_size, event->data,
           event->data_len);
    response->body_size += (size_t)event->data_len;
    response->body[response->body_size] = '\0';
    return ESP_OK;
}

static esp_err_t build_url(const char *path, char *url, size_t url_size)
{
    size_t base_size = strlen(AGORA_DEMO_BOT_STATION_BASE_URL);
    while (base_size > 0 &&
           AGORA_DEMO_BOT_STATION_BASE_URL[base_size - 1] == '/') {
        --base_size;
    }
    int written = snprintf(url, url_size, "%.*s%s", (int)base_size,
                           AGORA_DEMO_BOT_STATION_BASE_URL, path);
    if (written < 0 || (size_t)written >= url_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t http_request(esp_http_client_method_t method,
                              const char *path, const char *auth_scheme,
                              const char *auth_token, const char *body,
                              http_response_t *response)
{
    char url[512];
    esp_err_t ret = build_url(path, url, sizeof(url));
    if (ret != ESP_OK) {
        return ret;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = HTTP_BUFFER_SIZE,
        .buffer_size_tx = HTTP_BUFFER_SIZE,
        .max_redirection_count = 3,
        .keep_alive_enable = true,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *authorization = NULL;
    if (auth_scheme != NULL && auth_token != NULL) {
        size_t size = strlen(auth_scheme) + 1 + strlen(auth_token) + 1;
        authorization = malloc(size);
        if (authorization == NULL) {
            ret = ESP_ERR_NO_MEM;
        } else {
            snprintf(authorization, size, "%s %s", auth_scheme,
                     auth_token);
            ret = esp_http_client_set_header(client, "Authorization",
                                             authorization);
        }
    }
    if (ret == ESP_OK && body != NULL) {
        ret = esp_http_client_set_header(
            client, "Content-Type", "application/json; charset=utf-8");
    }
    if (ret == ESP_OK && body != NULL) {
        ret = esp_http_client_set_post_field(client, body, strlen(body));
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_perform(client);
    }
    if (ret == ESP_OK && response->callback_error != ESP_OK) {
        ret = response->callback_error;
    }
    if (ret == ESP_OK) {
        response->status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "%s returned HTTP %d", path, response->status);
    } else {
        ESP_LOGE(TAG, "%s request failed: %s", path,
                 esp_err_to_name(ret));
    }

    if (authorization != NULL) {
        clear_secret(authorization, strlen(authorization));
        free(authorization);
    }
    esp_http_client_cleanup(client);
    return ret;
}

static cJSON *parse_response_data(const http_response_t *response,
                                  cJSON **root)
{
    if (response->body == NULL) {
        return NULL;
    }
    *root = cJSON_Parse(response->body);
    if (*root == NULL) {
        return NULL;
    }
    cJSON *data = cJSON_GetObjectItemCaseSensitive(*root, "data");
    return cJSON_IsObject(data) ? data : NULL;
}

static void get_error_code(const http_response_t *response, char *code,
                           size_t code_size)
{
    code[0] = '\0';
    if (response->body == NULL) {
        return;
    }
    cJSON *root = cJSON_Parse(response->body);
    cJSON *error = root ? cJSON_GetObjectItemCaseSensitive(root, "error") :
                          NULL;
    cJSON *item = cJSON_IsObject(error) ?
                  cJSON_GetObjectItemCaseSensitive(error, "code") : NULL;
    if (cJSON_IsString(item)) {
        copy_string(code, code_size, item->valuestring);
    }
    cJSON_Delete(root);
}

static bool is_transient_response(const http_response_t *response)
{
    return response->status == 429 || response->status == 502 ||
           response->status == 503;
}

static bool is_auth_response(const http_response_t *response)
{
    return response->status == 401;
}

static void log_api_error(const char *operation,
                          const http_response_t *response)
{
    char code[64];
    get_error_code(response, code, sizeof(code));
    ESP_LOGE(TAG, "%s failed: HTTP %d code=%s", operation,
             response->status, code[0] ? code : "unknown");
}

static esp_err_t erase_device_token(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_key(nvs, NVS_DEVICE_TOKEN_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    clear_secret(device_token, sizeof(device_token));
    clear_secret(bound_agent_id, sizeof(bound_agent_id));
    next_binding_check_us = 0;
    return ret;
}

static esp_err_t save_device_token(const char *token)
{
    if (token == NULL || token[0] == '\0' ||
        strlen(token) >= sizeof(device_token)) {
        ESP_LOGE(TAG, "Device credential is empty or exceeds %u bytes",
                 (unsigned)(sizeof(device_token) - 1));
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_handle_t nvs = 0;
    ESP_LOGI(TAG, "Opening NVS namespace for device credential");
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NVS namespace opened; writing device credential");
    ret = nvs_set_str(nvs, NVS_DEVICE_TOKEN_KEY, token);
    ESP_LOGI(TAG, "nvs_set_str completed: %s", esp_err_to_name(ret));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Committing device credential to NVS");
        ret = nvs_commit(nvs);
        ESP_LOGI(TAG, "nvs_commit completed: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "Closing device credential NVS handle");
    nvs_close(nvs);
    if (ret == ESP_OK) {
        if (!copy_string(device_token, sizeof(device_token), token)) {
            ESP_LOGE(TAG, "Failed to copy saved device credential into RAM");
            return ESP_ERR_INVALID_SIZE;
        }
        ESP_LOGI(TAG, "Device credential is active in RAM");
    }
    return ret;
}

static esp_err_t load_device_token(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t size = sizeof(device_token);
    ret = nvs_get_str(nvs, NVS_DEVICE_TOKEN_KEY, device_token, &size);
    nvs_close(nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        clear_secret(device_token, sizeof(device_token));
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        clear_secret(device_token, sizeof(device_token));
    }
    return ret;
}

static esp_err_t create_json_payload(cJSON *root, char **payload)
{
    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *payload ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t request_pair_code(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "device_id", device_id) == NULL ||
        cJSON_AddStringToObject(root, "firmware_version",
                               esp_app_get_description()->version) == NULL ||
        cJSON_AddStringToObject(root, "hardware_model",
                               AGORA_DEMO_HARDWARE_MODEL) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *payload = NULL;
    esp_err_t ret = create_json_payload(root, &payload);
    if (ret != ESP_OK) {
        return ret;
    }

    http_response_t response = {};
    ret = http_request(HTTP_METHOD_POST, "/devices/pair-codes", NULL, NULL,
                       payload, &response);
    cJSON_free(payload);
    if (ret != ESP_OK) {
        free_http_response(&response);
        return ret;
    }
    if (response.status != 201) {
        char code[64];
        get_error_code(&response, code, sizeof(code));
        if (is_transient_response(&response) ||
            (response.status == 409 &&
             strcmp(code, "PAIRING_CLAIM_IN_PROGRESS") == 0)) {
            ESP_LOGW(TAG, "Pair-code request deferred: HTTP %d code=%s",
                     response.status, code[0] ? code : "unknown");
            free_http_response(&response);
            return ESP_ERR_TIMEOUT;
        }
        log_api_error("Pair-code request", &response);
        free_http_response(&response);
        return ESP_FAIL;
    }

    cJSON *json = NULL;
    cJSON *data = parse_response_data(&response, &json);
    cJSON *code = data ? cJSON_GetObjectItemCaseSensitive(data, "code") :
                         NULL;
    cJSON *token = data ?
        cJSON_GetObjectItemCaseSensitive(data, "pair_token") : NULL;
    cJSON *response_device_id = data ?
        cJSON_GetObjectItemCaseSensitive(data, "device_id") : NULL;
    cJSON *status = data ?
        cJSON_GetObjectItemCaseSensitive(data, "status") : NULL;
    if (cancelled) {
        ret = ESP_ERR_INVALID_STATE;
    } else if (!cJSON_IsString(code) || !is_pair_code(code->valuestring) ||
        !cJSON_IsString(token) ||
        !cJSON_IsString(response_device_id) ||
        strcmp(response_device_id->valuestring, device_id) != 0 ||
        !cJSON_IsString(status) || strcmp(status->valuestring, "active") != 0 ||
        !copy_string(pair_token, sizeof(pair_token), token->valuestring)) {
        ESP_LOGE(TAG, "Pair-code response is incomplete");
        ret = ESP_ERR_INVALID_RESPONSE;
    } else {
        pair_poll_seconds = poll_seconds_from_json(data, false);
        char pairing_code[PAIR_CODE_SIZE];
        copy_string(pairing_code, sizeof(pairing_code), code->valuestring);
        printf("\nBot Station pairing code: %s\n\n", pairing_code);
        clear_secret(pairing_code, sizeof(pairing_code));
        ESP_LOGI(TAG, "Waiting for this device to be claimed");
    }
    if (cJSON_IsString(code)) {
        clear_secret(code->valuestring, strlen(code->valuestring));
    }
    if (cJSON_IsString(token)) {
        clear_secret(token->valuestring, strlen(token->valuestring));
    }
    cJSON_Delete(json);
    free_http_response(&response);
    return ret;
}

static binding_result_t request_binding_status(bool runtime)
{
    const char *token = runtime ? device_token : pair_token;
    const char *scheme = runtime ? "Device" : "Pair";
    char path[160];
    int written = snprintf(path, sizeof(path),
                           "/devices/%s/binding-status", device_id);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return BINDING_FATAL;
    }

    http_response_t response = {};
    esp_err_t ret = http_request(HTTP_METHOD_GET, path, scheme, token, NULL,
                                 &response);
    if (ret != ESP_OK) {
        free_http_response(&response);
        return BINDING_FATAL;
    }
    if (response.status != 200) {
        if (is_auth_response(&response)) {
            log_api_error("Binding check authentication", &response);
            if (runtime) {
                erase_device_token();
            } else {
                clear_secret(pair_token, sizeof(pair_token));
            }
            free_http_response(&response);
            return BINDING_REPAIR;
        }
        if (is_transient_response(&response)) {
            log_api_error("Binding check deferred", &response);
            free_http_response(&response);
            return BINDING_TRANSIENT;
        }
        log_api_error("Binding check", &response);
        free_http_response(&response);
        return BINDING_FATAL;
    }

    cJSON *json = NULL;
    cJSON *data = parse_response_data(&response, &json);
    cJSON *status = data ?
        cJSON_GetObjectItemCaseSensitive(data, "status") : NULL;
    cJSON *response_device_id = data ?
        cJSON_GetObjectItemCaseSensitive(data, "device_id") : NULL;
    bool status_present = cJSON_IsString(status);
    bool device_id_present = cJSON_IsString(response_device_id);
    bool device_id_matches = device_id_present &&
        strcmp(response_device_id->valuestring, device_id) == 0;
    ESP_LOGI(TAG,
             "Binding response auth=%s body=%u data=%s status=%s "
             "device_id=%s",
             scheme, (unsigned)response.body_size,
             cJSON_IsObject(data) ? "present" : "missing",
             status_present ? status->valuestring : "missing",
             device_id_present ?
                 (device_id_matches ? "match" : "mismatch") : "missing");
    if (!status_present || !device_id_matches) {
        ESP_LOGE(TAG, "Rejecting malformed binding identity fields");
        ret = ESP_ERR_INVALID_RESPONSE;
    } else if (strcmp(status->valuestring, "pending") == 0 && !runtime) {
        pair_poll_seconds = poll_seconds_from_json(data, false);
        ESP_LOGI(TAG, "Pairing pending; next poll in %u seconds",
                 (unsigned)pair_poll_seconds);
        ret = ESP_ERR_NOT_FINISHED;
    } else if (strcmp(status->valuestring, "bound") == 0) {
        cJSON *agent = cJSON_GetObjectItemCaseSensitive(data, "agent_id");
        cJSON *token_item = cJSON_GetObjectItemCaseSensitive(
            data, "device_token");
        size_t token_size = cJSON_IsString(token_item) ?
                            strlen(token_item->valuestring) : 0;
        ESP_LOGI(TAG,
                 "Bound response agent_id=%s device_token=%s size=%u",
                 cJSON_IsString(agent) ? "present" : "missing",
                 cJSON_IsString(token_item) ? "present" :
                     (runtime ? "not-required" : "missing"),
                 (unsigned)token_size);
        if (!cJSON_IsString(agent) ||
            !copy_string(bound_agent_id, sizeof(bound_agent_id),
                         agent->valuestring)) {
            ESP_LOGE(TAG, "Agent ID is missing or too large");
            ret = ESP_ERR_INVALID_RESPONSE;
        } else if (!runtime && cancelled) {
            ESP_LOGW(TAG, "Binding delivery ignored because pairing was cancelled");
            ret = ESP_ERR_INVALID_STATE;
        } else if (!runtime) {
            if (!cJSON_IsString(token_item)) {
                ESP_LOGE(TAG, "Bound pairing response omitted device_token");
                ret = ESP_ERR_INVALID_RESPONSE;
            } else {
                ESP_LOGI(TAG, "Saving device credential to NVS");
                ret = save_device_token(token_item->valuestring);
                clear_secret(token_item->valuestring,
                             strlen(token_item->valuestring));
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Device credential saved to NVS");
                } else {
                    ESP_LOGE(TAG, "Failed to save device credential: %s",
                             esp_err_to_name(ret));
                }
            }
        } else {
            ESP_LOGI(TAG, "Runtime binding remains valid");
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            runtime_poll_seconds = poll_seconds_from_json(data, true);
            ESP_LOGI(TAG, "Runtime binding check scheduled in %u seconds",
                     (unsigned)runtime_poll_seconds);
            next_binding_check_us = esp_timer_get_time() +
                (int64_t)runtime_poll_seconds * 1000 * 1000;
        }
    } else if (strcmp(status->valuestring, "expired") == 0 ||
               strcmp(status->valuestring, "unbound") == 0) {
        ESP_LOGW(TAG, "Binding status=%s; returning to pairing",
                 status->valuestring);
        if (runtime) {
            erase_device_token();
        }
        clear_secret(pair_token, sizeof(pair_token));
        ret = ESP_ERR_INVALID_STATE;
    } else {
        ESP_LOGE(TAG, "Unexpected binding status=%s for auth=%s",
                 status->valuestring, scheme);
        ret = ESP_ERR_INVALID_RESPONSE;
    }

    cJSON_Delete(json);
    free_http_response(&response);
    if (ret == ESP_OK) {
        return BINDING_BOUND;
    }
    if (ret == ESP_ERR_NOT_FINISHED) {
        return BINDING_PENDING;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        return BINDING_REPAIR;
    }
    ESP_LOGE(TAG, "Binding response is incomplete or inconsistent");
    return BINDING_FATAL;
}

static esp_err_t ensure_binding(void)
{
    if (device_token[0] != '\0') {
        binding_result_t result = request_binding_status(true);
        if (result == BINDING_BOUND) {
            ESP_LOGI(TAG, "Restored Bot Station binding from NVS");
            return ESP_OK;
        }
        if (result != BINDING_REPAIR) {
            return ESP_FAIL;
        }
    }

    uint32_t backoff_seconds = 1;
    while (!cancelled) {
        if (pair_token[0] == '\0') {
            esp_err_t ret = request_pair_code();
            if (ret == ESP_ERR_TIMEOUT) {
                if (!sleep_cancellable(backoff_seconds)) {
                    break;
                }
                backoff_seconds *= 2;
                if (backoff_seconds > MAX_BACKOFF_SECONDS) {
                    backoff_seconds = MAX_BACKOFF_SECONDS;
                }
                continue;
            }
            if (ret != ESP_OK) {
                return ret;
            }
            backoff_seconds = 1;
            if (!sleep_cancellable(pair_poll_seconds)) {
                break;
            }
        }

        binding_result_t result = request_binding_status(false);
        if (result == BINDING_BOUND) {
            clear_secret(pair_token, sizeof(pair_token));
            ESP_LOGI(TAG, "Pairing completed; device credential saved");
            return ESP_OK;
        }
        if (result == BINDING_PENDING) {
            backoff_seconds = 1;
            if (!sleep_cancellable(pair_poll_seconds)) {
                break;
            }
        } else if (result == BINDING_TRANSIENT) {
            if (!sleep_cancellable(backoff_seconds)) {
                break;
            }
            backoff_seconds *= 2;
            if (backoff_seconds > MAX_BACKOFF_SECONDS) {
                backoff_seconds = MAX_BACKOFF_SECONDS;
            }
        } else if (result == BINDING_REPAIR) {
            backoff_seconds = 1;
        } else {
            return ESP_FAIL;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t parse_conversation(const http_response_t *response,
                                    bot_station_conversation_t *conversation)
{
    cJSON *root = NULL;
    cJSON *data = parse_response_data(response, &root);
    cJSON *rtc = data ? cJSON_GetObjectItemCaseSensitive(data, "rtc") : NULL;
    cJSON *conversation_id = data ? cJSON_GetObjectItemCaseSensitive(
        data, "conversation_id") : NULL;
    cJSON *agent_id = data ?
        cJSON_GetObjectItemCaseSensitive(data, "agent_id") : NULL;
    cJSON *agent_uid = data ?
        cJSON_GetObjectItemCaseSensitive(data, "agent_uid") : NULL;
    cJSON *local_uid = data ?
        cJSON_GetObjectItemCaseSensitive(data, "local_uid") : NULL;
    cJSON *data_channel = data ?
        cJSON_GetObjectItemCaseSensitive(data, "channel") : NULL;
    cJSON *response_device_id = data ?
        cJSON_GetObjectItemCaseSensitive(data, "device_id") : NULL;
    cJSON *status = data ?
        cJSON_GetObjectItemCaseSensitive(data, "status") : NULL;
    cJSON *app_id = cJSON_IsObject(rtc) ?
        cJSON_GetObjectItemCaseSensitive(rtc, "app_id") : NULL;
    cJSON *channel = cJSON_IsObject(rtc) ?
        cJSON_GetObjectItemCaseSensitive(rtc, "channel") : NULL;
    cJSON *uid = cJSON_IsObject(rtc) ?
        cJSON_GetObjectItemCaseSensitive(rtc, "uid") : NULL;
    cJSON *rtc_token = cJSON_IsObject(rtc) ?
        cJSON_GetObjectItemCaseSensitive(rtc, "token") : NULL;

    if (cJSON_IsString(conversation_id)) {
        copy_string(conversation->conversation_id,
                    sizeof(conversation->conversation_id),
                    conversation_id->valuestring);
    }
    bool valid = conversation->conversation_id[0] != '\0' &&
        cJSON_IsString(agent_id) && cJSON_IsString(agent_uid) &&
        cJSON_IsString(local_uid) && cJSON_IsString(data_channel) &&
        cJSON_IsString(response_device_id) && cJSON_IsString(status) &&
        cJSON_IsString(app_id) && cJSON_IsString(channel) &&
        cJSON_IsString(uid) &&
        strcmp(response_device_id->valuestring, device_id) == 0 &&
        strcmp(agent_id->valuestring, bound_agent_id) == 0 &&
        strcmp(status->valuestring, "started") == 0 &&
        strcmp(app_id->valuestring, AGORA_DEMO_APP_ID) == 0 &&
        strcmp(local_uid->valuestring, uid->valuestring) == 0 &&
        strcmp(data_channel->valuestring, channel->valuestring) == 0 &&
        is_rtc_uid(uid->valuestring) && is_rtc_uid(agent_uid->valuestring) &&
        copy_string(conversation->agent_id, sizeof(conversation->agent_id),
                    agent_id->valuestring) &&
        copy_string(conversation->agent_uid,
                    sizeof(conversation->agent_uid),
                    agent_uid->valuestring) &&
        copy_string(conversation->app_id, sizeof(conversation->app_id),
                    app_id->valuestring) &&
        copy_string(conversation->channel, sizeof(conversation->channel),
                    channel->valuestring) &&
        copy_string(conversation->uid, sizeof(conversation->uid),
                    uid->valuestring);
    if (cJSON_IsString(rtc_token)) {
        clear_secret(rtc_token->valuestring, strlen(rtc_token->valuestring));
    }
    cJSON_Delete(root);
    if (!valid) {
        ESP_LOGE(TAG, "Conversation response failed identity validation");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t bot_station_init(void)
{
    if (AGORA_DEMO_BOT_STATION_BASE_URL[0] == '\0') {
        ESP_LOGE(TAG, "Configure the Bot Station API base URL");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    int written = snprintf(device_id, sizeof(device_id),
                           "AG-%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
                           mac[2], mac[3], mac[4], mac[5]);
    if (written < 0 || (size_t)written >= sizeof(device_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    state_mutex = xSemaphoreCreateRecursiveMutex();
    if (state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ret = load_device_token();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load device credential: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    initialized = true;
    ESP_LOGI(TAG, "Device ID: %s", device_id);
    ESP_LOGI(TAG, "Persistent Bot Station credential: %s",
             device_token[0] ? "present" : "not present");
    return ESP_OK;
}

void bot_station_prepare_start(void)
{
    cancelled = false;
}

void bot_station_cancel(void)
{
    cancelled = true;
}

static esp_err_t start_conversation_locked(
    bot_station_conversation_t *conversation)
{
    if (!initialized || conversation == NULL || cancelled) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(conversation, 0, sizeof(*conversation));
    if (active_conversation_id[0] != '\0') {
        esp_err_t ret = bot_station_stop_conversation("error");
        if (ret != ESP_OK) {
            return ret;
        }
    }

    esp_err_t ret = ensure_binding();
    if (ret != ESP_OK || cancelled) {
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *audio = root ? cJSON_AddObjectToObject(root, "audio") : NULL;
    if (root == NULL ||
        cJSON_AddStringToObject(root, "trigger", "manual") == NULL ||
        audio == NULL ||
        cJSON_AddNumberToObject(audio, "p_time", 20) == NULL ||
        cJSON_AddStringToObject(audio, "codec", "OPUS") == NULL ||
        cJSON_AddStringToObject(root, "firmware_version",
                               esp_app_get_description()->version) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *payload = NULL;
    ret = create_json_payload(root, &payload);
    if (ret != ESP_OK) {
        return ret;
    }

    char path[176];
    int written = snprintf(path, sizeof(path),
        "/devices/%s/conversations/start", device_id);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        cJSON_free(payload);
        return ESP_ERR_INVALID_SIZE;
    }
    http_response_t response = {};
    ret = http_request(HTTP_METHOD_POST, path, "Device", device_token,
                       payload, &response);
    cJSON_free(payload);
    if (ret != ESP_OK) {
        free_http_response(&response);
        return ret;
    }
    if (response.status != 201) {
        char code[64];
        get_error_code(&response, code, sizeof(code));
        if (is_auth_response(&response) ||
            (response.status == 409 &&
             strcmp(code, "DEVICE_NOT_BOUND") == 0)) {
            erase_device_token();
        }
        log_api_error("Conversation start", &response);
        free_http_response(&response);
        return ESP_FAIL;
    }

    ret = parse_conversation(&response, conversation);
    free_http_response(&response);
    if (ret != ESP_OK) {
        if (conversation->conversation_id[0] != '\0') {
            copy_string(active_conversation_id,
                        sizeof(active_conversation_id),
                        conversation->conversation_id);
            bot_station_stop_conversation("error");
        }
        memset(conversation, 0, sizeof(*conversation));
        return ret;
    }
    copy_string(active_conversation_id, sizeof(active_conversation_id),
                conversation->conversation_id);
    ESP_LOGI(TAG, "Conversation started for the bound agent");
    if (cancelled) {
        bot_station_stop_conversation("device_hangup");
        memset(conversation, 0, sizeof(*conversation));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t bot_station_start_conversation(
    bot_station_conversation_t *conversation)
{
    if (!lock_state()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = start_conversation_locked(conversation);
    unlock_state();
    return ret;
}

static bool valid_stop_reason(const char *reason)
{
    return reason != NULL &&
        (strcmp(reason, "user_requested") == 0 ||
         strcmp(reason, "device_hangup") == 0 ||
         strcmp(reason, "timeout") == 0 || strcmp(reason, "error") == 0);
}

static esp_err_t stop_conversation_locked(const char *reason)
{
    if (!initialized || !valid_stop_reason(reason)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (active_conversation_id[0] == '\0') {
        return ESP_OK;
    }
    if (device_token[0] == '\0') {
        active_conversation_id[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    char conversation_id[BOT_STATION_CONVERSATION_ID_SIZE];
    copy_string(conversation_id, sizeof(conversation_id),
                active_conversation_id);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "conversation_id",
                               conversation_id) == NULL ||
        cJSON_AddStringToObject(root, "reason", reason) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *payload = NULL;
    esp_err_t ret = create_json_payload(root, &payload);
    if (ret != ESP_OK) {
        return ret;
    }

    char path[176];
    int written = snprintf(path, sizeof(path),
        "/devices/%s/conversations/stop", device_id);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        cJSON_free(payload);
        return ESP_ERR_INVALID_SIZE;
    }
    http_response_t response = {};
    ret = http_request(HTTP_METHOD_POST, path, "Device", device_token,
                       payload, &response);
    cJSON_free(payload);
    if (ret == ESP_OK &&
        ((response.status >= 200 && response.status < 300) ||
         response.status == 404)) {
        if (strcmp(active_conversation_id, conversation_id) == 0) {
            active_conversation_id[0] = '\0';
        }
        ESP_LOGI(TAG, "Conversation stopped");
    } else if (ret == ESP_OK && is_auth_response(&response)) {
        log_api_error("Conversation stop authentication", &response);
        erase_device_token();
        active_conversation_id[0] = '\0';
        ret = ESP_FAIL;
    } else if (ret == ESP_OK) {
        log_api_error("Conversation stop", &response);
        ret = ESP_FAIL;
    }
    free_http_response(&response);
    return ret;
}

esp_err_t bot_station_stop_conversation(const char *reason)
{
    if (!lock_state()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = stop_conversation_locked(reason);
    unlock_state();
    return ret;
}

bool bot_station_has_active_conversation(void)
{
    return active_conversation_id[0] != '\0';
}

bool bot_station_binding_check_due(void)
{
    return initialized && device_token[0] != '\0' &&
        esp_timer_get_time() >= next_binding_check_us;
}

static esp_err_t validate_active_binding_locked(bool *valid)
{
    if (!initialized || valid == NULL || device_token[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    char previous_agent_id[BOT_STATION_AGENT_ID_SIZE];
    bool had_agent = bound_agent_id[0] != '\0';
    if (had_agent) {
        copy_string(previous_agent_id, sizeof(previous_agent_id),
                    bound_agent_id);
    }
    *valid = false;
    binding_result_t result = request_binding_status(true);
    if (result == BINDING_BOUND) {
        *valid = !had_agent ||
            strcmp(previous_agent_id, bound_agent_id) == 0;
        if (!*valid) {
            ESP_LOGW(TAG, "The device binding changed; restarting session");
        }
        return ESP_OK;
    }
    next_binding_check_us = esp_timer_get_time() +
        (int64_t)MIN_RUNTIME_POLL_SECONDS * 1000 * 1000;
    if (result == BINDING_REPAIR) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t bot_station_validate_active_binding(bool *valid)
{
    if (!lock_state()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = validate_active_binding_locked(valid);
    unlock_state();
    return ret;
}

static esp_err_t clear_binding_locked(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    clear_secret(pair_token, sizeof(pair_token));
    active_conversation_id[0] = '\0';
    esp_err_t ret = erase_device_token();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Local Bot Station credential cleared");
    }
    return ret;
}

esp_err_t bot_station_clear_binding(void)
{
    if (!lock_state()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = clear_binding_locked();
    unlock_state();
    return ret;
}
