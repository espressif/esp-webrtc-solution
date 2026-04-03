/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include <esp_event.h>
#include <esp_system.h>
#include "esp_netif.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "esp_tls.h"
#include "cJSON.h"
#include <mbedtls/base64.h>

#include "esp_peer_signaling.h"
#include "https_client.h"

#define TAG "Signal"

#define WSS_SEND_WAIT_MS (5000)
#define BASE64_LEN(x) (((x) + 2) / 3 * 4)

/* Canonical URI paths for KVS Signalling */
#define HTTP_API_DESCRIBE_SIGNALING_CHANNEL     "/describeSignalingChannel"
#define HTTP_API_GET_SIGNALING_CHANNEL_ENDPOINT "/getSignalingChannelEndpoint"
#define HTTP_API_GET_ICE_CONFIG                 "/v1/get-ice-server-config"

#define SIGNALING_SDP_TYPE_OFFER       "SDP_OFFER"
#define SIGNALING_SDP_TYPE_ANSWER      "SDP_ANSWER"
#define SIGNALING_ICE_CANDIDATE        "ICE_CANDIDATE"
#define SIGNALING_GO_AWAY              "GO_AWAY"
#define SIGNALING_RECONNECT_ICE_SERVER "RECONNECT_ICE_SERVER"
#define SIGNALING_STATUS_RESPONSE      "STATUS_RESPONSE"

typedef struct {
    char *client_id;
    char *wss_url;        /* WSS endpoint from getSignalingChannelEndpoint */
    char *ice_server;     /* HTTPS endpoint for ICE config */
    char *wss_url_signed; /* SigV4 signed WSS URL */
    char *channel_arn;
} client_info_t;

typedef struct {
    esp_websocket_client_handle_t ws;
    int            connected;
} wss_client_t;

typedef struct {
    client_info_t     client_info;
    esp_peer_signaling_ice_info_t ice_info;
    wss_client_t*     wss_client;
    esp_peer_signaling_cfg_t      cfg;
} wss_sig_t;

static int wss_signal_stop(esp_peer_signaling_handle_t sig);

static void free_client_info(client_info_t* info) {
    free(info->channel_arn);
    free(info->wss_url);
    free(info->wss_url_signed);
    free(info->ice_server);
    free(info->client_id);
    memset(info, 0, sizeof(client_info_t));
}

static void free_ice_info(esp_peer_signaling_ice_info_t* info) {
    if (info->server_info.stun_url) {
        free(info->server_info.stun_url);
    }
    if (info->server_info.user) {
        free(info->server_info.user);
    }
    if (info->server_info.psw) {
        free(info->server_info.psw);
    }
    memset(info, 0, sizeof(esp_peer_signaling_ice_info_t));
}

static void describe_signalling_channel_resp(http_resp_t* resp, void* ctx) {
    const char* data = (const char*)resp->data;
    ESP_LOGD(TAG, "Received: %s", data);
    cJSON *root = cJSON_Parse((const char *)data);
    if (!root) {
        return;
    }
    client_info_t* info = (client_info_t*)ctx;
    cJSON *info_json = cJSON_GetObjectItem(root, "ChannelInfo");
    if (!info_json) {
        ESP_LOGE(TAG, "ChannelInfo not found in response");
    }
    cJSON *arn_json = cJSON_GetObjectItem(info_json, "ChannelARN");
    if (arn_json) {
        info->channel_arn = strdup(arn_json->valuestring);
        ESP_LOGI(TAG, "ChannelARN: %s", info->channel_arn);
    }

    cJSON_Delete(root);
}

