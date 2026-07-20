/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "https_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"

#include "webrtc_utils_time.h"
#include "sigv4.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include <mbedtls/private/sha256.h>
#else
#include <mbedtls/sha256.h>
#endif

static const char *TAG = "HTTPS_CLIENT";

// AWS credentials from Kconfig
const char *aws_access_key_id = CONFIG_KVS_AWS_ACCESS_KEY_ID;
const char *aws_secret_access_key = CONFIG_KVS_AWS_SECRET_ACCESS_KEY;
const char *aws_session_token = CONFIG_KVS_AWS_SESSION_TOKEN;

typedef struct {
    http_body_t body;
    uint8_t*    data;
    int         fill_size;
    int         size;
    void*       ctx;
} http_info_t;

/* TODO: Use credential provider for iot credentials */
#define AUTH_BUF_LENGTH 20000

mbedtls_sha256_context sha256_ctx;
static char   authBuf[AUTH_BUF_LENGTH];
static char * signature = NULL;
static size_t signatureLen;

static int32_t sha256_init(void *pHashContext)
{
    mbedtls_sha256_context *ctx = (mbedtls_sha256_context *)pHashContext;
    mbedtls_sha256_init(ctx);
    mbedtls_sha256_starts(ctx, false);
    return 0;
}

static int32_t sha256_update(void *pHashContext, const uint8_t *pInput, size_t inputLen)
{
    mbedtls_sha256_context *ctx = (mbedtls_sha256_context *)pHashContext;
    mbedtls_sha256_update(ctx, pInput, inputLen);
    return 0;
}

static int32_t sha256_finish(void *pHashContext, uint8_t *pOutput, size_t outputLen)
{
    mbedtls_sha256_context *ctx = (mbedtls_sha256_context *)pHashContext;
    mbedtls_sha256_finish(ctx, pOutput);
    return 0;
}

static SigV4CryptoInterface_t cryptoInterface = {
    .pHashContext = &sha256_ctx,
    .hashInit = sha256_init,
    .hashUpdate = sha256_update,
    .hashFinal = sha256_finish,
    .hashBlockLen = SIGV4_HASH_MAX_BLOCK_LENGTH,
    .hashDigestLen = SIGV4_HASH_MAX_DIGEST_LENGTH
};

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_info_t* info = evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
                     evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int content_len = esp_http_client_get_content_length(evt->client);
                if (info->data == NULL && content_len) {
                    info->data = heap_caps_calloc(1, content_len, MALLOC_CAP_SPIRAM);
                    if (info->data) {
                        info->size = content_len;
                    }
                }
                if (evt->data_len && info->fill_size + evt->data_len <= info->size) {
                    memcpy(info->data + info->fill_size, evt->data, evt->data_len);
                    info->fill_size += evt->data_len;
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (info->fill_size && info->body) {
                http_resp_t resp = {
                    .data = (char*)info->data,
                    .size = info->fill_size,
                };
                info->body(&resp, info->ctx);
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int       mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data,
                                                             &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
        case HTTP_EVENT_REDIRECT:
            esp_http_client_set_redirection(evt->client);
            break;
        default:
            break;
    }
    return ESP_OK;
}

#define SECONDS_IN_DAY      (24 * 60 * 60)
#define SECONDS_IN_WEEK     (7 * SECONDS_IN_DAY)
#define SIGNED_URL_SIZE_MAX 20000
#define HEADERS             "Host: %s\r\nX-Amz-Date: %s\r\n\r\n"

/* <aws_access_key_id>/<8bytes date>/<region>/kinesisvideo/aws4_request*/
#define CREDENTIAL_TEMPLATE "%s/%.*s/%s/kinesisvideo/aws4_request"

// Token query param template
#define SECURITY_TOKEN_PARAM_TEMPLATE "&X-Amz-Security-Token=%s"
#define AUTH_QUERY_TEMPLATE_WITH_TOKEN      \
    "X-Amz-Algorithm=%s&X-Amz-ChannelARN=%s&X-Amz-Credential=%s&X-Amz-Date=%s&X-Amz-Expires=%" PRIu32 SECURITY_TOKEN_PARAM_TEMPLATE "&X-Amz-SignedHeaders=%s"

#define SIGNATURE_TEMPLATE_WITH_TOKEN      \
    "&X-Amz-Algorithm=%s&X-Amz-ChannelARN=%s&X-Amz-Credential=%s&X-Amz-Date=%s&X-Amz-Expires=%" PRIu32 "&X-Amz-SignedHeaders=%s" SECURITY_TOKEN_PARAM_TEMPLATE

