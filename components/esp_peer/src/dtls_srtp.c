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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "mbedtls/ssl.h"
#include "dtls_srtp.h"
#include "esp_log.h"

#define TAG "DTLS"

#define DTLS_SIGN_ONCE
#define DTLS_MTU_SIZE 1500
// #define DUMP_DTLS_KEY

#define BREAK_ON_FAIL(ret) \
    if (ret != 0) {        \
        break;             \
    }

extern void measure_start(const char *tag);
extern void measure_stop(const char *tag);

static const mbedtls_ssl_srtp_profile default_profiles[] = {
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80, MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
    MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_80, MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_32,
    MBEDTLS_TLS_SRTP_UNSET
};

static bool already_signed = false;
#ifdef DTLS_SIGN_ONCE
static mbedtls_ctr_drbg_context signed_ctr_drbg;
static mbedtls_x509_crt signed_cert;
static mbedtls_pk_context signed_pkey;
static mbedtls_entropy_context signed_entropy;
#endif

static void dtls_srtp_x509_digest(const mbedtls_x509_crt *crt, char *buf)
{
    unsigned char digest[32];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);
    mbedtls_sha256_update(&sha256_ctx, crt->raw.p, crt->raw.len);
    mbedtls_sha256_finish(&sha256_ctx, (unsigned char *)digest);
    mbedtls_sha256_free(&sha256_ctx);

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

