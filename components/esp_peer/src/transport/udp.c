/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netdb.h>
#ifndef __linux__
#include "esp_netif.h"
#endif
#include "esp_log.h"
#include "udp.h"
#include "media_lib_os.h"
#include "peer_utils.h"

#define TAG "UDP"

#define WEAK __attribute__((weak))

void measure_start(const char *tag);
void measure_stop(const char *tag);

static void udp_add_user(udp_socket_t *udp_socket)
{
    peer_atomic_inc(&udp_socket->user_count);
}

static void udp_dec_user(udp_socket_t *udp_socket)
{
    peer_atomic_dec(&udp_socket->user_count);
}

void WEAK udp_blocking_timeout(udp_socket_t *udp_socket, long long int ms)
{
    udp_socket->timeout_sec = ms / 1000;
    udp_socket->timeout_usec = ms % 1000 * 1000;
}

int WEAK udp_socket_open(udp_socket_t *udp_socket, bool ipv6_support)
{
    udp_socket->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket->fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket on open");
        return -1;
    }

    int flags = fcntl(udp_socket->fd, F_GETFL, 0);
    fcntl(udp_socket->fd, F_SETFL, flags & (~O_NONBLOCK));

    if (ipv6_support) {
        udp_socket->ipv6_fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (udp_socket->ipv6_fd < 0) {
            ESP_LOGW(TAG, "Failed to create ipv6 socket on open");
        } else {
            int flags6 = fcntl(udp_socket->ipv6_fd, F_GETFL, 0);
            fcntl(udp_socket->ipv6_fd, F_SETFL, flags6 & (~O_NONBLOCK));
        }
        // On ESP32, explicitly setting the scope ID (netif index) helps routing for both Link-Local and Global addresses
#ifndef __linux__
        esp_netif_t *netif = esp_netif_get_default_netif();
        if (netif) {
            udp_socket->sin6_scope_id = esp_netif_get_netif_impl_index(netif);
        }
#endif
    } else {
        udp_socket->ipv6_fd = -1;
    }

    udp_blocking_timeout(udp_socket, 5);
    return 0;
}

int WEAK udp_socket_bind(udp_socket_t *udp_socket, esp_peer_addr_t *addr)
{
    if (udp_socket->fd < 0) {
        ESP_LOGE(TAG, "Failed to bind");
        return -1;
    }

    if (udp_socket->fd >= 0) {
        struct sockaddr_in sin;
        socklen_t sin_len = sizeof(sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons(addr->port);
        sin.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(udp_socket->fd, (struct sockaddr *)&sin, sin_len) < 0) {
            ESP_LOGE(TAG, "Failed to bind ipv4 socket");
            return -1;
        }
    }

    if (udp_socket->ipv6_fd >= 0) {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(addr->port);
        sin6.sin6_addr = in6addr_any;

        if (bind(udp_socket->ipv6_fd, (struct sockaddr *)&sin6, sizeof(sin6)) < 0) {
            ESP_LOGE(TAG, "Failed to bind ipv6 socket");
            return -1;
        }
    }

    udp_socket->bind_addr.family = addr->family;
    udp_socket->bind_addr.port = addr->port;
    if (addr->family == AF_INET) {
        memcpy(udp_socket->bind_addr.ipv4, addr->ipv4, 4);
    } else if (addr->family == AF_INET6) {
        memcpy(udp_socket->bind_addr.ipv6, addr->ipv6, 16);
    }
    return 0;
}

void WEAK udp_socket_close(udp_socket_t *udp_socket)
{
    if (udp_socket->fd > 0 || udp_socket->ipv6_fd > 0) {
        int fd = udp_socket->fd;
        int ipv6_fd = udp_socket->ipv6_fd;
        udp_socket->fd = -1;
        udp_socket->ipv6_fd = -1;
        if (fd > 0) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        if (ipv6_fd > 0) {
            shutdown(ipv6_fd, SHUT_RDWR);
            close(ipv6_fd);
        }
        // Wait for all users quit
        while (peer_atomic_load(&udp_socket->user_count)) {
            media_lib_thread_sleep(10);
        }
    }
}

int WEAK udp_get_local_address(udp_socket_t *udp_socket, bool ipv6, esp_peer_addr_t *addr)
{
    struct sockaddr_storage addr_storage;
    socklen_t len = sizeof(addr_storage);
    int fd = -1;

    if (ipv6) {
        fd = udp_socket->ipv6_fd;
    } else {
        fd = udp_socket->fd;
    }

    if (fd < 0) {
        ESP_LOGE(TAG, "Invalid socket for %s", ipv6 ? "IPv6" : "IPv4");
        return -1;
    }

    if (getsockname(fd, (struct sockaddr *)&addr_storage, &len) < 0) {
        ESP_LOGE(TAG, "Failed to get local address");
        return -1;
    }

    if (addr_storage.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr_storage;
        memcpy(addr->ipv4, &sin->sin_addr.s_addr, 4);
        addr->port = ntohs(sin->sin_port);
        addr->family = AF_INET;
    } else if (addr_storage.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr_storage;
        memcpy(addr->ipv6, &sin6->sin6_addr, 16);
        addr->port = ntohs(sin6->sin6_port);
        addr->family = AF_INET6;
    }

    return 0;
}