static void get_signalling_channel_endpoint_resp(http_resp_t* resp, void* ctx) {
    const char* data = (const char*)resp->data;

    cJSON *root = cJSON_Parse((const char *)data);
    if (!root) {
        return;
    }
    client_info_t* info = (client_info_t*)ctx;
    cJSON *endpoint_list_json = cJSON_GetObjectItem(root, "ResourceEndpointList");
    if (!endpoint_list_json) {
        ESP_LOGE(TAG, "ResourceEndpointList not found");
    }
    int total_items = cJSON_GetArraySize(endpoint_list_json);
    for (int i = 0; i < total_items; i++) {
		cJSON *subitem = cJSON_GetArrayItem(endpoint_list_json, i);
        cJSON *protocol = cJSON_GetObjectItem(subitem, "Protocol");
        cJSON *resource_ep = cJSON_GetObjectItem(subitem, "ResourceEndpoint");
        if (protocol && resource_ep) {
            if (strcmp(protocol->valuestring, "WSS") == 0) { /* wss */
                ESP_LOGI(TAG, "Found WSS Resource Endpoint %s", resource_ep->valuestring);
                info->wss_url = strdup(resource_ep->valuestring);
            } else if (strcmp(protocol->valuestring, "HTTPS") == 0) { /* https */
                ESP_LOGI(TAG, "Found HTTPS Resource Endpoint %s", resource_ep->valuestring);
                info->ice_server = strdup(resource_ep->valuestring);
            } else {
                ESP_LOGW(TAG, "Skipping Unsupported protocol endpoint %s: %s", protocol->valuestring, resource_ep->valuestring);
            }
        }
	}

    cJSON_Delete(root);
}

static void credential_body(http_resp_t* resp, void* ctx) {
    const char* data = (const char*)resp->data;

    cJSON *root = cJSON_Parse((const char *)data);
    if (!root) {
        return;
    }
    esp_peer_signaling_ice_info_t* ice = (esp_peer_signaling_ice_info_t*)ctx;
    do {
        cJSON *ice_servers = cJSON_GetObjectItem(root, "IceServerList");
        if (ice_servers == NULL) {
            break;
        }
        cJSON *server_arr = cJSON_GetArrayItem(ice_servers, 0);
        if (server_arr == NULL) {
            break;
        }
        cJSON *url_arr = cJSON_GetObjectItem(server_arr, "Uris");
        if (url_arr == NULL) {
            break;
        }
        cJSON *urls = cJSON_GetArrayItem(url_arr, 0);
        if (urls == NULL) {
            break;
        }

        /* TODO: Use other URLs as well */
        ice->server_info.stun_url = strdup(urls->valuestring);
        if (ice->server_info.stun_url == NULL) {
            break;
        }
        cJSON *user = cJSON_GetObjectItem(server_arr, "Username");
        if (user == NULL) {
            break;
        }
        ice->server_info.user = strdup(user->valuestring);
        cJSON *psw = cJSON_GetObjectItem(server_arr, "Password");
        if (psw == NULL) {
            break;
        }
        ice->server_info.psw = strdup(psw->valuestring);
        ESP_LOGI(TAG, "Got url:%s user_name: %s psw:%s",  ice->server_info.stun_url,
                 ice->server_info.user, ice->server_info.psw);
    } while(0);
    cJSON_Delete(root);
}

static void destroy_wss(wss_client_t* wss)
{
    if (wss->ws) {
        esp_websocket_client_stop(wss->ws);
        esp_websocket_client_destroy(wss->ws);
    }
    free(wss);
}

/* The custom on_connect handler for this instance of the websocket code. */
static int on_connect(void *user)
{
    wss_sig_t* sg = user;
    sg->wss_client->connected = true;
    if (sg->cfg.on_connected) {
        sg->cfg.on_connected(sg->cfg.ctx);
    }
    return 0;
}

