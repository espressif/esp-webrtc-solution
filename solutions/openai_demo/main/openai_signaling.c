/* OpenAI signaling

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "https_client.h"
#include "common.h"
#include "esp_log.h"
#include <cJSON.h>

#define TAG                   "OPENAI_SIGNALING"

#define OPENAI_REALTIME_CALLS_URL "https://api.openai.com/v1/realtime/calls"
#define OPENAI_CLIENT_SECRETS_URL "https://api.openai.com/v1/realtime/client_secrets"

#define SAFE_FREE(p) if (p) {   \
    free(p);                    \
    p = NULL;                   \
}

typedef struct {
    esp_peer_signaling_cfg_t cfg;
    uint8_t                 *remote_sdp;
    int                      remote_sdp_size;
    char                    *api_key;
    char                    *ephemeral_token;
    char                    *model;
    char                    *voice;
} openai_signaling_t;

static char *openai_strdup(const char *str)
{
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str) + 1;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

static char *dup_sdp_msg(esp_peer_signaling_msg_t *msg)
{
    if (msg == NULL || msg->data == NULL) {
        return NULL;
    }
    int sdp_size = msg->size > 0 ? msg->size : strlen((char *)msg->data);
    char *sdp = (char *)malloc(sdp_size + 1);
    if (sdp == NULL) {
        return NULL;
    }
    memcpy(sdp, msg->data, sdp_size);
    sdp[sdp_size] = 0;
    return sdp;
}

static cJSON *build_session_config(openai_signaling_t *sig)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *audio = cJSON_CreateObject();
    cJSON *output = cJSON_CreateObject();
    if (root == NULL || audio == NULL || output == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(audio);
        cJSON_Delete(output);
        return NULL;
    }

    cJSON_AddStringToObject(root, "type", "realtime");
    cJSON_AddStringToObject(root, "model", sig->model);
    cJSON_AddStringToObject(output, "voice", sig->voice);
    cJSON_AddItemToObject(audio, "output", output);
    cJSON_AddItemToObject(root, "audio", audio);

    return root;
}

static char *build_client_secret_request_body(openai_signaling_t *sig)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *session = build_session_config(sig);
    if (root == NULL || session == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(session);
        return NULL;
    }
    cJSON_AddItemToObject(root, "session", session);
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_string;
}

static void client_secret_answer(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    if (resp == NULL || resp->data == NULL || resp->size <= 0) {
        return;
    }

    char *json = (char *)malloc(resp->size + 1);
    if (json == NULL) {
        ESP_LOGE(TAG, "No enough memory for client secret response");
        return;
    }
    memcpy(json, resp->data, resp->size);
    json[resp->size] = 0;

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "Fail to parse client secret response: %.*s",
                 resp->size < 256 ? resp->size : 256, resp->data);
        free(json);
        return;
    }

    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (!cJSON_IsString(value)) {
        cJSON *client_secret = cJSON_GetObjectItemCaseSensitive(root, "client_secret");
        value = cJSON_IsObject(client_secret) ? cJSON_GetObjectItemCaseSensitive(client_secret, "value") : NULL;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        SAFE_FREE(sig->ephemeral_token);
        sig->ephemeral_token = openai_strdup(value->valuestring);
    } else {
        ESP_LOGE(TAG, "Client secret response does not contain token: %s", json);
    }

    cJSON_Delete(root);
    free(json);
}

static int fetch_ephemeral_token(openai_signaling_t *sig)
{
    char *body = build_client_secret_request_body(sig);
    if (body == NULL) {
        ESP_LOGE(TAG, "Fail to build client secret request");
        return -1;
    }

    char content_type[] = "Content-Type: application/json";
    int auth_len = strlen("Authorization: Bearer ") + strlen(sig->api_key) + 1;
    char auth[auth_len];
    snprintf(auth, auth_len, "Authorization: Bearer %s", sig->api_key);
    char *header[] = {
        content_type,
        auth,
        NULL,
    };

    https_request_cfg_t req_cfg = {
        .timeout_ms = 30000,
        .max_redirects = DEFAULT_HTTPS_MAX_REDIRECTS,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .keep_alive_enable = true,
        .keep_alive_count = DEFAULT_HTTPS_KEEP_ALIVE_COUNT,
        .keep_alive_idle = DEFAULT_HTTPS_KEEP_ALIVE_IDLE,
        .keep_alive_interval = DEFAULT_HTTPS_KEEP_ALIVE_INTERVAL,
    };
    https_request_t req = {
        .method = "POST",
        .url = OPENAI_CLIENT_SECRETS_URL,
        .headers = header,
        .data = body,
        .body_cb = client_secret_answer,
        .ctx = sig,
    };

    ESP_LOGI(TAG, "Creating OpenAI Realtime client secret");
    SAFE_FREE(sig->ephemeral_token);
    int ret = https_request_advance(&req_cfg, &req);
    SAFE_FREE(body);
    if (ret != 0 || sig->ephemeral_token == NULL) {
        ESP_LOGE(TAG, "Fail to create OpenAI Realtime client secret");
        return -1;
    }
    return 0;
}

static int openai_signaling_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *h)
{
    if (cfg == NULL || h == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    openai_signaling_cfg_t *openai_cfg = (openai_signaling_cfg_t *)cfg->extra_cfg;
    if (openai_cfg == NULL || openai_cfg->token == NULL || openai_cfg->token[0] == '\0') {
        ESP_LOGE(TAG, "OpenAI API key is empty");
        return ESP_PEER_ERR_INVALID_ARG;
    }

    openai_signaling_t *sig = (openai_signaling_t *)calloc(1, sizeof(openai_signaling_t));
    if (sig == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }

    sig->cfg = *cfg;
    sig->api_key = openai_strdup(openai_cfg->token);
    sig->model = openai_strdup(openai_cfg->model ? openai_cfg->model : OPENAI_DEFAULT_MODEL);
    sig->voice = openai_strdup(openai_cfg->voice ? openai_cfg->voice : OPENAI_DEFAULT_VOICE);
    if (sig->api_key == NULL || sig->model == NULL || sig->voice == NULL) {
        SAFE_FREE(sig->api_key);
        SAFE_FREE(sig->model);
        SAFE_FREE(sig->voice);
        SAFE_FREE(sig);
        return ESP_PEER_ERR_NO_MEM;
    }
    if (fetch_ephemeral_token(sig) != 0) {
        SAFE_FREE(sig->api_key);
        SAFE_FREE(sig->ephemeral_token);
        SAFE_FREE(sig->model);
        SAFE_FREE(sig->voice);
        SAFE_FREE(sig);
        return ESP_PEER_ERR_FAIL;
    }

    *h = sig;
    esp_peer_signaling_ice_info_t ice_info = {
        .is_initiator = true,
    };
    if (sig->cfg.on_ice_info) {
        sig->cfg.on_ice_info(&ice_info, sig->cfg.ctx);
    }
    if (sig->cfg.on_connected) {
        sig->cfg.on_connected(sig->cfg.ctx);
    }
    return ESP_PEER_ERR_NONE;
}

static void openai_sdp_answer(http_resp_t *resp, void *ctx)
{
    openai_signaling_t *sig = (openai_signaling_t *)ctx;
    if (resp == NULL || resp->data == NULL || resp->size <= 0) {
        return;
    }

    SAFE_FREE(sig->remote_sdp);
    sig->remote_sdp = (uint8_t *)malloc(resp->size + 1);
    if (sig->remote_sdp == NULL) {
        ESP_LOGE(TAG, "No enough memory for remote sdp");
        return;
    }
    memcpy(sig->remote_sdp, resp->data, resp->size);
    sig->remote_sdp[resp->size] = 0;
    sig->remote_sdp_size = resp->size;
    ESP_LOGI(TAG, "Get remote SDP answer, size=%d", sig->remote_sdp_size);
}

static int openai_signaling_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    if (h == NULL || msg == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    openai_signaling_t *sig = (openai_signaling_t *)h;
    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {

    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        printf("Receive local SDP\n");
        char *sdp = dup_sdp_msg(msg);
        if (sdp == NULL) {
            ESP_LOGE(TAG, "Fail to build Realtime GA SDP request");
            SAFE_FREE(sdp);
            return -1;
        }

        char content_type[] = "Content-Type: application/sdp";
        int len = strlen("Authorization: Bearer ") + strlen(sig->ephemeral_token) + 1;
        char auth[len];
        snprintf(auth, len, "Authorization: Bearer %s", sig->ephemeral_token);
        char *header[] = {
            content_type,
            auth,
            NULL,
        };
        https_request_cfg_t req_cfg = {
            .timeout_ms = 30000,
            .max_redirects = DEFAULT_HTTPS_MAX_REDIRECTS,
            .buffer_size = 4096,
            .buffer_size_tx = 8192,
            .keep_alive_enable = true,
            .keep_alive_count = DEFAULT_HTTPS_KEEP_ALIVE_COUNT,
            .keep_alive_idle = DEFAULT_HTTPS_KEEP_ALIVE_IDLE,
            .keep_alive_interval = DEFAULT_HTTPS_KEEP_ALIVE_INTERVAL,
        };
        https_request_t req = {
            .method = "POST",
            .url = OPENAI_REALTIME_CALLS_URL,
            .headers = header,
            .data = sdp,
            .body_cb = openai_sdp_answer,
            .ctx = h,
        };

        ESP_LOGI(TAG, "Exchanging SDP with OpenAI Realtime GA API");
        SAFE_FREE(sig->remote_sdp);
        sig->remote_sdp_size = 0;
        int ret = https_request_advance(&req_cfg, &req);
        SAFE_FREE(sdp);
        if (ret != 0 || sig->remote_sdp == NULL || strncmp((char *)sig->remote_sdp, "v=0", 3) != 0) {
            if (sig->remote_sdp) {
                int print_size = sig->remote_sdp_size < 256 ? sig->remote_sdp_size : 256;
                ESP_LOGE(TAG, "Realtime call failed, response: %.*s", print_size, (char *)sig->remote_sdp);
            }
            ESP_LOGE(TAG, "Fail to post data to %s", OPENAI_REALTIME_CALLS_URL);
            return -1;
        }
        esp_peer_signaling_msg_t sdp_msg = {
            .type = ESP_PEER_SIGNALING_MSG_SDP,
            .data = sig->remote_sdp,
            .size = sig->remote_sdp_size,
        };
        if (sig->cfg.on_msg) {
            sig->cfg.on_msg(&sdp_msg, sig->cfg.ctx);
        }
    }
    return 0;
}

static int openai_signaling_stop(esp_peer_signaling_handle_t h)
{
    if (h == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    openai_signaling_t *sig = (openai_signaling_t *)h;
    if (sig->cfg.on_close) {
        sig->cfg.on_close(sig->cfg.ctx);
    }
    SAFE_FREE(sig->remote_sdp);
    SAFE_FREE(sig->api_key);
    SAFE_FREE(sig->ephemeral_token);
    SAFE_FREE(sig->model);
    SAFE_FREE(sig->voice);
    SAFE_FREE(sig);
    return 0;
}

const esp_peer_signaling_impl_t *esp_signaling_get_openai_signaling(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = openai_signaling_start,
        .send_msg = openai_signaling_send_msg,
        .stop = openai_signaling_stop,
    };
    return &impl;
}
