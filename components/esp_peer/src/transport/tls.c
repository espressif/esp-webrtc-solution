/**
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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
#include "peer_tls.h"
#include "peer_utils.h"
#include "tcp.h"
#include "tls.h"

#define TAG "TLS"
#define WEAK __attribute__((weak))

#define TLS_DEFAULT_CONNECT_TIMEOUT_MS (30000)
#define TLS_DEFAULT_SEND_TIMEOUT_MS    (1000)
#define TLS_DEFAULT_RECV_TIMEOUT_MS    (150)
#define TLS_WORKER_SLEEP_MS            (10)

typedef struct {
    int                     fd;
    esp_peer_addr_t         addr;
    bool                    connected;
    bool                    connecting;
    uint32_t                connect_start_ms;
    peer_tls_handle_t       sess;
    pthread_mutex_t         io_lock;
} tls_connection_t;

typedef struct tls_send_item_t {
    struct tls_send_item_t *next;
    esp_peer_addr_t         addr;
    uint8_t                 prio;
    int                     len;
    uint8_t                 data[];
} tls_send_item_t;

struct tls_connections_t {
    tls_connections_cfg_t cfg;
    tcp_socket_t          listen_sock;
    tls_connection_t     *connections;
    uint8_t               connection_num;
    tls_send_item_t      *send_head;
    pthread_mutex_t       lock;
    pthread_t             worker;
    bool                  worker_started;
    bool                  server_started;
    bool                  closing;
    atomic_int            users;
};

static uint32_t tls_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static inline uint16_t tls_rd_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int tls_set_nonblock(int fd, bool nonblock)
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

static bool tls_same_addr(esp_peer_addr_t *a, esp_peer_addr_t *b)
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

static int tls_addr_to_sockaddr(esp_peer_addr_t *addr, struct sockaddr_storage *storage, socklen_t *addr_len)
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

static void tls_to_client_cfg(tls_connections_handle_t tls, peer_tls_client_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->cacert_buf = tls->cfg.ca_pem;
    if (tls->cfg.ca_pem && tls->cfg.ca_pem_len > 0) {
        out->cacert_bytes = tls->cfg.ca_pem_len;
    } else if (tls->cfg.ca_pem) {
        out->cacert_bytes = (int)strlen(tls->cfg.ca_pem);
    } else {
        out->cacert_bytes = 0;
    }
    out->skip_cert_verify = tls->cfg.skip_cert_verify;
    out->timeout_ms = tls->cfg.connect_timeout_ms ? tls->cfg.connect_timeout_ms : TLS_DEFAULT_CONNECT_TIMEOUT_MS;
}

static tls_connection_t *tls_connections_find(tls_connections_handle_t tls, esp_peer_addr_t *addr)
{
    for (int i = 0; i < tls->connection_num; i++) {
        if (tls->connections[i].fd >= 0 && tls_same_addr(&tls->connections[i].addr, addr)) {
            return &tls->connections[i];
        }
    }
    return NULL;
}

static tls_connection_t *tls_connections_alloc(tls_connections_handle_t tls)
{
    for (int i = 0; i < tls->connection_num; i++) {
        if (tls->connections[i].fd < 0) {
            return &tls->connections[i];
        }
    }
    return NULL;
}

/* Wake a connection blocked in select()/TLS I/O without freeing it. Used by close()
 * so any in-flight recv/send returns promptly; the actual free happens later under
 * io_lock once all users have drained. */
static void tls_connection_wake(tls_connection_t *conn)
{
    if (conn->fd >= 0) {
        shutdown(conn->fd, SHUT_RDWR);
    }
}

/* Tear down the session/socket. Takes io_lock so it waits for any in-flight I/O on
 * this connection to finish before freeing the session (no use-after-free). Callers
 * hold the manager lock; lock order is always manager -> io_lock. Idempotent. */
