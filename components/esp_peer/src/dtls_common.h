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
#include "peer_utils.h"

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
    mbedtls_ssl_conf_transport(conf, MBEDTLS_SSL_TRANSPORT_DATAGRAM);
    mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
}

/* Values match esp_peer_dtls_cipher_pref_t in esp_peer_default.h */
/*
 * Apply A/B cipher preference. Lists must be static: mbedTLS keeps the pointer.
 * AUTO keeps mbedTLS default order after config_defaults().
 */
static void dtls_srtp_conf_cipher_pref(mbedtls_ssl_config *conf)
{
    static const int aes_gcm_only[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        0,
    };
/* Prefer sdkconfig flag: MBEDTLS_CHACHAPOLY_C is internal in Mbed TLS 4 / TF-PSA. */
#if defined(CONFIG_MBEDTLS_CHACHAPOLY_C) || defined(MBEDTLS_CHACHAPOLY_C)
    static const int chacha_first[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        0,
    };
#endif
    esp_peer_dtls_cipher_pref_t pref = peer_get_dtls_cipher_pref();
    if (pref == ESP_PEER_DTLS_CIPHER_AES_GCM) {
        mbedtls_ssl_conf_ciphersuites(conf, aes_gcm_only);
        ESP_LOGI(TAG, "DTLS cipher pref: AES-GCM only (A/B)");
    } else if (pref == ESP_PEER_DTLS_CIPHER_CHACHA) {
#if defined(CONFIG_MBEDTLS_CHACHAPOLY_C) || defined(MBEDTLS_CHACHAPOLY_C)
        mbedtls_ssl_conf_ciphersuites(conf, chacha_first);
        ESP_LOGI(TAG, "DTLS cipher pref: ChaCha20-Poly1305 first (A/B)");
#else
        ESP_LOGW(TAG, "DTLS cipher pref: ChaCha requested but not compiled in");
#endif
    } else {
        ESP_LOGI(TAG, "DTLS cipher pref: auto (mbedtls default order)");
    }
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
        }
        init_count++;
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
#ifdef DUMP_DTLS_KEY
    printf("CLIENT_RANDOM ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", client_random[i]);
    }
    printf(" ");
    for (int i = 0; i < (int)secret_len; i++) {
        printf("%02x", secret[i]);
    }
    printf("\n\n");
#endif

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

#if defined(DTLS_USE_CH_REASM_BIO)
/*
 * Fragmented ClientHello reassembly BIO (see DTLS_USE_CH_REASM_BIO).
 * One datagram on stack (MTU), and one message-sized heap buffer only while a
 * fragmented ClientHello is active.
 */
#define DTLS_RECORD_HDR_LEN 13
#define DTLS_HS_HDR_LEN     12
#define DTLS_CH_MAX_MSG     4096

static void dtls_srtp_ch_reasm_reset(dtls_srtp_t *d)
{
    d->ch_total = 0;
    d->ch_got = 0;
    d->ch_msg_seq = 0;
    d->ch_active = 0;
}

static void dtls_srtp_ch_pending_clear(dtls_srtp_t *d)
{
    d->ch_len = 0;
    d->ch_pos = 0;
}

static void dtls_srtp_ch_reasm_free(dtls_srtp_t *d)
{
    if (d->ch_buf) {
        media_lib_free(d->ch_buf);
        d->ch_buf = NULL;
    }
    d->ch_cap = 0;
    dtls_srtp_ch_pending_clear(d);
    dtls_srtp_ch_reasm_reset(d);
}

static uint8_t *dtls_srtp_ch_mask(dtls_srtp_t *d)
{
    return d->ch_buf + DTLS_RECORD_HDR_LEN + DTLS_HS_HDR_LEN + d->ch_total;
}

