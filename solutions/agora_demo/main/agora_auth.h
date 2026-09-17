#pragma once

#include <stddef.h>
int agora_auth_create_session(const char *channel, const char *string_uid,
                              char *url, size_t url_size,
                              char *token, size_t token_size);
