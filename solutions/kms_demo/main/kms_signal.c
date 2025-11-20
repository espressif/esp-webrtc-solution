/* Kurento WebSocket Signaling Implementation

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_log.h"
#include "media_lib_os.h"
#include "esp_peer_signaling.h"
#include <esp_event.h>
#include <esp_system.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <sdkconfig.h>
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_websocket_client.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define TAG                  "KWS_SIG"
#define KWS_TASK_STACK_SIZE  8192
#define KWS_TASK_PRIORITY    5
#define KWS_EVENT_QUEUE_SIZE 10

typedef enum {
    KWS_STATE_WAIT_CONNECT,
    KWS_STATE_CREATE_PIPELINE,
    KWS_STATE_WAIT_PIPELINE,
    KWS_STATE_WAIT_ENDPOINT,
    KWS_STATE_CONNECTED,
    KWS_STATE_WAIT_SDP,
    KWS_STATE_PROCESS_SDP,
    KWS_STATE_WAIT_SDP_ANSWER,
    KWS_STATE_READY,
    KWS_STATE_WAIT_BROWSER_ENDPOINT,
} kws_state_t;

typedef enum {
    KWS_EVENT_WS_CONNECTED,
    KWS_EVENT_WS_DISCONNECTED,
    KWS_EVENT_PIPELINE_CREATED,
    KWS_EVENT_ENDPOINT_CREATED,
    KWS_EVENT_BROWSER_ENDPOINT_CREATED,
    KWS_EVENT_SDP_RECEIVED,
    KWS_EVENT_SDP_ANSWER_RECEIVED,
    KWS_EVENT_ICE_CANDIDATE,
    KWS_EVENT_STOP,
    KWS_EVENT_BROWSER_LEAVE
} kws_event_type_t;

typedef struct {
    kws_event_type_t type;         /*!< Event type */
    char            *data;         /*!< For SDP, candidate strings, etc. */
    int              data_len;     /*!< Data length */
    int              response_id;  /*!< For matching responses */
} kws_event_t;

typedef struct {
    esp_websocket_client_handle_t  ws;                            /*!< WebSocket client handle */
    char                          *ws_url;                        /*!< WebSocket URL */
    char                          *session_id;                    /*!< Session ID from Kurento */
    char                          *pipeline_id;                   /*!< MediaPipeline ID */
    char                          *endpoint_id;                   /*!< WebRtcEndpoint ID (ESP32's own endpoint) */
    char                          *browser_endpoint_id;           /*!< Browser endpoint ID (for browser to connect) */
    char                          *pending_sdp;                   /*!< Pending SDP offer to process */
    char                         **extracted_candidates;          /*!< Candidates extracted from SDP for trickle ICE */
    int                            candidate_count;               /*!< Number of extracted candidates */
    int                            request_id;                    /*!< Request ID for JSON-RPC */
    int                            pending_pipeline_req;          /*!< Request ID for pipeline creation */
    int                            pending_endpoint_req;          /*!< Request ID for endpoint creation */
    int                            pending_browser_endpoint_req;  /*!< Request ID for browser endpoint creation */
    int                            ice_subscribed;                /*!< Flag to track ICE subscription */
    char                          *browser_endpoint_state;        /*!< Last known browser endpoint state */
    bool                           browser_endpoint_subscribed;   /*!< Flag to track connection state subscription */
    TaskHandle_t                   task_handle;                   /*!< State machine task handle */
    QueueHandle_t                  event_queue;                   /*!< Event queue for task */
    kws_state_t                    state;                         /*!< Current state */
    bool                           exited;                        /*!< Flag to track if task exited */
    bool                           webrtc_ready;                  /*!< Flag to track if WebRTC is fully connected */
    bool                           connected;                     /*!< Flag to track if WebSocket is connected */ 
} kws_client_t;

typedef struct {
    kws_client_t                 *kws_client;
    esp_peer_signaling_cfg_t      cfg;
    esp_peer_signaling_ice_info_t ice_info;
    bool                          is_initiator;
} kws_sig_t;

