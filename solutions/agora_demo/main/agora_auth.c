#include "agora_auth.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "settings.h"

#define TAG "AGORA_AUTH"
#define JWT_HEADER_JSON "{\"alg\":\"HS256\",\"typ\":\"JWT\"}"

static int base64url_encode(const uint8_t *input, size_t input_size,
                            char *output, size_t output_size)
{
    size_t encoded_size = 0;
    int ret = mbedtls_base64_encode((unsigned char *)output, output_size - 1,
                                    &encoded_size, input, input_size);
    if (ret != 0 || encoded_size >= output_size) {
        ESP_LOGE(TAG, "Base64 encoding failed: %d", ret);
        return -1;
    }

    while (encoded_size > 0 && output[encoded_size - 1] == '=') {
        --encoded_size;
    }
    for (size_t i = 0; i < encoded_size; ++i) {
        if (output[i] == '+') {
            output[i] = '-';
        } else if (output[i] == '/') {
            output[i] = '_';
        }
    }
    output[encoded_size] = '\0';
    return 0;
}

static int json_escape(const char *input, char *output, size_t output_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t used = 0;

    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        char escaped[6];
        size_t escaped_size = 1;
        escaped[0] = (char)*p;
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            escaped_size = 2;
        } else if (*p < 0x20) {
            escaped[0] = '\\';
            escaped[1] = 'u';
            escaped[2] = '0';
            escaped[3] = '0';
            escaped[4] = hex[*p >> 4];
            escaped[5] = hex[*p & 0x0f];
            escaped_size = sizeof(escaped);
        }
        if (used + escaped_size >= output_size) {
            return -1;
        }
        memcpy(output + used, escaped, escaped_size);
        used += escaped_size;
    }
    output[used] = '\0';
    return 0;
}

static bool is_url_unreserved(unsigned char value)
{
    return isalnum(value) || value == '-' || value == '.' ||
           value == '_' || value == '~';
}

static int url_encode_path_segment(const char *input, char *output,
                                   size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;

    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        size_t required = is_url_unreserved(*p) ? 1 : 3;
        if (used + required >= output_size) {
            return -1;
        }
        if (required == 1) {
            output[used++] = (char)*p;
        } else {
            output[used++] = '%';
            output[used++] = hex[*p >> 4];
            output[used++] = hex[*p & 0x0f];
        }
    }
    output[used] = '\0';
    return 0;
}

int agora_auth_create_session(const char *channel, uint32_t uid,
                              char *url, size_t url_size,
                              char *token, size_t token_size)
{
    if (channel == NULL || channel[0] == '\0' || uid == 0 ||
        url == NULL || token == NULL || url_size == 0 || token_size == 0) {
        return -1;
    }
    if (AGORA_DEMO_APP_ID[0] == '\0' ||
        AGORA_DEMO_APP_CERT[0] == '\0' ||
        AGORA_DEMO_WHIP_BASE_URL[0] == '\0') {
        ESP_LOGE(TAG, "Configure App ID, App Cert, and WHIP base URL");
        return -1;
    }

    time_t now = time(NULL);
    if (now < AGORA_DEMO_MIN_VALID_UNIX_TIME) {
        ESP_LOGE(TAG, "System time is not synchronized");
        return -1;
    }

    char encoded_channel[AGORA_DEMO_CHANNEL_SIZE * 3 + 1];
    if (url_encode_path_segment(channel, encoded_channel,
                                sizeof(encoded_channel)) != 0) {
        ESP_LOGE(TAG, "Channel is too long for the WHIP URL");
        return -1;
    }

    size_t base_size = strlen(AGORA_DEMO_WHIP_BASE_URL);
    while (base_size > 0 && AGORA_DEMO_WHIP_BASE_URL[base_size - 1] == '/') {
        --base_size;
    }
    int written = snprintf(url, url_size,
                           "%.*s/pub/%s?Uid=%" PRIu32 "&duplex=true",
                           (int)base_size, AGORA_DEMO_WHIP_BASE_URL,
                           encoded_channel, uid);
    if (written < 0 || (size_t)written >= url_size) {
        ESP_LOGE(TAG, "WHIP URL buffer is too small");
        return -1;
    }

    char escaped_app_id[192];
    char escaped_channel[AGORA_DEMO_CHANNEL_SIZE * 3 + 1];
    if (json_escape(AGORA_DEMO_APP_ID, escaped_app_id,
                    sizeof(escaped_app_id)) != 0 ||
        json_escape(channel, escaped_channel, sizeof(escaped_channel)) != 0) {
        ESP_LOGE(TAG, "App ID or channel is too long for the JWT");
        return -1;
    }

    int64_t expires_at = (int64_t)now + AGORA_DEMO_TOKEN_TTL_SECONDS;
    char payload[896];
    written = snprintf(payload, sizeof(payload),
                       "{\"version\":\"1.0\",\"appID\":\"%s\","
                       "\"streamID\":\"%s\",\"uid\":\"%" PRIu32 "\","
                       "\"exp\":%" PRId64 ",\"action\":\"pub\","
                       "\"enableSubAuth\":true}",
                       escaped_app_id, escaped_channel, uid, expires_at);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        ESP_LOGE(TAG, "JWT payload buffer is too small");
        return -1;
    }

    char encoded_header[64];
    char encoded_payload[1200];
    if (base64url_encode((const uint8_t *)JWT_HEADER_JSON,
                         strlen(JWT_HEADER_JSON), encoded_header,
                         sizeof(encoded_header)) != 0 ||
        base64url_encode((const uint8_t *)payload, strlen(payload),
                         encoded_payload, sizeof(encoded_payload)) != 0) {
        return -1;
    }

    char signing_input[1280];
    written = snprintf(signing_input, sizeof(signing_input), "%s.%s",
                       encoded_header, encoded_payload);
    if (written < 0 || (size_t)written >= sizeof(signing_input)) {
        ESP_LOGE(TAG, "JWT signing input buffer is too small");
        return -1;
    }

    const mbedtls_md_info_t *sha256 =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t signature[32];
    if (sha256 == NULL ||
        mbedtls_md_hmac(sha256,
                        (const unsigned char *)AGORA_DEMO_APP_CERT,
                        strlen(AGORA_DEMO_APP_CERT),
                        (const unsigned char *)signing_input,
                        strlen(signing_input), signature) != 0) {
        ESP_LOGE(TAG, "JWT HMAC-SHA256 failed");
        return -1;
    }

    char encoded_signature[64];
    if (base64url_encode(signature, sizeof(signature), encoded_signature,
                         sizeof(encoded_signature)) != 0) {
        return -1;
    }
    written = snprintf(token, token_size, "%s.%s", signing_input,
                       encoded_signature);
    if (written < 0 || (size_t)written >= token_size) {
        ESP_LOGE(TAG, "JWT token buffer is too small");
        return -1;
    }

    ESP_LOGI(TAG, "Created WHIP credential channel=%s uid=%" PRIu32
             " exp=%" PRId64, channel, uid, expires_at);
    return 0;
}
