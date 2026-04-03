/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "mbedtls/ssl.h"
#include "mbedtls/md.h"
#include "dtls_srtp.h"
#include "esp_random.h"
#include "esp_log.h"

#pragma once

/*
 * Shared DTLS-SRTP functions reused by both IDF v5 and v6 implementations.
 */
#define DTLS_SIGN_ONCE
#define TAG          "DTLS"
#define DTLS_MTU_SIZE 1500

#define BREAK_ON_FAIL(ret) \
    if (ret != 0) {        \
        break;             \
    }

#ifdef DTLS_SIGN_ONCE
static unsigned char s_cached_cert_pem[DTLS_CERT_PEM_BUF_SIZE];
static unsigned char s_cached_key_pem[DTLS_CERT_PEM_BUF_SIZE];
static bool s_cached_cert_ready = false;
#endif

extern void measure_start(const char *tag);
extern void measure_stop(const char *tag);

static const mbedtls_ssl_srtp_profile default_profiles[] = {
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80, MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
    MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_80, MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_32,
    MBEDTLS_TLS_SRTP_UNSET
};

static int dtls_srtp_entropy_func(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

/* Pin DTLS stack to 1.2 for stable interop across mbedTLS versions. */
static void dtls_srtp_conf_force_dtls12(mbedtls_ssl_config *conf)
{
    mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
}

static void dtls_srtp_x509_digest(const mbedtls_x509_crt *crt, char *buf)
{
    unsigned char digest[32];
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL || mbedtls_md(md_info, crt->raw.p, crt->raw.len, digest) != 0) {
        memset(digest, 0, sizeof(digest));
    }

    for (int i = 0; i < sizeof(digest); i++) {
        snprintf(buf, 4, "%.2X:", digest[i]);
        buf += 3;
    }
    *(--buf) = '\0';
}

static int check_srtp(bool init)
{
    static int init_count = 0;
    if (init) {
        if (init_count == 0) {
            srtp_err_status_t ret = srtp_init();
            if (ret != srtp_err_status_ok) {
                ESP_LOGE(TAG, "Init SRTP failed ret %d", ret);
                return -1;
            }
            ESP_LOGI(TAG, "Init SRTP OK");
            init_count++;
            init_count++;
        }
    } else {
        if (init_count) {
            init_count--;
            if (init_count == 0) {
                srtp_shutdown();
                ESP_LOGI(TAG, "Shutdown SRTP");
            }
        }
    }
    return 0;
}