static int  kws_signal_stop(esp_peer_signaling_handle_t sig);
static int  kws_signal_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg);
static void kws_state_machine_task(void *arg);
static int  send_event_to_task(kws_client_t *kws, kws_event_type_t type, const char *data, int data_len, int response_id);
static int  send_json_rpc(kws_client_t *kws, cJSON *json);
static void create_browser_endpoint(kws_client_t *kws);
static void release_browser_endpoint(kws_client_t *kws);
static int  subscribe_to_ice_candidates(kws_client_t *kws);
static int  gather_ice_candidates(kws_client_t *kws);
static int  send_ice_candidate_to_kurento(kws_client_t *kws, const char *candidate_str);

static void destroy_kws(kws_client_t *kws)
{
    if (kws->task_handle) {
        send_event_to_task(kws, KWS_EVENT_STOP, NULL, 0, 0);
        while (!kws->exited) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (kws->event_queue) {
        vQueueDelete(kws->event_queue);
        kws->event_queue = NULL;
    }

    if (kws->ws) {
        esp_websocket_client_stop(kws->ws);
        esp_websocket_client_destroy(kws->ws);
        kws->ws = NULL;
    }
    if (kws->ws_url) {
        free(kws->ws_url);
        kws->ws_url = NULL;
    }
    if (kws->session_id) {
        free(kws->session_id);
        kws->session_id = NULL;
    }
    if (kws->pipeline_id) {
        free(kws->pipeline_id);
        kws->pipeline_id = NULL;
    }
    if (kws->endpoint_id) {
        free(kws->endpoint_id);
        kws->endpoint_id = NULL;
    }
    if (kws->browser_endpoint_id) {
        free(kws->browser_endpoint_id);
        kws->browser_endpoint_id = NULL;
    }
    if (kws->pending_sdp) {
        free(kws->pending_sdp);
        kws->pending_sdp = NULL;
    }
    if (kws->extracted_candidates) {
        for (int i = 0; i < kws->candidate_count; i++) {
            if (kws->extracted_candidates[i]) {
                free(kws->extracted_candidates[i]);
            }
        }
        free(kws->extracted_candidates);
        kws->extracted_candidates = NULL;
        kws->candidate_count = 0;
    }
    free(kws);
}

static int send_event_to_task(kws_client_t *kws, kws_event_type_t type, const char *data, int data_len, int response_id)
{
    if (!kws || !kws->event_queue) {
        return -1;
    }
    kws_event_t event = {
        .type = type,
        .data = NULL,
        .data_len = data_len,
        .response_id = response_id,
    };

    if (data && data_len > 0) {
        event.data = malloc(data_len + 1);
        if (event.data == NULL) {
            return -1;
        }
        if (event.data) {
            memcpy(event.data, data, data_len);
            event.data[data_len] = '\0';
        }
    }
    if (xQueueSend(kws->event_queue, &event, pdMS_TO_TICKS(1000)) != pdTRUE) {
        if (event.data) {
            free(event.data);
        }
        return -1;
    }
    return 0;
}

static void free_ice_info(esp_peer_signaling_ice_info_t *info)
{
    if (info->server_info.stun_url) {
        free(info->server_info.stun_url);
        info->server_info.stun_url = NULL;
    }
    if (info->server_info.user) {
        free(info->server_info.user);
        info->server_info.user = NULL;
    }
    if (info->server_info.psw) {
        free(info->server_info.psw);
        info->server_info.psw = NULL;
    }
    memset(info, 0, sizeof(esp_peer_signaling_ice_info_t));
}

static void handle_result(kws_client_t *kws, cJSON *root, int response_id)
{
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (result == NULL) {
        return;
    }
    cJSON *value_item = cJSON_GetObjectItem(result, "value");
    if (value_item && cJSON_IsString(value_item)) {
        const char *value_str = value_item->valuestring;
        if (strstr(value_str, "v=") != NULL || strstr(value_str, "m=") != NULL) {
            send_event_to_task(kws, KWS_EVENT_SDP_ANSWER_RECEIVED, value_str, strlen(value_str), response_id);
        } else if (response_id == kws->pending_pipeline_req) {
            cJSON *session_item = cJSON_GetObjectItem(result, "sessionId");
            if (session_item && cJSON_IsString(session_item)) {
                if (kws->session_id) {
                    free(kws->session_id);
                }
                kws->session_id = strdup(session_item->valuestring);
            }
            send_event_to_task(kws, KWS_EVENT_PIPELINE_CREATED, value_str, strlen(value_str), response_id);
        } else if (response_id == kws->pending_endpoint_req) {
            send_event_to_task(kws, KWS_EVENT_ENDPOINT_CREATED, value_str, strlen(value_str), response_id);
        } else if (response_id == kws->pending_browser_endpoint_req) {
            send_event_to_task(kws, KWS_EVENT_BROWSER_ENDPOINT_CREATED, value_str, strlen(value_str), response_id);
        }
    }
}

static void handle_event(kws_sig_t *sg, cJSON *root)
{
    kws_client_t *kws = sg->kws_client;

    cJSON *method_item = cJSON_GetObjectItem(root, "method");
    if (!(method_item && cJSON_IsString(method_item) && strcmp(method_item->valuestring, "onEvent") == 0)) {
        return;
    }
    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params == NULL) {
        return;
    }
    cJSON *value = cJSON_GetObjectItem(params, "value");
    if (value == NULL) {
        return;
    }

    cJSON *event_type = cJSON_GetObjectItem(value, "type");
    if (event_type && cJSON_IsString(event_type)) {
        const char *type_str = event_type->valuestring;

        if (strcmp(type_str, "MediaStateChanged") == 0) {
            cJSON *data = cJSON_GetObjectItem(value, "data");
            if (data) {
                cJSON *new_state = cJSON_GetObjectItem(data, "newState");
                cJSON *old_state = cJSON_GetObjectItem(data, "oldState");

                if (new_state && cJSON_IsString(new_state) && old_state && cJSON_IsString(old_state)) {
                    const char *new_state_str = new_state->valuestring;
                    const char *old_state_str = old_state->valuestring;

                    if (strcmp(old_state_str, "CONNECTED") == 0 && strcmp(new_state_str, "DISCONNECTED") == 0) {
                        ESP_LOGI(TAG, "Browser disconnected");
                        send_event_to_task(kws, KWS_EVENT_BROWSER_LEAVE, NULL, 0, 0);
                    }
                }
            }
            return;
        }

        if (strcmp(type_str, "IceCandidateFound") == 0) {
            cJSON *data = cJSON_GetObjectItem(value, "data");
            if (data == NULL) {
                return;
            }
            cJSON *candidate = cJSON_GetObjectItem(data, "candidate");
            if (candidate == NULL) {
                return;
            }
            const char *cand_str = NULL;
            if (cJSON_IsString(candidate)) {
                cand_str = candidate->valuestring;
            } else if (cJSON_IsObject(candidate)) {
                cJSON *cand_field = cJSON_GetObjectItem(candidate, "candidate");
                if (cand_field && cJSON_IsString(cand_field)) {
                    cand_str = cand_field->valuestring;
                }
            }
            if (cand_str && sg->cfg.on_msg) {
                esp_peer_signaling_msg_t msg = {
                    .type = ESP_PEER_SIGNALING_MSG_CANDIDATE,
                    .data = (uint8_t *)cand_str,
                    .size = strlen(cand_str),
                };
                sg->cfg.on_msg(&msg, sg->cfg.ctx);
            }
            return;
        }
    }
}

