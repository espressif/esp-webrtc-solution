#include "agora_signaling.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#define TAG "AGORA_SIGNAL"
#define HTTP_TIMEOUT_MS 15000
#define HTTP_BUFFER_SIZE 4096
#define MAX_HTTP_BODY_SIZE (64 * 1024)

typedef struct {
    uint8_t *body;
    size_t body_size;
    size_t body_capacity;
    char *location;
    char *ice_link;
    esp_err_t callback_error;
    int status;
} http_response_t;

typedef struct {
    esp_peer_signaling_cfg_t cfg;
    char *bearer_token;
    char *resource_url;
    esp_peer_ice_server_cfg_t ice_server;
    bool offer_sent;
} agora_signal_t;

static char *duplicate_range(const char *start, size_t size)
{
    char *copy = malloc(size + 1);
    if (copy != NULL) {
        memcpy(copy, start, size);
        copy[size] = '\0';
    }
    return copy;
}

static void free_ice_server(esp_peer_ice_server_cfg_t *server)
{
    free(server->stun_url);
    free(server->user);
    free(server->psw);
    memset(server, 0, sizeof(*server));
}

static void free_http_response(http_response_t *response)
{
    free(response->body);
    free(response->location);
    free(response->ice_link);
    memset(response, 0, sizeof(*response));
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;

    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(event->header_key, "Location") == 0) {
            char *location = strdup(event->header_value);
            if (location == NULL) {
                response->callback_error = ESP_ERR_NO_MEM;
                return ESP_FAIL;
            }
            free(response->location);
            response->location = location;
        } else if (response->ice_link == NULL &&
                   strcasecmp(event->header_key, "Link") == 0 &&
                   strstr(event->header_value, "rel=\"ice-server\"") != NULL) {
            response->ice_link = strdup(event->header_value);
            if (response->ice_link == NULL) {
                response->callback_error = ESP_ERR_NO_MEM;
                return ESP_FAIL;
            }
        }
    } else if (event->event_id == HTTP_EVENT_ON_DATA &&
               event->data_len > 0) {
        size_t needed = response->body_size + (size_t)event->data_len + 1;
        if (needed > MAX_HTTP_BODY_SIZE) {
            response->callback_error = ESP_ERR_INVALID_SIZE;
            return ESP_FAIL;
        }
        if (needed > response->body_capacity) {
            size_t capacity = response->body_capacity ?
                              response->body_capacity * 2 : 2048;
            while (capacity < needed) {
                capacity *= 2;
            }
            if (capacity > MAX_HTTP_BODY_SIZE) {
                capacity = MAX_HTTP_BODY_SIZE;
            }
            uint8_t *body = realloc(response->body, capacity);
            if (body == NULL) {
                response->callback_error = ESP_ERR_NO_MEM;
                return ESP_FAIL;
            }
            response->body = body;
            response->body_capacity = capacity;
        }
        memcpy(response->body + response->body_size, event->data,
               event->data_len);
        response->body_size += event->data_len;
        response->body[response->body_size] = '\0';
    }
    return ESP_OK;
}

static esp_err_t http_request(const char *method, const char *url,
                              const char *bearer_token,
                              const char *content_type,
                              const uint8_t *body, size_t body_size,
                              http_response_t *response)
{
    esp_http_client_config_t config = {
        .url = url,
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

    esp_err_t ret = ESP_OK;
    if (strcmp(method, "POST") == 0) {
        ret = esp_http_client_set_method(client, HTTP_METHOD_POST);
    } else if (strcmp(method, "PATCH") == 0) {
        ret = esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    } else if (strcmp(method, "DELETE") == 0) {
        ret = esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    } else {
        ret = ESP_ERR_INVALID_ARG;
    }

    char *authorization = NULL;
    if (ret == ESP_OK && bearer_token != NULL) {
        size_t size = strlen("Bearer ") + strlen(bearer_token) + 1;
        authorization = malloc(size);
        if (authorization == NULL) {
            ret = ESP_ERR_NO_MEM;
        } else {
            snprintf(authorization, size, "Bearer %s", bearer_token);
            ret = esp_http_client_set_header(client, "Authorization",
                                             authorization);
        }
    }
    if (ret == ESP_OK && content_type != NULL) {
        ret = esp_http_client_set_header(client, "Content-Type",
                                         content_type);
    }
    if (ret == ESP_OK && body != NULL && body_size > 0) {
        if (body_size > INT32_MAX) {
            ret = ESP_ERR_INVALID_SIZE;
        } else {
            ret = esp_http_client_set_post_field(
                client, (const char *)body, (int)body_size);
        }
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_perform(client);
    }
    if (ret == ESP_OK && response->callback_error != ESP_OK) {
        ret = response->callback_error;
    }
    if (ret == ESP_OK) {
        response->status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "%s %s returned HTTP %d", method, url,
                 response->status);
    } else {
        ESP_LOGE(TAG, "%s %s failed: %s", method, url,
                 esp_err_to_name(ret));
    }

    free(authorization);
    esp_http_client_cleanup(client);
    return ret;
}

