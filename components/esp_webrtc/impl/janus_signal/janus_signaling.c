/*
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "cJSON.h"
#include "https_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_peer_signaling.h"
#include "esp_peer_janus_signaling.h"
#include "media_lib_os.h"

#define TAG  "JANUS_SIGNALING"

#define SAFE_FREE(p)  if (p) {  \
    free(p);                    \
    p = NULL;                   \
}

typedef struct {
    esp_peer_signaling_cfg_t        cfg;
    esp_peer_signaling_janus_cfg_t  janus_cfg;
    uint64_t                        session_id;
    uint64_t                        handle_id;
    bool                            local_sdp_sent;
    bool                            running;
    bool                            poll_started;
    media_lib_thread_handle_t       poll_thread;
    media_lib_sema_handle_t         exit_sema;
} janus_signaling_t;

typedef struct {
    char *data;
    int   size;
    int   malloc_size;
} janus_http_resp_t;

static void janus_save_response(http_resp_t *resp, void *ctx)
{
    janus_http_resp_t *http_resp = (janus_http_resp_t *)ctx;
    if (resp->size > http_resp->malloc_size) {
        SAFE_FREE(http_resp->data);
        http_resp->malloc_size = 0;
        http_resp->data = malloc(resp->size + 1);
        if (http_resp->data == NULL) {
            return;
        }
        http_resp->malloc_size = resp->size + 1;
    }
    memcpy(http_resp->data, resp->data, resp->size);
    http_resp->data[resp->size] = 0;
    http_resp->size = resp->size;
}

static char *janus_create_txn(void)
{
    static uint32_t seq = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
    seq++;
    char *txn = calloc(1, 32);
    if (txn == NULL) {
        return NULL;
    }
    snprintf(txn, 32, "%08" PRIx32 "%08" PRIx32, now, seq);
    return txn;
}

static int janus_parse_error(cJSON *root)
{
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error == NULL) {
        return 0;
    }
    cJSON *code = cJSON_GetObjectItem(error, "code");
    cJSON *reason = cJSON_GetObjectItem(error, "reason");
    ESP_LOGE(TAG, "Janus error code:%d reason:%s", code ? code->valueint : -1,
             reason && cJSON_IsString(reason) ? reason->valuestring : "unknown");
    return -1;
}

static int janus_send_api(janus_signaling_t *sig, const char *method, const char *url, cJSON *payload, janus_http_resp_t *http_resp)
{
    if (url == NULL || payload == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    char content_type[] = "Content-Type: application/json";
    char *headers[] = {content_type, NULL};
    char *data = cJSON_PrintUnformatted(payload);
    if (data == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    int ret = https_send_request(method, headers, url, data, NULL, janus_save_response, http_resp);
    free(data);
    return ret;
}

static int janus_get_ids(janus_signaling_t *sig)
{
    janus_http_resp_t resp = {};
    int ret = ESP_PEER_ERR_FAIL;
    char post_url[320] = {0};
    char *txn = NULL;
    cJSON *root = NULL;
    cJSON *json = NULL;

    do {
        txn = janus_create_txn();
        if (txn == NULL) {
            ret = ESP_PEER_ERR_NO_MEM;
            break;
        }
        root = cJSON_CreateObject();
        if (root == NULL) {
            ret = ESP_PEER_ERR_NO_MEM;
            break;
        }
        cJSON_AddStringToObject(root, "janus", "create");
        cJSON_AddStringToObject(root, "transaction", txn);
        if (sig->janus_cfg.token) {
            cJSON_AddStringToObject(root, "token", sig->janus_cfg.token);
        }
        if (sig->janus_cfg.api_secret) {
            cJSON_AddStringToObject(root, "apisecret", sig->janus_cfg.api_secret);
        }
        if (janus_send_api(sig, "POST", sig->cfg.signal_url, root, &resp) != 0 || resp.data == NULL) {
            ESP_LOGE(TAG, "Failed to create session");
            break;
        }
        json = cJSON_Parse(resp.data);
        if (json == NULL || janus_parse_error(json) != 0) {
            break;
        }
        cJSON *data = cJSON_GetObjectItem(json, "data");
        cJSON *sid = data ? cJSON_GetObjectItem(data, "id") : NULL;
        if (sid && cJSON_IsNumber(sid)) {
            sig->session_id = (uint64_t)sid->valuedouble;
        }
        if (sig->session_id == 0) {
            break;
        }
        cJSON_Delete(json);
        cJSON_Delete(root);
        json = NULL;

        root = cJSON_CreateObject();
        if (root == NULL) {
            ret = ESP_PEER_ERR_NO_MEM;
            break;
        }
        cJSON_AddStringToObject(root, "janus", "attach");
        cJSON_AddStringToObject(root, "plugin", "janus.plugin.videoroom");
        cJSON_AddStringToObject(root, "transaction", txn);
        if (sig->janus_cfg.token) {
            cJSON_AddStringToObject(root, "token", sig->janus_cfg.token);
        }
        if (sig->janus_cfg.api_secret) {
            cJSON_AddStringToObject(root, "apisecret", sig->janus_cfg.api_secret);
        }
        snprintf(post_url, sizeof(post_url), "%s/%" PRIu64, sig->cfg.signal_url, sig->session_id);
        if (janus_send_api(sig, "POST", post_url, root, &resp) != 0 || resp.data == NULL) {
            break;
        }
        json = cJSON_Parse(resp.data);
        if (json == NULL || janus_parse_error(json) != 0) {
            break;
        }
        data = cJSON_GetObjectItem(json, "data");
        cJSON *hid = data ? cJSON_GetObjectItem(data, "id") : NULL;
        if (hid && cJSON_IsNumber(hid)) {
            sig->handle_id = (uint64_t)hid->valuedouble;
        }
        if (sig->handle_id == 0) {
            break;
        }
        cJSON_Delete(json);
        json = NULL;
        cJSON_Delete(root);
        root = cJSON_CreateObject();
        if (root == NULL) {
            ret = ESP_PEER_ERR_NO_MEM;
            break;
        }
        cJSON_AddStringToObject(root, "janus", "message");
        cJSON_AddStringToObject(root, "transaction", txn);
        if (sig->janus_cfg.token) {
            cJSON_AddStringToObject(root, "token", sig->janus_cfg.token);
        }
        if (sig->janus_cfg.api_secret) {
            cJSON_AddStringToObject(root, "apisecret", sig->janus_cfg.api_secret);
        }
        cJSON *body = cJSON_AddObjectToObject(root, "body");
        cJSON_AddStringToObject(body, "request", "join");
        cJSON_AddStringToObject(body, "ptype", "publisher");
        cJSON_AddNumberToObject(body, "room", sig->janus_cfg.room_id);
        if (sig->janus_cfg.pin) {
            cJSON_AddStringToObject(body, "pin", sig->janus_cfg.pin);
        }
        if (sig->janus_cfg.display) {
            cJSON_AddStringToObject(body, "display", sig->janus_cfg.display);
        }
        snprintf(post_url, sizeof(post_url), "%s/%" PRIu64 "/%" PRIu64,
                 sig->cfg.signal_url, sig->session_id, sig->handle_id);
        if (janus_send_api(sig, "POST", post_url, root, &resp) != 0 || resp.data == NULL) {
            break;
        }
        json = cJSON_Parse(resp.data);
        if (json == NULL || janus_parse_error(json) != 0) {
            break;
        }
        ret = ESP_PEER_ERR_NONE;
    } while (0);

    cJSON_Delete(json);
    SAFE_FREE(txn);
    cJSON_Delete(root);
    SAFE_FREE(resp.data);
    return ret;
}

static void janus_handle_event(janus_signaling_t *sig, cJSON *event)
{
    cJSON *janus = cJSON_GetObjectItem(event, "janus");
    if (janus == NULL || cJSON_IsString(janus) == false) {
        return;
    }
    if (strcmp(janus->valuestring, "event") == 0 || strcmp(janus->valuestring, "success") == 0) {
        cJSON *jsep = cJSON_GetObjectItem(event, "jsep");
        if (jsep) {
            cJSON *type = cJSON_GetObjectItem(jsep, "type");
            cJSON *sdp = cJSON_GetObjectItem(jsep, "sdp");
            if (type && sdp && cJSON_IsString(type) && cJSON_IsString(sdp)
                && strcmp(type->valuestring, "answer") == 0) {
                esp_peer_signaling_msg_t sdp_msg = {
                    .type = ESP_PEER_SIGNALING_MSG_SDP,
                    .data = (uint8_t *)sdp->valuestring,
                    .size = strlen(sdp->valuestring),
                };
                sig->cfg.on_msg(&sdp_msg, sig->cfg.ctx);
            }
        }
    } else if (strcmp(janus->valuestring, "trickle") == 0) {
        cJSON *candidate = cJSON_GetObjectItem(event, "candidate");
        if (candidate) {
            char *candidate_json = cJSON_PrintUnformatted(candidate);
            if (candidate_json) {
                esp_peer_signaling_msg_t candidate_msg = {
                    .type = ESP_PEER_SIGNALING_MSG_CANDIDATE,
                    .data = (uint8_t *)candidate_json,
                    .size = strlen(candidate_json),
                };
                sig->cfg.on_msg(&candidate_msg, sig->cfg.ctx);
                free(candidate_json);
            }
        }
    }
}

static void janus_poll_thread(void *arg)
{
    janus_signaling_t *sig = (janus_signaling_t *)arg;
    while (sig->running) {
        char poll_url[384];
        snprintf(poll_url, sizeof(poll_url), "%s/%" PRIu64 "?maxev=1&rid=%" PRIu64,
                 sig->cfg.signal_url, sig->session_id, (uint64_t)esp_timer_get_time());
        janus_http_resp_t resp = {};
        https_request_cfg_t req_cfg = {
            .timeout_ms = 10000,  // Use long timeout to avoid error log print too often
        };
        https_request_t req = {
            .method = "GET",
            .url = poll_url,
            .body_cb = janus_save_response,
            .ctx = &resp,
        };
        if (https_request_advance(&req_cfg, &req) == 0 && resp.data) {
            cJSON *root = cJSON_Parse(resp.data);
            if (root) {
                if (cJSON_IsArray(root)) {
                    int n = cJSON_GetArraySize(root);
                    for (int i = 0; i < n; i++) {
                        cJSON *item = cJSON_GetArrayItem(root, i);
                        janus_handle_event(sig, item);
                    }
                } else {
                    janus_handle_event(sig, root);
                }
                cJSON_Delete(root);
            }
            SAFE_FREE(resp.data);
        }
    }
    sig->poll_started = false;
    media_lib_sema_unlock(sig->exit_sema);
    media_lib_thread_destroy(NULL);
}

static int janus_signaling_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *h)
{
    if (cfg == NULL || h == NULL || cfg->signal_url == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    janus_signaling_t *sig = calloc(1, sizeof(janus_signaling_t));
    if (sig == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    int ret = ESP_PEER_ERR_FAIL;
    do {
        sig->cfg = *cfg;
        if (cfg->extra_cfg && cfg->extra_size == sizeof(esp_peer_signaling_janus_cfg_t)) {
            sig->janus_cfg = *(esp_peer_signaling_janus_cfg_t *)cfg->extra_cfg;
        }
        if (sig->janus_cfg.room_id == 0) {
            ESP_LOGE(TAG, "Please set Janus VideoRoom room_id");
            ret = ESP_PEER_ERR_INVALID_ARG;
            break;
        }
        ret = janus_get_ids(sig);
        if (ret != ESP_PEER_ERR_NONE) {
            break;
        }
        sig->running = true;
        if (media_lib_sema_create(&sig->exit_sema) != 0) {
            ret = ESP_PEER_ERR_FAIL;
            break;
        }
        if (media_lib_thread_create_from_scheduler(&sig->poll_thread, "janus_poll", janus_poll_thread, sig) != 0) {
            ret = ESP_PEER_ERR_FAIL;
            sig->running = false;
            break;
        }
        sig->poll_started = true;
        esp_peer_signaling_ice_info_t ice_info = {
            .is_initiator = true,
        };
        sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
        sig->cfg.on_connected(sig->cfg.ctx);
        *h = sig;
        return ESP_PEER_ERR_NONE;
    } while (0);

    if (sig->exit_sema) {
        media_lib_sema_destroy(sig->exit_sema);
        sig->exit_sema = NULL;
    }
    SAFE_FREE(sig);
    return ret;
}

static int janus_signaling_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    janus_signaling_t *sig = (janus_signaling_t *)h;
    if (sig == NULL || msg == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    char post_url[320];
    int ret = ESP_PEER_ERR_NONE;
    char *txn = NULL;
    cJSON *root = NULL;
    janus_http_resp_t resp = {};

    do {
        snprintf(post_url, sizeof(post_url), "%s/%" PRIu64 "/%" PRIu64,
                 sig->cfg.signal_url, sig->session_id, sig->handle_id);
        txn = janus_create_txn();
        if (txn == NULL) {
            ret = ESP_PEER_ERR_NO_MEM;
            break;
        }
        root = cJSON_CreateObject();
        if (root == NULL) {
            ret = ESP_PEER_ERR_NO_MEM;
            break;
        }
        cJSON_AddStringToObject(root, "transaction", txn);
        if (sig->janus_cfg.token) {
            cJSON_AddStringToObject(root, "token", sig->janus_cfg.token);
        }
        if (sig->janus_cfg.api_secret) {
            cJSON_AddStringToObject(root, "apisecret", sig->janus_cfg.api_secret);
        }
        if (msg->type == ESP_PEER_SIGNALING_MSG_SDP && sig->local_sdp_sent == false) {
            cJSON_AddStringToObject(root, "janus", "message");
            cJSON *body = cJSON_AddObjectToObject(root, "body");
            cJSON_AddStringToObject(body, "request", "publish");
            cJSON_AddBoolToObject(body, "audio", true);
            cJSON_AddBoolToObject(body, "video", true);
            cJSON *jsep = cJSON_AddObjectToObject(root, "jsep");
            cJSON_AddStringToObject(jsep, "type", "offer");
            cJSON_AddStringToObject(jsep, "sdp", (char *)msg->data);
            if (janus_send_api(sig, "POST", post_url, root, &resp) != 0) {
                ESP_LOGE(TAG, "Failed to send SDP");
                ret = ESP_PEER_ERR_FAIL;
                break;
            }
            sig->local_sdp_sent = true;
        } else if (msg->type == ESP_PEER_SIGNALING_MSG_CANDIDATE) {
            cJSON *cand = cJSON_Parse((char *)msg->data);
            if (cand == NULL) {
                ret = ESP_PEER_ERR_INVALID_ARG;
                break;
            }
            cJSON_AddStringToObject(root, "janus", "trickle");
            cJSON_AddItemToObject(root, "candidate", cand);
            if (janus_send_api(sig, "POST", post_url, root, &resp) != 0) {
                ret = ESP_PEER_ERR_FAIL;
                break;
            }
        } else if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
            cJSON_AddStringToObject(root, "janus", "message");
            cJSON *body = cJSON_AddObjectToObject(root, "body");
            cJSON_AddStringToObject(body, "request", "unpublish");
            janus_send_api(sig, "POST", post_url, root, &resp);
        }
    } while (0);

    SAFE_FREE(txn);
    cJSON_Delete(root);
    SAFE_FREE(resp.data);
    return ret;
}

static void janus_wait_for_pool_thread(janus_signaling_t *sig)
{
    if (sig->poll_started && sig->exit_sema) {
        media_lib_sema_lock(sig->exit_sema, 0xFFFFFFFF);
    }
}

static int janus_signaling_stop(esp_peer_signaling_handle_t h)
{
    janus_signaling_t *sig = (janus_signaling_t *)h;
    if (sig == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    char post_url[320] = {0};
    char *txn = NULL;
    cJSON *root = NULL;
    janus_http_resp_t resp = {};
    do {
        sig->running = false;
        txn = janus_create_txn();
        if (txn == NULL || sig->handle_id == 0) {
            janus_wait_for_pool_thread(sig);
            break;
        }
        if (sig->cfg.signal_url) {
            snprintf(post_url, sizeof(post_url), "%s/%" PRIu64 "/%" PRIu64,
                     sig->cfg.signal_url, sig->session_id, sig->handle_id);
        }
        root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "janus", "detach");
            cJSON_AddStringToObject(root, "transaction", txn);
            if (sig->janus_cfg.token) {
                cJSON_AddStringToObject(root, "token", sig->janus_cfg.token);
            }
            if (sig->janus_cfg.api_secret) {
                cJSON_AddStringToObject(root, "apisecret", sig->janus_cfg.api_secret);
            }
            janus_send_api(sig, "POST", post_url, root, &resp);
            cJSON_Delete(root);
            root = NULL;
        }

        janus_wait_for_pool_thread(sig);

        root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "janus", "destroy");
            cJSON_AddStringToObject(root, "transaction", txn);
            if (sig->janus_cfg.token) {
                cJSON_AddStringToObject(root, "token", sig->janus_cfg.token);
            }
            if (sig->janus_cfg.api_secret) {
                cJSON_AddStringToObject(root, "apisecret", sig->janus_cfg.api_secret);
            }
            snprintf(post_url, sizeof(post_url), "%s/%" PRIu64, sig->cfg.signal_url, sig->session_id);
            janus_send_api(sig, "POST", post_url, root, &resp);
        }
    } while (0);

    sig->cfg.on_close(sig->cfg.ctx);

    SAFE_FREE(resp.data);
    if (root) {
        cJSON_Delete(root);
    }
    SAFE_FREE(txn);
    if (sig->exit_sema) {
        media_lib_sema_destroy(sig->exit_sema);
        sig->exit_sema = NULL;
    }
    SAFE_FREE(sig);
    return ESP_PEER_ERR_NONE;
}

const esp_peer_signaling_impl_t *esp_signaling_get_janus_impl(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = janus_signaling_start,
        .send_msg = janus_signaling_send_msg,
        .stop = janus_signaling_stop,
    };
    return &impl;
}