static int dtls_srtp_ch_ensure_msg_buf(dtls_srtp_t *d, size_t total)
{
    size_t need = DTLS_RECORD_HDR_LEN + DTLS_HS_HDR_LEN + total + ((total + 7) / 8);

    if (total == 0 || total > DTLS_CH_MAX_MSG) {
        return -1;
    }
    if (d->ch_buf && d->ch_cap >= need) {
        return 0;
    }
    uint8_t *p = (uint8_t *)media_lib_malloc(need);
    if (p == NULL) {
        return -1;
    }
    if (d->ch_buf) {
        media_lib_free(d->ch_buf);
    }
    d->ch_buf = p;
    d->ch_cap = need;
    dtls_srtp_ch_pending_clear(d);
    return 0;
}

static int dtls_srtp_ch_queue_datagram(dtls_srtp_t *d, const uint8_t *data, size_t len)
{
    if (len == 0 || len > DTLS_CH_MAX_MSG) {
        return -1;
    }
    if (d->ch_buf == NULL || d->ch_cap < len) {
        uint8_t *p = (uint8_t *)media_lib_malloc(len);
        if (p == NULL) {
            return -1;
        }
        if (d->ch_buf) {
            media_lib_free(d->ch_buf);
        }
        d->ch_buf = p;
        d->ch_cap = len;
    }
    memcpy(d->ch_buf, data, len);
    d->ch_len = len;
    d->ch_pos = 0;
    return 0;
}

/*
 * mbedTLS TLS1.2-only server maps DTLS wire versions via read_version():
 *   0xfefd -> TLS1.2 (ok), 0xfeff -> TLS1.1 -> MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION.
 * Browsers often put DTLS 1.0 (0xfeff) in ClientHello.client_version for
 * HelloVerify compatibility while still negotiating DTLS 1.2. Rewrite to 0xfefd.
 */
static void dtls_srtp_ch_normalize_legacy_version(uint8_t *rec, size_t len)
{
    const size_t ver_off = DTLS_RECORD_HDR_LEN + DTLS_HS_HDR_LEN;
    if (len < ver_off + 2 || rec[0] != 0x16) {
        return;
    }
    if (rec[DTLS_RECORD_HDR_LEN] != 0x01) {
        return; /* not ClientHello */
    }
    /* Record layer often carries DTLS 1.0 (feff); keep content-version authoritative. */
    if (rec[1] == 0xfe && rec[2] == 0xff) {
        rec[2] = 0xfd;
    }
    ESP_LOGD(TAG, "ClientHello version %02x%02x (rec %02x%02x len=%u)",
             rec[ver_off], rec[ver_off + 1], rec[1], rec[2], (unsigned)len);
    if (rec[ver_off] == 0xfe && rec[ver_off + 1] == 0xff) {
        rec[ver_off + 1] = 0xfd;
        ESP_LOGD(TAG, "Rewrite ClientHello version feff->fefd for mbedTLS");
    }
}

/* DTLS application content types are in 20..63 (RFC 6347). */
static inline int dtls_srtp_is_dtls_record(const uint8_t *data, int len)
{
    return (len >= DTLS_RECORD_HDR_LEN) && (data[0] > 19) && (data[0] < 64);
}

static int dtls_srtp_ch_finish(dtls_srtp_t *d)
{
    size_t hs_len = DTLS_HS_HDR_LEN + d->ch_total;
    size_t rec_len = DTLS_RECORD_HDR_LEN + hs_len;
    uint8_t *out = d->ch_buf;

    memcpy(out, d->ch_rec_hdr, DTLS_RECORD_HDR_LEN);
    out[11] = (uint8_t)((hs_len >> 8) & 0xff);
    out[12] = (uint8_t)(hs_len & 0xff);
    out[13] = 0x01; /* ClientHello */
    out[14] = (uint8_t)((d->ch_total >> 16) & 0xff);
    out[15] = (uint8_t)((d->ch_total >> 8) & 0xff);
    out[16] = (uint8_t)(d->ch_total & 0xff);
    out[17] = (uint8_t)((d->ch_msg_seq >> 8) & 0xff);
    out[18] = (uint8_t)(d->ch_msg_seq & 0xff);
    out[19] = out[20] = out[21] = 0; /* fragment_offset */
    out[22] = out[14];
    out[23] = out[15];
    out[24] = out[16]; /* fragment_length = total */
    /* body already at out+25 */
    dtls_srtp_ch_normalize_legacy_version(out, rec_len);

    d->ch_len = rec_len;
    d->ch_pos = 0;
    dtls_srtp_ch_reasm_reset(d);
    return 0;
}