static int dtls_srtp_selfsign_cert(dtls_srtp_t *dtls_srtp)
{
    int ret;
    mbedtls_x509write_cert crt;
    unsigned char *cert_buf = (unsigned char *)malloc(RSA_KEY_LENGTH);
    if (cert_buf == NULL) {
        return -1;
    }
    const char *pers = "dtls_srtp";

    mbedtls_x509write_crt_init(&crt);
    mbedtls_ctr_drbg_seed(&dtls_srtp->ctr_drbg, mbedtls_entropy_func, &dtls_srtp->entropy, (const unsigned char *)pers,
                          strlen(pers));
    mbedtls_pk_setup(&dtls_srtp->pkey, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    mbedtls_rsa_gen_key(mbedtls_pk_rsa(dtls_srtp->pkey), mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg, RSA_KEY_LENGTH,
                        65537);

    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_name(&crt, "CN=dtls_srtp");
    mbedtls_x509write_crt_set_issuer_name(&crt, "CN=dtls_srtp");

#if MBEDTLS_VERSION_MAJOR == 3 && MBEDTLS_VERSION_MINOR >= 4 || MBEDTLS_VERSION_MAJOR >= 4
    unsigned char *serial = (unsigned char *)"1";
    size_t serial_len = 1;
    ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial, serial_len);
    if (ret < 0) {
        printf("mbedtls_x509write_crt_set_serial_raw failed\n");
    }
#else
    mbedtls_mpi serial;
    mbedtls_mpi_init(&serial);
    mbedtls_mpi_fill_random(&serial, 16, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
    mbedtls_x509write_crt_set_serial(&crt, &serial);
    mbedtls_mpi_free(&serial);
#endif

    mbedtls_x509write_crt_set_validity(&crt, "20230101000000", "20280101000000");
    mbedtls_x509write_crt_set_subject_key(&crt, &dtls_srtp->pkey);
    mbedtls_x509write_crt_set_issuer_key(&crt, &dtls_srtp->pkey);
    ret = mbedtls_x509write_crt_pem(&crt, cert_buf, RSA_KEY_LENGTH, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
    if (ret < 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_pem failed");
    }
    mbedtls_x509_crt_parse(&dtls_srtp->cert, cert_buf, RSA_KEY_LENGTH);
    mbedtls_x509write_crt_free(&crt);
    free(cert_buf);
    return ret;
}

static int dtls_srtp_try_gen_cert(dtls_srtp_t *dtls_srtp)
{
    int ret = 0;
#ifdef DTLS_SIGN_ONCE
    if (already_signed) {
        dtls_srtp->ctr_drbg = signed_ctr_drbg;
        dtls_srtp->cert = signed_cert;
        dtls_srtp->pkey = signed_pkey;
        dtls_srtp->entropy = signed_entropy;
        return 0;
    }
#endif
    mbedtls_x509_crt_init(&dtls_srtp->cert);
    mbedtls_pk_init(&dtls_srtp->pkey);
    mbedtls_entropy_init(&dtls_srtp->entropy);
    mbedtls_ctr_drbg_init(&dtls_srtp->ctr_drbg);
    ret = dtls_srtp_selfsign_cert(dtls_srtp);
    if (ret != 0) {
        return ret;
    }
#ifdef DTLS_SIGN_ONCE
    already_signed = true;
    signed_ctr_drbg = dtls_srtp->ctr_drbg;
    signed_cert = dtls_srtp->cert;
    signed_pkey = dtls_srtp->pkey;
    signed_entropy = dtls_srtp->entropy;
#endif
    return 0;
}

int dtls_srtp_gen_cert(void)
{
#ifdef DTLS_SIGN_ONCE
    dtls_srtp_t *dtls_srtp = (dtls_srtp_t *) media_lib_calloc(1, sizeof(dtls_srtp_t));
    if (dtls_srtp == NULL) {
        return -1;
    }
    if (already_signed) {
        mbedtls_x509_crt_free(&signed_cert);
        mbedtls_pk_free(&signed_pkey);
        mbedtls_ctr_drbg_free(&signed_ctr_drbg);
        already_signed = false;
    }
    int ret = dtls_srtp_try_gen_cert(dtls_srtp);
    dtls_srtp_deinit(dtls_srtp);
    return ret;
#else
    return -1;
 #endif
}

dtls_srtp_t *dtls_srtp_init(dtls_srtp_cfg_t *cfg)
{
    dtls_srtp_t *dtls_srtp = (dtls_srtp_t *) media_lib_calloc(1, sizeof(dtls_srtp_t));
    if (dtls_srtp == NULL) {
        return NULL;
    }
    int ret = check_srtp(true);
    do {
        BREAK_ON_FAIL(ret);
        media_lib_mutex_create(&dtls_srtp->lock);
        dtls_srtp->role = cfg->role;
        dtls_srtp->state = DTLS_SRTP_STATE_INIT;
        dtls_srtp->ctx = cfg->ctx;
        dtls_srtp->udp_send = cfg->udp_send;
        dtls_srtp->udp_recv = cfg->udp_recv;

        mbedtls_ssl_config_init(&dtls_srtp->conf);
        mbedtls_ssl_init(&dtls_srtp->ssl);
        ret = dtls_srtp_try_gen_cert(dtls_srtp);
        BREAK_ON_FAIL(ret);

        mbedtls_ssl_conf_ca_chain(&dtls_srtp->conf, &dtls_srtp->cert, NULL);
        ret = mbedtls_ssl_conf_own_cert(&dtls_srtp->conf, &dtls_srtp->cert, &dtls_srtp->pkey);
        mbedtls_ssl_conf_rng(&dtls_srtp->conf, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
        BREAK_ON_FAIL(ret);

        mbedtls_ssl_conf_read_timeout(&dtls_srtp->conf, 1000);
        mbedtls_ssl_conf_handshake_timeout(&dtls_srtp->conf, 1000, 6000);
        // mbedtls_ssl_conf_dtls_replay_window(&dtls_srtp->conf, 1000);
        mbedtls_ssl_conf_dtls_anti_replay(&dtls_srtp->conf, MBEDTLS_SSL_ANTI_REPLAY_DISABLED);

        if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
            ret = mbedtls_ssl_config_defaults(&dtls_srtp->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT);
            mbedtls_ssl_cookie_init(&dtls_srtp->cookie_ctx);
            mbedtls_ssl_cookie_setup(&dtls_srtp->cookie_ctx, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
            mbedtls_ssl_conf_dtls_cookies(&dtls_srtp->conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check,
                                        &dtls_srtp->cookie_ctx);
        } else {
            ret = mbedtls_ssl_config_defaults(&dtls_srtp->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT);
        }
        BREAK_ON_FAIL(ret);

        dtls_srtp_x509_digest(&dtls_srtp->cert, dtls_srtp->local_fingerprint);
        ret = mbedtls_ssl_conf_dtls_srtp_protection_profiles(&dtls_srtp->conf, default_profiles);
        BREAK_ON_FAIL(ret);

        mbedtls_ssl_conf_srtp_mki_value_supported(&dtls_srtp->conf, MBEDTLS_SSL_DTLS_SRTP_MKI_UNSUPPORTED);
        ret = mbedtls_ssl_setup(&dtls_srtp->ssl, &dtls_srtp->conf);
        BREAK_ON_FAIL(ret);
        mbedtls_ssl_set_mtu(&dtls_srtp->ssl, DTLS_MTU_SIZE);
        return dtls_srtp;
    } while (0);
    dtls_srtp_deinit(dtls_srtp);
    return NULL;
}

char *dtls_srtp_get_local_fingerprint(dtls_srtp_t *dtls_srtp)
{
    return dtls_srtp->local_fingerprint;
}

void dtls_srtp_deinit(dtls_srtp_t *dtls_srtp)
{
    if (dtls_srtp->state == DTLS_SRTP_STATE_NONE) {
        return;
    }
    mbedtls_ssl_free(&dtls_srtp->ssl);
    mbedtls_ssl_config_free(&dtls_srtp->conf);

    if (already_signed == false) {
        mbedtls_entropy_free(&dtls_srtp->entropy);
        mbedtls_x509_crt_free(&dtls_srtp->cert);
        mbedtls_pk_free(&dtls_srtp->pkey);
        mbedtls_ctr_drbg_free(&dtls_srtp->ctr_drbg);
    }

    if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
        mbedtls_ssl_cookie_free(&dtls_srtp->cookie_ctx);
    }
    if (dtls_srtp->srtp_in) {
        srtp_dealloc(dtls_srtp->srtp_in);
        dtls_srtp->srtp_in = NULL;
    }
    if (dtls_srtp->srtp_out) {
        srtp_dealloc(dtls_srtp->srtp_out);
        dtls_srtp->srtp_out = NULL;
    }
    if (dtls_srtp->lock) {
        media_lib_mutex_destroy(dtls_srtp->lock);
    }
    check_srtp(false);
    dtls_srtp->state = DTLS_SRTP_STATE_NONE;
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

    memcpy(randbytes, client_random, 32);
    memcpy(randbytes + 32, server_random, 32);
    // Debug key for wireshark
#ifdef DUMP_DTLS_KEY
    printf("CLIENT_RANDOM ");
    for (int i = 0; i < 32; i++)
        printf("%02x", client_random[i]);
    printf(" ");
    for (int i = 0; i < secret_len; i++)
        printf("%02x", secret[i]);
    printf("\n\n");
#endif
    // Export keying material
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
    // derive outbounds keys
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
#endif
#if !defined(MBEDTLS_X509_REMOVE_INFO)
        mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
#endif
    }
    return ret;
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

void dtls_srtp_reset_session(dtls_srtp_t *dtls_srtp, dtls_srtp_role_t role)
{
    if (dtls_srtp->state == DTLS_SRTP_STATE_CONNECTED) {
        srtp_dealloc(dtls_srtp->srtp_in);
        dtls_srtp->srtp_in = NULL;
        srtp_dealloc(dtls_srtp->srtp_out);
        dtls_srtp->srtp_out = NULL;
        mbedtls_ssl_session_reset(&dtls_srtp->ssl);
    }
    if (role != dtls_srtp->role) {
        if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
            mbedtls_ssl_cookie_free(&dtls_srtp->cookie_ctx);
        }
        if (role == DTLS_SRTP_ROLE_SERVER) {
            mbedtls_ssl_config_defaults(&dtls_srtp->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);

            mbedtls_ssl_cookie_init(&dtls_srtp->cookie_ctx);
            mbedtls_ssl_cookie_setup(&dtls_srtp->cookie_ctx, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
            mbedtls_ssl_conf_dtls_cookies(&dtls_srtp->conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check,
                                          &dtls_srtp->cookie_ctx);
        } else {
            // CLient need skip verify server CA verify fingerprint later
            mbedtls_ssl_config_defaults(&dtls_srtp->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
            mbedtls_ssl_conf_authmode(&dtls_srtp->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        }
        mbedtls_ssl_conf_dtls_srtp_protection_profiles(&dtls_srtp->conf, default_profiles);
        mbedtls_ssl_conf_srtp_mki_value_supported(&dtls_srtp->conf, MBEDTLS_SSL_DTLS_SRTP_MKI_UNSUPPORTED);
        mbedtls_ssl_conf_dtls_anti_replay(&dtls_srtp->conf, MBEDTLS_SSL_ANTI_REPLAY_DISABLED);
        mbedtls_ssl_setup(&dtls_srtp->ssl, &dtls_srtp->conf);
        mbedtls_ssl_set_mtu(&dtls_srtp->ssl, DTLS_MTU_SIZE);
        dtls_srtp->role = role;
    }
    dtls_srtp->state = DTLS_SRTP_STATE_INIT;
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
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { // Detected DTLS connection close ret
            ESP_LOGE(TAG, "Detected DTLS connection close ret %d", ret);
            ret = -1;
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
