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
#include "esp_tls.h"
#include <sdkconfig.h>
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_http_client.h"

static const char *TAG = "HTTPS_CLIENT";

typedef struct {
    http_header_t header;
    http_body_t   body;
    uint8_t      *data;
    int           fill_size;
    int           size;
    void         *ctx;
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
            ESP_LOGI(TAG, "📋 Header: %s = %s", evt->header_key, evt->header_value);
            if (info->header) {
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
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT - Following redirect");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

int https_send_request(const char *method, char **headers, const char *url, char *data, http_header_t header_cb, http_body_t body, void *ctx)
{
    http_info_t info = {
        .body = body,
        .header = header_cb,
        .ctx = ctx,
    };
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        .user_data = &info,
        .timeout_ms = 30000, // Increase timeout to 30s for WHIP servers
        .buffer_size = 1536,  // Reasonable buffer size for headers (1.5KB)
        .buffer_size_tx = 1536, // Reasonable TX buffer size (1.5KB)
        .max_redirection_count = 0, // Disable auto redirects to preserve headers/body
        .disable_auto_redirect = true, // Handle redirects manually
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    };
    
    int err = 0;
    int retry_count = 0;
    const int max_retries = 3;
    const int retry_delay_ms = 2000;
    const int max_redirects = 10;
    char current_url[512];
    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';
    
    while (retry_count <= max_retries) {
        int redirect_count = 0;
        
        // Handle redirects manually
        while (redirect_count < max_redirects) {
            config.url = current_url; // Use current URL (may be redirected)
            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client == NULL) {
                ESP_LOGE(TAG, "Fail to init client");
                return -1;
            }
            
            esp_http_client_set_url(client, current_url);
        if (strcmp(method, "POST") == 0) {
            esp_http_client_set_method(client, HTTP_METHOD_POST);
        } else if (strcmp(method, "DELETE") == 0) {
            esp_http_client_set_method(client, HTTP_METHOD_DELETE);
        } else if (strcmp(method, "PATCH") == 0) {
            esp_http_client_set_method(client, HTTP_METHOD_PATCH);
        } else {
            err = -1;
            esp_http_client_cleanup(client);
            break; // Exit redirect loop and retry loop
        }
        
        bool has_content_type = false;
        if (headers) {
            int i = 0;
            // TODO suppose header writable
            while (headers[i]) {
                char *dot = strchr(headers[i], ':');
                if (dot) {
                    *dot = 0;
                    if (strcmp(headers[i], "Content-Type") == 0) {
                        has_content_type = true;
                    }
                    char *cont = dot + 2;
                    esp_http_client_set_header(client, headers[i], cont);
                    *dot = ':';
                }
                i++;
            }
        }
        
        if (data != NULL) {
            if (has_content_type == false) {
                esp_http_client_set_header(client, "Content-Type", "text/plain;charset=UTF-8");
            }
            esp_http_client_set_post_field(client, data, strlen(data));
        }
        
        // Log request headers for debugging redirects
        if (redirect_count > 0) {
            ESP_LOGD(TAG, "Request headers for redirect %d:", redirect_count);
            if (headers) {
                int i = 0;
                while (headers[i]) {
                    ESP_LOGD(TAG, "  %s", headers[i]);
                    i++;
                }
            }
        }
        
            ESP_LOGI(TAG, "Performing HTTP %s request to %s (attempt %d/%d, redirect %d/%d)", 
                     method, current_url, retry_count + 1, max_retries + 1, redirect_count + 1, max_redirects);
            
            if (data) {
                ESP_LOGD(TAG, "Request body length: %d bytes", strlen(data));
                ESP_LOGD(TAG, "Request body preview: %.200s%s", data, strlen(data) > 200 ? "..." : "");
            }
            
            err = esp_http_client_perform(client);
            
            if (err == ESP_OK) {
                int status_code = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "HTTP %s Status = %d, content_length = %lld",
                         method, status_code, esp_http_client_get_content_length(client));
                
                // Handle redirects manually
                if (status_code >= 300 && status_code < 400) {
                    ESP_LOGI(TAG, "Received redirect response %d", status_code);
                    
                    // Log all response headers for debugging
                    ESP_LOGD(TAG, "Response headers:");
                    char *header_buffer = malloc(1024);
                    if (header_buffer) {
                        int header_len = esp_http_client_get_header(client, "Location", &header_buffer);
                        if (header_len > 0) {
                            ESP_LOGD(TAG, "  Location: %s", header_buffer);
                        }
                        
                        header_len = esp_http_client_get_header(client, "Content-Type", &header_buffer);
                        if (header_len > 0) {
                            ESP_LOGD(TAG, "  Content-Type: %s", header_buffer);
                        }
                        
                        header_len = esp_http_client_get_header(client, "Server", &header_buffer);
                        if (header_len > 0) {
                            ESP_LOGD(TAG, "  Server: %s", header_buffer);
                        }
                        
                        free(header_buffer);
                    }
                    
                    // Get Location header for redirect
                    char *location = NULL;
                    int location_len = esp_http_client_get_header(client, "Location", &location);
                    
                    if (location_len > 0 && location) {
                        ESP_LOGI(TAG, "Redirect %d: %s -> %s", redirect_count + 1, current_url, location);
                        
                        // Validate URL length
                        if (strlen(location) >= sizeof(current_url)) {
                            ESP_LOGE(TAG, "Redirect URL too long: %d chars", strlen(location));
                            esp_http_client_cleanup(client);
                            break;
                        }
                        
                        // Handle relative URLs
                        if (strncmp(location, "http://", 7) == 0 || strncmp(location, "https://", 8) == 0) {
                            // Absolute URL
                            strncpy(current_url, location, sizeof(current_url) - 1);
                            current_url[sizeof(current_url) - 1] = '\0';
                        } else {
                            // Relative URL - need to construct full URL
                            ESP_LOGW(TAG, "Relative redirect URL not fully supported: %s", location);
                            strncpy(current_url, location, sizeof(current_url) - 1);
                            current_url[sizeof(current_url) - 1] = '\0';
                        }
                        esp_http_client_cleanup(client);
                        redirect_count++;
                        continue; // Try again with new URL
                    } else {
                        ESP_LOGW(TAG, "Redirect response %d but no valid Location header (len=%d)", 
                                status_code, location_len);
                        
                        // Log response body for debugging
                        if (info.data && info.fill_size > 0) {
                            ESP_LOGD(TAG, "Response body: %.*s", info.fill_size, (char*)info.data);
                        }
                        
                        esp_http_client_cleanup(client);
                        break; // Exit redirect loop, will retry
                    }
                } else {
                    // Success or non-redirect error
                    if (strcmp(url, current_url) != 0) {
                        ESP_LOGI(TAG, "Final URL after %d redirects: %s", redirect_count, current_url);
                    }
                    esp_http_client_cleanup(client);
                    break; // Exit redirect loop
                }
            } else {
                ESP_LOGW(TAG, "HTTP %s request failed (attempt %d/%d, redirect %d/%d): %s", 
                         method, retry_count + 1, max_retries + 1, redirect_count + 1, max_redirects, esp_err_to_name(err));
                
                // Log additional error details
                if (err == ESP_ERR_HTTP_EAGAIN) {
                    ESP_LOGW(TAG, "Connection timeout - server may be slow to respond");
                } else if (err == ESP_FAIL) {
                    ESP_LOGW(TAG, "HTTP client internal error");
                }
                
                esp_http_client_cleanup(client);
                break; // Exit redirect loop, will retry
            }
        }
        
        if (redirect_count >= max_redirects) {
            ESP_LOGE(TAG, "Too many redirects (%d), giving up", max_redirects);
            err = ESP_FAIL;
        }
        
        if (err == ESP_OK) {
            break; // Success, exit retry loop
        }
        
        // Retry logic
        if (retry_count < max_retries) {
            ESP_LOGI(TAG, "Retrying in %d ms...", retry_delay_ms);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            retry_count++;
            // Reset URL for retry
            strncpy(current_url, url, sizeof(current_url) - 1);
            current_url[sizeof(current_url) - 1] = '\0';
        } else {
            ESP_LOGE(TAG, "HTTP %s request failed after %d attempts", method, max_retries + 1);
            break;
        }
    }
    
    if (info.data) {
        free(info.data);
    }
    return err;
}

int https_post(const char *url, char **headers, char *data, http_header_t header_cb, http_body_t body, void *ctx)
{
    return https_send_request("POST", headers, url, data, header_cb, body, ctx);
}
