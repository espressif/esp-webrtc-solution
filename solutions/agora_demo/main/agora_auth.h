#pragma once

#include <stddef.h>
#include <stdint.h>

int agora_auth_create_session(const char *channel, uint32_t uid,
                              char *url, size_t url_size,
                              char *token, size_t token_size);