static void websocket_event_handler(void *ctx, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    kws_sig_t *sg = (kws_sig_t *)ctx;
    kws_client_t *kws = sg->kws_client;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            if (kws) {
                kws->connected = true;
                send_event_to_task(kws, KWS_EVENT_WS_CONNECTED, NULL, 0, 0);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            if (kws) {
                kws->connected = false;
                send_event_to_task(kws, KWS_EVENT_WS_DISCONNECTED, NULL, 0, 0);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->data_len > 0 && kws) {
                char *text = (char *)data->data_ptr;
                text[data->data_len] = '\0';
                cJSON *root = cJSON_Parse(data->data_ptr);
                if (root) {
                    cJSON *error = cJSON_GetObjectItem(root, "error");
                    cJSON *id_item = cJSON_GetObjectItem(root, "id");
                    int response_id = id_item ? cJSON_GetNumberValue(id_item) : -1;
                    if (error) {
                        ESP_LOGE(TAG, "Kurento error: %s", cJSON_Print(error));
                    } else {
                        handle_result(kws, root, response_id);
                        handle_event(sg, root);
                    }
                    cJSON_Delete(root);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR");
            break;
    }
}

static void subscribe_browser_endpoint_events(kws_client_t *kws)
{
    if (!kws || !kws->browser_endpoint_id || !kws->connected || kws->browser_endpoint_subscribed) {
        return;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(json, "id", kws->request_id++);
    cJSON_AddStringToObject(json, "method", "subscribe");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "MediaStateChanged");
    cJSON_AddStringToObject(params, "object", kws->browser_endpoint_id);
    cJSON_AddItemToObject(json, "params", params);

    send_json_rpc(kws, json);
    cJSON_Delete(json);

    kws->browser_endpoint_subscribed = true;
}

static void kws_state_machine_task(void *arg)
{
    kws_sig_t *sg = (kws_sig_t *)arg;
    kws_client_t *kws = sg->kws_client;
    kws_event_t event;

    while (1) {
        if (xQueueReceive(kws->event_queue, &event, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        if (event.type == KWS_EVENT_WS_DISCONNECTED) {
            if (sg->cfg.on_close) {
                sg->cfg.on_close(sg->cfg.ctx);
            }
        }
        if (event.type == KWS_EVENT_STOP) {
            if (event.data) {
                free(event.data);
            }
            break;
        }

        switch (kws->state) {
            case KWS_STATE_WAIT_CONNECT:
                if (event.type == KWS_EVENT_WS_CONNECTED) {
                    if (kws->browser_endpoint_id) {
                        release_browser_endpoint(kws);
                    }
                    kws->state = KWS_STATE_CREATE_PIPELINE;
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
                    cJSON_AddNumberToObject(json, "id", kws->request_id++);
                    cJSON_AddStringToObject(json, "method", "create");
                    cJSON *params = cJSON_CreateObject();
                    cJSON_AddStringToObject(params, "type", "MediaPipeline");
                    cJSON_AddStringToObject(params, "sessionId", "default");
                    cJSON_AddItemToObject(json, "params", params);
                    kws->pending_pipeline_req = kws->request_id - 1;
                    send_json_rpc(kws, json);
                    cJSON_Delete(json);
                    kws->state = KWS_STATE_WAIT_PIPELINE;
                }
                break;

            case KWS_STATE_WAIT_PIPELINE:
                if (event.type == KWS_EVENT_PIPELINE_CREATED && event.data) {
                    if (kws->pipeline_id) {
                        free(kws->pipeline_id);
                    }
                    kws->pipeline_id = strdup(event.data);
                    if (!kws->session_id) {
                        kws->session_id = strdup("default");
                    }

                    kws->state = KWS_STATE_CONNECTED;
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
                    cJSON_AddNumberToObject(json, "id", kws->request_id++);
                    cJSON_AddStringToObject(json, "method", "create");
                    cJSON *params = cJSON_CreateObject();
                    cJSON_AddStringToObject(params, "type", "WebRtcEndpoint");
                    cJSON *ctor_params = cJSON_CreateObject();
                    cJSON_AddStringToObject(ctor_params, "mediaPipeline", kws->pipeline_id);
                    cJSON_AddItemToObject(params, "constructorParams", ctor_params);
                    cJSON_AddItemToObject(json, "params", params);
                    kws->pending_endpoint_req = kws->request_id - 1;
                    send_json_rpc(kws, json);
                    cJSON_Delete(json);

                    kws->state = KWS_STATE_WAIT_ENDPOINT;
                }
                break;

            case KWS_STATE_CREATE_PIPELINE:
                break;
            case KWS_STATE_WAIT_ENDPOINT:
                if (event.type == KWS_EVENT_ENDPOINT_CREATED && event.data) {
                    if (kws->endpoint_id) {
                        free(kws->endpoint_id);
                    }
                    kws->endpoint_id = strdup(event.data);
                    subscribe_to_ice_candidates(kws);
                    if (sg->cfg.on_connected) {
                        sg->cfg.on_connected(sg->cfg.ctx);
                    }
                    if (kws->pending_sdp) {
                        char *pending_sdp = kws->pending_sdp;
                        kws->pending_sdp = NULL;
                        send_event_to_task(kws, KWS_EVENT_SDP_RECEIVED, pending_sdp, strlen(pending_sdp), 0);
                        free(pending_sdp);
                    } else {
                        kws->state = KWS_STATE_WAIT_SDP;
                    }
                }
                break;

            case KWS_STATE_CONNECTED:
            case KWS_STATE_WAIT_SDP:
                if (event.type == KWS_EVENT_SDP_RECEIVED && event.data) {
                    if (!kws->ice_subscribed) {
                        subscribe_to_ice_candidates(kws);
                    }
                    kws->state = KWS_STATE_PROCESS_SDP;
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
                    cJSON_AddNumberToObject(json, "id", kws->request_id++);
                    cJSON_AddStringToObject(json, "method", "invoke");
                    cJSON *params = cJSON_CreateObject();
                    cJSON_AddStringToObject(params, "object", kws->endpoint_id);
                    cJSON_AddStringToObject(params, "operation", "processOffer");
                    cJSON *op_params = cJSON_CreateObject();
                    cJSON_AddStringToObject(op_params, "offer", event.data);
                    cJSON_AddItemToObject(params, "operationParams", op_params);
                    cJSON_AddItemToObject(json, "params", params);
                    send_json_rpc(kws, json);
                    cJSON_Delete(json);
                    kws->state = KWS_STATE_WAIT_SDP_ANSWER;
                    if (kws->extracted_candidates && kws->candidate_count > 0) {
                        for (int i = 0; i < kws->candidate_count; i++) {
                            if (kws->extracted_candidates[i]) {
                                send_ice_candidate_to_kurento(kws, kws->extracted_candidates[i]);
                            }
                        }
                    }
                }
                break;

            case KWS_STATE_PROCESS_SDP:
                break;

            case KWS_STATE_WAIT_SDP_ANSWER:
                if (event.type == KWS_EVENT_SDP_ANSWER_RECEIVED && event.data) {
                    if (sg->cfg.on_msg) {
                        esp_peer_signaling_msg_t msg = {
                            .type = ESP_PEER_SIGNALING_MSG_SDP,
                            .data = (uint8_t *)event.data,
                            .size = strlen(event.data),
                        };
                        sg->cfg.on_msg(&msg, sg->cfg.ctx);
                    }
                    if (!kws->ice_subscribed) {
                        subscribe_to_ice_candidates(kws);
                    }
                    gather_ice_candidates(kws);
                    create_browser_endpoint(kws);
                    kws->state = KWS_STATE_WAIT_BROWSER_ENDPOINT;
                }
                break;

            case KWS_STATE_WAIT_BROWSER_ENDPOINT:
                if (event.type == KWS_EVENT_BROWSER_ENDPOINT_CREATED && event.data) {
                    if (kws->browser_endpoint_id) {
                        free(kws->browser_endpoint_id);
                    }
                    kws->browser_endpoint_id = strdup(event.data);
                    kws->browser_endpoint_subscribed = false;
                    subscribe_browser_endpoint_events(kws);

                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
                    cJSON_AddNumberToObject(json, "id", kws->request_id++);
                    cJSON_AddStringToObject(json, "method", "invoke");
                    cJSON *params = cJSON_CreateObject();
                    cJSON_AddStringToObject(params, "object", kws->endpoint_id);
                    cJSON_AddStringToObject(params, "operation", "connect");
                    cJSON *op_params = cJSON_CreateObject();
                    cJSON_AddStringToObject(op_params, "sink", kws->browser_endpoint_id);
                    cJSON_AddItemToObject(params, "operationParams", op_params);
                    cJSON_AddItemToObject(json, "params", params);
                    send_json_rpc(kws, json);
                    cJSON_Delete(json);

                    ESP_LOGI(TAG, "========================================");
                    ESP_LOGI(TAG, "BROWSER_ENDPOINT_ID: %s", kws->browser_endpoint_id);
                    ESP_LOGI(TAG, "========================================");
                    kws->state = KWS_STATE_READY;
                    kws->webrtc_ready = true;
                }
                break;

            case KWS_STATE_READY:
                if (event.type == KWS_EVENT_BROWSER_LEAVE) {
                    release_browser_endpoint(kws);
                    kws->browser_endpoint_subscribed = false;
                    create_browser_endpoint(kws);
                    kws->state = KWS_STATE_WAIT_BROWSER_ENDPOINT;
                }
                break;
        }

        if (event.data) {
            free(event.data);
        }
    }

    kws->exited = true;
    vTaskDelete(NULL);
}

static int create_kws(kws_sig_t *sg)
{
    kws_client_t *kws = calloc(1, sizeof(kws_client_t));
    if (kws == NULL) {
        return -1;
    }
    do {
        char *signal_url = sg->cfg.signal_url;
        kws->ws_url = strdup(signal_url);
        if (kws->ws_url == NULL) {
            break;
        }
        if (strstr(kws->ws_url, "/kurento") == NULL) {
            char *new_url = realloc(kws->ws_url, strlen(kws->ws_url) + 9);
            if (new_url) {
                kws->ws_url = new_url;
                strcat(kws->ws_url, "/kurento");
            }
        }
        esp_websocket_client_config_t ws_cfg = {
            .uri = kws->ws_url,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
            .crt_bundle_attach = esp_crt_bundle_attach,
#endif
            .reconnect_timeout_ms = 60 * 1000,
            .network_timeout_ms = 10000,
            .buffer_size = 20 * 1024,
        };

        kws->ws = esp_websocket_client_init(&ws_cfg);
        if (kws->ws == NULL) {
            break;
        }

        esp_websocket_register_events(kws->ws, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)sg);
        int ret = esp_websocket_client_start(kws->ws);
        if (ret != 0) {
            break;
        }

        kws->event_queue = xQueueCreate(KWS_EVENT_QUEUE_SIZE, sizeof(kws_event_t));
        if (kws->event_queue == NULL) {
            break;
        }
        kws->state = KWS_STATE_WAIT_CONNECT;
        kws->request_id = 1;
        sg->kws_client = kws;
        if (xTaskCreate(kws_state_machine_task, "kws_state_machine", KWS_TASK_STACK_SIZE, sg, KWS_TASK_PRIORITY, &kws->task_handle) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create state machine task");
            kws->exited = true;
            break;
        }
        return 0;
    } while (0);
    destroy_kws(kws);
    return -1;
}

static int send_json_rpc(kws_client_t *kws, cJSON *json)
{
    if (!kws || !kws->ws || !kws->connected) {
        ESP_LOGE(TAG, "WebSocket not connected");
        return -1;
    }
    char *payload = cJSON_PrintUnformatted(json);
    if (payload) {
        int ret = esp_websocket_client_send_text(kws->ws, payload, strlen(payload), portMAX_DELAY);
        free(payload);
        return ret > 0 ? 0 : -1;
    }
    return -1;
}

static void release_browser_endpoint(kws_client_t *kws)
{
    if (!kws || !kws->browser_endpoint_id) {
        return;
    }
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(json, "id", kws->request_id++);
    cJSON_AddStringToObject(json, "method", "release");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "object", kws->browser_endpoint_id);
    cJSON_AddItemToObject(json, "params", params);
    send_json_rpc(kws, json);
    cJSON_Delete(json);
    free(kws->browser_endpoint_id);
    kws->browser_endpoint_id = NULL;
}

static void create_browser_endpoint(kws_client_t *kws)
{
    if (!kws || !kws->pipeline_id) {
        return;
    }
    release_browser_endpoint(kws);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(json, "id", kws->request_id++);
    cJSON_AddStringToObject(json, "method", "create");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "WebRtcEndpoint");
    cJSON *ctor_params = cJSON_CreateObject();
    cJSON_AddStringToObject(ctor_params, "mediaPipeline", kws->pipeline_id);
    cJSON_AddItemToObject(params, "constructorParams", ctor_params);
    cJSON_AddItemToObject(json, "params", params);
    kws->pending_browser_endpoint_req = kws->request_id - 1;
    send_json_rpc(kws, json);
    cJSON_Delete(json);
}

static int subscribe_to_ice_candidates(kws_client_t *kws)
{
    if (!kws || !kws->endpoint_id || !kws->connected) {
        return -1;
    }
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
    int req_id = kws->request_id++;
    cJSON_AddNumberToObject(json, "id", req_id);
    cJSON_AddStringToObject(json, "method", "subscribe");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "IceCandidateFound");
    cJSON_AddStringToObject(params, "object", kws->endpoint_id);
    cJSON_AddItemToObject(json, "params", params);

    int ret = send_json_rpc(kws, json);
    cJSON_Delete(json);
    return ret;
}

