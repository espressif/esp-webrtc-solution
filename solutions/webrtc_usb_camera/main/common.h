#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "settings.h"
#include "network.h"
#include "sys_state.h"

int start_webrtc(char *url);
void query_webrtc(void);
int stop_webrtc(void);

#ifdef __cplusplus
}
#endif