static char *resolve_location(const char *request_url, const char *location)
{
    if (location == NULL || location[0] == '\0') {
        return NULL;
    }
    if (strncmp(location, "http://", 7) == 0 ||
        strncmp(location, "https://", 8) == 0) {
        return strdup(location);
    }

    const char *scheme = strstr(request_url, "://");
    if (scheme == NULL) {
        return NULL;
    }
    const char *authority_end = strchr(scheme + 3, '/');
    if (location[0] == '/') {
        size_t prefix_size = authority_end ?
                             (size_t)(authority_end - request_url) :
                             strlen(request_url);
        size_t size = prefix_size + strlen(location) + 1;
        char *result = malloc(size);
        if (result != NULL) {
            snprintf(result, size, "%.*s%s", (int)prefix_size, request_url,
                     location);
        }
        return result;
    }

    const char *query = strchr(request_url, '?');
    const char *end = query ? query : request_url + strlen(request_url);
    const char *last_slash = end;
    while (last_slash > scheme + 2 && last_slash[-1] != '/') {
        --last_slash;
    }
    size_t prefix_size = (size_t)(last_slash - request_url);
    size_t size = prefix_size + strlen(location) + 1;
    char *result = malloc(size);
    if (result != NULL) {
        snprintf(result, size, "%.*s%s", (int)prefix_size, request_url,
                 location);
    }
    return result;
}

static char *extract_quoted_parameter(const char *link, const char *name)
{
    const char *start = strstr(link, name);
    if (start == NULL) {
        return NULL;
    }
    start += strlen(name);
    const char *end = strchr(start, '"');
    return end ? duplicate_range(start, (size_t)(end - start)) : NULL;
}

static int parse_ice_link(agora_signal_t *signal, const char *link)
{
    if (link == NULL || strstr(link, "rel=\"ice-server\"") == NULL) {
        return 0;
    }
    const char *url_start = strchr(link, '<');
    const char *url_end = url_start ? strchr(url_start + 1, '>') : NULL;
    if (url_start == NULL || url_end == NULL) {
        ESP_LOGW(TAG, "Ignoring malformed ICE Link header");
        return -1;
    }

    esp_peer_ice_server_cfg_t server = {
        .stun_url = duplicate_range(url_start + 1,
                                    (size_t)(url_end - url_start - 1)),
        .user = extract_quoted_parameter(link, "username=\""),
        .psw = extract_quoted_parameter(link, "credential=\""),
    };
    if (server.stun_url == NULL) {
        free_ice_server(&server);
        return -1;
    }

    free_ice_server(&signal->ice_server);
    signal->ice_server = server;
    ESP_LOGI(TAG, "Using ICE server from WHIP Link: %s",
             signal->ice_server.stun_url);
    return 1;
}

static uint8_t *normalize_sdp(const uint8_t *input, size_t input_size,
                              size_t *output_size)
{
    size_t bare_lf_count = 0;
    for (size_t i = 0; i < input_size; ++i) {
        if (input[i] == '\n' && (i == 0 || input[i - 1] != '\r')) {
            ++bare_lf_count;
        }
    }

    uint8_t *output = malloc(input_size + bare_lf_count + 1);
    if (output == NULL) {
        return NULL;
    }
    size_t used = 0;
    for (size_t i = 0; i < input_size; ++i) {
        if (input[i] == '\n' && (i == 0 || input[i - 1] != '\r')) {
            output[used++] = '\r';
        }
        output[used++] = input[i];
    }
    output[used] = '\0';
    *output_size = used;
    return output;
}

static const uint8_t *find_bytes(const uint8_t *data, size_t data_size,
                                 const char *needle)
{
    size_t needle_size = strlen(needle);
    if (needle_size > data_size) {
        return NULL;
    }
    for (size_t i = 0; i <= data_size - needle_size; ++i) {
        if (memcmp(data + i, needle, needle_size) == 0) {
            return data + i;
        }
    }
    return NULL;
}

static int send_offer(agora_signal_t *signal,
                      const esp_peer_signaling_msg_t *message)
{
    if (message->data == NULL || message->size <= 0) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    http_response_t response = {};
    esp_err_t ret = http_request("POST", signal->cfg.signal_url,
                                 signal->bearer_token, "application/sdp",
                                 message->data, message->size, &response);
    if (ret != ESP_OK || response.status < 200 || response.status >= 300 ||
        response.body == NULL || response.body_size == 0) {
        ESP_LOGE(TAG, "WHIP offer failed: status=%d answer_size=%u",
                 response.status, (unsigned)response.body_size);
        free_http_response(&response);
        return ESP_PEER_ERR_FAIL;
    }

    char *resource_url = resolve_location(signal->cfg.signal_url,
                                          response.location);
    if (resource_url != NULL) {
        free(signal->resource_url);
        signal->resource_url = resource_url;
    } else {
        ESP_LOGW(TAG, "WHIP answer has no Location; PATCH/DELETE unavailable");
    }

    int ice_result = parse_ice_link(signal, response.ice_link);
    if (ice_result > 0) {
        esp_peer_signaling_ice_info_t ice_info = {
            .server_info = signal->ice_server,
            .is_initiator = true,
        };
        signal->cfg.on_ice_info(&ice_info, signal->cfg.ctx);
    }

    size_t normalized_size = 0;
    uint8_t *normalized = normalize_sdp(response.body, response.body_size,
                                        &normalized_size);
    if (normalized == NULL) {
        free_http_response(&response);
        return ESP_PEER_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Received SDP answer: %u bytes%s",
             (unsigned)normalized_size,
             normalized_size == response.body_size ? "" :
             " (normalized to CRLF)");

    esp_peer_signaling_msg_t answer = {
        .type = ESP_PEER_SIGNALING_MSG_SDP,
        .data = normalized,
        .size = (int)normalized_size,
    };
    int callback_ret = signal->cfg.on_msg(&answer, signal->cfg.ctx);
    free(normalized);
    free_http_response(&response);
    if (callback_ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "Peer rejected the SDP answer: %d", callback_ret);
        return callback_ret;
    }

    signal->offer_sent = true;
    return ESP_PEER_ERR_NONE;
}