static int gather_ice_candidates(kws_client_t *kws)
{
    if (!kws || !kws->endpoint_id || !kws->connected) {
        return -1;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(json, "id", kws->request_id++);
    cJSON_AddStringToObject(json, "method", "invoke");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "object", kws->endpoint_id);
    cJSON_AddStringToObject(params, "operation", "gatherCandidates");
    cJSON *op_params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "operationParams", op_params);
    cJSON_AddItemToObject(json, "params", params);

    int ret = send_json_rpc(kws, json);
    cJSON_Delete(json);
    return ret;
}

static char *map_sdp(const char *esp32_sdp, size_t sdp_len, bool use_trickle_ice, char ***candidates_out, int *candidate_count_out)
{
    if (!esp32_sdp || sdp_len == 0) {
        return NULL;
    }
    char *mapped_sdp = malloc(sdp_len + 1);
    if (!mapped_sdp) {
        return NULL;
    }
    const char *src = esp32_sdp;
    char *dst = mapped_sdp;
    size_t remaining = sdp_len;
    while (remaining > 0) {
        const char *setup_pos = strstr(src, "a=setup:");
        if (setup_pos == NULL) {
            size_t copy_len = remaining;
            memcpy(dst, src, copy_len);
            dst += copy_len;
            *dst = '\0';
            break;
        }
        size_t before_len = setup_pos - src;
        if (before_len > 0) {
            memcpy(dst, src, before_len);
            dst += before_len;
            remaining -= before_len;
        }
        const char *line_end = strstr(setup_pos, "\r\n");
        if (!line_end) {
            size_t copy_len = remaining;
            memcpy(dst, src, copy_len);
            dst += copy_len;
            *dst = '\0';
            break;
        }
        memcpy(dst, "a=setup:active\r\n", 16);
        dst += 16;
        src = line_end + (line_end[0] == '\r' ? 2 : 1);
        remaining = sdp_len - (src - esp32_sdp);
    }
    return mapped_sdp;
}