static int dtls_srtp_ch_add_fragment(dtls_srtp_t *d, const uint8_t *rec, size_t rec_size)
{
    const uint8_t *hs = rec + DTLS_RECORD_HDR_LEN;
    size_t payload = rec_size - DTLS_RECORD_HDR_LEN;
    size_t total, frag_off, frag_len;
    uint16_t msg_seq;
    uint8_t *mask;
    uint8_t *body;
    size_t i;

    if (payload < DTLS_HS_HDR_LEN || hs[0] != 0x01) {
        return -1;
    }
    total = ((size_t)hs[1] << 16) | ((size_t)hs[2] << 8) | hs[3];
    msg_seq = ((uint16_t)hs[4] << 8) | hs[5];
    frag_off = ((size_t)hs[6] << 16) | ((size_t)hs[7] << 8) | hs[8];
    frag_len = ((size_t)hs[9] << 16) | ((size_t)hs[10] << 8) | hs[11];

    if (total == 0 || frag_off + frag_len > total || DTLS_HS_HDR_LEN + frag_len > payload) {
        return -1;
    }

    if (!d->ch_active || d->ch_msg_seq != msg_seq || d->ch_total != total) {
        if (dtls_srtp_ch_ensure_msg_buf(d, total) != 0) {
            return -1;
        }
        dtls_srtp_ch_pending_clear(d);
        memcpy(d->ch_rec_hdr, rec, DTLS_RECORD_HDR_LEN);
        d->ch_total = total;
        d->ch_msg_seq = msg_seq;
        d->ch_got = 0;
        d->ch_active = 1;
        body = d->ch_buf + DTLS_RECORD_HDR_LEN + DTLS_HS_HDR_LEN;
        memset(body, 0, total);
        memset(dtls_srtp_ch_mask(d), 0, (total + 7) / 8);
        ESP_LOGD(TAG, "ClientHello reasm start seq=%u total=%u", (unsigned)msg_seq,
                 (unsigned)total);
    }

    mask = dtls_srtp_ch_mask(d);
    body = d->ch_buf + DTLS_RECORD_HDR_LEN + DTLS_HS_HDR_LEN;
    for (i = 0; i < frag_len; i++) {
        size_t idx = frag_off + i;
        size_t byte = idx / 8;
        uint8_t bit = (uint8_t)(1u << (idx % 8));
        if ((mask[byte] & bit) == 0) {
            mask[byte] |= bit;
            body[idx] = hs[DTLS_HS_HDR_LEN + i];
            d->ch_got++;
        }
    }
    ESP_LOGD(TAG, "ClientHello frag off=%u len=%u got=%u/%u",
             (unsigned)frag_off, (unsigned)frag_len, (unsigned)d->ch_got,
             (unsigned)d->ch_total);
    if (d->ch_got >= d->ch_total) {
        return dtls_srtp_ch_finish(d);
    }
    return 0;
}