static void dtls_srtp_key_derivation(void *context, mbedtls_ssl_key_export_type secret_type,
                                     const unsigned char *secret, size_t secret_len,
                                     const unsigned char client_random[32], const unsigned char server_random[32],
                                     mbedtls_tls_prf_types tls_prf_type)
{
    dtls_srtp_t *dtls_srtp = (dtls_srtp_t *)context;
    int ret;
    const char *dtls_srtp_label = "EXTRACTOR-dtls_srtp";
    unsigned char randbytes[64];
    uint8_t key_material[DTLS_SRTP_KEY_MATERIAL_LENGTH];

    (void)secret_type;
    memcpy(randbytes, client_random, 32);
    memcpy(randbytes + 32, server_random, 32);

    if ((ret = mbedtls_ssl_tls_prf(tls_prf_type, secret, secret_len, dtls_srtp_label, randbytes, sizeof(randbytes),
                                   key_material, sizeof(key_material)))
        != 0) {
        ESP_LOGE(TAG, "Fail to export key material ret %d", ret);
        return;
    }
    memset(&dtls_srtp->remote_policy, 0, sizeof(dtls_srtp->remote_policy));
    srtp_crypto_policy_set_rtp_default(&dtls_srtp->remote_policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&dtls_srtp->remote_policy.rtcp);

    memcpy(dtls_srtp->remote_policy_key, key_material, SRTP_MASTER_KEY_LENGTH);
    memcpy(dtls_srtp->remote_policy_key + SRTP_MASTER_KEY_LENGTH,
           key_material + SRTP_MASTER_KEY_LENGTH + SRTP_MASTER_KEY_LENGTH, SRTP_MASTER_SALT_LENGTH);

    dtls_srtp->remote_policy.ssrc.type = ssrc_any_inbound;
    dtls_srtp->remote_policy.key = dtls_srtp->remote_policy_key;
    dtls_srtp->remote_policy.next = NULL;
    srtp_t *send_session = (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) ? &dtls_srtp->srtp_in : &dtls_srtp->srtp_out;
    ret = srtp_create(send_session, &dtls_srtp->remote_policy);
    if (ret != srtp_err_status_ok) {
        ESP_LOGE(TAG, "Fail to create in SRTP session ret %d", ret);
        return;
    }

    memset(&dtls_srtp->local_policy, 0, sizeof(dtls_srtp->local_policy));
    srtp_crypto_policy_set_rtp_default(&dtls_srtp->local_policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&dtls_srtp->local_policy.rtcp);

    memcpy(dtls_srtp->local_policy_key, key_material + SRTP_MASTER_KEY_LENGTH, SRTP_MASTER_KEY_LENGTH);
    memcpy(dtls_srtp->local_policy_key + SRTP_MASTER_KEY_LENGTH,
           key_material + SRTP_MASTER_KEY_LENGTH + SRTP_MASTER_KEY_LENGTH + SRTP_MASTER_SALT_LENGTH,
           SRTP_MASTER_SALT_LENGTH);

    dtls_srtp->local_policy.ssrc.type = ssrc_any_outbound;
    dtls_srtp->local_policy.key = dtls_srtp->local_policy_key;
    dtls_srtp->local_policy.next = NULL;
    srtp_t *recv_session = (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) ? &dtls_srtp->srtp_out : &dtls_srtp->srtp_in;
    ret = srtp_create(recv_session, &dtls_srtp->local_policy);
    if (ret != srtp_err_status_ok) {
        ESP_LOGE(TAG, "Fail to create out SRTP session ret %d", ret);
        return;
    }
    ESP_LOGI(TAG, "SRTP connected OK");
    dtls_srtp->state = DTLS_SRTP_STATE_CONNECTED;
}

static int dtls_srtp_do_handshake(dtls_srtp_t *dtls_srtp)
{
    int ret;
    static mbedtls_timing_delay_context timer;
    mbedtls_ssl_set_timer_cb(&dtls_srtp->ssl, &timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);
    mbedtls_ssl_set_export_keys_cb(&dtls_srtp->ssl, dtls_srtp_key_derivation, dtls_srtp);
    mbedtls_ssl_set_bio(&dtls_srtp->ssl, dtls_srtp, dtls_srtp->udp_send, dtls_srtp->udp_recv, NULL);

    do {
        ret = mbedtls_ssl_handshake(&dtls_srtp->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        mbedtls_ssl_session_reset(&dtls_srtp->ssl);
    }
    return ret;
}

static int dtls_srtp_handshake_server(dtls_srtp_t *dtls_srtp)
{
    int ret;
    ESP_LOGI(TAG, "Start to do server handshake");
    while (1) {
        unsigned char client_ip[] = "test";
        mbedtls_ssl_session_reset(&dtls_srtp->ssl);
        mbedtls_ssl_set_client_transport_id(&dtls_srtp->ssl, client_ip, sizeof(client_ip));
        ret = dtls_srtp_do_handshake(dtls_srtp);
        if (ret != MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
            if (ret != 0) {
                ESP_LOGE(TAG, "Server handshake return -0x%.4x", (unsigned int)-ret);
            }
            break;
        }
    }
    return ret;
}

static int dtls_srtp_handshake_client(dtls_srtp_t *dtls_srtp)
{
    int ret = dtls_srtp_do_handshake(dtls_srtp);
    if (ret != 0) {
        ESP_LOGE(TAG, "CLient handshake fail ret -0x%.4x", (unsigned int)-ret);
        return -1;
    }
    int flags;
    if ((flags = mbedtls_ssl_get_verify_result(&dtls_srtp->ssl)) != 0) {
#if !defined(MBEDTLS_X509_REMOVE_INFO)
        char vrfy_buf[512];
        mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
#endif
    }
    return ret;
}

char *dtls_srtp_get_local_fingerprint(dtls_srtp_t *dtls_srtp)
{
    return dtls_srtp->local_fingerprint;
}

int dtls_srtp_handshake(dtls_srtp_t *dtls_srtp)
{
    int ret;
    if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
        ret = dtls_srtp_handshake_server(dtls_srtp);
    } else {
        ret = dtls_srtp_handshake_client(dtls_srtp);
    }
    if (ret == 0) {
        ESP_LOGI(TAG, "%s handshake success", dtls_srtp->role == DTLS_SRTP_ROLE_SERVER ? "Server" : "Client");
    }
    mbedtls_dtls_srtp_info dtls_srtp_negotiation_result;
    mbedtls_ssl_get_dtls_srtp_negotiation_result(&dtls_srtp->ssl, &dtls_srtp_negotiation_result);
    return ret;
}

