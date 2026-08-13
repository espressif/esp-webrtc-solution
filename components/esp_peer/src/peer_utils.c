/**
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2026 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include "peer_utils.h"

static esp_peer_dtls_cipher_pref_t s_dtls_cipher_pref = ESP_PEER_DTLS_CIPHER_AUTO;

void peer_atomic_inc(atomic_int *v)
{
    atomic_fetch_add(v, 1);
}

int peer_atomic_load(atomic_int *v)
{
    return atomic_load(v);
}

int peer_atomic_dec(atomic_int *v)
{
    return atomic_fetch_sub(v, 1);
}

esp_peer_dtls_cipher_pref_t peer_get_dtls_cipher_pref(void)
{
    return s_dtls_cipher_pref;
}

int esp_peer_set_dtls_cipher_pref(esp_peer_dtls_cipher_pref_t pref)
{
    if (pref > ESP_PEER_DTLS_CIPHER_CHACHA) {
        return -1;
    }
    s_dtls_cipher_pref = pref;
    return 0;
}
