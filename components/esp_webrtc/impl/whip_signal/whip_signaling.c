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
#include "https_client.h"
#include "esp_peer_signaling.h"
#include "esp_peer_whip_signaling.h"
#include "esp_tls_crypto.h"
#include "esp_log.h"

#define TAG "WHIP_SIGNALING"

#define SAFE_FREE(p) if (p) {   \
    free(p);                    \
    p = NULL;                   \
}

#define MAX_SERVER_SUPPORT        (4)
#define GET_PARAM_VALUE(str, key) get_param_value(str, key, sizeof(key) - 1)

typedef struct {
    esp_peer_signaling_cfg_t       cfg;
    esp_peer_signaling_whip_cfg_t *whip_cfg;
    uint8_t                       *remote_sdp;
    int                            remote_sdp_size;
    bool                           local_sdp_sent;
    char                          *location;
    esp_peer_ice_server_cfg_t     *ice_servers[MAX_SERVER_SUPPORT];
    uint8_t                        server_num;
} whip_signaling_t;

static int whip_signaling_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *h)
{
    whip_signaling_t *sig = (whip_signaling_t *)calloc(1, sizeof(whip_signaling_t));
    if (sig == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    do {
        sig->cfg = *cfg;
        esp_peer_signaling_whip_cfg_t *whip_cfg = (esp_peer_signaling_whip_cfg_t *)cfg->extra_cfg;
        if (whip_cfg && whip_cfg->token) {
            sig->whip_cfg = calloc(1, sizeof(esp_peer_signaling_whip_cfg_t));
            if (sig->whip_cfg == NULL) {
                break;
            }
            sig->whip_cfg->auth_type = whip_cfg->auth_type;
            sig->whip_cfg->token = strdup(whip_cfg->token);
            if (sig->whip_cfg->token == NULL) {
                break;
            }
        }
        *h = sig;
        // Initial ICE info - let WebRTC layer use user-configured servers
        esp_peer_signaling_ice_info_t ice_info = {
            .is_initiator = true,
        };
        ESP_LOGI(TAG, "Using user-configured ICE servers from WebRTC config");
        sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
        // Trigger connected
        sig->cfg.on_connected(sig->cfg.ctx);
        return ESP_PEER_ERR_NONE;
    } while (0);
    SAFE_FREE(sig);
    return ESP_PEER_ERR_NO_MEM;
}

static void whip_sdp_answer(http_resp_t *resp, void *ctx)
{
    whip_signaling_t *sig = (whip_signaling_t *)ctx;
    printf("Get remote SDP %s\n", (char *)resp->data);
    SAFE_FREE(sig->remote_sdp);
    sig->remote_sdp = (uint8_t *)malloc(resp->size);
    if (sig->remote_sdp == NULL) {
        ESP_LOGE(TAG, "No enough memory for remote sdp");
        return;
    }
    memcpy(sig->remote_sdp, resp->data, resp->size);
    sig->remote_sdp_size = resp->size;
}

static char *get_full_path(char *base, const char *url)
{
    if (strncmp(url, "http", 3) == 0) {
        return strdup(url); // Already a full URL
    }
    char *root_path = strstr(base, "//");
    if (!root_path) {
        return NULL;
    }
    root_path += 2;
    char *root_path_end = url[0] == '/' ? strchr(root_path, '/') : strrchr(root_path, '/') + 1;
    int base_len = root_path_end ? root_path_end - base : strlen(base);
    int url_len = strlen(url);
    char *full_path = malloc(base_len + url_len + 1);
    if (!full_path) {
        return NULL;
    }
    strncpy(full_path, base, base_len);
    strcpy(full_path + base_len, url);
    return full_path;
}

static char *get_param_value(char *s, char *param, int param_size)
{
    char *v = strstr(s, param);
    if (v == NULL) {
        return NULL;
    }
    v += param_size;
    char *v_end = strchr(v, '"');
    if (v_end == NULL) {
        return NULL;
    }
    *v_end = 0;
    v = strdup(v);
    *v_end = '"';
    return v;
}

static int extract_ice_info(whip_signaling_t *sig, char *link)
{
    if (sig->server_num > MAX_SERVER_SUPPORT) {
        return ESP_PEER_ERR_OVER_LIMITED;
    }
    
    // Check if this Link header is for an ICE server
    if (strstr(link, "rel=\"ice-server\"") == NULL) {
        ESP_LOGD(TAG, "Skipping non-ICE server Link header: %s", link);
        return ESP_PEER_ERR_NONE; // Not an error, just not an ICE server
    }
    
    esp_peer_ice_server_cfg_t server_cfg = {};
    char *start, *end;
    start = strstr(link, "<");
    if (start == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    end = strstr(start, ">");
    if (end == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    *end = '\0'; // Terminate the URL
    server_cfg.stun_url = strdup(start + 1);
    *end = '>';
    start = end + 1;
    server_cfg.user = GET_PARAM_VALUE(link, "username=\"");
    server_cfg.psw = GET_PARAM_VALUE(link, "credential=\"");
    
    ESP_LOGI(TAG, "Found ICE server in Link header: %s", server_cfg.stun_url);
    
    sig->ice_servers[sig->server_num] = (esp_peer_ice_server_cfg_t *)calloc(1, sizeof(esp_peer_ice_server_cfg_t));
    if (sig->ice_servers[sig->server_num]) {
        memcpy(sig->ice_servers[sig->server_num], &server_cfg, sizeof(esp_peer_ice_server_cfg_t));
        sig->server_num++;
    }
    return ESP_PEER_ERR_NONE;
}

static void whip_sdp_header(const char *key, const char *value, void *ctx)
{
    whip_signaling_t *sig = (whip_signaling_t *)ctx;
    if (value == NULL) {
        return;
    }
    if (strcasecmp(key, "Location") == 0) {
        SAFE_FREE(sig->location);
        sig->location = get_full_path(sig->cfg.signal_url, value);
    }
    if (strcasecmp(key, "Link") == 0) {
        extract_ice_info(sig, (char *)value);
    }
}

static char *get_auth_header(esp_peer_signaling_whip_cfg_t *whip_cfg)
{
    if (whip_cfg == NULL || whip_cfg->token == NULL) {
        return NULL;
    }
    int cfg_len = strlen(whip_cfg->token);
    size_t token_len = cfg_len;
    int auth_len = (whip_cfg->auth_type == ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BASIC) ? strlen("Authorization: Basic ") : strlen("Authorization: Bearer ");
    if (whip_cfg->auth_type == ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BASIC) {
        token_len = ((token_len + 2) / 3) * 4 + 1;
    }
    int len = auth_len + token_len + 1;
    char *auth = malloc(len);
    if (auth == NULL) {
        return NULL;
    }
    if (whip_cfg->auth_type == ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BASIC) {
        snprintf(auth, len, "Authorization: Basic ");
        size_t encoded_len = token_len;
        esp_crypto_base64_encode((unsigned char *)auth + auth_len, encoded_len, &encoded_len, (unsigned char *)whip_cfg->token, cfg_len);
        auth[auth_len + encoded_len] = '\0';
    } else {
        snprintf(auth, len, "Authorization: Bearer %s", whip_cfg->token);
    }
    return auth;
}

static int whip_signaling_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    whip_signaling_t *sig = (whip_signaling_t *)h;
    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {

    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        if (sig->local_sdp_sent == false) {
            char content_type[] = "Content-Type: application/sdp";
            char *auth = get_auth_header(sig->whip_cfg);
            char *header[] = { content_type, auth, NULL };
            int ret = https_post(sig->cfg.signal_url, header, (char *)msg->data, whip_sdp_header, whip_sdp_answer, h);
            SAFE_FREE(auth);
            if (ret != 0 || sig->remote_sdp == NULL) {
                ESP_LOGE(TAG, "Fail to post data to %s", sig->cfg.signal_url);
                return ESP_PEER_ERR_FAIL;
            }
            sig->local_sdp_sent = true;
            if (sig->server_num) {
                // Update ice_info with servers from WHIP Link headers
                ESP_LOGI(TAG, "🧊 WHIP Server provided ICE server: %s", sig->ice_servers[0]->stun_url);
                esp_peer_signaling_ice_info_t iec_info = {
                    .is_initiator = true,
                    .server_info = *sig->ice_servers[0],
                };
                // TODO support more servers?
                sig->cfg.on_ice_info(&iec_info, sig->cfg.ctx);
            } else {
                ESP_LOGI(TAG, "No ICE servers provided by WHIP server, using user configuration");
            }
            // Try to extractor stun lists
            esp_peer_signaling_msg_t sdp_msg = {
                .type = ESP_PEER_SIGNALING_MSG_SDP,
                .data = sig->remote_sdp,
                .size = sig->remote_sdp_size,
            };
            sig->cfg.on_msg(&sdp_msg, sig->cfg.ctx);
            SAFE_FREE(sig->remote_sdp);
        } else if (sig->location) {
            char *sdp = strstr((char *)msg->data, "a=group:BUNDLE");
            if (sdp) {
                // Patch for SDP
                char content_type[] = "Content-Type: application/trickle-ice-sdpfrag";
                char *auth = get_auth_header(sig->whip_cfg);
                char *header[] = { content_type, auth, NULL };
                int ret = https_send_request("PATCH", header, sig->location, sdp, NULL, NULL, h);
                SAFE_FREE(auth);
                if (ret != 0) {
                    ESP_LOGE(TAG, "Fail to post data to %s", sig->location);
                    return ESP_PEER_ERR_NONE;
                }
            }
        }
    }
    return ESP_PEER_ERR_NONE;
}

static int whip_signaling_stop(esp_peer_signaling_handle_t h)
{
    whip_signaling_t *sig = (whip_signaling_t *)h;
    if (sig->location) {
        char *auth = get_auth_header(sig->whip_cfg);
        char *header[] = { auth, NULL };
        https_send_request("DELETE", auth ? header : NULL,
                           sig->location, NULL, NULL, NULL, NULL);
        SAFE_FREE(auth);
    }
    sig->cfg.on_close(sig->cfg.ctx);
    SAFE_FREE(sig->location);
    for (int i = 0; i < sig->server_num; i++) {
        SAFE_FREE(sig->ice_servers[i]->stun_url);
        SAFE_FREE(sig->ice_servers[i]->user);
        SAFE_FREE(sig->ice_servers[i]->psw);
        SAFE_FREE(sig->ice_servers[i]);
    }
    if (sig->whip_cfg) {
        SAFE_FREE(sig->whip_cfg->token);
        SAFE_FREE(sig->whip_cfg);
    }
    SAFE_FREE(sig);
    return 0;
}

const esp_peer_signaling_impl_t *esp_signaling_get_whip_impl(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = whip_signaling_start,
        .send_msg = whip_signaling_send_msg,
        .stop = whip_signaling_stop,
    };
    return &impl;
}