/* The custom on_text handler for this instance of the websocket code. */
static int on_text(void *user, const char *text, size_t len)
{
    if (len == 0) {
        return 0;
    }
    ESP_LOGD(TAG, "on_text(user, ws, '%.*s', %zd)", (int) len, text, len);
    wss_sig_t* sg = user;
    char *base64_decoded_msg = NULL;
    size_t sdp_json_len = 0;
    bool is_ice_candidate = false;

    if (sg->cfg.on_msg) {
        cJSON *_json = cJSON_Parse(text);
        cJSON *msg = NULL;
        do {
            if (_json == NULL) {
                break;
            }
            msg = cJSON_GetObjectItem(_json, "senderClientId");
            if (msg != NULL) {
                ESP_LOGI(TAG, "Received msg from client %s", msg->valuestring);
                sg->client_info.client_id = strdup(msg->valuestring);
            }
            msg = cJSON_GetObjectItem(_json, "messageType");
            if (msg != NULL) {
                ESP_LOGI(TAG, "MessageType: %s", msg->valuestring);
                if (strcmp(msg->valuestring, "SDP_OFFER") != 0) {
                    is_ice_candidate = true;
                }
            } else {
                break;
            }

            msg = cJSON_GetObjectItem(_json, "messagePayload");

            int required_len = strlen(msg->valuestring); /* TODO: Do precise compact allocation */
            base64_decoded_msg = heap_caps_calloc(1, required_len + 1, MALLOC_CAP_SPIRAM);
            if (!base64_decoded_msg) {
                ESP_LOGE(TAG, "Failed to allocate memory for sdp json");
                break;
            }
            /* TODO: Check error code */
            mbedtls_base64_decode((unsigned char *) base64_decoded_msg, required_len, &sdp_json_len,
                                   (const unsigned char *) msg->valuestring, required_len);
        } while (0);

        cJSON_Delete(_json);
    }

    if (base64_decoded_msg) {
        ESP_LOGI(TAG, "base64_decoded_msg: \n%s", base64_decoded_msg);
        cJSON *_json = cJSON_Parse(base64_decoded_msg);
        cJSON *msg = NULL;

        do {
            if (_json == NULL) {
                break;
            }

            if (is_ice_candidate) { /* Just get the candidate and do away with it */
                cJSON *candidate = cJSON_GetObjectItem(_json, "candidate");
                if (candidate) {
                    esp_peer_signaling_msg_t msg = {
                        .type = ESP_PEER_SIGNALING_MSG_CANDIDATE,
                        .data = (uint8_t*)candidate->valuestring,
                        .size = strlen(candidate->valuestring),
                    };
                    sg->cfg.on_msg(&msg, sg->cfg.ctx);
                }
                break;
            }

            // Json string in json
            cJSON * method;
            if (msg == NULL) {
                method = cJSON_GetObjectItem(_json, "type");
                if (method == NULL) {
                    break;
                }
                msg = _json;
            } else {
                msg = cJSON_Parse(msg->valuestring);
                method = cJSON_GetObjectItem(msg, "type");
                if (method == NULL) {
                    break;
                }
            }
            if (strcmp(method->valuestring, "offer") == 0) {
                ESP_LOGI(TAG, "Received SDP OFFER from peer");
                cJSON *sdp = cJSON_GetObjectItem(msg, "sdp");
                if (sdp) {
                    ESP_LOGI(TAG, "Forwarding offer to WebRTC stack, client_id=%s, is_initiator=%d",
                             sg->client_info.client_id ? sg->client_info.client_id : "NULL",
                             sg->ice_info.is_initiator);
                    /* Set is_initiator to false BEFORE forwarding to WebRTC stack */
                    /* This ensures WebRTC stack knows we should send an answer, not an offer */
                    sg->ice_info.is_initiator = false;
                    ESP_LOGI(TAG, "Set is_initiator=false before forwarding offer");
                    esp_peer_signaling_msg_t msg = {
                        .type = ESP_PEER_SIGNALING_MSG_SDP,
                        .data = (uint8_t*)sdp->valuestring,
                        .size = strlen(sdp->valuestring),
                    };
                    sg->cfg.on_msg(&msg, sg->cfg.ctx);
                }
            }
            else if (strcmp(method->valuestring, "answer") == 0) {
                cJSON *sdp = cJSON_GetObjectItem(msg, "sdp");
                if (sdp) {
                    esp_peer_signaling_msg_t msg = {
                        .type = ESP_PEER_SIGNALING_MSG_SDP,
                        .data = (uint8_t*)sdp->valuestring,
                        .size = strlen(sdp->valuestring),
                    };
                    sg->cfg.on_msg(&msg, sg->cfg.ctx);
                }
            }
            else if (strcmp(method->valuestring, "bye") == 0) {
                // Peer closed
                esp_peer_signaling_msg_t msg = {
                    .type = ESP_PEER_SIGNALING_MSG_BYE,
                };
                sg->cfg.on_msg(&msg, sg->cfg.ctx);
                // When peer leave change rule to caller directly
                ESP_LOGI(TAG, "Peer leaved become controlling now");
                sg->ice_info.is_initiator = true;
            }
            else if (strcmp(method->valuestring, "candidate") == 0) {
                ESP_LOGI(TAG, "Received ICE candidate from peer");
                cJSON *candidate = cJSON_GetObjectItem(msg, "candidate");
                if (candidate) {
                    esp_peer_signaling_msg_t msg = {
                        .type = ESP_PEER_SIGNALING_MSG_CANDIDATE,
                        .data = (uint8_t*)candidate->valuestring,
                        .size = strlen(candidate->valuestring),
                    };
                    sg->cfg.on_msg(&msg, sg->cfg.ctx);
                }
            }
        } while(0);

        if (!is_ice_candidate) {
            if (msg == NULL) {
                ESP_LOGE(TAG, "Bad json input");
            } else if (msg != _json) {
                cJSON_Delete(msg);
            }
        }
        cJSON_Delete(_json);
        free(base64_decoded_msg);
    }
    return 0;
}