/* Returns 1 if data ready to serve, 0 need more, -1 skip/ignore this datagram. */
static int dtls_srtp_ch_handle_datagram(dtls_srtp_t *d, const uint8_t *data, int len)
{
    if (!dtls_srtp_is_dtls_record(data, len)) {
        /* ICE/STUN/RTP can still appear on the selected pair; never feed to mbedTLS. */
        return d->ch_active ? 0 : -1;
    }
    uint16_t rec_len = ((uint16_t)data[11] << 8) | data[12];
    int rec_size = DTLS_RECORD_HDR_LEN + (int)rec_len;

    if (rec_size > len) {
        return d->ch_active ? 0 : -1;
    }

    if (data[0] == 0x16 && rec_len >= DTLS_HS_HDR_LEN) {
        const uint8_t *hs = data + DTLS_RECORD_HDR_LEN;
        size_t total = ((size_t)hs[1] << 16) | ((size_t)hs[2] << 8) | hs[3];
        size_t frag_off = ((size_t)hs[6] << 16) | ((size_t)hs[7] << 8) | hs[8];
        size_t frag_len = ((size_t)hs[9] << 16) | ((size_t)hs[10] << 8) | hs[11];

        if (hs[0] == 0x01 && (frag_off != 0 || frag_len != total)) {
            int ret = dtls_srtp_ch_add_fragment(d, data, (size_t)rec_size);
            if (ret < 0) {
                /* Keep gathering; do not fall back to a raw partial ClientHello. */
                ESP_LOGW(TAG, "ClientHello fragment rejected (total=%u off=%u len=%u)",
                         (unsigned)total, (unsigned)frag_off, (unsigned)frag_len);
                return 0;
            }
            return (d->ch_len > d->ch_pos) ? 1 : 0;
        }
    }

    /* Unfragmented DTLS: queue the full UDP datagram (may contain several
     * records, e.g. CKE+CCS+Finished). Truncating to the first record drops
     * the rest and hangs the handshake after key derivation. */
    dtls_srtp_ch_reasm_reset(d);
    if (dtls_srtp_ch_queue_datagram(d, data, (size_t)len) != 0) {
        return -1;
    }
    dtls_srtp_ch_normalize_legacy_version(d->ch_buf, d->ch_len);
    return 1;
}

/* Max UDP pulls while waiting for remaining ClientHello fragments in one BIO call. */
#define DTLS_CH_RECV_MAX_ATTEMPTS 8

/*
 * DTLS BIO must return one full datagram per call. Returning a partial
 * reassembled ClientHello makes mbedTLS treat the slice as a complete record.
 */
static int dtls_srtp_bio_serve_pending(dtls_srtp_t *d, unsigned char *buf, size_t len)
{
    size_t avail;

    if (d->ch_buf == NULL || d->ch_pos >= d->ch_len) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    avail = d->ch_len - d->ch_pos;
    if (len < avail) {
        ESP_LOGE(TAG, "DTLS BIO buffer too small (%u < %u)", (unsigned)len, (unsigned)avail);
        return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
    }
    memcpy(buf, d->ch_buf + d->ch_pos, avail);
    d->ch_pos = d->ch_len;
    if (!d->ch_active) {
        dtls_srtp_ch_reasm_free(d);
    }
    return (int)avail;
}

static int dtls_srtp_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    dtls_srtp_t *d = (dtls_srtp_t *)ctx;
    uint8_t pkt[DTLS_MTU_SIZE];
    int ret;
    int proc;
    int attempts = 0;

    if (d->ch_pos < d->ch_len && d->ch_buf) {
        return dtls_srtp_bio_serve_pending(d, buf, len);
    }

    for (;;) {
        ret = d->udp_recv(ctx, pkt, sizeof(pkt));
        if (ret < 0) {
            /* Do not map errors to WANT_READ — that busy-loops handshake + agent_recv. */
            return ret;
        }
        if (ret == 0) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        proc = dtls_srtp_ch_handle_datagram(d, pkt, ret);
        if (proc < 0) {
            /* Non-DTLS or unusable packet before assembly started: keep reading. */
            if (++attempts >= DTLS_CH_RECV_MAX_ATTEMPTS) {
                return MBEDTLS_ERR_SSL_WANT_READ;
            }
            continue;
        }
        if (proc > 0) {
            break;
        }
        /* Need more ClientHello fragments (or skipped ICE/STUN while assembling). */
        if (++attempts >= DTLS_CH_RECV_MAX_ATTEMPTS) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
    }

    return dtls_srtp_bio_serve_pending(d, buf, len);
}
#endif /* DTLS_USE_CH_REASM_BIO */

