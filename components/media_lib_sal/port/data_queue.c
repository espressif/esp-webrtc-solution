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

#include "media_lib_os.h"
#include "data_queue.h"

#define DATA_Q_ALLOC_HEAD_SIZE   (4)
#define DATA_Q_DATA_ARRIVE_BITS  (1)
#define DATA_Q_DATA_CONSUME_BITS (2)
#define DATA_Q_USER_FREE_BITS    (4)

#define _SET_BITS(group, bit)    media_lib_event_group_set_bits((media_lib_event_grp_handle_t) group, bit)
// Need manual clear bits
#define _WAIT_BITS(group, bit)                                                                           \
    media_lib_event_group_wait_bits((media_lib_event_grp_handle_t) group, bit, MEDIA_LIB_MAX_LOCK_TIME); \
    media_lib_event_group_clr_bits(group, bit)

#define _MUTEX_LOCK(mutex)   media_lib_mutex_lock((media_lib_mutex_handle_t) mutex, MEDIA_LIB_MAX_LOCK_TIME)
#define _MUTEX_UNLOCK(mutex) media_lib_mutex_unlock((media_lib_mutex_handle_t) mutex)

static int data_queue_release_user(data_queue_t *q)
{
    _SET_BITS(q->event, DATA_Q_USER_FREE_BITS);
    return 0;
}

static int data_queue_notify_data(data_queue_t *q)
{
    _SET_BITS(q->event, DATA_Q_DATA_ARRIVE_BITS);
    return 0;
}

static int data_queue_wait_data(data_queue_t *q)
{
    q->user++;
    _MUTEX_UNLOCK(q->lock);
    _WAIT_BITS(q->event, DATA_Q_DATA_ARRIVE_BITS);
    _MUTEX_LOCK(q->lock);
    int ret = (q->quit) ? -1 : 0;
    q->user--;
    data_queue_release_user(q);
    return ret;
}

static int data_queue_data_consumed(data_queue_t *q)
{
    _SET_BITS(q->event, DATA_Q_DATA_CONSUME_BITS);
    return 0;
}

static int data_queue_wait_consume(data_queue_t *q)
{
    q->user++;
    _MUTEX_UNLOCK(q->lock);
    _WAIT_BITS(q->event, DATA_Q_DATA_CONSUME_BITS);
    _MUTEX_LOCK(q->lock);
    int ret = (q->quit) ? -1 : 0;
    q->user--;
    data_queue_release_user(q);
    return ret;
}

static int data_queue_wait_user(data_queue_t *q)
{
    _MUTEX_UNLOCK(q->lock);
    _WAIT_BITS(q->event, DATA_Q_USER_FREE_BITS);
    _MUTEX_LOCK(q->lock);
    return 0;
}

static bool _data_queue_have_data(data_queue_t *q)
{
    if (q->wp == q->rp && q->fill_end == 0) {
        return false;
    }
    return true;
}

static bool _data_queue_have_data_from_last(data_queue_t *q)
{
    return q->filled ? true : false;
}

data_queue_t *data_queue_init(int size)
{
    data_queue_t *q = media_lib_calloc(1, sizeof(data_queue_t));
    if (q == NULL) {
        return NULL;
    }
    q->buffer = media_lib_malloc(size);
    media_lib_mutex_create(&q->lock);
    media_lib_mutex_create(&q->write_lock);
    media_lib_event_group_create(&q->event);
    if (q->buffer == NULL || q->lock == NULL || q->write_lock == NULL || q->event == NULL) {
        data_queue_deinit(q);
        return NULL;
    }
    q->size = size;
    return q;
}

void data_queue_wakeup(data_queue_t *q)
{
    if (q && q->lock) {
        _MUTEX_LOCK(q->lock);
        q->quit = 1;
        // send quit message to let user quit
        data_queue_notify_data(q);
        data_queue_data_consumed(q);
        while (q->user) {
            data_queue_wait_user(q);
        }
        _MUTEX_UNLOCK(q->lock);
    }
}

