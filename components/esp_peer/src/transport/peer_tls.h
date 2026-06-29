/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque TLS session handle. */
typedef void *peer_tls_handle_t;

/** Client side TLS configuration. */
typedef struct {
    const char *cacert_buf;       /*!< CA certificate PEM buffer (NULL to skip verification) */
    int         cacert_bytes;     /*!< CA PEM length, <=0 means NUL terminated */
    bool        skip_cert_verify; /*!< Skip peer certificate verification when true */
    int         timeout_ms;       /*!< Handshake timeout hint in milliseconds (0 = backend default) */
} peer_tls_client_cfg_t;

/** Server side TLS configuration. */
typedef struct {
    const char *servercert_buf;   /*!< Server certificate PEM buffer */
    int         servercert_bytes; /*!< Server certificate PEM length, <=0 means NUL terminated */
    const char *serverkey_buf;    /*!< Server private key PEM buffer */
    int         serverkey_bytes;  /*!< Server private key PEM length, <=0 means NUL terminated */
} peer_tls_server_cfg_t;

/**
 * @brief  Wrap an already-connected TCP socket with a TLS client session.
 *
 *         The handshake is performed synchronously. On success the session owns `fd`.
 *         On failure the socket `fd` is closed by this call and NULL is returned.
 *
 * @param  fd            Connected TCP socket file descriptor
 * @param  hostname      SNI host name (may be NULL)
 * @param  hostname_len  SNI host name length (<=0 when hostname is NULL)
 * @param  cfg           Client TLS configuration (may be NULL for defaults)
 *
 * @return
 *       - TLS handle on success
 *       - NULL on failure
 */
peer_tls_handle_t peer_tls_new_client(int fd, const char *hostname, int hostname_len,
                                      const peer_tls_client_cfg_t *cfg);

/**
 * @brief  Wrap an accepted TCP socket with a TLS server session.
 *
 *          The handshake is performed synchronously. On success the session owns `fd`.
 *          On failure the socket `fd` is closed by this call and NULL is returned.
 *
 * @param  fd   Accepted TCP socket file descriptor
 * @param  cfg  Server TLS configuration (certificate + key)
 *
 * @return
 *       - TLS handle on success
 *       - NULL on failure
 */
peer_tls_handle_t peer_tls_new_server(int fd, const peer_tls_server_cfg_t *cfg);

/**
 * @brief  Write plain data over the TLS session.
 *
 * @return
 *       - > 0  Number of bytes written, 0 when the backend needs to be retried
 *              (non-blocking want-read/want-write),
 *       -  < 0 on fatal error
 */
int peer_tls_write(peer_tls_handle_t handle, const void *data, int len);

/**
 * @brief  Read plain data from the TLS session.
 *
 * @return
 *       -  >0  Number of bytes read, 0 when the backend needs to be retried
 *              (non-blocking want-read/want-write)
 *       - < 0  On fatal error / closed
 */
int peer_tls_read(peer_tls_handle_t handle, void *data, int len);

/**
 * @brief  Number of decrypted bytes already buffered inside the TLS session.
 *
 *         Used to avoid blocking on select() when the TLS record layer still has
 *         application data buffered.
 */
int peer_tls_pending(peer_tls_handle_t handle);

/**
 * @brief  Get the underlying TCP socket file descriptor of the session.
 */
int peer_tls_get_fd(peer_tls_handle_t handle);

/**
 * @brief  Tear down the TLS session and close the underlying socket.
 */
void peer_tls_free(peer_tls_handle_t handle);

#ifdef __cplusplus
}
#endif