/* The custom on_close handler for this instance of the websocket code. */
static int on_close(void *user,  int code)
{
    ESP_LOGD(TAG, "on_close code %d", code);
    wss_sig_t* sg = (wss_sig_t*)user;
    if (sg->cfg.on_close) {
        sg->cfg.on_close(sg->cfg.ctx);
    }
    return 0;
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

#define RECEIVED_MSG_BUF_SIZE 20000
typedef struct received_msg {
    char buf[RECEIVED_MSG_BUF_SIZE];
    int data_size;
} received_msg_t;
static received_msg_t *websocket_msg;

static void websocket_event_handler(void *ctx, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        on_connect(ctx);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        on_close(ctx, (int)data->error_handle.esp_ws_handshake_status_code);
        log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        // ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);
        // ESP_LOGI(TAG, "data_ptr: %p, fin: %d\n", data->data_ptr, (int) data->fin);
        if (!websocket_msg) {
            websocket_msg = heap_caps_malloc(sizeof(received_msg_t), MALLOC_CAP_SPIRAM);
            if (!websocket_msg) {
                ESP_LOGE(TAG, "Could not allocate for websocket message");
                break;
            }
            websocket_msg->data_size = 0;
        }
        if (!data->fin) {
            if (websocket_msg->data_size + data->data_len > RECEIVED_MSG_BUF_SIZE) {
                ESP_LOGE(TAG, "Cannot fit all the data in %d buffer. current_filled: %d, incoming_data: %d",
                         RECEIVED_MSG_BUF_SIZE, websocket_msg->data_size, data->data_len);
                websocket_msg->data_size = RECEIVED_MSG_BUF_SIZE + 1; /* Overflow the limit forcefully */
            } else { /* Gather fragmented message */
                memcpy(websocket_msg->buf + websocket_msg->data_size, data->data_ptr, data->data_len);
                websocket_msg->data_size += data->data_len;
            }
        } else if (websocket_msg->data_size == 0) { /* non-fragmented message */
            on_text(ctx, data->data_ptr, data->data_len);
        } else { /* final part of the fragment */
            if (websocket_msg->data_size + data->data_len <= RECEIVED_MSG_BUF_SIZE) { /* valid fragmented gathered message */
                /* Copy last piece */
                memcpy(websocket_msg->buf + websocket_msg->data_size, data->data_ptr, data->data_len);
                websocket_msg->data_size += data->data_len;
                /* Process now... */
                on_text(ctx, websocket_msg->buf, websocket_msg->data_size);
            } else {
                ESP_LOGW(TAG, "Discarding the message which could not be fit");
            }
            websocket_msg->data_size = 0;
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
        }
        break;
    }
}

