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
 * @brief  Message queue handle
 */
typedef struct msg_q_t* msg_q_handle_t;

/**
 * @brief  Create message queue
 *
 * @param[in]   msg_number The maximum number of messages in the queue
 * @param[out]  msg_size   Message size
 *
 * @return
 *       - NULL    No resource to create message queue
 *       - Others  Message queue handle
 *
 */
msg_q_handle_t msg_q_create(int msg_number, int msg_size);

/**
 * @brief  Send message to queue
 *
 * @param[in]   q     Message queue handle
 * @param[in]   msg   Message to be inserted into queue
 * @param[in]   size  Message size, need not larger than msg_size when created
 *
 * @return
 *       - 0    On success
 *       - -1   On failure
 *
 */
int msg_q_send(msg_q_handle_t q, void *msg, int size);

/**
 * @brief  Receive message from queue
 *
 * @param[in]   q        Message queue handle
 * @param[out]  msg      Message to be inserted into queue
 * @param[in]   size     Message size, need not larger than msg_size when created
 * @param[in]   no_wait  If true, return immediately if no message in queue
 *
 * @return
 *       - 0    On success
 *       - -1   On failure
 *       - 1    If no message in queue and no_wait is true
 *
 */
int msg_q_recv(msg_q_handle_t q, void *msg, int size, bool no_wait);

/**
 * @brief  Get items number in message queue
 *
 * @param[in]   q        Message queue handle
 *
 * @return
 *       - 0      No message in queue
 *       - Others Current queued items number
 *
 */
int msg_q_number(msg_q_handle_t q);

/**
 * @brief  Destroy message queue
 *
 * @param[in]  q  Message queue handle
 *
 */
void msg_q_destroy(msg_q_handle_t q);

#ifdef __cplusplus
}
#endif