int data_queue_consume_all(data_queue_t *q)
{
    if (q && q->lock) {
        _MUTEX_LOCK(q->lock);
        while (_data_queue_have_data(q)) {
            if (q->quit) {
                break;
            }
            uint8_t *buffer = (uint8_t *) q->buffer + q->rp;
            int size = *((int *) buffer);
            if (size < 0 || size >= q->size) {
                *(int*)0 = 0;
            }
            q->rp += size;
            q->filled -= size;
            if (q->fill_end && q->rp >= q->fill_end) {
                q->fill_end = 0;
                q->rp = 0;
            }
            data_queue_data_consumed(q);
        }
        _MUTEX_UNLOCK(q->lock);
    }
    return 0;
}

void data_queue_deinit(data_queue_t *q)
{
    if (q == NULL) {
        return;
    }
    if (q->lock) {
        media_lib_mutex_destroy((media_lib_mutex_handle_t) q->lock);
    }
    if (q->write_lock) {
        media_lib_mutex_destroy((media_lib_mutex_handle_t) q->write_lock);
    }
    if (q->event) {
        media_lib_event_group_destroy((media_lib_mutex_handle_t) q->event);
    }
    if (q->buffer) {
        media_lib_free(q->buffer);
    }
    media_lib_free(q);
}

/*   case 1:  [0...rp...wp...size]
 *   case 2:  [0...wp...rp...fillend...size]
 *   special case: wp == rp  fill_end set buffer full else empty
 */
static int get_available_size(data_queue_t *q)
{
    if (q->wp > q->rp) {
        return q->size - q->wp;
    }
    if (q->wp == q->rp) {
        if (q->fill_end) {
            return 0;
        }
        return q->size - q->wp;
    }
    return q->rp - q->wp;
}

int data_queue_get_available(data_queue_t *q)
{
    if (q == NULL) {
        return 0;
    }
    _MUTEX_LOCK(q->lock);
    int avail;
    // Handle corner case [0 rp==wp fifo_end]
    // Left fifo is not enough but actually fifo is empty
    if (q->wp == q->rp && q->fill_end == 0) {
        avail = q->size;
    } else {
        avail = get_available_size(q);
    }
    if (avail >= DATA_Q_ALLOC_HEAD_SIZE) {
        avail -= DATA_Q_ALLOC_HEAD_SIZE;
    } else {
        avail = 0;
    }
    _MUTEX_UNLOCK(q->lock);
    return avail;
}

void *data_queue_get_buffer(data_queue_t *q, int size)
{
    int avail = 0;
    size += DATA_Q_ALLOC_HEAD_SIZE;
    if (q == NULL || size > q->size) {
        return NULL;
    }
    _MUTEX_LOCK(q->write_lock);
    _MUTEX_LOCK(q->lock);
    while (!q->quit) {
        avail = get_available_size(q);
        // size not enough
        if (avail < size && q->fill_end == 0) {
            if (q->wp == q->rp) {
                q->wp = q->rp = 0;
            }
            q->fill_end = q->wp;
            q->wp = 0;
            avail = get_available_size(q);
        }
        if (avail >= size) {
            uint8_t *buffer = (uint8_t *) q->buffer + q->wp;
            q->user++;
            _MUTEX_UNLOCK(q->lock);
            return buffer + DATA_Q_ALLOC_HEAD_SIZE;
        }
        int ret = data_queue_wait_consume(q);
        if (ret != 0) {
            break;
        }
    }
    _MUTEX_UNLOCK(q->lock);
    _MUTEX_UNLOCK(q->write_lock);
    return NULL;
}

void *data_queue_get_write_data(data_queue_t *q)
{
    if (q == NULL) {
        return NULL;
    }
    _MUTEX_LOCK(q->lock);
    uint8_t *buffer = (uint8_t *) q->buffer + q->wp;
    _MUTEX_UNLOCK(q->lock);
    return buffer + DATA_Q_ALLOC_HEAD_SIZE;
}

