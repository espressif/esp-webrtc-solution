/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "media_lib_os.h"
#include "peer_utils.h"
#include "tcp.h"

#define TAG "TCP"
#define WEAK __attribute__((weak))

#define TCP_DEFAULT_TIMEOUT_MS         (5)
#define TCP_DEFAULT_CONNECT_TIMEOUT_MS (30000)
#define TCP_DEFAULT_SEND_TIMEOUT_MS    (1000)
#define TCP_DEFAULT_RECV_TIMEOUT_MS    (150)
#define TCP_WORKER_SLEEP_MS            (10)

typedef struct {
    int             fd;
    esp_peer_addr_t addr;
    bool            connected;
    bool            connecting;
    uint32_t        connect_start_ms;
    pthread_mutex_t io_lock;
} tcp_connection_t;

typedef struct tcp_send_item_t {
    struct tcp_send_item_t *next;
    esp_peer_addr_t         addr;
    uint8_t                 prio;
    int                     len;
    uint8_t                 data[];
} tcp_send_item_t;

struct tcp_connections_t {
    tcp_connections_cfg_t cfg;
    tcp_socket_t          listen_sock;
    tcp_connection_t     *connections;
    uint8_t               connection_num;
    tcp_send_item_t      *send_head;
    pthread_mutex_t       lock;
    pthread_t             worker;
    bool                  worker_started;
    bool                  server_started;
    bool                  closing;
    atomic_int            users;
};

static uint32_t tcp_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static inline uint16_t tcp_rd_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int tcp_set_nonblock(int fd, bool nonblock)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (nonblock) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags);
}

static bool tcp_same_addr(esp_peer_addr_t *a, esp_peer_addr_t *b)
{
    if (a->family != b->family || a->port != b->port) {
        return false;
    }
    if (a->family == AF_INET) {
        return memcmp(a->ipv4, b->ipv4, 4) == 0;
    }
    if (a->family == AF_INET6) {
        return memcmp(a->ipv6, b->ipv6, 16) == 0;
    }
    return false;
}

static int tcp_addr_to_sockaddr(esp_peer_addr_t *addr, struct sockaddr_storage *storage, socklen_t *addr_len)
{
    memset(storage, 0, sizeof(*storage));
    if (addr->family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)storage;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->port);
        memcpy(&sin6->sin6_addr, addr->ipv6, 16);
        *addr_len = sizeof(struct sockaddr_in6);
        return 0;
    }
    if (addr->family == AF_INET || addr->family == 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)storage;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(addr->port);
        memcpy(&sin->sin_addr.s_addr, addr->ipv4, 4);
        *addr_len = sizeof(struct sockaddr_in);
        return 0;
    }
    return -1;
}

static void tcp_sockaddr_to_addr(struct sockaddr_storage *storage, esp_peer_addr_t *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (storage->ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)storage;
        addr->family = AF_INET6;
        addr->port = ntohs(sin6->sin6_port);
        memcpy(addr->ipv6, &sin6->sin6_addr, 16);
    } else {
        struct sockaddr_in *sin = (struct sockaddr_in *)storage;
        addr->family = AF_INET;
        addr->port = ntohs(sin->sin_port);
        memcpy(addr->ipv4, &sin->sin_addr.s_addr, 4);
    }
}

void WEAK tcp_blocking_timeout(tcp_socket_t *tcp_socket, long long int ms)
{
    tcp_socket->timeout_sec = ms / 1000;
    tcp_socket->timeout_usec = ms % 1000 * 1000;
}

