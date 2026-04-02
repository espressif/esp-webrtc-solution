/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

/**
 * @file sigv4_config.h
 * @brief Configs overriding the the defaults set by by SigV4
 */

#pragma once

#include <stdio.h>
#include <esp_log.h>

#define SIGV4_PROCESSING_BUFFER_LENGTH (2048)

/* map sigv4 logs to ESP_LOGx macros */

#define PrintfError( ... )          ESP_LOGE("sigv4", ##__VA_ARGS__ );
#define PrintfWarn( ... )           ESP_LOGW("sigv4", ##__VA_ARGS__ );
#define PrintfInfo( ... )           ESP_LOGI("sigv4", ##__VA_ARGS__ );
#define PrintfDebug( ... )          ESP_LOGD("sigv4", ##__VA_ARGS__ );

#define LogError( message )         PrintfError message
#define LogWarn( message )          PrintfWarn message
#define LogInfo( message )          PrintfInfo message
#define LogDebug( message )         PrintfDebug message
