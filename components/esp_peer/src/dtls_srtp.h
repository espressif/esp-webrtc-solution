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

#include <stdio.h>
#include <stdlib.h>
#include "esp_idf_version.h"
#if MBEDTLS_MAJOR_VERSION >= 4
#include <mbedtls/private/ctr_drbg.h>
#else
#include <mbedtls/ctr_drbg.h>
#endif
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/timing.h>
#include <srtp.h>
#include "media_lib_os.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DTLS_CERT_PEM_BUF_SIZE        2048
#define SRTP_MASTER_KEY_LENGTH        16
#define SRTP_MASTER_SALT_LENGTH       14
#define DTLS_SRTP_KEY_MATERIAL_LENGTH 60
#define DTLS_SRTP_FINGERPRINT_LENGTH  160

/**
 * @brief  DTLS role
 */
typedef enum {
    DTLS_SRTP_ROLE_CLIENT,
    DTLS_SRTP_ROLE_SERVER
} dtls_srtp_role_t;

/**
 * @brief  DTLS state
 */
typedef enum {
    DTLS_SRTP_STATE_NONE,
    DTLS_SRTP_STATE_INIT,
    DTLS_SRTP_STATE_HANDSHAKE,
    DTLS_SRTP_STATE_CONNECTED
} dtls_srtp_state_t;

/**
 * @brief  Struct for DTLS SRTP
 */
typedef struct {
    void                    *ctx;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_ssl_cookie_ctx   cookie_ctx;
    mbedtls_x509_crt         cert;
    mbedtls_pk_context       pkey;
#if ESP_IDF_VERSION_MAJOR >= 6
    mbedtls_svc_key_id_t     psa_key_id;
#endif
    mbedtls_ctr_drbg_context ctr_drbg;
    dtls_srtp_role_t         role;
    dtls_srtp_state_t        state;
    srtp_policy_t            remote_policy;
    srtp_policy_t            local_policy;
    srtp_t                   srtp_in;
    srtp_t                   srtp_out;
    unsigned char            remote_policy_key[SRTP_MASTER_KEY_LENGTH + SRTP_MASTER_SALT_LENGTH];
    unsigned char            local_policy_key[SRTP_MASTER_KEY_LENGTH + SRTP_MASTER_SALT_LENGTH];
    char                     local_fingerprint[DTLS_SRTP_FINGERPRINT_LENGTH];
    char                     remote_fingerprint[DTLS_SRTP_FINGERPRINT_LENGTH];
    media_lib_mutex_handle_t lock;
    int                      (*udp_send)(void *ctx, const unsigned char *buf, size_t len);
    int                      (*udp_recv)(void *ctx, unsigned char *buf, size_t len);
} dtls_srtp_t;

/**
 * @brief  DTLS configuration
 */
typedef struct {
    dtls_srtp_role_t role;
    int              (*udp_send)(void *ctx, const unsigned char *buf, size_t len);
    int              (*udp_recv)(void *ctx, unsigned char *buf, size_t len);
    void             *ctx;
} dtls_srtp_cfg_t;


/**
 * @brief  Generate certification data for DTLS
 *
 * @note  Each time call will re-generate a new one
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to generate
 */
int dtls_srtp_gen_cert(void);

/**
 * @brief  Initialize for DTSP SRTP
 *
 * @param[in]  cfg  DTLS configuration
 *
 * @return
 *       - NULL    Not enough memory or gen cert failed
 *       - Others  DTLS SRTP instance
 */
dtls_srtp_t* dtls_srtp_init(dtls_srtp_cfg_t *cfg);

/**
 * @brief  Get local fingerprint
 *
 * @param[in]  dtls_srtp DTLS SRTP instance
 *
 * @return
 *       - Local fingerprint string
 */
char *dtls_srtp_get_local_fingerprint(dtls_srtp_t *dtls_srtp);

/**
 * @brief  Do handshake for DTLS
 *
 * @param[in]  dtls_srtp  DTLS SRTP instance
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to handshake
 */
int dtls_srtp_handshake(dtls_srtp_t *dtls_srtp);

/**
 * @brief  Reset session to use defined role
 *
 * @param[in]  dtls_srtp  DTLS SRTP instance
 * @param[in]  role       DTLS role
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to reset
 */
void dtls_srtp_reset_session(dtls_srtp_t *dtls_srtp, dtls_srtp_role_t role);

/**
 * @brief  Get DTLS role
 *
 * @param[in]  dtls_srtp DTLS SRTP instance
 *
 * @return
 *       - Current DTLS role
 */
dtls_srtp_role_t dtls_srtp_get_role(dtls_srtp_t *dtls_srtp);

/**
 * @brief  Write data through DTLS
 *
 * @param[in]  dtls_srtp  DTLS SRTP instance
 * @param[in]  buf        Buffer to write
 * @param[in]  len        Buffer size to write
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to write
 */
int dtls_srtp_write(dtls_srtp_t *dtls_srtp, const uint8_t *buf, size_t len);

/**
 * @brief  Read data through DTLS
 *
 * @param[in]  dtls_srtp  DTLS SRTP instance
 * @param[in]  buf        Buffer to read
 * @param[in]  len        Data length to read
 *
 * @return
 *       - < 0     Failed to read
 *       - Others  Actual read bytes
 */
int dtls_srtp_read(dtls_srtp_t *dtls_srtp, uint8_t *buf, size_t len);

/**
 * @brief  Verify whether packet is DTLS packet
 *
 * @param[in]  buf  Buffer to check
 *
 * @return
 *       - true   It's DTLS packet
 *       - false  Not DTLS packet
 */
bool dtls_srtp_probe(uint8_t *buf);

/**
 * @brief  Encrypt RTP packet use SRTP
 *
 * @param[in]   dtls_srtp  DTLS SRTP instance
 * @param[in]   packet     RTP packet data
 * @param[in]   buf_size   RTP packet size
 * @param[out]  bytes      Packet size after encrypt
 *
 */
void dtls_srtp_encrypt_rtp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int buf_size, int *bytes);

/**
 * @brief  Decrypt RTP packet use SRTP
 *
 * @param[in]      dtls_srtp  DTLS SRTP instance
 * @param[in]      packet     RTP packet data
 * @param[in,out]  bytes      Packet size after encrypt
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to decrypt
 */
int dtls_srtp_decrypt_rtp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int *bytes);

/**
 * @brief  Encrypt RTCP packet use SRTP
 *
 * @param[in]   dtls_srtp  DTLS SRTP instance
 * @param[in]   packet     RTCP packet data
 * @param[in]   buf_size   RTCP packet size
 * @param[out]  bytes      Packet size after encrypt
 *
 */
void dtls_srtp_encrypt_rctp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int buf_size, int *bytes);

/**
 * @brief  Decrypt RTCP packet use SRTP
 *
 * @param[in]      dtls_srtp  DTLS SRTP instance
 * @param[in]      packet     RTCP packet data
 * @param[in,out]  bytes      Packet size after encrypt
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to decrypt
 */
int dtls_srtp_decrypt_rtcp_packet(dtls_srtp_t *dtls_srtp, uint8_t *packet, int *bytes);


/**
 * @brief  Deinitialize of DTLS SRTP
 *
 * @param[in]      dtls_srtp  DTLS SRTP instance
 */
void dtls_srtp_deinit(dtls_srtp_t *dtls_srtp);

#ifdef __cplusplus
}
#endif