int WEAK tcp_socket_open_family(tcp_socket_t *tcp_socket, uint8_t family)
{
    memset(tcp_socket, 0, sizeof(*tcp_socket));
    tcp_socket->fd = socket(family == AF_INET6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (tcp_socket->fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    tcp_socket->family = family == AF_INET6 ? AF_INET6 : AF_INET;
    int reuse = 1;
    setsockopt(tcp_socket->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    tcp_blocking_timeout(tcp_socket, TCP_DEFAULT_TIMEOUT_MS);
    return 0;
}

int WEAK tcp_socket_open(tcp_socket_t *tcp_socket)
{
    return tcp_socket_open_family(tcp_socket, AF_INET);
}

int WEAK tcp_socket_bind(tcp_socket_t *tcp_socket, esp_peer_addr_t *addr)
{
    struct sockaddr_storage storage;
    socklen_t addr_len = 0;
    esp_peer_addr_t bind_addr = *addr;
    bind_addr.family = tcp_socket->family;

    if (tcp_addr_to_sockaddr(&bind_addr, &storage, &addr_len) != 0) {
        return -1;
    }
    if (bind(tcp_socket->fd, (struct sockaddr *)&storage, addr_len) < 0) {
        ESP_LOGE(TAG, "Failed to bind TCP socket: %s", strerror(errno));
        return -1;
    }
    return tcp_get_local_address(tcp_socket, &tcp_socket->bind_addr);
}

int WEAK tcp_socket_listen(tcp_socket_t *tcp_socket, int backlog)
{
    if (listen(tcp_socket->fd, backlog) < 0) {
        ESP_LOGE(TAG, "Failed to listen: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int WEAK tcp_socket_accept(tcp_socket_t *tcp_socket, tcp_socket_t *client, esp_peer_addr_t *addr)
{
    struct sockaddr_storage storage;
    socklen_t addr_len = sizeof(storage);
    int fd = accept(tcp_socket->fd, (struct sockaddr *)&storage, &addr_len);
    if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "Failed to accept: %s", strerror(errno));
        }
        return -1;
    }
    memset(client, 0, sizeof(*client));
    client->fd = fd;
    client->family = storage.ss_family;
    tcp_blocking_timeout(client, TCP_DEFAULT_TIMEOUT_MS);
    tcp_sockaddr_to_addr(&storage, addr);
    return 0;
}

int WEAK tcp_socket_connect(tcp_socket_t *tcp_socket, esp_peer_addr_t *addr)
{
    struct sockaddr_storage storage;
    socklen_t addr_len = 0;
    if (tcp_addr_to_sockaddr(addr, &storage, &addr_len) != 0) {
        return -1;
    }

    if (tcp_set_nonblock(tcp_socket->fd, true) != 0) {
        return -1;
    }
    int ret = connect(tcp_socket->fd, (struct sockaddr *)&storage, addr_len);
    if (ret < 0 && errno != EINPROGRESS) {
        ESP_LOGE(TAG, "Failed to connect: %s", strerror(errno));
        return -1;
    }
    if (ret == 0) {
        tcp_set_nonblock(tcp_socket->fd, false);
        return 0;
    }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(tcp_socket->fd, &write_set);
    struct timeval tv = {
        .tv_sec = tcp_socket->timeout_sec,
        .tv_usec = tcp_socket->timeout_usec,
    };
    ret = select(tcp_socket->fd + 1, NULL, &write_set, NULL, &tv);
    if (ret <= 0) {
        ESP_LOGE(TAG, "TCP connect timeout");
        return -1;
    }
    int err = 0;
    socklen_t err_len = sizeof(err);
    if (getsockopt(tcp_socket->fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0) {
        ESP_LOGE(TAG, "TCP connect failed: %s", strerror(err ? err : errno));
        return -1;
    }
    tcp_set_nonblock(tcp_socket->fd, false);
    return 0;
}

void WEAK tcp_socket_close(tcp_socket_t *tcp_socket)
{
    if (tcp_socket->fd >= 0) {
        int fd = tcp_socket->fd;
        tcp_socket->fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

int WEAK tcp_socket_send(tcp_socket_t *tcp_socket, const uint8_t *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(tcp_socket->fd, &write_set);
        struct timeval tv = {
            .tv_sec = tcp_socket->timeout_sec,
            .tv_usec = tcp_socket->timeout_usec,
        };
        int ret = select(tcp_socket->fd + 1, NULL, &write_set, NULL, &tv);
        if (ret <= 0) {
            return sent > 0 ? sent : ret;
        }
        ret = send(tcp_socket->fd, buf + sent, len - sent, 0);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Failed to send: %s", strerror(errno));
            return sent > 0 ? sent : -1;
        }
        sent += ret;
    }
    return sent;
}

int WEAK tcp_socket_recv(tcp_socket_t *tcp_socket, uint8_t *buf, int len)
{
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(tcp_socket->fd, &read_set);
    struct timeval tv = {
        .tv_sec = tcp_socket->timeout_sec,
        .tv_usec = tcp_socket->timeout_usec,
    };
    int ret = select(tcp_socket->fd + 1, &read_set, NULL, NULL, &tv);
    if (ret <= 0) {
        return ret;
    }
    ret = recv(tcp_socket->fd, buf, len, 0);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to recv: %s", strerror(errno));
        return -1;
    }
    return ret;
}

int WEAK tcp_get_local_address(tcp_socket_t *tcp_socket, esp_peer_addr_t *addr)
{
    struct sockaddr_storage storage;
    socklen_t len = sizeof(storage);
    if (getsockname(tcp_socket->fd, (struct sockaddr *)&storage, &len) < 0) {
        ESP_LOGE(TAG, "Failed to get local address: %s", strerror(errno));
        return -1;
    }
    tcp_sockaddr_to_addr(&storage, addr);
    return 0;
}

static tcp_connection_t *tcp_connections_find(tcp_connections_handle_t tcp, esp_peer_addr_t *addr)
{
    for (int i = 0; i < tcp->connection_num; i++) {
        if (tcp->connections[i].fd >= 0 && tcp_same_addr(&tcp->connections[i].addr, addr)) {
            return &tcp->connections[i];
        }
    }
    return NULL;
}

static tcp_connection_t *tcp_connections_alloc(tcp_connections_handle_t tcp)
{
    for (int i = 0; i < tcp->connection_num; i++) {
        if (tcp->connections[i].fd < 0) {
            return &tcp->connections[i];
        }
    }
    return NULL;
}

/* Wake a connection blocked in select()/recv()/send() without recycling the slot. Used
 * by close() so any in-flight recv/send returns promptly; the fd is actually closed
 * later under io_lock once all users have drained. */
static void tcp_connection_wake(tcp_connection_t *conn)
{
    if (conn->fd >= 0) {
        shutdown(conn->fd, SHUT_RDWR);
    }
}

/* Close the socket / free the slot. Takes io_lock so it waits for any in-flight I/O on
 * this connection to finish before closing the fd (no recycled-fd read). Callers hold
 * the manager lock; lock order is always manager -> io_lock. Idempotent. */
static void tcp_connection_close(tcp_connection_t *conn)
{
    pthread_mutex_lock(&conn->io_lock);
    if (conn->fd >= 0) {
        int fd = conn->fd;
        conn->fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    conn->connected = false;
    conn->connecting = false;
    pthread_mutex_unlock(&conn->io_lock);
}

static int tcp_connection_send(tcp_connection_t *conn, uint8_t *data, int len, uint32_t timeout_ms)
{
    tcp_socket_t sock = {
        .fd = conn->fd,
        .family = conn->addr.family,
    };
    tcp_blocking_timeout(&sock, timeout_ms);
    return tcp_socket_send(&sock, data, len);
}

static int tcp_connection_recv_exact(tcp_connection_t *conn, uint8_t *data, int len, uint32_t timeout_ms)
{
    int received = 0;
    while (received < len) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(conn->fd, &read_set);
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        int ret = select(conn->fd + 1, &read_set, NULL, NULL, &tv);
        if (ret <= 0) {
            return received > 0 ? -1 : ret;
        }
        ret = recv(conn->fd, data + received, len - received, 0);
        if (ret <= 0) {
            return -1;
        }
        received += ret;
    }
    return received;
}

static void tcp_connections_accept_all(tcp_connections_handle_t tcp)
{
    if (tcp->server_started == false) {
        return;
    }
    while (tcp->listen_sock.fd >= 0) {
        tcp_socket_t client;
        esp_peer_addr_t addr;
        if (tcp_socket_accept(&tcp->listen_sock, &client, &addr) != 0) {
            break;
        }
        tcp_set_nonblock(client.fd, true);
        pthread_mutex_lock(&tcp->lock);
        tcp_connection_t *old = tcp_connections_find(tcp, &addr);
        if (old) {
            tcp_connection_close(old);
        }
        tcp_connection_t *conn = old ? old : tcp_connections_alloc(tcp);
        if (conn == NULL) {
            ESP_LOGW(TAG, "Drop accepted TCP connection: over limit");
            tcp_socket_close(&client);
        } else {
            conn->fd = client.fd;
            conn->addr = addr;
            conn->connected = true;
            conn->connecting = false;
        }
        pthread_mutex_unlock(&tcp->lock);
    }
}

static void tcp_connections_check_connecting(tcp_connections_handle_t tcp)
{
    uint32_t now = tcp_get_time_ms();
    pthread_mutex_lock(&tcp->lock);
    for (int i = 0; i < tcp->connection_num; i++) {
        tcp_connection_t *conn = &tcp->connections[i];
        if (conn->fd < 0 || conn->connecting == false) {
            continue;
        }
        if (now - conn->connect_start_ms > tcp->cfg.connect_timeout_ms) {
            tcp_connection_close(conn);
            continue;
        }
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(conn->fd, &write_set);
        struct timeval tv = { 0 };
        int ret = select(conn->fd + 1, NULL, &write_set, NULL, &tv);
        if (ret <= 0) {
            continue;
        }
        int err = 0;
        socklen_t err_len = sizeof(err);
        if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0) {
            ESP_LOGW(TAG, "Async TCP connect failed: %s", strerror(err ? err : errno));
            tcp_connection_close(conn);
        } else {
            conn->connecting = false;
            conn->connected = true;
        }
    }
    pthread_mutex_unlock(&tcp->lock);
}

static tcp_send_item_t *tcp_connections_pop_send(tcp_connections_handle_t tcp, tcp_connection_t **conn)
{
    tcp_send_item_t *prev = NULL;
    tcp_send_item_t *cur = tcp->send_head;
    while (cur) {
        *conn = tcp_connections_find(tcp, &cur->addr);
        if (*conn && (*conn)->connected) {
            if (prev) {
                prev->next = cur->next;
            } else {
                tcp->send_head = cur->next;
            }
            cur->next = NULL;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    *conn = NULL;
    return NULL;
}

static void tcp_connections_process_send(tcp_connections_handle_t tcp)
{
    while (true) {
        pthread_mutex_lock(&tcp->lock);
        tcp_connection_t *conn = NULL;
        tcp_send_item_t *item = tcp_connections_pop_send(tcp, &conn);
        if (item == NULL) {
            pthread_mutex_unlock(&tcp->lock);
            break;
        }
        /* Grab io_lock while still holding the manager lock so the connection cannot
         * be closed between popping and sending, then drop the manager lock so other
         * API calls (e.g. recv) are not blocked while this send is in progress. */
        pthread_mutex_lock(&conn->io_lock);
        pthread_mutex_unlock(&tcp->lock);
        int sret = tcp_connection_send(conn, item->data, item->len, tcp->cfg.send_timeout_ms);
        pthread_mutex_unlock(&conn->io_lock);
        if (sret != item->len) {
            pthread_mutex_lock(&tcp->lock);
            tcp_connection_close(conn);
            pthread_mutex_unlock(&tcp->lock);
        }
        free(item);
    }
}

static void *tcp_connections_worker(void *arg)
{
    tcp_connections_handle_t tcp = (tcp_connections_handle_t)arg;
    while (tcp->closing == false) {
        tcp_connections_accept_all(tcp);
        tcp_connections_check_connecting(tcp);
        tcp_connections_process_send(tcp);
        media_lib_thread_sleep(TCP_WORKER_SLEEP_MS);
    }
    return NULL;
}

tcp_connections_handle_t WEAK tcp_connections_open(tcp_connections_cfg_t *cfg)
{
    tcp_connections_handle_t tcp = calloc(1, sizeof(struct tcp_connections_t));
    if (tcp == NULL) {
        return NULL;
    }
    tcp->cfg = *cfg;
    if (tcp->cfg.max_connections == 0) {
        tcp->cfg.max_connections = 4;
    }
    if (tcp->cfg.connect_timeout_ms == 0) {
        tcp->cfg.connect_timeout_ms = TCP_DEFAULT_CONNECT_TIMEOUT_MS;
    }
    if (tcp->cfg.send_timeout_ms == 0) {
        tcp->cfg.send_timeout_ms = TCP_DEFAULT_SEND_TIMEOUT_MS;
    }
    if (tcp->cfg.recv_timeout_ms == 0) {
        tcp->cfg.recv_timeout_ms = TCP_DEFAULT_RECV_TIMEOUT_MS;
    }
    tcp->listen_sock.fd = -1;
    tcp->connections = calloc(tcp->cfg.max_connections, sizeof(tcp_connection_t));
    if (tcp->connections == NULL) {
        free(tcp);
        return NULL;
    }
    tcp->connection_num = tcp->cfg.max_connections;
    for (int i = 0; i < tcp->connection_num; i++) {
        tcp->connections[i].fd = -1;
        if (pthread_mutex_init(&tcp->connections[i].io_lock, NULL) != 0) {
            for (int j = 0; j < i; j++) {
                pthread_mutex_destroy(&tcp->connections[j].io_lock);
            }
            free(tcp->connections);
            free(tcp);
            return NULL;
        }
    }
    if (pthread_mutex_init(&tcp->lock, NULL) != 0) {
        for (int i = 0; i < tcp->connection_num; i++) {
            pthread_mutex_destroy(&tcp->connections[i].io_lock);
        }
        free(tcp->connections);
        free(tcp);
        return NULL;
    }
    if (pthread_create(&tcp->worker, NULL, tcp_connections_worker, tcp) == 0) {
        tcp->worker_started = true;
    } else {
        pthread_mutex_destroy(&tcp->lock);
        for (int i = 0; i < tcp->connection_num; i++) {
            pthread_mutex_destroy(&tcp->connections[i].io_lock);
        }
        free(tcp->connections);
        free(tcp);
        return NULL;
    }
    return tcp;
}

void WEAK tcp_connections_set_recv_timeout(tcp_connections_handle_t tcp, uint32_t timeout_ms)
{
    if (tcp) {
        tcp->cfg.recv_timeout_ms = timeout_ms;
    }
}

int WEAK tcp_connections_bind(tcp_connections_handle_t tcp, esp_peer_addr_t *addr)
{
    if (tcp == NULL || addr == NULL) {
        return -1;
    }
    uint8_t family = addr->family == AF_INET6 ? AF_INET6 : AF_INET;
    if (tcp_socket_open_family(&tcp->listen_sock, family) != 0) {
        return -1;
    }
    if (tcp_socket_bind(&tcp->listen_sock, addr) != 0) {
        tcp_socket_close(&tcp->listen_sock);
        return -1;
    }
    *addr = tcp->listen_sock.bind_addr;
    return 0;
}

int WEAK tcp_connections_start_server(tcp_connections_handle_t tcp)
{
    if (tcp == NULL || tcp->listen_sock.fd < 0) {
        return -1;
    }
    if (tcp_socket_listen(&tcp->listen_sock, tcp->cfg.max_connections) != 0) {
        return -1;
    }
    int ret = tcp_set_nonblock(tcp->listen_sock.fd, true);
    if (ret == 0) {
        tcp->server_started = true;
    }
    return ret;
}

int WEAK tcp_connections_connect(tcp_connections_handle_t tcp, esp_peer_addr_t *addr)
{
    if (tcp == NULL || addr == NULL) {
        return -1;
    }
    if (tcp->closing) {
        return -1;
    }
    pthread_mutex_lock(&tcp->lock);
    tcp_connection_t *conn = tcp_connections_find(tcp, addr);
    if (conn && (conn->connected || conn->connecting)) {
        pthread_mutex_unlock(&tcp->lock);
        return 0;
    }
    conn = conn ? conn : tcp_connections_alloc(tcp);
    if (conn == NULL) {
        pthread_mutex_unlock(&tcp->lock);
        return -1;
    }
    int fd = socket(addr->family == AF_INET6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        pthread_mutex_unlock(&tcp->lock);
        return -1;
    }
    tcp_set_nonblock(fd, true);
    struct sockaddr_storage storage;
    socklen_t addr_len = 0;
    if (tcp_addr_to_sockaddr(addr, &storage, &addr_len) != 0) {
        close(fd);
        pthread_mutex_unlock(&tcp->lock);
        return -1;
    }
    int ret = connect(fd, (struct sockaddr *)&storage, addr_len);
    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        pthread_mutex_unlock(&tcp->lock);
        return -1;
    }
    conn->fd = fd;
    conn->addr = *addr;
    conn->connected = ret == 0;
    conn->connecting = ret != 0;
    conn->connect_start_ms = tcp_get_time_ms();
    pthread_mutex_unlock(&tcp->lock);
    return 0;
}

bool WEAK tcp_connections_is_connected(tcp_connections_handle_t tcp, esp_peer_addr_t *addr)
{
    if (tcp == NULL || addr == NULL || tcp->closing) {
        return false;
    }
    pthread_mutex_lock(&tcp->lock);
    tcp_connection_t *conn = tcp_connections_find(tcp, addr);
    bool connected = conn && conn->connected;
    pthread_mutex_unlock(&tcp->lock);
    return connected;
}

int WEAK tcp_connections_send_to(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, const uint8_t *buf, int len, uint8_t prio)
{
    if (tcp == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    if (len > UINT16_MAX) {
        return -1;
    }
    if (tcp->closing) {
        return -1;
    }
    tcp_send_item_t *item = calloc(1, sizeof(tcp_send_item_t) + len + 2);
    if (item == NULL) {
        return -1;
    }
    item->addr = *addr;
    item->prio = prio;
    item->len = len + 2;
    uint16_t net_len = htons((uint16_t)len);
    memcpy(item->data, &net_len, sizeof(net_len));
    memcpy(item->data + 2, buf, len);

    pthread_mutex_lock(&tcp->lock);
    tcp_send_item_t **cur = &tcp->send_head;
    while (*cur && (*cur)->prio >= prio) {
        cur = &(*cur)->next;
    }
    item->next = *cur;
    *cur = item;
    pthread_mutex_unlock(&tcp->lock);
    return len;
}

int WEAK tcp_connections_send_raw_to(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, const uint8_t *buf, int len)
{
    if (tcp == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    int rc = -1;
    tcp_connection_t *conn = NULL;
    peer_atomic_inc(&tcp->users);
    if (tcp->closing) {
        goto done;
    }
    pthread_mutex_lock(&tcp->lock);
    conn = tcp_connections_find(tcp, addr);
    if (conn == NULL || conn->connected == false) {
        pthread_mutex_unlock(&tcp->lock);
        conn = NULL;
        goto done;
    }
    /* Hold io_lock (taken under the manager lock so the connection can't be closed in
     * between), then drop the manager lock so a concurrent recv is not blocked while
     * this send is in flight. */
    pthread_mutex_lock(&conn->io_lock);
    pthread_mutex_unlock(&tcp->lock);
    int ret = tcp_connection_send(conn, (uint8_t *)buf, len, tcp->cfg.send_timeout_ms);
    pthread_mutex_unlock(&conn->io_lock);
    if (ret != len) {
        pthread_mutex_lock(&tcp->lock);
        tcp_connection_close(conn);
        pthread_mutex_unlock(&tcp->lock);
    }
    rc = ret;
done:
    peer_atomic_dec(&tcp->users);
    return rc;
}

/* Build the read fd_set over all connected connections (short manager-lock section).
 * Returns the max fd or -1 if none/closing. */
static int tcp_build_read_set(tcp_connections_handle_t tcp, fd_set *read_set)
{
    int max_fd = -1;
    FD_ZERO(read_set);
    pthread_mutex_lock(&tcp->lock);
    for (int i = 0; i < tcp->connection_num; i++) {
        tcp_connection_t *conn = &tcp->connections[i];
        if (conn->fd >= 0 && conn->connected) {
            FD_SET(conn->fd, read_set);
            if (conn->fd > max_fd) {
                max_fd = conn->fd;
            }
        }
    }
    pthread_mutex_unlock(&tcp->lock);
    return max_fd;
}

/* After select() reports readiness, pick the first ready connection and return it with
 * its io_lock held (taken under the manager lock so it cannot be closed in between). The
 * caller does the blocking record read without holding the manager lock. NULL if none. */
static tcp_connection_t *tcp_acquire_ready(tcp_connections_handle_t tcp, fd_set *read_set)
{
    tcp_connection_t *ready = NULL;
    pthread_mutex_lock(&tcp->lock);
    for (int i = 0; i < tcp->connection_num; i++) {
        tcp_connection_t *conn = &tcp->connections[i];
        if (conn->fd >= 0 && conn->connected && FD_ISSET(conn->fd, read_set)) {
            ready = conn;
            pthread_mutex_lock(&ready->io_lock);
            break;
        }
    }
    pthread_mutex_unlock(&tcp->lock);
    return ready;
}

/* Release a connection acquired by tcp_acquire_ready(); close it (under the manager
 * lock) when the read failed. */
static void tcp_release_ready(tcp_connections_handle_t tcp, tcp_connection_t *conn, bool failed)
{
    pthread_mutex_unlock(&conn->io_lock);
    if (failed) {
        pthread_mutex_lock(&tcp->lock);
        tcp_connection_close(conn);
        pthread_mutex_unlock(&tcp->lock);
    }
}

int WEAK tcp_connections_recv_from(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait)
{
    if (tcp == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    int rc = -1;
    fd_set read_set;
    tcp_connection_t *conn = NULL;
    peer_atomic_inc(&tcp->users);
    if (tcp->closing) {
        goto done;
    }
    int max_fd = tcp_build_read_set(tcp, &read_set);
    if (max_fd < 0) {
        goto done;
    }
    struct timeval tv = { 0 };
    if (nowait == false) {
        tv.tv_sec = tcp->cfg.recv_timeout_ms / 1000;
        tv.tv_usec = (tcp->cfg.recv_timeout_ms % 1000) * 1000;
    }
    int ret = select(max_fd + 1, &read_set, NULL, NULL, &tv);
    if (ret <= 0) {
        rc = ret;
        goto done;
    }
    conn = tcp_acquire_ready(tcp, &read_set);
    if (conn == NULL) {
        rc = 0;
        goto done;
    }
    uint16_t net_len = 0;
    ret = tcp_connection_recv_exact(conn, (uint8_t *)&net_len, sizeof(net_len), tcp->cfg.recv_timeout_ms);
    if (ret != sizeof(net_len)) {
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    uint16_t payload_len = ntohs(net_len);
    if (payload_len > len) {
        ESP_LOGE(TAG, "TCP packet size %u over receive buffer %d", payload_len, len);
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    ret = tcp_connection_recv_exact(conn, buf, payload_len, tcp->cfg.recv_timeout_ms);
    if (ret != payload_len) {
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    *addr = conn->addr;
    rc = ret;
    tcp_release_ready(tcp, conn, false);
done:
    peer_atomic_dec(&tcp->users);
    return rc;
}

int WEAK tcp_connections_recv_raw_from(tcp_connections_handle_t tcp, esp_peer_addr_t *addr, uint8_t *buf, int len, bool nowait)
{
    if (tcp == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    int rc = -1;
    fd_set read_set;
    tcp_connection_t *conn = NULL;
    peer_atomic_inc(&tcp->users);
    if (tcp->closing) {
        goto done;
    }
    int max_fd = tcp_build_read_set(tcp, &read_set);
    if (max_fd < 0) {
        goto done;
    }
    struct timeval tv = { 0 };
    if (nowait == false) {
        tv.tv_sec = tcp->cfg.recv_timeout_ms / 1000;
        tv.tv_usec = (tcp->cfg.recv_timeout_ms % 1000) * 1000;
    }
    int ret = select(max_fd + 1, &read_set, NULL, NULL, &tv);
    if (ret <= 0) {
        rc = ret;
        goto done;
    }
    conn = tcp_acquire_ready(tcp, &read_set);
    if (conn == NULL) {
        rc = 0;
        goto done;
    }
    if (len < 4) {
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    ret = tcp_connection_recv_exact(conn, buf, 4, tcp->cfg.recv_timeout_ms);
    if (ret != 4) {
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    uint16_t first = tcp_rd_be16(buf);
    if ((first & 0xC000) == 0x4000) {
        uint16_t payload_len = tcp_rd_be16(buf + 2);
        uint16_t padded_len = (uint16_t)(4 * ((payload_len + 3) / 4));
        if (padded_len + 4 > len) {
            ESP_LOGE(TAG, "Raw TCP channel data size %u over receive buffer %d", padded_len + 4, len);
            tcp_release_ready(tcp, conn, true);
            rc = -1;
            goto done;
        }
        if (padded_len) {
            ret = tcp_connection_recv_exact(conn, buf + 4, padded_len, tcp->cfg.recv_timeout_ms);
            if (ret != padded_len) {
                tcp_release_ready(tcp, conn, true);
                rc = -1;
                goto done;
            }
        }
        *addr = conn->addr;
        rc = payload_len + 4;
        tcp_release_ready(tcp, conn, false);
        goto done;
    }
    if (len < 20) {
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    ret = tcp_connection_recv_exact(conn, buf + 4, 16, tcp->cfg.recv_timeout_ms);
    if (ret != 16) {
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    uint16_t payload_len = tcp_rd_be16(buf + 2);
    if (payload_len + 20 > len) {
        ESP_LOGE(TAG, "Raw TCP STUN packet size %u over receive buffer %d", payload_len + 20, len);
        tcp_release_ready(tcp, conn, true);
        rc = -1;
        goto done;
    }
    if (payload_len) {
        ret = tcp_connection_recv_exact(conn, buf + 20, payload_len, tcp->cfg.recv_timeout_ms);
        if (ret != payload_len) {
            tcp_release_ready(tcp, conn, true);
            rc = -1;
            goto done;
        }
    }
    *addr = conn->addr;
    rc = payload_len + 20;
    tcp_release_ready(tcp, conn, false);
done:
    peer_atomic_dec(&tcp->users);
    return rc;
}

void WEAK tcp_connections_close(tcp_connections_handle_t tcp)
{
    if (tcp == NULL) {
        return;
    }
    tcp->closing = true;
    /* Follow udp_socket_close(): close/shutdown the sockets FIRST, BEFORE joining the
     * worker, so a worker blocked in connect()/send() or any in-flight recv/send blocked
     * in select()/recv()/send() returns immediately and close aborts the connection
     * promptly instead of pthread_join() waiting for a timeout. */
    tcp_socket_close(&tcp->listen_sock);
    pthread_mutex_lock(&tcp->lock);
    for (int i = 0; i < tcp->connection_num; i++) {
        tcp_connection_wake(&tcp->connections[i]);
    }
    pthread_mutex_unlock(&tcp->lock);
    if (tcp->worker_started) {
        pthread_join(tcp->worker, NULL);
    }
    while (peer_atomic_load(&tcp->users)) {
        media_lib_thread_sleep(10);
    }
    pthread_mutex_lock(&tcp->lock);
    for (int i = 0; i < tcp->connection_num; i++) {
        tcp_connection_close(&tcp->connections[i]);
    }
    tcp_send_item_t *item = tcp->send_head;
    while (item) {
        tcp_send_item_t *next = item->next;
        free(item);
        item = next;
    }
    pthread_mutex_unlock(&tcp->lock);
    for (int i = 0; i < tcp->connection_num; i++) {
        pthread_mutex_destroy(&tcp->connections[i].io_lock);
    }
    pthread_mutex_destroy(&tcp->lock);
    free(tcp->connections);
    free(tcp);
}