static int generate_sigv4_signed_wss_url(char* method, char* url, const char* channel_arn, char **signed_url)
{
    int ret = -1;
    char *arn_encoded = NULL, *session_token_encoded = NULL, *credential_str_encoded = NULL;
    char *signed_url_local = NULL;
    char *signed_url_base = NULL; // Original pointer for freeing
    char *host_header = NULL;

    SigV4Credentials_t sigv4_creds = {
        .accessKeyIdLen = strlen(aws_access_key_id),
        .pAccessKeyId = aws_access_key_id,
        .secretAccessKeyLen = strlen(aws_secret_access_key),
        .pSecretAccessKey = aws_secret_access_key
    };

    SigV4HttpParameters_t http_params = {
        .pHttpMethod = method,
        .httpMethodLen = strlen(method),
        .flags = 0,
        .pPath = NULL, /* Only the path */
        .pathLen = 0,
        .pQuery = NULL,
        .queryLen = 0,
    };

    char iso_8601_time[18];
    webrtc_utils_get_time_iso8601(iso_8601_time);
    ESP_LOGI(TAG, "Time: %s", iso_8601_time);

    /* Extract host from URL for signing (e.g. wss://host.example.com -> host.example.com) */
    const char *host_start = strstr(url, "://");
    if (!host_start) {
        ESP_LOGE(TAG, "Invalid URL format");
        return -1;
    }
    host_start += 3;
    const char *host_end = strchr(host_start, '/');
    if (!host_end) {
        host_end = host_start + strlen(host_start);
    }
    size_t host_len = host_end - host_start;

    /* Build "host: <hostname>\r\n\r\n" header for SigV4 signing */
    size_t header_size = strlen("host: ") + host_len + strlen("\r\n\r\n") + 1;
    host_header = heap_caps_calloc(1, header_size, MALLOC_CAP_SPIRAM);
    if (!host_header) {
        ESP_LOGE(TAG, "Failed to allocate host_header");
        return -1;
    }
    snprintf(host_header, header_size, "host: %.*s\r\n\r\n", (int)host_len, host_start);

    http_params.pHeaders = host_header;
    http_params.headersLen = strlen(host_header);

    const SigV4Parameters_t sigv4_params = {
        .pHttpParameters = &http_params,
        .pCredentials = &sigv4_creds,
        .pCryptoInterface = &cryptoInterface,
        .pAlgorithm = SIGV4_AWS4_HMAC_SHA256,
        .algorithmLen = SIGV4_AWS4_HMAC_SHA256_LENGTH,
        .pDateIso8601 = iso_8601_time,
        .pRegion = REGION,
        .regionLen = strlen(REGION),
        .pService = "kinesisvideo",
        .serviceLen = strlen("kinesisvideo"),
    };

    /* allocate and prepare credential string */
    int credential_str_req_size = strlen(CREDENTIAL_TEMPLATE) + strlen(aws_access_key_id) + 8 + strlen(REGION) + 1;
    char *credential_str = heap_caps_calloc(1, credential_str_req_size + 1, MALLOC_CAP_SPIRAM);
    if (!credential_str) {
        ESP_LOGE(TAG, "Failed to allocate for credential_str");
        goto __gen_sigv4_exit;
    }
    int cred_len = snprintf(credential_str, credential_str_req_size + 1, CREDENTIAL_TEMPLATE, aws_access_key_id, 8, iso_8601_time, REGION);
    if (cred_len < 0 || cred_len >= credential_str_req_size + 1) {
        ESP_LOGE(TAG, "Credential string formatting failed or too long");
        free(credential_str);
        goto __gen_sigv4_exit;
    }

    /* Ideal req size is: credential_str_req_size * 3 + 1, but we only need to encode 4 slashes */
    int extra_needed = 4 * 2; /* 2 extra bytes per slash */
    size_t buf_len = credential_str_req_size + extra_needed + 1 /* for NULL termination */;

    credential_str_encoded = heap_caps_calloc(1, buf_len, MALLOC_CAP_SPIRAM);
    if (!credential_str_encoded) {
        ESP_LOGE(TAG, "Failed to allocate for credential_str_encoded");
        free(credential_str);
        goto __gen_sigv4_exit;
    }

    SigV4_EncodeURI(credential_str, strlen(credential_str), credential_str_encoded,
                    &buf_len, true, false);
    ESP_LOGI(TAG, "credential_str_encoded: %s\n", credential_str_encoded);
    /* credential_str not needed anymore */
    free(credential_str);

    buf_len = strlen(channel_arn) * 3 + 1; /* Max buffer needed for encoding */

    arn_encoded = heap_caps_calloc(1, buf_len, MALLOC_CAP_SPIRAM);
    if (!arn_encoded) {
        ESP_LOGE(TAG, "Failed to allocate for arn_encoded");
        goto __gen_sigv4_exit;
    }
    SigV4_EncodeURI(channel_arn, strlen(channel_arn), arn_encoded, &buf_len, true, false);
    arn_encoded[buf_len] = '\0';
    ESP_LOGI(TAG, "ARN encoded: %s\n", arn_encoded);

    buf_len = 2000; /* Theoritically, it should be `3 * strlen(aws_session_token) + 1` */

    session_token_encoded = heap_caps_calloc(1, buf_len, MALLOC_CAP_SPIRAM);
    if (!session_token_encoded) {
        ESP_LOGE(TAG, "Failed to allocate for session_token_encoded");
        goto __gen_sigv4_exit;
    }

    SigV4_EncodeURI(aws_session_token, strlen(aws_session_token), session_token_encoded,
                    &buf_len, true, false);
    session_token_encoded[buf_len] = '\0';
    ESP_LOGI(TAG, "Encoded session token: %s\n", session_token_encoded);

    signed_url_local = heap_caps_calloc(1, SIGNED_URL_SIZE_MAX, MALLOC_CAP_SPIRAM);
    if (!signed_url_local) {
        ESP_LOGE(TAG, "Failed to allocate signed URL");
        goto __gen_sigv4_exit;
    }

    signed_url_base = signed_url_local; // Save original pointer for freeing
    *signed_url = signed_url_local;

    // Check URL length before copying - need space for '?' and query string
    int url_len = strlen(url);
    if (url_len >= SIGNED_URL_SIZE_MAX - 1) { // Reserve space for '?' and at least some query string
        ESP_LOGE(TAG, "URL too long: %d bytes, max %d", url_len, SIGNED_URL_SIZE_MAX - 1);
        goto __gen_sigv4_exit;
    }
    strncpy(signed_url_local, url, SIGNED_URL_SIZE_MAX - 1);
    signed_url_local[SIGNED_URL_SIZE_MAX - 1] = '\0';
    int base_url_end = url_len;
    signed_url_local[base_url_end] = '?';
    base_url_end++;
    char *query_start = signed_url_local + base_url_end; // Start of query string

    // Calculate remaining buffer size - ensure we don't go past buffer end
    size_t remaining_size = SIGNED_URL_SIZE_MAX - base_url_end;
    if (remaining_size == 0) {
        ESP_LOGE(TAG, "No space remaining for query string");
        goto __gen_sigv4_exit;
    }

    /* Generate url_encoded Query String (without signature) */
    int query_len = snprintf(query_start, remaining_size,
             AUTH_QUERY_TEMPLATE_WITH_TOKEN, SIGV4_AWS4_HMAC_SHA256,
             arn_encoded, credential_str_encoded, iso_8601_time,
             (uint32_t) SECONDS_IN_WEEK, session_token_encoded, "host");

    if (query_len < 0 || query_len >= (int)remaining_size) {
        ESP_LOGE(TAG, "Query string too long or snprintf failed");
        goto __gen_sigv4_exit;
    }

    // Update remaining size after query string
    remaining_size -= query_len;
    char *query_end = query_start + query_len;

    http_params.pQuery = query_start;
    http_params.queryLen = query_len;
    http_params.flags = SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG;

    // Reset authBufLen to buffer size before each call
    size_t auth_buf_len = AUTH_BUF_LENGTH;
    SigV4Status_t sign_status =
        SigV4_GenerateHTTPAuthorization(&sigv4_params,
                                        authBuf,
                                        &auth_buf_len,
                                        &signature,
                                        &signatureLen);
    if (sign_status != SigV4Success) {
        ESP_LOGE(TAG, "Signature Error! return: %d\n", (int) sign_status);
        goto __gen_sigv4_exit;
    } else {
        ESP_LOGI(TAG, "Success! AuthBuf: %.*s\n", (int)auth_buf_len, authBuf);
        // Do NOT modify authBuf - signature pointer points into it
        // The sigv4 library manages the buffer, and we use %.*s which doesn't need null termination
    }

    /* Append signature to query string with bounds checking */
    if (signature && signatureLen > 0) {
        // Validate signature pointer is within authBuf bounds
        if (signature < authBuf || signature >= (authBuf + AUTH_BUF_LENGTH) ||
            signatureLen > AUTH_BUF_LENGTH ||
            (signature + signatureLen) > (authBuf + AUTH_BUF_LENGTH)) {
            ESP_LOGE(TAG, "Invalid signature pointer or length");
            goto __gen_sigv4_exit;
        }

        // Limit signatureLen to prevent buffer overread
        size_t safe_sig_len = signatureLen;
        if ((signature + safe_sig_len) > (authBuf + AUTH_BUF_LENGTH)) {
            safe_sig_len = (authBuf + AUTH_BUF_LENGTH) - signature;
        }

        int sig_append_len = snprintf(query_end, remaining_size,
                                      "&X-Amz-Signature=%.*s", (int)safe_sig_len, signature);
        if (sig_append_len < 0 || sig_append_len >= (int)remaining_size) {
            ESP_LOGE(TAG, "Signature append failed or buffer too small");
            goto __gen_sigv4_exit;
        }
        // snprintf already null-terminates, but ensure we don't overflow for logging
        size_t total_used = (query_end - signed_url_base) + sig_append_len;
        if (total_used >= SIGNED_URL_SIZE_MAX) {
            signed_url_base[SIGNED_URL_SIZE_MAX - 1] = '\0';
        }
        // Log with length limit to be safe
        ESP_LOGI(TAG, "Signed URL: %.1000s", signed_url_base);
    } else {
        ESP_LOGE(TAG, "Invalid signature");
        goto __gen_sigv4_exit;
    }

    ret = 0; /* Success */

__gen_sigv4_exit:

    if (arn_encoded) {
        free(arn_encoded);
    }
    if (credential_str_encoded) {
        free(credential_str_encoded);
    }
    if (session_token_encoded) {
        free(session_token_encoded);
    }
    if (host_header) {
        free(host_header);
    }
    if (ret != 0 && signed_url_base) {
        free(signed_url_base);
        *signed_url = NULL;
    }
    return ret;
}

