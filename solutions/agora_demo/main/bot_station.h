#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define BOT_STATION_AGENT_ID_SIZE        128
#define BOT_STATION_APP_ID_SIZE          128
#define BOT_STATION_CHANNEL_SIZE         128
#define BOT_STATION_CONVERSATION_ID_SIZE 128
#define BOT_STATION_UID_SIZE             65

typedef struct {
    char conversation_id[BOT_STATION_CONVERSATION_ID_SIZE];
    char agent_id[BOT_STATION_AGENT_ID_SIZE];
    char agent_uid[BOT_STATION_UID_SIZE];
    char app_id[BOT_STATION_APP_ID_SIZE];
    char channel[BOT_STATION_CHANNEL_SIZE];
    char uid[BOT_STATION_UID_SIZE];
} bot_station_conversation_t;

esp_err_t bot_station_init(void);
void bot_station_prepare_start(void);
void bot_station_cancel(void);
esp_err_t bot_station_start_conversation(
    bot_station_conversation_t *conversation);
esp_err_t bot_station_stop_conversation(const char *reason);
bool bot_station_has_active_conversation(void);
bool bot_station_binding_check_due(void);
esp_err_t bot_station_validate_active_binding(bool *valid);
esp_err_t bot_station_clear_binding(void);
