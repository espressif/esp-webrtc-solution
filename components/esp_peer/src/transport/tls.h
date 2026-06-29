/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef tcp_socket_t tls_socket_t;

#define tls_blocking_timeout    tcp_blocking_timeout
#define tls_socket_open         tcp_socket_open
#define tls_socket_open_family  tcp_socket_open_family
#define tls_socket_bind         tcp_socket_bind
#define tls_socket_listen       tcp_socket_listen
#define tls_socket_accept       tcp_socket_accept
#define tls_socket_connect      tcp_socket_connect
#define tls_socket_close        tcp_socket_close
#define tls_socket_send         tcp_socket_send
#define tls_socket_recv         tcp_socket_recv
#define tls_get_local_address   tcp_get_local_address

typedef struct {
    bool     ipv6_support;
    uint8_t  max_connections;
    uint32_t connect_timeout_ms;
    uint32_t send_timeout_ms;
    uint32_t recv_timeout_ms;
    /** Client: SNI host name (e.g. "localhost"); may be NULL to default to "localhost". */
    const char *tls_hostname;
    int         tls_hostname_len;
    const char *ca_pem;
    int         ca_pem_len;
    bool        skip_cert_verify;
    /** Server (listen + accept): PEM certificate and private key buffers. */
    const char *server_cert_pem;
    int         server_cert_pem_len;
    const char *server_key_pem;
    int         server_key_pem_len;
} tls_connections_cfg_t;

typedef struct tls_connections_t *tls_connections_handle_t;

tls_connections_handle_t tls_connections_open(tls_connections_cfg_t *cfg);

void tls_connections_set_recv_timeout(tls_connections_handle_t tls, uint32_t timeout_ms);

int tls_connections_bind(tls_connections_handle_t tls, esp_peer_addr_t *addr);

int tls_connections_start_server(tls_connections_handle_t tls);

int tls_connections_connect(tls_connections_handle_t tls, esp_peer_addr_t *addr);

int tls_connections_is_connected(tls_connections_handle_t tls, esp_peer_addr_t *addr);

int tls_connections_send_to(tls_connections_handle_t tls, esp_peer_addr_t *addr, const uint8_t *buf, int len,
                            uint8_t prio);

int tls_connections_recv_from(tls_connections_handle_t tls, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait);

int tls_connections_send_raw_to(tls_connections_handle_t tls, esp_peer_addr_t *addr, const uint8_t *buf, int len);

int tls_connections_recv_raw_from(tls_connections_handle_t tls, esp_peer_addr_t *addr, uint8_t *buf, int len,
                                  bool nowait);

void tls_connections_close(tls_connections_handle_t tls);

#ifdef __cplusplus
}
#endif
