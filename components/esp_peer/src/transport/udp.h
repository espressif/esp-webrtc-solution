/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include "esp_peer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int             fd;
    int             ipv6_fd;
    uint32_t        sin6_scope_id;
    esp_peer_addr_t bind_addr;
    long long int   timeout_sec;
    long int        timeout_usec;
    atomic_int      user_count;
} udp_socket_t;

int udp_socket_open(udp_socket_t *udp_socket, bool ipv6_support);

int udp_socket_bind(udp_socket_t *udp_socket, esp_peer_addr_t *addr);

void udp_socket_close(udp_socket_t *udp_socket);

int udp_socket_sendto(udp_socket_t *udp_socket, esp_peer_addr_t *addr, const uint8_t *buf, int len);

int udp_socket_recvfrom_nowait(udp_socket_t *udp_socket, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait);

int udp_get_local_address(udp_socket_t *udp_socket, bool ipv6, esp_peer_addr_t *addr);

int udp_socket_get_host_address(udp_socket_t *udp_socket, esp_peer_addr_t *addr);

int udp_resolve_mdns_host(const char *host, esp_peer_addr_t *addr);

void udp_blocking_timeout(udp_socket_t *udp_socket, long long int ms);

#ifdef __cplusplus
}
#endif