static int send_ice_candidate_to_kurento(kws_client_t *kws, const char *candidate_str)
{
    if (!kws || !kws->endpoint_id || !kws->connected || !candidate_str) {
        return -1;
    }
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(json, "id", kws->request_id++);
    cJSON_AddStringToObject(json, "method", "invoke");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "object", kws->endpoint_id);
    cJSON_AddStringToObject(params, "operation", "addIceCandidate");
    cJSON *op_params = cJSON_CreateObject();
    cJSON *candidate_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(candidate_obj, "candidate", candidate_str);
    cJSON_AddStringToObject(candidate_obj, "sdpMid", "0");
    cJSON_AddNumberToObject(candidate_obj, "sdpMLineIndex", 0);
    cJSON_AddItemToObject(op_params, "candidate", candidate_obj);
    cJSON_AddItemToObject(params, "operationParams", op_params);
    cJSON_AddItemToObject(json, "params", params);
    int ret = send_json_rpc(kws, json);
    cJSON_Delete(json);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to send ICE candidate");
    }
    return ret;
}

static int kws_signal_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *h)
{
    if (cfg == NULL || cfg->signal_url == NULL || h == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return -1;
    }

    kws_sig_t *sg = calloc(1, sizeof(kws_sig_t));
    if (sg == NULL) {
        return -1;
    }
    sg->cfg = *cfg;
    sg->is_initiator = true;
    if (sg->cfg.on_ice_info) {
        if (sg->ice_info.server_info.stun_url == NULL) {
            sg->ice_info.server_info.stun_url = strdup("stun:stun.l.google.com:19302");
        }
        sg->ice_info.is_initiator = sg->is_initiator;
        sg->cfg.on_ice_info(&sg->ice_info, sg->cfg.ctx);
    }
    *h = sg;
    int ret = create_kws(sg);
    if (ret != 0) {
        *h = NULL;
        kws_signal_stop(sg);
        return ret;
    }
    return 0;
}