int https_send_request(char* method, char** headers, char* url,
                       char* data, http_body_t body, void* ctx) {
    int err = -1;
    char *host = NULL;
    char *headers_string = NULL;
    http_info_t info = {
        .body = body,
        .ctx = ctx,
    };
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = &info,
        .buffer_size_tx = 4096,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Fail to init client");
        return err;
    }

    SigV4Credentials_t sigv4_creds = {
        .accessKeyIdLen = strlen(aws_access_key_id),
        .pAccessKeyId = aws_access_key_id,
        .secretAccessKeyLen = strlen(aws_secret_access_key),
        .pSecretAccessKey = aws_secret_access_key
    };

    const char *host_start = strstr(url, "://");
    if (!host_start) {
        ESP_LOGE(TAG, "Invalid URL format: missing ://");
        goto __https_send_request_exit;
    }
    host_start += 3; // Skip "://"

    /* Find the end of the host */
    const char *host_end = strchr(host_start, '/');
    if (!host_end) {
        /* If no path, the host goes until the end of the string */
        host_end = url + strlen(url);
    }

    /* Calculate the length of the host */
    size_t host_length = host_end - host_start;

    /* Copy the host to a new string*/
    host = heap_caps_calloc(1, host_length + 1, MALLOC_CAP_SPIRAM);
    if (!host) {
        ESP_LOGE(TAG, "Failed to allocate for host");
        goto __https_send_request_exit;
    }

    strncpy(host, host_start, host_length);
    host[host_length] = '\0'; /* Null-terminate the string */

    /* Use pointer directly into URL string, like the working version */
    const char *canonical_path = host_end;

    ESP_LOGD(TAG, "url:%s", url);
    ESP_LOGD(TAG, "host:%s", host);
    ESP_LOGD(TAG, "canonical_path:%s", canonical_path);

    SigV4HttpParameters_t http_params = {
        .pHttpMethod = method,
        .httpMethodLen = strlen(method),
        .flags = 0,
        .pPath = canonical_path, /* Pointer directly into URL string */
        .pathLen = strlen(canonical_path),
        .pQuery = NULL,
        .queryLen = 0,
        .pHeaders = NULL, /* Will be set after allocation */
        .headersLen = 0,  /* Will be set after allocation */
        .pPayload = data, /* Body */
        .payloadLen = strlen(data), /* Length of the body */
    };

    headers_string = heap_caps_calloc(1, 2000, MALLOC_CAP_SPIRAM);
    if (!headers_string) {
        ESP_LOGE(TAG, "Failed to allocate for headers_string");
        goto __https_send_request_exit;
    }

    /* Use stack buffer like current implementation (working version might have different API) */
    char iso_8601_time[18];
    webrtc_utils_get_time_iso8601(iso_8601_time);
    ESP_LOGI(TAG, "Time: %s", iso_8601_time);
    sprintf(headers_string, HEADERS, host, iso_8601_time);
    http_params.pHeaders = headers_string;
    http_params.headersLen = strlen(headers_string);

    const SigV4Parameters_t sigv4_params = {
        .pHttpParameters = &http_params,
        .pCredentials = &sigv4_creds,
        .pCryptoInterface = &cryptoInterface,
        .pAlgorithm = SIGV4_AWS4_HMAC_SHA256,
        .algorithmLen = SIGV4_AWS4_HMAC_SHA256_LENGTH,
        .pDateIso8601 = iso_8601_time,
        .pRegion = REGION,
        .regionLen = strlen(REGION),
        .pService = "kinesisvideo",
        .serviceLen = strlen("kinesisvideo"),
    };

    // Reset authBufLen to buffer size before each call
    size_t auth_buf_len = AUTH_BUF_LENGTH;
    SigV4Status_t sign_status =
        SigV4_GenerateHTTPAuthorization(&sigv4_params,
                                        authBuf,
                                        &auth_buf_len,
                                        &signature,
                                        &signatureLen);

    if (sign_status != SigV4Success) {
        ESP_LOGE(TAG, "Signature Error! return: %d\n", (int) sign_status);
        goto __https_send_request_exit;
    } else {
        ESP_LOGI(TAG, "Success! AuthBuf: %.*s\n", (int)auth_buf_len, authBuf);
        authBuf[auth_buf_len] = 0;
    }

    // POST
    esp_http_client_set_url(client, url);
    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    } else if (strcmp(method, "DELETE") == 0) {
       esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    } else {
        goto __https_send_request_exit;
    }

    esp_http_client_set_header(client, "Host", host);
    esp_http_client_set_header(client, "user-agent", USER_AGENT);
    esp_http_client_set_header(client, "X-Amz-Date", iso_8601_time);
    esp_http_client_set_header(client, "Authorization", authBuf);
    esp_http_client_set_header(client, "x-amz-security-token", aws_session_token);

    if (headers) {
        int i = 0;

        while(headers[i]) {
            // Create a copy before modifying (like apprtc_signal does)
            char* h = strdup(headers[i]);
            if (h == NULL) {
                ESP_LOGE(TAG, "Failed to duplicate header string");
                i++;
                continue;
            }

            char* dot = strchr(h, ':');
            if (dot) {
                *dot = 0;  // Null-terminate key
                char* cont = dot + 1;
                // Skip whitespace after colon
                while (*cont == ' ' || *cont == '\t') {
                    cont++;
                }
                esp_http_client_set_header(client, h, cont);
            }
            free(h);
            i++;
        }
    }

    if (data != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, data, strlen(data));
    }

#define ESP_HTTP_CLIENT_POST_RETRIES_MAX    5

    int retry_cnt = 0;
    while (retry_cnt++ < ESP_HTTP_CLIENT_POST_RETRIES_MAX) {
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
            err = 0;
            break;
        } else if (err == ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGI(TAG, "HTTP POST request EAGAIN! Retry %d/%d", retry_cnt, ESP_HTTP_CLIENT_POST_RETRIES_MAX );
            continue;
        } else {
            ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
            break;
        }
    }

__https_send_request_exit:
    esp_http_client_cleanup(client);
    if (info.data) {
        free(info.data);
    }
    if (headers_string) {
        free(headers_string);
    }
    if (host) {
        free(host);
    }
    /* canonical_path is just a pointer into url, no need to free */
    /* query_ptr is not used in this simplified version */
    return err;
}

int https_post(char *url, char **headers, char *data, http_body_t body, void *ctx)
{
    return https_send_request("POST", headers, url, data, body, ctx);
}

int get_signed_wss_url(char *url, const char *channel_arn, char **signed_url)
{
    return generate_sigv4_signed_wss_url("GET", url, channel_arn, signed_url);
}