static void send_trickle_ice(agora_signal_t *signal,
                             const esp_peer_signaling_msg_t *message)
{
    if (signal->resource_url == NULL || message->data == NULL ||
        message->size <= 0) {
        return;
    }
    const char *marker = "a=group:BUNDLE";
    const uint8_t *fragment = find_bytes(message->data, message->size, marker);
    if (fragment == NULL) {
        return;
    }
    size_t fragment_size = message->size - (size_t)(fragment - message->data);
    while (fragment_size > 0 && fragment[fragment_size - 1] == '\0') {
        --fragment_size;
    }

    http_response_t response = {};
    esp_err_t ret = http_request("PATCH", signal->resource_url,
                                 signal->bearer_token,
                                 "application/trickle-ice-sdpfrag",
                                 fragment, fragment_size, &response);
    if (ret != ESP_OK || response.status < 200 || response.status >= 300) {
        ESP_LOGW(TAG, "WHIP trickle PATCH failed: status=%d",
                 response.status);
    }
    free_http_response(&response);
}

static int signaling_start(esp_peer_signaling_cfg_t *cfg,
                           esp_peer_signaling_handle_t *handle)
{
    if (cfg == NULL || handle == NULL || cfg->signal_url == NULL ||
        cfg->on_ice_info == NULL || cfg->on_connected == NULL ||
        cfg->on_msg == NULL || cfg->on_close == NULL ||
        cfg->extra_cfg == NULL ||
        cfg->extra_size != sizeof(agora_signaling_cfg_t)) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    const agora_signaling_cfg_t *signaling_cfg = cfg->extra_cfg;
    if (signaling_cfg->bearer_token == NULL ||
        signaling_cfg->bearer_token[0] == '\0') {
        return ESP_PEER_ERR_INVALID_ARG;
    }

    agora_signal_t *signal = calloc(1, sizeof(*signal));
    if (signal == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    signal->cfg = *cfg;
    signal->bearer_token = strdup(signaling_cfg->bearer_token);
    if (signal->bearer_token == NULL) {
        free(signal);
        return ESP_PEER_ERR_NO_MEM;
    }
    *handle = signal;

    esp_peer_signaling_ice_info_t ice_info = {
        .is_initiator = true,
    };
    signal->cfg.on_ice_info(&ice_info, signal->cfg.ctx);
    signal->cfg.on_connected(signal->cfg.ctx);
    return ESP_PEER_ERR_NONE;
}

static int signaling_send_msg(esp_peer_signaling_handle_t handle,
                              esp_peer_signaling_msg_t *message)
{
    agora_signal_t *signal = handle;
    if (signal == NULL || message == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    if (message->type != ESP_PEER_SIGNALING_MSG_SDP) {
        return ESP_PEER_ERR_NONE;
    }
    if (!signal->offer_sent) {
        return send_offer(signal, message);
    }
    send_trickle_ice(signal, message);
    return ESP_PEER_ERR_NONE;
}

static int signaling_stop(esp_peer_signaling_handle_t handle)
{
    agora_signal_t *signal = handle;
    if (signal == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }

    if (signal->resource_url != NULL) {
        http_response_t response = {};
        esp_err_t ret = http_request("DELETE", signal->resource_url,
                                     signal->bearer_token, NULL, NULL, 0,
                                     &response);
        if (ret != ESP_OK || response.status < 200 ||
            response.status >= 300) {
            ESP_LOGW(TAG, "WHIP DELETE failed: status=%d", response.status);
        }
        free_http_response(&response);
    }

    signal->cfg.on_close(signal->cfg.ctx);
    free_ice_server(&signal->ice_server);
    free(signal->resource_url);
    free(signal->bearer_token);
    free(signal);
    return ESP_PEER_ERR_NONE;
}

const esp_peer_signaling_impl_t *agora_signaling_get_impl(void)
{
    static const esp_peer_signaling_impl_t implementation = {
        .start = signaling_start,
        .send_msg = signaling_send_msg,
        .stop = signaling_stop,
    };
    return &implementation;
}
