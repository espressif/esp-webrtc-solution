/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#ifdef ESP_PLATFORM

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_tls.h"
#include "esp_tls_errors.h"
#include "lwip/sockets.h"

#include "peer_tls.h"

#define TAG "PEER_TLS_ESP"

typedef struct {
    esp_tls_t *tls;
    int        fd;
} peer_tls_esp_t;

peer_tls_handle_t peer_tls_new_client(int fd, const char *hostname, int hostname_len,
                                      const peer_tls_client_cfg_t *cfg)
{
    if (fd < 0) {
        return NULL;
    }
    peer_tls_esp_t *handle = calloc(1, sizeof(peer_tls_esp_t));
    if (handle == NULL) {
        close(fd);
        return NULL;
    }
    handle->fd = fd;
    handle->tls = esp_tls_init();
    if (handle->tls == NULL) {
        close(fd);
        free(handle);
        return NULL;
    }

    char host[256] = "localhost";
    if (hostname != NULL && hostname_len > 0) {
        int n = hostname_len < (int)sizeof(host) - 1 ? hostname_len : (int)sizeof(host) - 1;
        memcpy(host, hostname, n);
        host[n] = '\0';
    }

    static const peer_tls_client_cfg_t empty_cfg = { 0 };
    if (cfg == NULL) {
        cfg = &empty_cfg;
    }

    esp_tls_cfg_t tls_cfg = {
        .non_block = false,
        .timeout_ms = cfg->timeout_ms,
    };
    if (cfg->skip_cert_verify || cfg->cacert_buf == NULL) {
        /* INSECURE: no CA is supplied, so esp-tls falls back to VERIFY_NONE. That
         * fallback only exists when the build opts into it; otherwise esp-tls
         * aborts the handshake with "No server verification option set". Surface
         * a clear, actionable error instead of the opaque mbedtls setup failure. */
#if !defined(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY)
        ESP_LOGE(TAG,
                 "TURNS cert verification skip requested but not built in. Add "
                 "CONFIG_ESP_TLS_INSECURE=y and CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y "
                 "to sdkconfig (lab/testing only), or provide a CA certificate.");
        esp_tls_conn_destroy(handle->tls);
        free(handle);
        return NULL;
#else
        ESP_LOGW(TAG, "INSECURE: skipping TURNS server certificate verification");
        tls_cfg.skip_common_name = true;
#endif
    } else {
        tls_cfg.cacert_buf = (const unsigned char *)cfg->cacert_buf;
        tls_cfg.cacert_bytes = (unsigned int)(cfg->cacert_bytes > 0 ? cfg->cacert_bytes
                                                                    : (int)strlen(cfg->cacert_buf) + 1);
        tls_cfg.common_name = host;
    }

    /* Reuse the already-connected socket: set the fd and move past the plain-TCP
     * connect stage so esp_tls only performs the TLS handshake. */
    if (esp_tls_set_conn_sockfd(handle->tls, fd) != ESP_OK ||
        esp_tls_set_conn_state(handle->tls, ESP_TLS_CONNECTING) != ESP_OK) {
        ESP_LOGE(TAG, "Fail to attach socket to esp_tls");
        esp_tls_conn_destroy(handle->tls);
        free(handle);
        return NULL;
    }

    int ret = esp_tls_conn_new_sync(host, (int)strlen(host), 443, &tls_cfg, handle->tls);
    if (ret != 1) {
        ESP_LOGE(TAG, "esp_tls client handshake failed (%d)", ret);
        esp_tls_conn_destroy(handle->tls);
        free(handle);
        return NULL;
    }
    return handle;
}

peer_tls_handle_t peer_tls_new_server(int fd, const peer_tls_server_cfg_t *cfg)
{
    if (fd < 0 || cfg == NULL) {
        if (fd >= 0) {
            close(fd);
        }
        return NULL;
    }
    peer_tls_esp_t *handle = calloc(1, sizeof(peer_tls_esp_t));
    if (handle == NULL) {
        close(fd);
        return NULL;
    }
    handle->fd = fd;
    handle->tls = esp_tls_init();
    if (handle->tls == NULL) {
        close(fd);
        free(handle);
        return NULL;
    }

    esp_tls_cfg_server_t scfg = {
        .servercert_buf = (const unsigned char *)cfg->servercert_buf,
        .servercert_bytes = (unsigned int)(cfg->servercert_bytes > 0 ? cfg->servercert_bytes
                                                                     : (cfg->servercert_buf ? (int)strlen(cfg->servercert_buf) + 1 : 0)),
        .serverkey_buf = (const unsigned char *)cfg->serverkey_buf,
        .serverkey_bytes = (unsigned int)(cfg->serverkey_bytes > 0 ? cfg->serverkey_bytes
                                                                  : (cfg->serverkey_buf ? (int)strlen(cfg->serverkey_buf) + 1 : 0)),
    };
    int ret = esp_tls_server_session_create(&scfg, fd, handle->tls);
    if (ret != 0) {
        ESP_LOGW(TAG, "esp_tls server handshake failed (%d)", ret);
        esp_tls_conn_destroy(handle->tls);
        free(handle);
        return NULL;
    }
    return handle;
}

int peer_tls_write(peer_tls_handle_t handle, const void *data, int len)
{
    peer_tls_esp_t *h = (peer_tls_esp_t *)handle;
    if (h == NULL || h->tls == NULL) {
        return -1;
    }
    ssize_t ret = esp_tls_conn_write(h->tls, data, (size_t)len);
    if (ret > 0) {
        return (int)ret;
    }
    if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == 0) {
        return 0;
    }
    return -1;
}

int peer_tls_read(peer_tls_handle_t handle, void *data, int len)
{
    peer_tls_esp_t *h = (peer_tls_esp_t *)handle;
    if (h == NULL || h->tls == NULL) {
        return -1;
    }
    ssize_t ret = esp_tls_conn_read(h->tls, data, (size_t)len);
    if (ret > 0) {
        return (int)ret;
    }
    if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    /* 0 means the peer closed the connection -> treat as fatal for the framer. */
    return -1;
}

int peer_tls_pending(peer_tls_handle_t handle)
{
    peer_tls_esp_t *h = (peer_tls_esp_t *)handle;
    if (h == NULL || h->tls == NULL) {
        return 0;
    }
    ssize_t avail = esp_tls_get_bytes_avail(h->tls);
    return avail > 0 ? (int)avail : 0;
}

int peer_tls_get_fd(peer_tls_handle_t handle)
{
    peer_tls_esp_t *h = (peer_tls_esp_t *)handle;
    if (h == NULL || h->tls == NULL) {
        return h ? h->fd : -1;
    }
    int sockfd = -1;
    if (esp_tls_get_conn_sockfd(h->tls, &sockfd) == ESP_OK && sockfd >= 0) {
        return sockfd;
    }
    return h->fd;
}

void peer_tls_free(peer_tls_handle_t handle)
{
    peer_tls_esp_t *h = (peer_tls_esp_t *)handle;
    if (h == NULL) {
        return;
    }
    if (h->tls) {
        /* esp_tls_conn_destroy() also closes the underlying socket. */
        esp_tls_conn_destroy(h->tls);
    } else if (h->fd >= 0) {
        close(h->fd);
    }
    free(h);
}

#endif /* ESP_PLATFORM */
