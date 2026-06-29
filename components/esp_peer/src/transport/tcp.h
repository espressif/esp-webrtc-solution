/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_peer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int             fd;
    uint8_t         family;
    esp_peer_addr_t bind_addr;
    long long int   timeout_sec;
    long int        timeout_usec;
} tcp_socket_t;

typedef struct {
    bool     ipv6_support;
    uint8_t  max_connections;
    uint32_t connect_timeout_ms;
    uint32_t send_timeout_ms;
    uint32_t recv_timeout_ms;
} tcp_connections_cfg_t;

typedef struct tcp_connections_t *tcp_connections_handle_t;

int tcp_socket_open(tcp_socket_t *tcp_socket);

int tcp_socket_open_family(tcp_socket_t *tcp_socket, uint8_t family);

int tcp_socket_bind(tcp_socket_t *tcp_socket, esp_peer_addr_t *addr);

int tcp_socket_listen(tcp_socket_t *tcp_socket, int backlog);

int tcp_socket_accept(tcp_socket_t *tcp_socket, tcp_socket_t *client, esp_peer_addr_t *addr);

int tcp_socket_connect(tcp_socket_t *tcp_socket, esp_peer_addr_t *addr);

void tcp_socket_close(tcp_socket_t *tcp_socket);

int tcp_socket_send(tcp_socket_t *tcp_socket, const uint8_t *buf, int len);

int tcp_socket_recv(tcp_socket_t *tcp_socket, uint8_t *buf, int len);

void tcp_blocking_timeout(tcp_socket_t *tcp_socket, long long int ms);

int tcp_get_local_address(tcp_socket_t *tcp_socket, esp_peer_addr_t *addr);

tcp_connections_handle_t tcp_connections_open(tcp_connections_cfg_t *cfg);

void tcp_connections_set_recv_timeout(tcp_connections_handle_t tcp, uint32_t timeout_ms);

int tcp_connections_bind(tcp_connections_handle_t tcp, esp_peer_addr_t *addr);

int tcp_connections_start_server(tcp_connections_handle_t tcp);

int tcp_connections_connect(tcp_connections_handle_t tcp, esp_peer_addr_t *addr);

bool tcp_connections_is_connected(tcp_connections_handle_t tcp, esp_peer_addr_t *addr);

int tcp_connections_send_to(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, const uint8_t *buf, int len, uint8_t prio);

int tcp_connections_recv_from(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait);

int tcp_connections_send_raw_to(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, const uint8_t *buf, int len);

int tcp_connections_recv_raw_from(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait);

void tcp_connections_close(tcp_connections_handle_t tcp);

#ifdef __cplusplus
}
#endif
