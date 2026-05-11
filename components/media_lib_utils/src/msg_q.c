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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include "msg_q.h"

typedef struct msg_q_t {
   pthread_mutex_t data_mutex;
   pthread_cond_t  data_cond;
   void**          data;
   const char*     name;
   int             cur;
   int             each_size;
   int             number;
   int             filled;
   bool            quit;
   bool            reset;
   int             user;
} msg_q_t;

msg_q_handle_t msg_q_create(int msg_number, int msg_size) {
    msg_q_t* q = (msg_q_t*)calloc(1, sizeof(msg_q_t));
    if (msg_size == 0 || msg_number == 0) {
        return NULL;
    }
    if (q) {
        q->name = "";
        pthread_mutex_init(&(q->data_mutex), NULL);
        pthread_cond_init(&q->data_cond, NULL);
        q->data = (void**)malloc(sizeof(void*) * msg_number);
        for (int i = 0; i < msg_number; i++) {
            q->data[i] = malloc(msg_size);
        }
        q->number = msg_number;
        q->each_size = msg_size;
    }
    return q;
}

msg_q_handle_t msg_q_create_by_name(const char* name, int msg_size, int msg_number) {
    msg_q_t* q = (msg_q_t*)calloc(1, sizeof(msg_q_t));
    if (msg_size == 0 || msg_number == 0) {
        return NULL;
    }
    if (q) {
        q->name = name;
        pthread_mutex_init(&(q->data_mutex), NULL);
        pthread_cond_init(&q->data_cond, NULL);
        q->data = (void**)malloc(sizeof(void*) * msg_number);
        for (int i = 0; i < msg_number; i++) {
            q->data[i] = malloc(msg_size);
        }
        q->number = msg_number;
        q->each_size = msg_size;
    }
    return q;
}

int msg_q_wait_consume(msg_q_handle_t q) {
    int ret = -1;
    if (q) {
        pthread_mutex_lock(&(q->data_mutex));
        if (q->filled) {
            q->user++;
            pthread_cond_wait(&(q->data_cond), &(q->data_mutex));
            q->user--;
            if (q->quit == false && q->reset == false) {
                ret = 0;
            }
        }
        else {
            ret = 0;
        }
        pthread_mutex_unlock(&(q->data_mutex));
    }
    return ret;
}

int msg_q_send(msg_q_handle_t q, void* msg, int size) {
    if (q) {
        int ret = 0;
        if (size > q->each_size) {
            return -1;
        }
        pthread_mutex_lock(&(q->data_mutex));
        while (q->quit == false && q->filled >= q->number && q->reset == false) {
            q->user++;
            pthread_cond_wait(&(q->data_cond), &(q->data_mutex));
            q->user--;
        }
        if (q->reset) {
            q->reset = false;
        }
        if (q->quit == false && q->reset == false) {
            int idx = (q->cur + q->filled) % q->number;
            memcpy(q->data[idx], msg, size);
            q->filled++;
        }
        else {
            ret = -2;
        }
        pthread_mutex_unlock(&(q->data_mutex));
        //notify have data
        if (ret == 0) {
            pthread_cond_signal(&(q->data_cond));
        }
        return ret;
    }
    return -1;
}

int msg_q_recv(msg_q_handle_t q, void* msg, int size, bool no_wait) {
    if (q) {
        int ret = 0;
        if (size > q->each_size) {
            return -1;
        }
        pthread_mutex_lock(&(q->data_mutex));
        while (q->quit == false && q->filled == 0 && q->reset == false) {
            if (no_wait) {
                pthread_mutex_unlock(&(q->data_mutex));
                return 1;
            }
            q->user++;
            ret = pthread_cond_wait(&(q->data_cond), &(q->data_mutex));
            q->user--;
        }
        if (q->quit == false && q->reset == false) {
            memcpy(msg, q->data[q->cur], size);
            q->filled--;
            q->cur++;
            q->cur %= q->number;
        }
        else {
            if (q->reset) {
                q->reset = false;
            }
            ret = -2;
        }
        pthread_mutex_unlock(&(q->data_mutex));
        if (ret == 0) {
            pthread_cond_signal(&(q->data_cond));
        }
        return ret;
    }
    return -1;
}

int msg_q_add_user(msg_q_handle_t q, int dir) {
    if (q) {
        pthread_mutex_lock(&(q->data_mutex));
        if (dir) {
            q->user++;
        }
        else {
            q->user--;
        }
        pthread_mutex_unlock(&(q->data_mutex));
        return 0;
    }
    return -1;
}

int msg_q_reset(msg_q_handle_t q) {
    if (q) {
        while (q->user) {
            pthread_mutex_lock(&(q->data_mutex));
            q->reset = true;
            pthread_cond_broadcast(&(q->data_cond));
            pthread_mutex_unlock(&(q->data_mutex));
            usleep(2000);
        }
        pthread_mutex_lock(&(q->data_mutex));
        q->cur = 0;
        q->filled = 0;
        pthread_mutex_unlock(&(q->data_mutex));
    }
    return 0;
}

int msg_q_wakeup(msg_q_handle_t q) {
    if (q) {
        pthread_mutex_lock(&(q->data_mutex));
        q->reset = true;
        pthread_cond_signal(&(q->data_cond));
        pthread_mutex_unlock(&(q->data_mutex));
        while (q->user) {
            usleep(1000);
        }
        pthread_mutex_lock(&(q->data_mutex));
        q->reset = false;
        pthread_mutex_unlock(&(q->data_mutex));
    }
    return 0;
}

int msg_q_number(msg_q_handle_t q) {
   int n = 0;
   if (q) {
        pthread_mutex_lock(&(q->data_mutex));;
        n = q->filled;
        pthread_mutex_unlock(&(q->data_mutex));
    }
    return n;
}


void msg_q_destroy(msg_q_handle_t q) {
    if (q) {
        pthread_mutex_lock(&(q->data_mutex));
        q->quit = true;
        pthread_cond_signal(&(q->data_cond));
        pthread_mutex_unlock(&(q->data_mutex));
        while (q->user) {
            usleep(1000);
        }
        pthread_mutex_lock(&(q->data_mutex));
        pthread_mutex_unlock(&(q->data_mutex));

        pthread_mutex_destroy(&(q->data_mutex));
        pthread_cond_destroy(&(q->data_cond));
        int i;
        for (i = 0; i < q->number; i++) {
            free(q->data[i]);
        }
        free(q->data);
        free(q);
    }
}