static void tls_connection_close(tls_connection_t *conn)
{
    pthread_mutex_lock(&conn->io_lock);
    if (conn->sess) {
        peer_tls_free(conn->sess);
        conn->sess = NULL;
        conn->fd = -1;
    } else if (conn->fd >= 0) {
        int fd = conn->fd;
        conn->fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    conn->connected = false;
    conn->connecting = false;
    pthread_mutex_unlock(&conn->io_lock);
}

static int tls_connection_send(tls_connection_t *conn, uint8_t *data, int len, uint32_t timeout_ms)
{
    if (conn->sess) {
        int sent = 0;
        while (sent < len) {
            int w = peer_tls_write(conn->sess, data + sent, len - sent);
            if (w < 0) {
                return sent > 0 ? sent : -1;
            }
            if (w == 0) {
                /* WANT_READ/WANT_WRITE: wait for the socket then retry peer_tls_write.
                 * Do NOT call peer_tls_read here — SSL_write pumps any pending inbound
                 * TLS records itself, and reading would discard real inbound app data
                 * (relayed ChannelData / STUN) on a TURNS stream. */
                fd_set rfds, wfds;
                FD_ZERO(&rfds);
                FD_ZERO(&wfds);
                FD_SET(conn->fd, &rfds);
                FD_SET(conn->fd, &wfds);
                struct timeval tv = {
                    .tv_sec = timeout_ms / 1000,
                    .tv_usec = (timeout_ms % 1000) * 1000,
                };
                if (select(conn->fd + 1, &rfds, &wfds, NULL, &tv) <= 0) {
                    return sent > 0 ? sent : -1;
                }
                continue;
            }
            sent += w;
        }
        return sent;
    }
    tcp_socket_t sock = {
        .fd = conn->fd,
        .family = conn->addr.family,
    };
    tcp_blocking_timeout(&sock, timeout_ms);
    return tcp_socket_send(&sock, data, len);
}

static int tls_connection_recv_exact(tls_connection_t *conn, uint8_t *data, int len, uint32_t timeout_ms)
{
    int received = 0;
    while (received < len) {
        bool skip_select = conn->sess != NULL && peer_tls_pending(conn->sess) > 0;
        if (!skip_select) {
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
        }
        int ret;
        if (conn->sess) {
            ret = peer_tls_read(conn->sess, data + received, len - received);
            if (ret < 0) {
                return -1;
            }
            if (ret == 0) {
                continue;
            }
        } else {
            ret = recv(conn->fd, data + received, len - received, 0);
            if (ret <= 0) {
                return -1;
            }
        }
        received += ret;
    }
    return received;
}

static void tls_connections_accept_all(tls_connections_handle_t tls)
{
    if (tls->server_started == false) {
        return;
    }
    while (tls->listen_sock.fd >= 0) {
        tcp_socket_t client;
        esp_peer_addr_t addr;
        if (tcp_socket_accept(&tls->listen_sock, &client, &addr) != 0) {
            break;
        }
        tls_set_nonblock(client.fd, true);

        peer_tls_server_cfg_t scfg = {
            .servercert_buf = tls->cfg.server_cert_pem,
            .servercert_bytes = tls->cfg.server_cert_pem_len,
            .serverkey_buf = tls->cfg.server_key_pem,
            .serverkey_bytes = tls->cfg.server_key_pem_len,
        };
        peer_tls_handle_t sess = peer_tls_new_server(client.fd, &scfg);
        if (sess == NULL) {
            ESP_LOGW(TAG, "TLS server handshake failed");
            /* peer_tls_new_server already closed client.fd on failure */
            continue;
        }
        int tls_fd = peer_tls_get_fd(sess);
        pthread_mutex_lock(&tls->lock);
        tls_connection_t *old = tls_connections_find(tls, &addr);
        if (old) {
            tls_connection_close(old);
        }
        tls_connection_t *conn = old ? old : tls_connections_alloc(tls);
        if (conn == NULL) {
            ESP_LOGW(TAG, "Drop TLS connection: over limit");
            peer_tls_free(sess);
        } else {
            conn->sess = sess;
            conn->fd = tls_fd;
            conn->addr = addr;
            conn->connected = true;
            conn->connecting = false;
        }
        pthread_mutex_unlock(&tls->lock);
    }
}

static void tls_connections_check_connecting(tls_connections_handle_t tls)
{
    uint32_t now = tls_get_time_ms();
    pthread_mutex_lock(&tls->lock);
    for (int i = 0; i < tls->connection_num; i++) {
        tls_connection_t *conn = &tls->connections[i];
        if (conn->fd < 0 || conn->connecting == false) {
            continue;
        }
        if (now - conn->connect_start_ms > tls->cfg.connect_timeout_ms) {
            tls_connection_close(conn);
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
            tls_connection_close(conn);
            continue;
        }
        /* TCP is up; run the (blocking) TLS handshake WITHOUT holding the manager
         * lock so other API calls are not stalled for up to connect_timeout_ms.
         * conn->connecting stays true so a concurrent connect() to the same addr
         * is a no-op, and the worker is single-threaded so this slot is not
         * re-processed while we are unlocked. */
        peer_tls_client_cfg_t mc;
        tls_to_client_cfg(tls, &mc);
        const char *hn = tls->cfg.tls_hostname;
        int hlen = tls->cfg.tls_hostname_len;
        if (hn == NULL || hlen <= 0) {
            hn = "localhost";
            hlen = (int)strlen(hn);
        }
        int conn_fd = conn->fd;
        pthread_mutex_unlock(&tls->lock);
        peer_tls_handle_t sess = peer_tls_new_client(conn_fd, hn, hlen, &mc);
        pthread_mutex_lock(&tls->lock);
        conn = &tls->connections[i];
        if (sess == NULL) {
            ESP_LOGE(TAG, "TLS client handshake failed");
            /* peer_tls_new_client already closed conn_fd on failure */
            conn->fd = -1;
            tls_connection_close(conn);
            continue;
        }
        conn->sess = sess;
        conn->fd = peer_tls_get_fd(sess);
        conn->connecting = false;
        conn->connected = true;
    }
    pthread_mutex_unlock(&tls->lock);
}

static tls_send_item_t *tls_connections_pop_send(tls_connections_handle_t tls, tls_connection_t **conn)
{
    tls_send_item_t *prev = NULL;
    tls_send_item_t *cur = tls->send_head;
    while (cur) {
        *conn = tls_connections_find(tls, &cur->addr);
        if (*conn && (*conn)->connected) {
            if (prev) {
                prev->next = cur->next;
            } else {
                tls->send_head = cur->next;
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

static void tls_connections_process_send(tls_connections_handle_t tls)
{
    while (true) {
        pthread_mutex_lock(&tls->lock);
        tls_connection_t *conn = NULL;
        tls_send_item_t *item = tls_connections_pop_send(tls, &conn);
        if (item == NULL) {
            pthread_mutex_unlock(&tls->lock);
            break;
        }
        /* Grab io_lock while still holding the manager lock so the connection cannot
         * be closed between popping and sending, then drop the manager lock so other
         * API calls (e.g. recv) are not blocked while this send is in progress. */
        pthread_mutex_lock(&conn->io_lock);
        pthread_mutex_unlock(&tls->lock);
        int sret = tls_connection_send(conn, item->data, item->len, tls->cfg.send_timeout_ms);
        pthread_mutex_unlock(&conn->io_lock);
        if (sret != item->len) {
            pthread_mutex_lock(&tls->lock);
            tls_connection_close(conn);
            pthread_mutex_unlock(&tls->lock);
        }
        free(item);
    }
}

static void *tls_connections_worker(void *arg)
{
    tls_connections_handle_t tls = (tls_connections_handle_t)arg;
    while (tls->closing == false) {
        tls_connections_accept_all(tls);
        tls_connections_check_connecting(tls);
        tls_connections_process_send(tls);
        media_lib_thread_sleep(TLS_WORKER_SLEEP_MS);
    }
    return NULL;
}

tls_connections_handle_t WEAK tls_connections_open(tls_connections_cfg_t *cfg)
{
    tls_connections_handle_t tls = calloc(1, sizeof(struct tls_connections_t));
    if (tls == NULL) {
        return NULL;
    }
    tls->cfg = *cfg;
    if (tls->cfg.max_connections == 0) {
        tls->cfg.max_connections = 4;
    }
    if (tls->cfg.connect_timeout_ms == 0) {
        tls->cfg.connect_timeout_ms = TLS_DEFAULT_CONNECT_TIMEOUT_MS;
    }
    if (tls->cfg.send_timeout_ms == 0) {
        tls->cfg.send_timeout_ms = TLS_DEFAULT_SEND_TIMEOUT_MS;
    }
    if (tls->cfg.recv_timeout_ms == 0) {
        tls->cfg.recv_timeout_ms = TLS_DEFAULT_RECV_TIMEOUT_MS;
    }
    tls->listen_sock.fd = -1;
    tls->connections = calloc(tls->cfg.max_connections, sizeof(tls_connection_t));
    if (tls->connections == NULL) {
        free(tls);
        return NULL;
    }
    tls->connection_num = tls->cfg.max_connections;
    for (int i = 0; i < tls->connection_num; i++) {
        tls->connections[i].fd = -1;
        if (pthread_mutex_init(&tls->connections[i].io_lock, NULL) != 0) {
            for (int j = 0; j < i; j++) {
                pthread_mutex_destroy(&tls->connections[j].io_lock);
            }
            free(tls->connections);
            free(tls);
            return NULL;
        }
    }
    if (pthread_mutex_init(&tls->lock, NULL) != 0) {
        for (int i = 0; i < tls->connection_num; i++) {
            pthread_mutex_destroy(&tls->connections[i].io_lock);
        }
        free(tls->connections);
        free(tls);
        return NULL;
    }
    if (pthread_create(&tls->worker, NULL, tls_connections_worker, tls) == 0) {
        tls->worker_started = true;
    } else {
        pthread_mutex_destroy(&tls->lock);
        for (int i = 0; i < tls->connection_num; i++) {
            pthread_mutex_destroy(&tls->connections[i].io_lock);
        }
        free(tls->connections);
        free(tls);
        return NULL;
    }
    return tls;
}

void WEAK tls_connections_set_recv_timeout(tls_connections_handle_t tls, uint32_t timeout_ms)
{
    if (tls) {
        tls->cfg.recv_timeout_ms = timeout_ms;
    }
}

int WEAK tls_connections_bind(tls_connections_handle_t tls, esp_peer_addr_t *addr)
{
    if (tls == NULL || addr == NULL) {
        return -1;
    }
    uint8_t family = addr->family == AF_INET6 ? AF_INET6 : AF_INET;
    if (tcp_socket_open_family(&tls->listen_sock, family) != 0) {
        return -1;
    }
    if (tcp_socket_bind(&tls->listen_sock, addr) != 0) {
        tcp_socket_close(&tls->listen_sock);
        return -1;
    }
    *addr = tls->listen_sock.bind_addr;
    return 0;
}

int WEAK tls_connections_start_server(tls_connections_handle_t tls)
{
    if (tls == NULL || tls->listen_sock.fd < 0) {
        return -1;
    }
    if (tcp_socket_listen(&tls->listen_sock, tls->cfg.max_connections) != 0) {
        return -1;
    }
    int ret = tls_set_nonblock(tls->listen_sock.fd, true);
    if (ret == 0) {
        tls->server_started = true;
    }
    return ret;
}

int WEAK tls_connections_connect(tls_connections_handle_t tls, esp_peer_addr_t *addr)
{
    if (tls == NULL || addr == NULL) {
        return -1;
    }
    if (tls->closing) {
        return -1;
    }
    pthread_mutex_lock(&tls->lock);
    tls_connection_t *conn = tls_connections_find(tls, addr);
    if (conn && (conn->connected || conn->connecting)) {
        pthread_mutex_unlock(&tls->lock);
        return 0;
    }
    conn = conn ? conn : tls_connections_alloc(tls);
    if (conn == NULL) {
        pthread_mutex_unlock(&tls->lock);
        return -1;
    }
    int fd = socket(addr->family == AF_INET6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        pthread_mutex_unlock(&tls->lock);
        return -1;
    }
    tls_set_nonblock(fd, true);
    struct sockaddr_storage storage;
    socklen_t addr_len = 0;
    if (tls_addr_to_sockaddr(addr, &storage, &addr_len) != 0) {
        close(fd);
        pthread_mutex_unlock(&tls->lock);
        return -1;
    }
    int ret = connect(fd, (struct sockaddr *)&storage, addr_len);
    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        pthread_mutex_unlock(&tls->lock);
        return -1;
    }
    conn->fd = fd;
    conn->addr = *addr;
    conn->sess = NULL;
    /* Always run TLS handshake in worker (covers both EINPROGRESS and immediate connect()). */
    conn->connected = false;
    conn->connecting = true;
    conn->connect_start_ms = tls_get_time_ms();
    pthread_mutex_unlock(&tls->lock);
    return 0;
}

int WEAK tls_connections_is_connected(tls_connections_handle_t tls, esp_peer_addr_t *addr)
{
    if (tls == NULL || addr == NULL || tls->closing) {
        return -1;
    }
    pthread_mutex_lock(&tls->lock);
    int ret = -1;
    tls_connection_t *conn = tls_connections_find(tls, addr);
    if (conn) {
        ret = conn->connected ? 1 : 0;
    }
    pthread_mutex_unlock(&tls->lock);
    return ret;
}

int WEAK tls_connections_send_to(tls_connections_handle_t tls, esp_peer_addr_t *addr, const uint8_t *buf, int len,
                                 uint8_t prio)
{
    if (tls == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    if (len > UINT16_MAX) {
        return -1;
    }
    if (tls->closing) {
        return -1;
    }
    tls_send_item_t *item = calloc(1, sizeof(tls_send_item_t) + len + 2);
    if (item == NULL) {
        return -1;
    }
    item->addr = *addr;
    item->prio = prio;
    item->len = len + 2;
    uint16_t net_len = htons((uint16_t)len);
    memcpy(item->data, &net_len, sizeof(net_len));
    memcpy(item->data + 2, buf, len);

    pthread_mutex_lock(&tls->lock);
    tls_send_item_t **cur = &tls->send_head;
    while (*cur && (*cur)->prio >= prio) {
        cur = &(*cur)->next;
    }
    item->next = *cur;
    *cur = item;
    pthread_mutex_unlock(&tls->lock);
    return len;
}

int WEAK tls_connections_send_raw_to(tls_connections_handle_t tls, esp_peer_addr_t *addr, const uint8_t *buf, int len)
{
    if (tls == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    int rc = -1;
    int total = 0;
    int tries = 0;
    bool failed = false;
    tls_connection_t *conn = NULL;
    peer_atomic_inc(&tls->users);
    if (tls->closing) {
        goto done;
    }
    pthread_mutex_lock(&tls->lock);
    conn = tls_connections_find(tls, addr);
    if (conn == NULL || conn->connected == false) {
        pthread_mutex_unlock(&tls->lock);
        conn = NULL;
        goto done;
    }
    /* Hold io_lock (taken under the manager lock so the connection can't be closed in
     * between), then drop the manager lock so a concurrent recv on another connection
     * is not blocked while this send is in flight. */
    pthread_mutex_lock(&conn->io_lock);
    pthread_mutex_unlock(&tls->lock);
    /* tls_connection_send can return a short count on transient SSL_WANT_* / select timeouts; retry instead of
     * treating as fatal — closing here caused flaky TURN-over-TLS ICE when coturn or the kernel buffered bursts. */
    while (total < len && tries < 64) {
        tries++;
        int chunk = tls_connection_send(conn, (uint8_t *)buf + total, len - total, tls->cfg.send_timeout_ms);
        if (chunk < 0) {
            failed = true;
            break;
        }
        if (chunk == 0) {
            media_lib_thread_sleep(1);
            continue;
        }
        total += chunk;
    }
    if (total != len) {
        failed = true;
    }
    pthread_mutex_unlock(&conn->io_lock);
    if (failed) {
        pthread_mutex_lock(&tls->lock);
        tls_connection_close(conn);
        pthread_mutex_unlock(&tls->lock);
        rc = -1;
    } else {
        rc = len;
    }
done:
    peer_atomic_dec(&tls->users);
    return rc;
}

/* Build the read fd_set over all connected connections (short manager-lock sections),
 * waiting up to recv_timeout for at least one connection to appear. Returns the max fd
 * or -1 if none/closing. */
static int tls_build_read_set(tls_connections_handle_t tls, fd_set *read_set, bool nowait)
{
    int max_fd = -1;
    uint32_t wait_deadline = tls_get_time_ms() + tls->cfg.recv_timeout_ms;
    do {
        FD_ZERO(read_set);
        max_fd = -1;
        pthread_mutex_lock(&tls->lock);
        for (int i = 0; i < tls->connection_num; i++) {
            tls_connection_t *conn = &tls->connections[i];
            if (conn->fd >= 0 && conn->connected) {
                FD_SET(conn->fd, read_set);
                if (conn->fd > max_fd) {
                    max_fd = conn->fd;
                }
            }
        }
        pthread_mutex_unlock(&tls->lock);
        if (max_fd >= 0 || nowait || tls->closing) {
            break;
        }
        media_lib_thread_sleep(TLS_WORKER_SLEEP_MS);
    } while (tls_get_time_ms() < wait_deadline);
    return max_fd;
}

/* After select() reports readiness, pick the first ready connection and return it with
 * its io_lock held (taken under the manager lock so it cannot be closed in between). The
 * caller does the blocking record read without holding the manager lock. NULL if none. */
static tls_connection_t *tls_acquire_ready(tls_connections_handle_t tls, fd_set *read_set)
{
    tls_connection_t *ready = NULL;
    pthread_mutex_lock(&tls->lock);
    for (int i = 0; i < tls->connection_num; i++) {
        tls_connection_t *conn = &tls->connections[i];
        if (conn->fd >= 0 && conn->connected && FD_ISSET(conn->fd, read_set)) {
            ready = conn;
            pthread_mutex_lock(&ready->io_lock);
            break;
        }
    }
    pthread_mutex_unlock(&tls->lock);
    return ready;
}

/* Release a connection acquired by tls_acquire_ready(); close it (under the manager
 * lock) when the read failed. */
static void tls_release_ready(tls_connections_handle_t tls, tls_connection_t *conn, bool failed)
{
    pthread_mutex_unlock(&conn->io_lock);
    if (failed) {
        pthread_mutex_lock(&tls->lock);
        tls_connection_close(conn);
        pthread_mutex_unlock(&tls->lock);
    }
}

int WEAK tls_connections_recv_from(tls_connections_handle_t tls, esp_peer_addr_t *addr, uint8_t *buf, int len,
                                   bool nowait)
{
    if (tls == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    int rc = -1;
    fd_set read_set;
    tls_connection_t *conn = NULL;
    peer_atomic_inc(&tls->users);
    if (tls->closing) {
        goto done;
    }
    int max_fd = tls_build_read_set(tls, &read_set, nowait);
    if (max_fd < 0) {
        goto done;
    }
    struct timeval tv = { 0 };
    if (nowait == false) {
        tv.tv_sec = tls->cfg.recv_timeout_ms / 1000;
        tv.tv_usec = (tls->cfg.recv_timeout_ms % 1000) * 1000;
    }
    int ret = select(max_fd + 1, &read_set, NULL, NULL, &tv);
    if (ret <= 0) {
        rc = ret;
        goto done;
    }
    conn = tls_acquire_ready(tls, &read_set);
    if (conn == NULL) {
        rc = 0;
        goto done;
    }
    uint16_t net_len = 0;
    ret = tls_connection_recv_exact(conn, (uint8_t *)&net_len, sizeof(net_len), tls->cfg.recv_timeout_ms);
    if (ret != sizeof(net_len)) {
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    uint16_t payload_len = ntohs(net_len);
    if (payload_len > len) {
        ESP_LOGE(TAG, "TLS packet size %u over receive buffer %d", payload_len, len);
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    ret = tls_connection_recv_exact(conn, buf, payload_len, tls->cfg.recv_timeout_ms);
    if (ret != payload_len) {
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    *addr = conn->addr;
    rc = ret;
    tls_release_ready(tls, conn, false);
done:
    peer_atomic_dec(&tls->users);
    return rc;
}

int WEAK tls_connections_recv_raw_from(tls_connections_handle_t tls, esp_peer_addr_t *addr, uint8_t *buf, int len,
                                       bool nowait)
{
    if (tls == NULL || addr == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    int rc = -1;
    fd_set read_set;
    tls_connection_t *conn = NULL;
    peer_atomic_inc(&tls->users);
    if (tls->closing) {
        goto done;
    }
    int max_fd = tls_build_read_set(tls, &read_set, nowait);
    if (max_fd < 0) {
        goto done;
    }
    struct timeval tv = { 0 };
    if (nowait == false) {
        tv.tv_sec = tls->cfg.recv_timeout_ms / 1000;
        tv.tv_usec = (tls->cfg.recv_timeout_ms % 1000) * 1000;
    }
    int ret = select(max_fd + 1, &read_set, NULL, NULL, &tv);
    if (ret <= 0) {
        rc = ret;
        goto done;
    }
    conn = tls_acquire_ready(tls, &read_set);
    if (conn == NULL) {
        rc = 0;
        goto done;
    }
    if (len < 4) {
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    ret = tls_connection_recv_exact(conn, buf, 4, tls->cfg.recv_timeout_ms);
    if (ret != 4) {
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    uint16_t first = tls_rd_be16(buf);
    if ((first & 0xC000) == 0x4000) {
        uint16_t payload_len = tls_rd_be16(buf + 2);
        uint16_t padded_len = (uint16_t)(4 * ((payload_len + 3) / 4));
        if (padded_len + 4 > len) {
            ESP_LOGE(TAG, "Raw TLS channel data size %u over receive buffer %d", padded_len + 4, len);
            tls_release_ready(tls, conn, true);
            rc = -1;
            goto done;
        }
        if (padded_len) {
            ret = tls_connection_recv_exact(conn, buf + 4, padded_len, tls->cfg.recv_timeout_ms);
            if (ret != padded_len) {
                tls_release_ready(tls, conn, true);
                rc = -1;
                goto done;
            }
        }
        *addr = conn->addr;
        rc = payload_len + 4;
        tls_release_ready(tls, conn, false);
        goto done;
    }
    if (len < 20) {
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    ret = tls_connection_recv_exact(conn, buf + 4, 16, tls->cfg.recv_timeout_ms);
    if (ret != 16) {
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    uint16_t payload_len = tls_rd_be16(buf + 2);
    if (payload_len + 20 > len) {
        ESP_LOGE(TAG, "Raw TLS STUN packet size %u over receive buffer %d", payload_len + 20, len);
        tls_release_ready(tls, conn, true);
        rc = -1;
        goto done;
    }
    if (payload_len) {
        ret = tls_connection_recv_exact(conn, buf + 20, payload_len, tls->cfg.recv_timeout_ms);
        if (ret != payload_len) {
            tls_release_ready(tls, conn, true);
            rc = -1;
            goto done;
        }
    }
    *addr = conn->addr;
    rc = payload_len + 20;
    tls_release_ready(tls, conn, false);
done:
    peer_atomic_dec(&tls->users);
    return rc;
}

void WEAK tls_connections_close(tls_connections_handle_t tls)
{
    if (tls == NULL) {
        return;
    }
    tls->closing = true;
    /* Follow udp_socket_close(): close/shutdown the sockets FIRST, BEFORE joining the
     * worker. The worker can be blocked in a TLS handshake (esp_tls_conn_new_sync may
     * block up to connect_timeout) or any in-flight recv/send can be blocked in
     * select()/TLS I/O; shutting the fds down makes those return immediately so close
     * aborts the connection promptly instead of pthread_join() waiting for a timeout. */
    tcp_socket_close(&tls->listen_sock);
    pthread_mutex_lock(&tls->lock);
    for (int i = 0; i < tls->connection_num; i++) {
        tls_connection_wake(&tls->connections[i]);
    }
    pthread_mutex_unlock(&tls->lock);
    if (tls->worker_started) {
        pthread_join(tls->worker, NULL);
    }
    while (peer_atomic_load(&tls->users)) {
        media_lib_thread_sleep(10);
    }
    pthread_mutex_lock(&tls->lock);
    for (int i = 0; i < tls->connection_num; i++) {
        tls_connection_close(&tls->connections[i]);
    }
    tls_send_item_t *item = tls->send_head;
    while (item) {
        tls_send_item_t *next = item->next;
        free(item);
        item = next;
    }
    pthread_mutex_unlock(&tls->lock);
    for (int i = 0; i < tls->connection_num; i++) {
        pthread_mutex_destroy(&tls->connections[i].io_lock);
    }
    pthread_mutex_destroy(&tls->lock);
    free(tls->connections);
    free(tls);
}