static int create_wss(wss_sig_t* sg)
{
    wss_client_t* wss = calloc(1, sizeof(wss_client_t));
    if (wss == NULL) {
        return -1;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = sg->client_info.wss_url_signed,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 60 * 1000,
        .network_timeout_ms = 10000,
        .buffer_size = 4 * 1024,
    };
    ESP_LOGI(TAG, "Connecting to %s...", ws_cfg.uri);
    wss->ws = esp_websocket_client_init(&ws_cfg);

    do {
        if (wss->ws == NULL) {
            break;
        }
        esp_websocket_register_events(wss->ws, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)sg);
        int ret = esp_websocket_client_start(wss->ws);
        if (ret != 0) {
            break;
        }
        sg->wss_client = wss;
        return ret;
    } while (0);
    return  -1;
}

/**
 * Parameterized string for KVS STUN Server
 */
#define KINESIS_VIDEO_STUN_URL_POSTFIX      "amazonaws.com"
#define KINESIS_VIDEO_STUN_URL_POSTFIX_CN   "amazonaws.com.cn"
#define KINESIS_VIDEO_STUN_URL              "stun:stun.kinesisvideo.%s.%s:443"

const char *channel_name = CONFIG_KVS_CHANNEL_NAME;
const char *describe_channel_body_template =
"{\
    \"ChannelName\": \"%s\"\
}";

const char *get_signalling_endpoint_body_template =
"{\
    \"ChannelARN\": \"%s\",\
    \"SingleMasterChannelEndpointConfiguration\": {\
        \"Protocols\": [\"WSS\", \"HTTPS\"],\
        \"Role\": \"MASTER\"\
    }\
}";

/**
 * @brief   API_AWSAcuitySignalingService_GetIceServerConfig
 * POST /v1/get-ice-server-config HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelARN": "string",
 *    "ClientId": "string",
 *    "Service": "string",
 *    "Username": "string"
 * }
 */
#define APP_MASTER_CLIENT_ID                 "ProducerMaster"

const char *get_ice_config_template =
"{\
    \"ChannelARN\": \"%s\",\
    \"ClientId\": \"%s\",\
    \"Service\": \"TURN\"\
}";

#define HTTP_BODY_BUF_SIZE   2048

