/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include "sdkconfig.h"

extern const char *aws_access_key_id;
extern const char *aws_secret_access_key;
extern const char *aws_session_token;

#define REGION      CONFIG_KVS_AWS_REGION
#define USER_AGENT  "ESP32"

typedef struct {
    char* data;
    int   size;
} http_resp_t;

typedef void (*http_body_t)(http_resp_t* resp, void* ctx);

int https_send_request(char* method, char** headers, char* url, char* data, http_body_t body, void* ctx);

int https_post(char* url, char** headers, char* data, http_body_t body, void* ctx);

/**
 * @brief Get the signed wss url object
 *
 * @param[in]       wss_url     url to be signed
 * @param[in]       channel_arn KVS channel ARN
 * @param[inout]    signed_url  allocated signed_url
 * @return int      0 if success, -1 on error
 *
 * @note    the signed URL is allocated and returned in signed_url object.
 *  must be freed by caller
 */
int get_signed_wss_url(char *wss_url, const char *channel_arn, char **signed_url);