int data_queue_send_buffer(data_queue_t *q, int size)
{
    int ret = -1;
    if (q == NULL) {
        return -1;
    }
    _MUTEX_LOCK(q->lock);
    if (size == 0) {
        q->user--;
        data_queue_release_user(q);
        _MUTEX_UNLOCK(q->lock);
        _MUTEX_UNLOCK(q->write_lock);
        return 0;
    }
    size += DATA_Q_ALLOC_HEAD_SIZE;
    if (get_available_size(q) >= size) {
        uint8_t *buffer = (uint8_t *) q->buffer + q->wp;
        *((int *) buffer) = size;
        if (size < 0 || size > q->size) {
            *(int*)0 = 0;
        }
        q->wp += size;
        q->filled += size;
        q->user--;
        data_queue_notify_data(q);
        data_queue_release_user(q);
        _MUTEX_UNLOCK(q->lock);
        _MUTEX_UNLOCK(q->write_lock);
        ret = 0;
    } else {
        q->user--;
        data_queue_release_user(q);
        printf("Release for avail %d\n", get_available_size(q));
        _MUTEX_UNLOCK(q->lock);
        _MUTEX_UNLOCK(q->write_lock);
    }
    return ret;
}

bool data_queue_have_data(data_queue_t *q)
{
    int has_data = false;
    if (q == NULL) {
        return has_data;
    }
    _MUTEX_LOCK(q->lock);
    if (!q->quit) {
        has_data = _data_queue_have_data(q);
    }
    _MUTEX_UNLOCK(q->lock);
    return has_data;
}

int data_queue_read_lock(data_queue_t *q, void **buffer, int *size)
{
    int ret = -1;
    if (q == NULL) {
        return -1;
    }
    _MUTEX_LOCK(q->lock);
    while (!q->quit) {
        if (_data_queue_have_data_from_last(q) == false) {
            if (data_queue_wait_data(q) != 0) {
                ret = -1;
                break;
            }
            continue;
        }
        int cur_rp;
        if (q->filled <= q->wp) {
           cur_rp = q->wp - q->filled;
        } else {
            cur_rp = q->wp + q->fill_end - q->filled;
        }
        uint8_t *data_buffer = (uint8_t *) q->buffer + cur_rp;
        int data_size = *((int *) data_buffer);
        if (data_size < 0 || data_size >q->size) {
            *(int*)0 = 0;
        }
        *buffer = data_buffer + DATA_Q_ALLOC_HEAD_SIZE;
        *size = data_size - DATA_Q_ALLOC_HEAD_SIZE;
        q->user++;
        ret = 0;
        break;
    }
    _MUTEX_UNLOCK(q->lock);
    return ret;
}

int data_queue_peek_unlock(data_queue_t *q)
{
    int ret = -1;
    if (q) {
        _MUTEX_LOCK(q->lock);
        q->user--;
        _MUTEX_UNLOCK(q->lock);
        return 0;
    }
    return ret;
}

int data_queue_read_unlock(data_queue_t *q)
{
    int ret = -1;
    if (q) {
        _MUTEX_LOCK(q->lock);
        if (_data_queue_have_data(q)) {
            uint8_t *buffer = (uint8_t *) q->buffer + q->rp;
            int size = *((int *) buffer);
            if (size < 0 || size >q->size) {
                *(int*)0 = 0;
            }
            q->rp += size;
            q->filled -= size;
            if (q->fill_end && q->rp >= q->fill_end) {
                q->fill_end = 0;
                q->rp = 0;
            }
            q->user--;
            data_queue_data_consumed(q);
            data_queue_release_user(q);
        }
        _MUTEX_UNLOCK(q->lock);
        return 0;
    }
    return ret;
}

int data_queue_query(data_queue_t *q, int *q_num, int *q_size)
{
    if (q) {
        _MUTEX_LOCK(q->lock);
        *q_num = *q_size = 0;
        if (_data_queue_have_data(q)) {
            int rp = q->rp;
            int ring = q->fill_end;
            while (rp != q->wp || ring) {
                int size = *(int *) (q->buffer + rp);
                if (size < 0 || size > q->size) {
                    *(int*)0 = 0;
                }
                rp += size;
                if (ring && rp == ring) {
                    ring = 0;
                    rp = 0;
                }
                (*q_num)++;
                (*q_size) += size - DATA_Q_ALLOC_HEAD_SIZE;
            }
        }
        _MUTEX_UNLOCK(q->lock);
    }
    return 0;
}
