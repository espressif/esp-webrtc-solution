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

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *scheme_start;
    const char *host_start;
    uint16_t    scheme_len;
    uint16_t    host_len;
    uint16_t    port;
} url_component_t;

static const char* get_slash(const char* s, int len, int dir)
{
    if (len == 0) {
        return NULL;
    }
    if (dir) {
        while (len-- > 0) {
           if (s[len] == '/') {
               return &s[len];
           }
        }
    } else {
        const char* e = s + len;
        while (s < e) {
            if (*(s++) == '/') {
                if (*s == '/') {
                   s++;
                   continue;
                }
                return s-1;
            }
        }
    }
    return NULL;
}

static char* join_url(const char* base, const char* ext)
{
    if (memcmp(ext, "http", 4) == 0) {
        return strdup(ext);
    }
    int base_len = strlen(base);
    int ext_len  = strlen(ext);
    int ext_skip = 0;
    const char* s;
    if (*ext == '/') {
        if (*(ext+1) == '/') {
            s = strstr(base, "//");
        } else {
            s = get_slash(base, base_len, 0);
        }
        if (s == NULL) {
           return NULL;
        }
        base_len = s - base;
    } else if (*ext == '.') {
        s = get_slash(base, base_len, 1);
        if (s == NULL) {
            return NULL;
        }
        base_len = s - base;
        if (ext_len == 1) {
            ext_skip = 1;
        } else if (*(ext+1) == '/') {
           ext_skip = 2;
        } else {
            while (memcmp(ext + ext_skip, "../", 3) == 0) {
                ext_skip += 3;
                s = get_slash(base, base_len, 1);
                if (s == NULL) {
                return NULL;
                }
                base_len = s - base;
            }
        }
        base_len++;
    } else if (*ext == '#') {
    } else if (*ext == '?') {
         char* a = strrchr(base, '?');
         if (a) {
            base_len = a - base;
         }
    } else {
        s = get_slash(base, base_len, 1);
        if (s == NULL) {
            return NULL;
        }
        base_len = s - base + 1;
    }
    int t = base_len + ext_len - ext_skip + 1;
    char* dst = (char*) malloc(t);
    if (dst == NULL) {
        return dst;
    }
    memcpy(dst, base, base_len);
    memcpy(dst + base_len, ext + ext_skip, ext_len - ext_skip);
    dst[t-1] = 0;
    return dst;
}

static bool parse_url(const char *url, url_component_t *c)
{
    c->port = 0;

    const char *p = strstr(url, "://");
    if (!p) return false;

    c->scheme_start = url;
    c->scheme_len   = (size_t)(p - url);
    p += 3;

    const char *h = p;
    while (*h && *h != ':' && *h != '/') h++;

    c->host_start = p;
    c->host_len   = (size_t)(h - p);

    if (*h == ':') {
        c->port = atoi(h + 1);
    } else {
        // assign default port immediately
        if (c->scheme_len == 5 && !memcmp(c->scheme_start, "https", 5))
            c->port = 443;
        else if (c->scheme_len == 4 && !memcmp(c->scheme_start, "http", 4))
            c->port = 80;
    }
    return true;
}

static bool is_same_origin(const char *url1, const char *url2)
{
    url_component_t c1, c2;
    if (!parse_url(url1, &c1) || !parse_url(url2, &c2)) return false;

    if (c1.scheme_len != c2.scheme_len ||
        memcmp(c1.scheme_start, c2.scheme_start, c1.scheme_len))
        return false;

    if (c1.host_len != c2.host_len ||
        memcmp(c1.host_start, c2.host_start, c1.host_len))
        return false;

    return (c1.port == c2.port);
}

#ifdef __cplusplus
}
#endif