static int wss_signal_start(esp_peer_signaling_cfg_t* cfg, esp_peer_signaling_handle_t* h)
{
    if (cfg->signal_url == NULL || cfg == NULL || h == NULL) {
        return -1;
    }
    wss_sig_t* sg = calloc(1, sizeof(wss_sig_t));
    if (sg == NULL) {
        return -1;
    }
    int ret = 0;

    char *http_body = heap_caps_calloc(1, HTTP_BODY_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!http_body) {
        ESP_LOGE(TAG, "Could not allocate buffer for http_body");
    }

    /* For describeSignalingChannel, use control plane endpoint: kinesisvideo.<region>.amazonaws.com */
    /* The data plane endpoint (m-<endpoint-id>) is obtained later from getSignalingChannelEndpoint */
    int required_len = 8 + strlen("kinesisvideo.") + strlen(REGION) + strlen(".amazonaws.com") + strlen(HTTP_API_DESCRIBE_SIGNALING_CHANNEL) + 1;
    char *complete_url = heap_caps_calloc(1, required_len, MALLOC_CAP_SPIRAM);
    if (!complete_url) {
        ESP_LOGE(TAG, "Failed to allocate complete_url");
        ret = -1;
        goto __exit;
    }
    snprintf(complete_url, required_len, "https://kinesisvideo.%s.amazonaws.com%s", REGION, HTTP_API_DESCRIBE_SIGNALING_CHANNEL);
    sprintf(http_body, describe_channel_body_template, channel_name);

    // Http post to get client info firstly
    https_post(complete_url, NULL, http_body, describe_signalling_channel_resp, &sg->client_info);
    free(complete_url);

    if (sg->client_info.channel_arn == NULL) {
        ESP_LOGE(TAG, "Failed to get the ChannelArn");
        ret = -1;
        goto __exit;
    }

    /* For getSignalingChannelEndpoint, also use control plane endpoint */
    required_len = 8 + strlen("kinesisvideo.") + strlen(REGION) + strlen(".amazonaws.com") + strlen(HTTP_API_GET_SIGNALING_CHANNEL_ENDPOINT) + 1;
    complete_url = heap_caps_calloc(1, required_len, MALLOC_CAP_SPIRAM);
    if (!complete_url) {
        ESP_LOGE(TAG, "Failed to allocate complete_url");
        ret = -1;
        goto __exit;
    }
    snprintf(complete_url, required_len, "https://kinesisvideo.%s.amazonaws.com%s", REGION, HTTP_API_GET_SIGNALING_CHANNEL_ENDPOINT);
    sprintf(http_body, get_signalling_endpoint_body_template, sg->client_info.channel_arn);

    // Http post to get client info firstly
    https_post(complete_url, NULL, http_body, get_signalling_channel_endpoint_resp, &sg->client_info);
    free(complete_url);

    if (sg->client_info.wss_url == NULL || sg->client_info.ice_server == NULL) {
        ESP_LOGE(TAG, "Fail to get wss_url and/or ice_server URL");
        ret = -1;
        goto __exit;
    }

    /* get Ice server config */
    int ice_server_len = strlen(sg->client_info.ice_server) + strlen(HTTP_API_GET_ICE_CONFIG);
    complete_url = heap_caps_calloc(1, ice_server_len + 1, MALLOC_CAP_SPIRAM);
    sprintf(complete_url, "%s%s", sg->client_info.ice_server, HTTP_API_GET_ICE_CONFIG);
    sprintf(http_body, get_ice_config_template, sg->client_info.channel_arn, APP_MASTER_CLIENT_ID);

    https_post(complete_url, NULL, http_body, credential_body, &sg->ice_info);
    free(complete_url);
    free(http_body);
    http_body = NULL;

    /* KVS MASTER receives offer from VIEWER, so we are NOT the initiator */
    sg->ice_info.is_initiator = false;
    if (sg->ice_info.server_info.psw == NULL) {
        ESP_LOGE(TAG, "Fail to get password");
        ret = -1;
        goto __exit;
    }
    // Copy configuration
    sg->cfg = *cfg;
    if (sg->cfg.on_ice_info) {
        sg->cfg.on_ice_info(&sg->ice_info, sg->cfg.ctx);
    }

    /* Get Signed URL */
    get_signed_wss_url(sg->client_info.wss_url, sg->client_info.channel_arn, &sg->client_info.wss_url_signed);

    if (!sg->client_info.wss_url_signed) {
        ESP_LOGE(TAG, "Failed to get signed wss_url");
    }

    ESP_LOGI(TAG, "Connecting to signaling channel.");
    *h = sg;
    ret = create_wss(sg);
    if (ret == 0) {
        return ret;
    }
__exit:
    *h = NULL;
    if (http_body) {
        free(http_body);
    }
    wss_signal_stop(sg);
    return ret;
}

// Send message JSON template
#define WSS_MESSAGE_TEMPLATE                        \
    "{\n"                                           \
    "\t\"action\": \"%s\",\n"                       \
    "\t\"RecipientClientId\": \"%s\",\n"            \
    "\t\"MessagePayload\": \""

