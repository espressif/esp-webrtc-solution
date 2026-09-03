#pragma once

#include "esp_peer_signaling.h"

typedef struct {
    const char *bearer_token;
} agora_signaling_cfg_t;

const esp_peer_signaling_impl_t *agora_signaling_get_impl(void);
