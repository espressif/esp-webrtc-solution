/**
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <sys/param.h>
#include <string.h>
#include "esp_log.h"
#include "https_client.h"
#include "https_client_utils.h"
#include "esp_tls.h"
#include <sdkconfig.h>
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_http_client.h"

static const char *TAG = "HTTPS_CLIENT";

typedef struct {
    const char *location;
    const char *auth;
    const char *cookie;
} http_resp_keep_t;

typedef struct {
    http_header_t    header;
    http_body_t      body;
    http_resp_keep_t resp_keep;
    uint8_t         *data;
    int              fill_size;
    int              size;
    void            *ctx;
} http_info_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_info_t *info = evt->user_data;
    switch (evt->event_id) {
        default:
            break;
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
            if (info->header) {
                if (strcmp(evt->header_key, "Location") == 0) {
                    info->resp_keep.location = strdup(evt->header_value);
                } else if (strcmp(evt->header_key, "Authorization") == 0) {
                    info->resp_keep.auth = strdup(evt->header_value);
                } else if (strcmp(evt->header_key, "Set-Cookie") == 0) {
                    info->resp_keep.cookie = strdup(evt->header_value);
                }
                info->header(evt->header_key, evt->header_value, info->ctx);
            }
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
                     evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int content_len = esp_http_client_get_content_length(evt->client);
                if (info->data == NULL && content_len) {
                    info->data = malloc(content_len);
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
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (info->fill_size && info->body) {
                http_resp_t resp = {
                    .data = (char *)info->data,
                    .size = info->fill_size,
                };
                info->body(&resp, info->ctx);
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data,
                                                             &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGD(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGD(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static int https_handle_redirect(esp_http_client_handle_t client, https_request_t *req, http_resp_keep_t *resp, char **last_url)
{
    if (resp->location == NULL) {
        ESP_LOGE(TAG, "Redirect location not found");
        return -1;
    }
    int status_code = esp_http_client_get_status_code(client);
    if (!(status_code == 307 || status_code == 308)) {
        esp_http_client_set_post_field(client, NULL, 0);
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }
    char *full_url = strdup(resp->location);
    if (full_url == NULL) {
        return -1;
    }
    if (strncmp(resp->location , "http", 4) != 0) {
        full_url = join_url(*last_url, resp->location);
        if (full_url == NULL) {
            return -1;
        }
    }
    // Handle auth
    if (resp->auth) {
        // Only valid when origin same
        if (is_same_origin(*last_url, full_url)) {
            esp_http_client_set_header(client, "Authorization", resp->auth);
        } else {
            esp_http_client_set_header(client, "Authorization", NULL);
        }
    }
    // Handle cookie
    if (resp->cookie != NULL) {
        esp_http_client_set_header(client, "Cookie", resp->cookie);
    }
    // Store to last url
    if (*last_url != req->url) {
        free(*last_url);
    }
    *last_url = full_url;
    ESP_LOGI(TAG, "Redirect to: %s %p\n", full_url, full_url);
    esp_http_client_set_url(client, full_url);
    return 0;
}

static void free_resp_header(http_resp_keep_t *resp)
{
    if (resp->location) {
        free((void *)resp->location);
        resp->location = NULL;
    }
    if (resp->auth) {
        free((void *)resp->auth);
        resp->auth = NULL;
    }
    if (resp->cookie) {
        free((void *)resp->cookie);
        resp->cookie = NULL;
    }
}

int https_request_advance(https_request_cfg_t *cfg, https_request_t *req)
{
    https_request_cfg_t default_cfg = {
        .timeout_ms = DEFAULT_HTTPS_TIMEOUT,
        .max_redirects = DEFAULT_HTTPS_MAX_REDIRECTS,
        .buffer_size = DEFAULT_HTTPS_HEAD_SIZE,
        .buffer_size_tx = DEFAULT_HTTPS_TX_SIZE,
        .keep_alive_enable = DEFAULT_HTTPS_KEEP_ALIVE_ENABLE,
        .keep_alive_count = DEFAULT_HTTPS_KEEP_ALIVE_COUNT,
        .keep_alive_idle = DEFAULT_HTTPS_KEEP_ALIVE_IDLE,
        .keep_alive_interval = DEFAULT_HTTPS_KEEP_ALIVE_INTERVAL,
    };
    if (cfg == NULL) {
        cfg = &default_cfg;
    }
    http_info_t info = {
        .body = req->body_cb,
        .header = req->header_cb,
        .ctx = req->ctx,
    };
    esp_http_client_config_t http_config = {
        .url = req->url,
        .event_handler = _http_event_handler,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        .timeout_ms = cfg->timeout_ms ? cfg->timeout_ms : default_cfg.timeout_ms,
        .buffer_size = cfg->buffer_size ? cfg->buffer_size : default_cfg.buffer_size,
        .buffer_size_tx = cfg->buffer_size_tx ? cfg->buffer_size_tx : default_cfg.buffer_size_tx,
        .disable_auto_redirect = true, // Handle redirects manually
        .keep_alive_enable = cfg->keep_alive_enable,
        .keep_alive_idle = cfg->keep_alive_idle,
        .keep_alive_interval = cfg->keep_alive_interval,
        .keep_alive_count = cfg->keep_alive_count,
        .user_data = &info,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Fail to init client");
        return -1;
    }
    // POST
    int err = 0;
    char *last_url = (char*)req->url;
    esp_http_client_set_url(client, req->url);
    if (strcmp(req->method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    } else if (strcmp(req->method, "DELETE") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    } else if (strcmp(req->method, "PATCH") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    } else if (strcmp(req->method, "GET") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    } else {
        err = -1;
        goto _exit;
    }

    bool has_content_type = false;
    uint8_t redirect_limit = cfg->max_redirects ? cfg->max_redirects : DEFAULT_HTTPS_MAX_REDIRECTS;
    uint8_t redirect_count = 0;
    const char *post_data = req->data;
    if (req->headers) {
        int i = 0;
        // TODO suppose header writable
        while (req->headers[i]) {
            char *h = strdup(req->headers[i]);
            if (h == NULL) {
                continue;
            }
            char *dot = strchr(h, ':');
            if (dot) {
                *dot = 0;
                if (strcmp(h, "Content-Type") == 0) {
                    has_content_type = true;
                }
                char *cont = dot + 2;
                esp_http_client_set_header(client, h, cont);
                *dot = ':';
            }
            free(h);
            i++;
        }
    }
    if (post_data != NULL) {
        if (has_content_type == false) {
            esp_http_client_set_header(client, "Content-Type", "text/plain;charset=UTF-8");
        }
        esp_http_client_set_post_field(client, req->data, strlen(req->data));
    }
RETRY_PERFORM:
    if (info.data) {
        free(info.data);
        info.data = NULL;
    }
    info.fill_size = 0;
    free_resp_header(&info.resp_keep);

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 301 && status_code < 400) {
            // Handle redirection
            if (redirect_count >= redirect_limit) {
                ESP_LOGE(TAG, "Redirect limit exceeded: %d", redirect_count);
                err = -1;
            } else {
                err = https_handle_redirect(client, req, &info.resp_keep, &last_url);
                if (!(status_code == 307 || status_code == 308)) {
                    post_data = NULL;
                }
                redirect_count++;
                if (err == 0) {
                    goto RETRY_PERFORM;
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP %s request failed: %s", req->method, esp_err_to_name(err));
    }
_exit:
    if (last_url != req->url) {
        free(last_url);
    }
    free_resp_header(&info.resp_keep);
    // Free Body
    if (info.data) {
        free(info.data);
    }
    esp_http_client_cleanup(client);
    return err;
}

int https_send_request(const char *method, char **headers, const char *url, char *data, http_header_t header_cb, http_body_t body, void *ctx)
{
    https_request_t req = {
        .method = method,
        .url = url,
        .headers = headers,
        .data = data,
        .header_cb = header_cb,
        .body_cb = body,
        .ctx = ctx,
    };
    return https_request_advance(NULL, &req);
}

int https_post(const char *url, char **headers, char *data, http_header_t header_cb, http_body_t body, void *ctx)
{
    return https_send_request("POST", headers, url, data, header_cb, body, ctx);
}
