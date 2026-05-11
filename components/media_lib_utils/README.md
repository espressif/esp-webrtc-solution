# Media Library Utilities

Utilities that help you build cross-platform applications on **media_lib_sal**.

## Highlights

**`data_queue`** — **Copy-free ring FIFO**: one backing buffer; producer and consumer get **direct pointers** into the ring.

**`msg_q`** — Fixed-depth queue of fixed-size messages; send/recv with optional no-wait.

## Usage

Added dependency in `idf_component.yml`:
```yaml
dependencies:
  espressif/media_lib_utils:
    version: "~0.9"
```

Sample usage (call order):

**Data queue** — producer and consumer each hold a pointer into the ring; no extra memcpy inside the queue for that path.

```c
#include "data_queue.h"
#include <string.h>

/* Stage 1: create backing ring */
data_queue_t *q = data_queue_init(64 * 1024);

/* Stage 2 and 3 run on different threads; order here is for reading only */

/* Stage 2: producer (own thread/task) */
for (;;) {
    int   want = 4096;
    int   got;
    void *wr;

    /* Reserve contiguous write span; blocks when ring is full */
    wr = data_queue_get_buffer(q, want);
    if (!wr) {
        /* Shutdown path or allocation failure */
        break;
    }

    /* Write payload into wr .. (read, memcpy, encoder output, etc.) */
    got = want;

    /* Hand chunk to reader side of the FIFO */
    data_queue_send_buffer(q, got);
}

/* Stage 3: consumer (own thread/task) */
for (;;) {
    void *rd = NULL;
    int   sz = 0;

    /* Lock front chunk; blocks until data exists or queue tears down */
    if (data_queue_read_lock(q, &rd, &sz) != 0) {
        /* No data / error / closing */
        break;
    }

    /* Use rd[0 .. sz) in place (no extra copy out of the ring) */
    process(rd, sz);

    /* Release chunk so producer can reclaim space */
    data_queue_read_unlock(q);
}

/* Stage 4: shutdown */
/* Wake blocked producer/consumer if you stop out-of-band */
/* data_queue_wakeup(q); */

/* Free mutex, events, and backing buffer */
data_queue_deinit(q);
```

**Message queue** — fixed slots; each send size must be ≤ `msg_size` from create.

```c
#include "msg_q.h"
#include <stdint.h>
#include <string.h>

enum {
    MSG_MAX  = 16,
    MSG_SIZE = 256,
};

/* Stage 1: create slot pool */
msg_q_handle_t mq = msg_q_create(MSG_MAX, MSG_SIZE);

/* Stage 2: enqueue */
uint8_t in[MSG_SIZE];
int     n;

memset(in, 0, sizeof(in));
/* Application fills in[0 .. n) */
n = 32;

/* Block while all slots are busy */
msg_q_send(mq, in, n);

/* Stage 3: dequeue */
uint8_t out[MSG_SIZE];
int      r;

/* false = wait for a message; true = no_wait (r==1 if empty) */
r = msg_q_recv(mq, out, sizeof(out), false);
if (r == 0) {
    /* Valid message in out */
}
if (r == 1) {
    /* Only when no_wait: queue was empty */
}

/* Stage 4: status and teardown */
int pending = msg_q_number(mq);

msg_q_destroy(mq);
```

APIs are documented in the headers. More on SAL: [media_lib_sal README](https://github.com/espressif/esp-webrtc-solution/tree/main/components/media_lib_sal/README.md).