int WEAK udp_socket_sendto(udp_socket_t *udp_socket, esp_peer_addr_t *addr, const uint8_t *buf, int len)
{
    int ret = -1;
    int fd = -1;
    struct sockaddr_storage addr_storage;
    socklen_t addr_len = 0;

    if (addr->family == AF_INET) {
        fd = udp_socket->fd;
        struct sockaddr_in *sin = (struct sockaddr_in *)&addr_storage;
        sin->sin_family = AF_INET;
        memcpy(&sin->sin_addr.s_addr, addr->ipv4, 4);
        sin->sin_port = htons(addr->port);
        addr_len = sizeof(struct sockaddr_in);
    } else if (addr->family == AF_INET6) {
        fd = udp_socket->ipv6_fd;
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr_storage;
        memset(sin6, 0, sizeof(struct sockaddr_in6));
        sin6->sin6_family = AF_INET6;
        memcpy(&sin6->sin6_addr, addr->ipv6, 16);
        sin6->sin6_port = htons(addr->port);
        sin6->sin6_scope_id = udp_socket->sin6_scope_id;
        addr_len = sizeof(struct sockaddr_in6);
    }

    if (fd < 0) {
        return -1;
    }
    udp_add_user(udp_socket);

    uint32_t retry_count = 0;
RETRY:
    measure_start("sendto");
    ret = sendto(fd, buf, len, 0, (struct sockaddr *)&addr_storage, addr_len);
    measure_stop("sendto");
    if (ret < 0) {
        if ((errno == ENOBUFS || errno == 12) && retry_count < 2) {
            media_lib_thread_sleep(20);
            fd_set write_set;
            struct timeval tv = { .tv_usec = 5000 };
            FD_ZERO(&write_set);
            FD_SET(fd, &write_set);
            retry_count++;
            if ((ret = select(fd + 1, NULL, &write_set, NULL, &tv)) < 0) {
                ESP_LOGE(TAG, "Failed to select: %s", strerror(errno));
                udp_dec_user(udp_socket);
                return -1;
            }
            goto RETRY;
        }
        // Special return value so that can retry
        if (retry_count > 0) {
            udp_dec_user(udp_socket);
            return -200;
        }
        ESP_LOGE(TAG, "Failed to sendto: %d %s", errno, strerror(errno));
        ret = -1;
    }
    udp_dec_user(udp_socket);
    return ret;
}

static int udp_socket_recv_dispatch(udp_socket_t *udp_socket, esp_peer_addr_t *addr, uint8_t *buf, int len, struct timeval *tv)
{
    fd_set read_set;
    int max_fd = -1;
    FD_ZERO(&read_set);
    if (udp_socket->fd >= 0) {
        FD_SET(udp_socket->fd, &read_set);
        max_fd = udp_socket->fd;
    }
    if (udp_socket->ipv6_fd >= 0) {
        FD_SET(udp_socket->ipv6_fd, &read_set);
        if (udp_socket->ipv6_fd > max_fd) {
            max_fd = udp_socket->ipv6_fd;
        }
    }
    if (max_fd < 0) {
        return -1;
    }

    int ret = select(max_fd + 1, &read_set, NULL, NULL, tv);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to select: %s", strerror(errno));
        return -1;
    } else if (ret == 0) {
        return 0;
    }

    struct sockaddr_storage addr_storage;
    socklen_t addr_len = sizeof(addr_storage);
    int target_fd = -1;

    if (udp_socket->fd >= 0 && FD_ISSET(udp_socket->fd, &read_set)) {
        target_fd = udp_socket->fd;
    } else if (udp_socket->ipv6_fd >= 0 && FD_ISSET(udp_socket->ipv6_fd, &read_set)) {
        target_fd = udp_socket->ipv6_fd;
    }

    if (target_fd >= 0) {
        ret = recvfrom(target_fd, buf, len, 0, (struct sockaddr *)&addr_storage, &addr_len);
        if (ret < 0) {
            if (errno == EWOULDBLOCK) {
                return 0;
            } else {
                ESP_LOGE(TAG, "Failed to recvfrom: %s", strerror(errno));
                return -1;
            }
        } else if (ret == 0) {
            ESP_LOGW(TAG, "socket connection should be closed");
            return -1;
        }
        if (addr_storage.ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&addr_storage;
            addr->family = AF_INET;
            addr->port = ntohs(sin->sin_port);
            memcpy(addr->ipv4, &sin->sin_addr.s_addr, 4);
        } else if (addr_storage.ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr_storage;
            addr->family = AF_INET6;
            addr->port = ntohs(sin6->sin6_port);
            memcpy(addr->ipv6, &sin6->sin6_addr, 16);
        }
        return ret;
    }
    return 0;
}

int WEAK udp_socket_recvfrom_nowait(udp_socket_t *udp_socket, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait)
{
    struct timeval tv;
    int ret;
    if (udp_socket->fd < 0 && udp_socket->ipv6_fd < 0) {
        return -1;
    }

    if (nowait) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    } else {
        tv.tv_sec = udp_socket->timeout_sec;
        tv.tv_usec = udp_socket->timeout_usec;
    }

    udp_add_user(udp_socket);
    ret = udp_socket_recv_dispatch(udp_socket, addr, buf, len, &tv);
    udp_dec_user(udp_socket);
    return ret;
}
