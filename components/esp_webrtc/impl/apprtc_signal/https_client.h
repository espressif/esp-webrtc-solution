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

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_HTTPS_TIMEOUT             10000
#define DEFAULT_HTTPS_MAX_REDIRECTS       3
#define DEFAULT_HTTPS_HEAD_SIZE           4096
#define DEFAULT_HTTPS_TX_SIZE             4096
#define DEFAULT_HTTPS_KEEP_ALIVE_ENABLE   true
#define DEFAULT_HTTPS_KEEP_ALIVE_COUNT    3
#define DEFAULT_HTTPS_KEEP_ALIVE_IDLE     5
#define DEFAULT_HTTPS_KEEP_ALIVE_INTERVAL 3

/**
 * @brief  Https response data
 */
typedef struct {
    char *data; /*!< Response data */
    int   size; /*!< Response data size */
} http_resp_t;

/**
 * @brief  Https response callback
 *
 * @param[in]  resp  Body content
 * @param[in]  ctx   User context
 */
typedef void (*http_body_t)(http_resp_t *resp, void *ctx);

/**
 * @brief  Https header callback
 *
 * @param[in]  key  Header key
 * @param[in]  key  Header value
 * @param[in]  ctx  User context
 */
typedef void (*http_header_t)(const char *key, const char *value, void *ctx);

/**
 * @brief  HTTP request configuration
 *         These settings will bypass to `esp_http_client` or handled by this module internally
 */
typedef struct {
    uint16_t  timeout_ms;          /*!< Connection timeout (unit milliseconds) */
    uint8_t   max_redirects;       /*!< Maximum redirection limit */
    uint16_t  buffer_size;         /*!< HTTP header buffer size */
    uint16_t  buffer_size_tx;      /*!< HTTP send buffer size */
    bool      keep_alive_enable;   /*!< Keep alive enable or not */
    uint8_t   keep_alive_count;    /*!< Keep alive count */
    uint16_t  keep_alive_idle;     /*!< Keep alive idle */
    uint16_t  keep_alive_interval; /*!< Keep alive send interval */
} https_request_cfg_t;

/**
 * @brief  HTTP request information
 */
typedef struct {
    const char    *method;     /*!< HTTP method POST, GET, DELETE, PATCH etc */
    const char    *url;        /*!< Request URL */
    char         **headers;    /*!< HTTP headers Arrays of "Type: Info" */
    char          *data;       /*!< Content data to send (special for post) */
    http_header_t  header_cb;  /*!< Response header callback */
    http_body_t    body_cb;    /*!< Response body callback */
    void          *ctx;        /*!< Callback context */
} https_request_t;

/**
 * @brief  Send https requests
 *
 * @note  This API is run in synchronized until response or error returns
 *
 * @param[in]  method     HTTP method to do
 * @param[in]  headers    HTTP headers, headers are array of "Type: Info", last one need set to NULL
 * @param[in]  url        HTTPS URL
 * @param[in]  data       Content data to be sent
 * @param[in]  header_cb  Header callback
 * @param[in]  body       Body callback
 * @param[in]  ctx        User context
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to do https request
 */
int https_send_request(const char *method, char **headers, const char *url, char *data, http_header_t header_cb, http_body_t body_cb, void *ctx);

/**
 * @brief  Do post https request
 *
 * @note  This API will internally call `https_request_advance`
 *
 * @param[in]  url        HTTPS URL to post
 * @param[in]  headers    HTTP headers, headers are array of "Type: Info", last one need set to NULL
 * @param[in]  data       Content data to be sent
 * @param[in]  header_cb  Header callback
 * @param[in]  body       Body callback
 * @param[in]  ctx        User context
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to do https request
 */
int https_post(const char *url, char **headers, char *data, http_header_t header_cb, http_body_t body, void *ctx);

/**
 * @brief  Set https request with advanced options
 *
 * @note  This API is run in synchronized until response or error returns
 *
 * @param[in]  cfg  Request HTTP configurations
 * @param[in]  req  Request settings
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to do https request
 */
int https_request_advance(https_request_cfg_t *cfg, https_request_t *req);

#ifdef __cplusplus
}
#endif