static int wss_signal_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t* msg)
{
    wss_sig_t *sg = (wss_sig_t*) h;
    char *msg_type = SIGNALING_SDP_TYPE_OFFER;

    ESP_LOGI(TAG, "wss_signal_send_msg called: type=%d", msg->type);

    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        return 0;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return -1;
    }

    if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        if (sg->ice_info.is_initiator) {
            cJSON_AddStringToObject(json, "type", "offer");
            msg_type = SIGNALING_SDP_TYPE_OFFER;
        } else {
            cJSON_AddStringToObject(json, "type", "answer");
            msg_type = SIGNALING_SDP_TYPE_ANSWER;
        }
        cJSON_AddStringToObject(json, "sdp", (char*)msg->data);
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_CANDIDATE) {
        cJSON_AddStringToObject(json, "type", "candidate");
        cJSON_AddStringToObject(json, "candidate", (char*)msg->data);
        msg_type = SIGNALING_ICE_CANDIDATE;
    } else {
        return 0;
    }

    int ret = -1;
    char *payload_encoded = NULL;
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (payload) {
        ESP_LOGI(TAG, "json payload non-encoded: %s", payload);
    } else {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted as payload failed");
        return -1;
    }

    int payload_len = strlen(payload);
    int base64_encoded_len = BASE64_LEN(payload_len);
    char *client_id = sg->client_info.client_id;

    /* Check if client_id is set - if not, we can't send the message */
    if (!client_id) {
        ESP_LOGE(TAG, "client_id not set yet, cannot send message! Message type: %d", msg->type);
        ESP_LOGE(TAG, "This should not happen - client_id should be set when receiving first message from peer");
        free(payload);
        return -1;
    }

    ESP_LOGI(TAG, "Sending message to client_id: %s, msg_type: %s", client_id, msg_type);

    int required_len = strlen(WSS_MESSAGE_TEMPLATE) + strlen(msg_type)
                + strlen(client_id) + base64_encoded_len;
    payload_encoded = heap_caps_calloc(1, required_len + 1, MALLOC_CAP_SPIRAM);
    if (!payload_encoded) {
        ESP_LOGE(TAG, "Failed to allocate for encoded payload");
        free(payload);
        return -1;
    }

    int consumed = sprintf(payload_encoded, WSS_MESSAGE_TEMPLATE, msg_type, client_id);

    size_t out_len = 0;
    if (mbedtls_base64_encode((unsigned char *) (payload_encoded + consumed), required_len - consumed,
                              &out_len, (const unsigned char *) payload, payload_len) != 0) {

        ESP_LOGE(TAG, "mbedtls_base64_encode failed");
        free(payload_encoded);
        free(payload);
        return -1;
    }
    consumed += out_len;
    payload_encoded[consumed] = '\0';

    /* Close the json */
    strcat(payload_encoded, "\"}");

    ESP_LOGI(TAG, "Sending json payload encoded: %s", payload_encoded);

    esp_websocket_client_handle_t client = sg->wss_client->ws;
    payload_len = strlen(payload_encoded);
    ret = esp_websocket_client_send_text(client, (const char *) payload_encoded,
                                         payload_len, pdMS_TO_TICKS(WSS_SEND_WAIT_MS)) <= 0;

    free(payload_encoded);
    free(payload);

    return ret;
}

int wss_signal_stop(esp_peer_signaling_handle_t h)
{
    wss_sig_t* sg = (wss_sig_t*)h;
    if (sg->wss_client) {
        destroy_wss(sg->wss_client);
    }
    free_client_info(&sg->client_info);
    free_ice_info(&sg->ice_info);
    free(sg);
    return 0;
}

const esp_peer_signaling_impl_t* esp_signaling_get_kvs_impl(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = wss_signal_start,
        .send_msg = wss_signal_send_msg,
        .stop = wss_signal_stop,
    };
    return &impl;
}