static int kws_signal_send_msg(esp_peer_signaling_handle_t h, esp_peer_signaling_msg_t *msg)
{
    kws_sig_t *sg = (kws_sig_t *)h;
    if (sg == NULL || msg == NULL || sg->kws_client == NULL) {
        return -1;
    }

    kws_client_t *kws = sg->kws_client;

    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(json, "id", kws->request_id++);
        cJSON_AddStringToObject(json, "method", "release");
        if (kws->session_id) {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "object", kws->session_id);
            cJSON_AddItemToObject(json, "params", params);
        }
        int ret = send_json_rpc(kws, json);
        cJSON_Delete(json);
        return ret;
    }

    if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        char **extracted_candidates = NULL;
        int candidate_count = 0;
        char *mapped_sdp = map_sdp((const char *)msg->data, msg->size, true, &extracted_candidates, &candidate_count);
        if (mapped_sdp) {
            if (kws->extracted_candidates) {
                for (int i = 0; i < kws->candidate_count; i++) {
                    if (kws->extracted_candidates[i]) {
                        free(kws->extracted_candidates[i]);
                    }
                }
                free(kws->extracted_candidates);
            }
            kws->extracted_candidates = extracted_candidates;
            kws->candidate_count = candidate_count;
            int ret = send_event_to_task(kws, KWS_EVENT_SDP_RECEIVED, mapped_sdp, strlen(mapped_sdp), 0);
            free(mapped_sdp);
            return ret;
        } else {
            return send_event_to_task(kws, KWS_EVENT_SDP_RECEIVED, (const char *)msg->data, msg->size, 0);
        }
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_CANDIDATE) {
        if (!kws->endpoint_id || msg->data == NULL) {
            return 0;
        }
        return send_ice_candidate_to_kurento(kws, (const char *)msg->data);
    }

    return 0;
}

int kws_signal_stop(esp_peer_signaling_handle_t h)
{
    kws_sig_t *sg = (kws_sig_t *)h;
    if (sg == NULL) {
        return -1;
    }

    if (sg->kws_client) {
        esp_peer_signaling_msg_t msg = {
            .type = ESP_PEER_SIGNALING_MSG_BYE,
        };
        kws_signal_send_msg(h, &msg);

        destroy_kws(sg->kws_client);
        sg->kws_client = NULL;
    }

    free_ice_info(&sg->ice_info);
    free(sg);
    return 0;
}

const esp_peer_signaling_impl_t *esp_signaling_get_kms_impl(void)
{
    static const esp_peer_signaling_impl_t impl = {
        .start = kws_signal_start,
        .send_msg = kws_signal_send_msg,
        .stop = kws_signal_stop,
    };
    return &impl;
}