static int dtls_srtp_do_handshake(dtls_srtp_t *dtls_srtp)
{
    int ret;
#if defined(DTLS_USE_CH_REASM_BIO)
    int want_read_loops = 0;
#endif
    /* Re-pin before every attempt; -0x6e00 is consistent with STREAM mapping of fefd. */
    dtls_srtp_conf_force_dtls12(&dtls_srtp->conf);
    /* Clear any leftover delay from a previous session on this instance. */
    mbedtls_timing_set_delay(&dtls_srtp->timer, 0, 0);
    mbedtls_ssl_set_timer_cb(&dtls_srtp->ssl, &dtls_srtp->timer, mbedtls_timing_set_delay,
                             mbedtls_timing_get_delay);
    mbedtls_ssl_set_export_keys_cb(&dtls_srtp->ssl, dtls_srtp_key_derivation, dtls_srtp);
#if defined(DTLS_USE_CH_REASM_BIO)
    mbedtls_ssl_set_bio(&dtls_srtp->ssl, dtls_srtp, dtls_srtp->udp_send, dtls_srtp_bio_recv, NULL);
#else
    mbedtls_ssl_set_bio(&dtls_srtp->ssl, dtls_srtp, dtls_srtp->udp_send, dtls_srtp->udp_recv, NULL);
#endif

    do {
        ret = mbedtls_ssl_handshake(&dtls_srtp->ssl);
#if defined(DTLS_USE_CH_REASM_BIO)
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            /*
             * While a fragmented ClientHello is buffered, keep calling so the
             * BIO can pull the remaining UDP fragments in this handshake attempt.
             * Only yield to the peer after too many empty reads.
             */
            if (++want_read_loops >= (dtls_srtp->ch_active ? (DTLS_CH_RECV_MAX_ATTEMPTS * 4)
                                                           : DTLS_CH_RECV_MAX_ATTEMPTS)) {
                break;
            }
            continue;
        }
        want_read_loops = 0;
#endif
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
        dtls_srtp_conf_force_dtls12(&dtls_srtp->conf);
        mbedtls_ssl_session_reset(&dtls_srtp->ssl);
        mbedtls_ssl_set_client_transport_id(&dtls_srtp->ssl, client_ip, sizeof(client_ip));
#if defined(DTLS_USE_CH_REASM_BIO)
        /* Drop stale queued bytes; keep in-progress fragment assembly in ch_buf. */
        if (!dtls_srtp->ch_active) {
            dtls_srtp_ch_pending_clear(dtls_srtp);
        }
#endif
        ret = dtls_srtp_do_handshake(dtls_srtp);
        if (ret != MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
#if defined(DTLS_USE_CH_REASM_BIO)
            /* Partial fragmented ClientHello buffered; peer will call handshake again. */
            if (ret == MBEDTLS_ERR_SSL_WANT_READ && dtls_srtp->ch_active) {
                return ret;
            }
            /* Hard failure: drop partial reassembly so next attempt starts clean. */
            if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ) {
                dtls_srtp_ch_reasm_free(dtls_srtp);
            }
#endif
            if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ) {
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
        {
            const char *cs = mbedtls_ssl_get_ciphersuite(&dtls_srtp->ssl);
            ESP_LOGI(TAG, "Negotiated DTLS ciphersuite: %s", cs ? cs : "(null)");
        }
#if defined(DTLS_USE_CH_REASM_BIO)
        /* Datachannel/DTLS app data must not pay reassembly BIO overhead. */
        mbedtls_ssl_set_bio(&dtls_srtp->ssl, dtls_srtp, dtls_srtp->udp_send, dtls_srtp->udp_recv, NULL);
        dtls_srtp_ch_reasm_free(dtls_srtp);
#endif
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
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
                   ret == MBEDTLS_ERR_SSL_CLIENT_RECONNECT) {
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
