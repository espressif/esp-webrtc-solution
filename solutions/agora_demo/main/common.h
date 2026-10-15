#pragma once

#include <stdbool.h>

#include "media_sys.h"
#include "network.h"
#include "settings.h"
#include "sys_state.h"

int init_board(void);
int start_webrtc(const char *url, const char *token);
void query_webrtc(void);
void media_sys_query(void);
bool webrtc_needs_restart(void);
int stop_webrtc(void);
