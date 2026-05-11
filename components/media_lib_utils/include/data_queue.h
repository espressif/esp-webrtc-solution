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

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Struct for data queue
 *        Data queue works like a queue, you can receive the exact size of data as you send previously.
 *        It allows you to get continuous buffer so that no need to care ring back issue.
 *        It adds a fill_end member to record fifo write end position before ring back.
 */
typedef struct {
    void *buffer;     /*!< Buffer for queue */
    int   size;       /*!< Buffer size */
    int   fill_end;   /*!< Buffer write position before ring back */
    int   wp;         /*!< Write pointer */
    int   rp;         /*!< Read pointer */
    int   filled;     /*!< Buffer filled size */
    int   user;       /*!< Buffer reference by reader or writer */
    int   quit;       /*!< Buffer quit flag */
    void *lock;       /*!< Protect lock */
    void *write_lock; /*!< Write lock to let only one writer at same time */
    void *event;      /*!< Event group to wake up reader or writer */
} data_queue_t;

/**
 * @brief         Initialize data queue
 *
 * @param         size: Buffer size
 * @return        - NULL: Fail to initialize queue
 *                - Others: Data queue instance
 */
data_queue_t *data_queue_init(int size);

/**
 * @brief         Wakeup thread which wait on queue data
 *
 * @param         q: Data queue instance
 */
void data_queue_wakeup(data_queue_t *q);

/**
 * @brief         Deinitialize data queue
 *
 * @param         q: Data queue instance
 */
void data_queue_deinit(data_queue_t *q);

/**
 * @brief         Get continuous buffer from data queue
 *
 * @param         q: Data queue instance
 * @param         size: Buffer size want to get
 * @return        - NULL: Fail to get buffer
 *                - Others: Buffer data
 */
void *data_queue_get_buffer(data_queue_t *q, int size);

/**
 * @brief         Get data pointer being written but not send yet
 *
 * @param         q: Data queue instance
 * @return        - NULL: Fail to get write buffer
 *                - Others: Write data pointer
 */
void *data_queue_get_write_data(data_queue_t *q);

/**
 * @brief         Send data into data queue
 *
 * @param         q: Data queue instance
 * @param         size: Buffer size want to get
 * @return        - 0: On success
 *                - Others: Fail to send buffer
 */
int data_queue_send_buffer(data_queue_t *q, int size);

/**
 * @brief         Read data from data queue, and add reference count
 *
 * @param         q: Data queue instance
 * @param[out]    buffer: Buffer in front of queue, this buffer is always valid before call `data_queue_read_unlock`
 * @param[out]    size: Buffer size in front of queue
 * @return        - 0: On success
 *                - Others: Fail to read buffer
 */
int data_queue_read_lock(data_queue_t *q, void **buffer, int *size);

/**
 * @brief         Release data be read and decrease reference count
 *
 * @param         q: Data queue instance
 * @param[out]    buffer: Buffer in front of queue
 * @param[out]    size: Buffer size in front of queue
 * @return        - 0: On success
 *                - Others: Fail to read buffer
 */
int data_queue_read_unlock(data_queue_t *q);

/**
 * @brief         Peak data unlock, call `data_queue_read_lock` to read data with block
 *                After peek data, not consume the data and release the lock
 *
 * @param         q: Data queue instance
 * @return        - 0: On success
 *                - Others: Fail to read buffer
 */
int data_queue_peek_unlock(data_queue_t *q);

/**
 * @brief         Consume all data in queue
 *
 * @param         q: Data queue instance
 * @return        - 0: On success
 *                - Others: Fail to read buffer
 */
int data_queue_consume_all(data_queue_t *q);

/**
 * @brief         Check whether there are filled data in queue
 *
 * @param         q: Data queue instance
 * @return        - true: Have data in queue
 *                - false: Empty, no buffer filled
 */
bool data_queue_have_data(data_queue_t *q);

/**
 * @brief         Query data queue information
 *
 * @param         q: Data queue instance
 * @param[out]    q_num: Data block number in queue
 * @param[out]    q_size: Total data size kept in queue
 * @return        - 0: On success
 *                - Others: Fail to query
 */
int data_queue_query(data_queue_t *q, int *q_num, int *q_size);

/**
 * @brief         Query available data size
 *
 * @param         q: Data queue instance
 * @return        Available size for write queue
 */
int data_queue_get_available(data_queue_t *q);

#ifdef __cplusplus
}
#endif
