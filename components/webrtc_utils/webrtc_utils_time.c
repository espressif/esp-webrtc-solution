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

#include <sys/time.h>
#include <esp_log.h>
#include <time.h>
#include "esp_netif.h"
#include "esp_netif_sntp.h"

static const char *TAG = "webrtc_time_utils";

#define DATE_TIME_STRING_FORMAT (char *) "%Y%m%dT%H%M%SZ"

/**
 * https://en.wikipedia.org/wiki/ISO_8601
 */
void webrtc_utils_get_time_iso8601(char time_buf[18])
{
    time_t now;
    time(&now);
    strftime(time_buf, 17, DATE_TIME_STRING_FORMAT, gmtime(&now));
}

esp_err_t webrtc_utils_wait_for_time_sync(uint32_t timeout_ms)
{
    /* wait for time to be set */
    int retry = 0;
    int max_retries = timeout_ms / 2000; /* split the retries in 2 seconds each */

    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) != ESP_OK && ++retry < max_retries) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, max_retries);
    }

    return ESP_OK;
}

esp_err_t webrtc_utils_time_sync_init()
{
    static bool init_done = false;
    if (init_done) {
        ESP_LOGW(TAG, "Time sync is done already...");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing SNTP");
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        CONFIG_LWIP_SNTP_MAX_SERVERS,
        ESP_SNTP_SERVER_LIST("time.windows.com", "pool.ntp.org"));
#else
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
#endif
    esp_netif_sntp_init(&config);
    init_done = true;
    return ESP_OK;
}