dtls_srtp_role_t dtls_srtp_get_role(dtls_srtp_t *dtls_srtp)
{
    return dtls_srtp->role;
}

int dtls_srtp_write(dtls_srtp_t *dtls_srtp, const unsigned char *buf, size_t len)
{
    int ret;
    int consume = 0;
    media_lib_mutex_lock(dtls_srtp->lock, MEDIA_LIB_MAX_LOCK_TIME);
    while (len) {
        measure_start("ssl_write");
        ret = mbedtls_ssl_write(&dtls_srtp->ssl, buf, len);
        measure_stop("ssl_write");
        if (ret > 0) {
            consume += ret;
            buf += ret;
            len -= ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        } else {
            consume = ret;
            break;
        }
    }
    media_lib_mutex_unlock(dtls_srtp->lock);
    return consume;
}

int dtls_srtp_read(dtls_srtp_t *dtls_srtp, unsigned char *buf, size_t len)
{
    int ret = 0;
    int read_bytes = 0;
    media_lib_mutex_lock(dtls_srtp->lock, MEDIA_LIB_MAX_LOCK_TIME);
    while (read_bytes < len) {
        measure_start("ssl_read");
        ret = mbedtls_ssl_read(&dtls_srtp->ssl, buf + read_bytes, len - read_bytes);
        measure_stop("ssl_read");
        if (ret > 0) {
            read_bytes += ret;
            continue;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            ESP_LOGE(TAG, "Detected DTLS connection close ret %d", ret);
            ret = MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            ret = 0;
            break;
        } else {
            ESP_LOGE(TAG, "mbedtls_ssl_read error: %d", ret);
            ret = 0;
            break;
        }
    }
    if (ret != -1 && read_bytes) {
        ret = read_bytes;
    }
    media_lib_mutex_unlock(dtls_srtp->lock);
    return ret;
}

bool dtls_srtp_probe(uint8_t *buf)
{
    if (buf == NULL) {
        return false;
    }
    return ((*buf > 19) && (*buf < 64));
}

int dtls_srtp_decrypt_rtp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int *bytes)
{
    size_t size = *bytes;
    int ret = srtp_unprotect(dtls_srtp->srtp_in, packet, size, packet, &size);
    *bytes = size;
    return ret;
}

int dtls_srtp_decrypt_rtcp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int *bytes)
{
    size_t size = *bytes;
    int ret = srtp_unprotect_rtcp(dtls_srtp->srtp_in, packet, size, packet, &size);
    *bytes = size;
    return ret;
}

void dtls_srtp_encrypt_rtp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int buf_size, int *bytes)
{
    size_t size = buf_size;
    srtp_protect(dtls_srtp->srtp_out, packet, *bytes, packet, &size, 0);
    *bytes = size;
}

void dtls_srtp_encrypt_rctp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int buf_size, int *bytes)
{
    size_t size = buf_size;
    srtp_protect_rtcp(dtls_srtp->srtp_out, packet, *bytes, packet, &size, 0);
    *bytes = size;
}
