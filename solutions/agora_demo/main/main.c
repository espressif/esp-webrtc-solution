#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "agora_auth.h"
#include "common.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_capture.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "webrtc_utils_time.h"

#define TAG "AGORA_DEMO"
#define WIFI_RETRY_INTERVAL_US (5LL * 1000 * 1000)
#define WIFI_IP_TIMEOUT_US (15LL * 1000 * 1000)
#define SESSION_RETRY_INTERVAL_US (5LL * 1000 * 1000)

static char session_url[AGORA_DEMO_URL_SIZE];
static char session_token[AGORA_DEMO_TOKEN_SIZE];
static char session_channel[AGORA_DEMO_CHANNEL_SIZE];
static volatile bool auto_start = true;
static volatile bool session_starting;

static void thread_scheduler(const char *name, media_lib_thread_cfg_t *cfg)
{
    if (strcmp(name, "aenc_0") == 0) {
        cfg->stack_size = 40 * 1024;
        cfg->priority = 10;
        cfg->core_id = 1;
    } else if (strcmp(name, "AUD_SRC") == 0) {
        cfg->stack_size = 40 * 1024;
        cfg->priority = 15;
    } else if (strcmp(name, "Adec") == 0) {
        cfg->stack_size = 40 * 1024;
        cfg->priority = 15;
        cfg->core_id = 0;
    } else if (strcmp(name, "pc_task") == 0) {
        cfg->stack_size = 25 * 1024;
        cfg->priority = 18;
        cfg->core_id = 1;
    } else if (strcmp(name, "start") == 0) {
        cfg->stack_size = 16 * 1024;
    }
}

static void capture_scheduler(const char *name,
                              esp_capture_thread_schedule_cfg_t *cfg)
{
    media_lib_thread_cfg_t thread_cfg = {
        .stack_size = cfg->stack_size,
        .priority = cfg->priority,
        .core_id = cfg->core_id,
    };
    cfg->stack_in_ext = true;
    thread_scheduler(name, &thread_cfg);
    cfg->stack_size = thread_cfg.stack_size;
    cfg->priority = thread_cfg.priority;
    cfg->core_id = thread_cfg.core_id;
}

static uint32_t create_uid(void)
{
    uint32_t uid;
    do {
        uid = esp_random() & 0x7fffffffU;
    } while (uid == 0);
    return uid;
}

static int create_and_start_session(void)
{
    if (AGORA_DEMO_CHANNEL_NAME[0] == '\0') {
        ESP_LOGE(TAG, "Configure an Agora channel name");
        return -1;
    }
    int written = snprintf(session_channel, sizeof(session_channel), "%s",
                           AGORA_DEMO_CHANNEL_NAME);
    if (written < 0 || (size_t)written >= sizeof(session_channel)) {
        ESP_LOGE(TAG, "Configured channel name is too long");
        return -1;
    }

    static bool sntp_initialized;
    if (!sntp_initialized) {
        if (webrtc_utils_time_sync_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize SNTP");
            return -1;
        }
        sntp_initialized = true;
    }
    webrtc_utils_wait_for_time_sync(15000);
    if (time(NULL) < AGORA_DEMO_MIN_VALID_UNIX_TIME) {
        ESP_LOGE(TAG, "SNTP has not supplied a valid time");
        return -1;
    }

    uint32_t uid = create_uid();
    ESP_LOGI(TAG, "Starting channel=%s uid=%lu",
             session_channel, (unsigned long)uid);
    if (agora_auth_create_session(session_channel, uid,
                                  session_url, sizeof(session_url),
                                  session_token,
                                  sizeof(session_token)) != 0) {
        return -1;
    }
    return start_webrtc(session_url, session_token);
}

static void session_start_task(void *argument)
{
    (void)argument;
    int ret = create_and_start_session();
    if (ret != 0) {
        ESP_LOGW(TAG, "Duplex session start failed: %d", ret);
    }
    session_starting = false;
    media_lib_thread_destroy(NULL);
}

static bool start_session_async(void)
{
    if (session_starting || !network_is_connected()) {
        return false;
    }
    session_starting = true;
    int ret = media_lib_thread_create_from_scheduler(
        NULL, "start", session_start_task, NULL);
    if (ret != 0) {
        session_starting = false;
        ESP_LOGE(TAG, "Failed to create session task: %d", ret);
        return false;
    }
    return true;
}

static int start_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    auto_start = true;
    stop_webrtc();
    return start_session_async() ? 0 : -1;
}

static int stop_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    auto_start = false;
    return stop_webrtc();
}

static int info_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sys_state_show();
    return 0;
}

static int wifi_command(int argc, char **argv)
{
    if (argc < 2) {
        ESP_LOGE(TAG, "Usage: wifi <ssid> [password]");
        return -1;
    }
    return network_connect_wifi(argv[1], argc > 2 ? argv[2] : NULL);
}

static int init_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg =
        ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "agora>";
    repl_cfg.task_stack_size = 10 * 1024;
    repl_cfg.task_priority = 22;
    repl_cfg.max_cmdline_length = 1024;

#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t device_cfg =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&device_cfg, &repl_cfg, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t device_cfg =
        ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&device_cfg, &repl_cfg,
                                                 &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t device_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(
        &device_cfg, &repl_cfg, &repl));
#endif

    const esp_console_cmd_t commands[] = {
        {.command = "start", .help = "Start a fresh duplex session",
         .func = start_command},
        {.command = "stop", .help = "Stop automatic session handling",
         .func = stop_command},
        {.command = "i", .help = "Show system status",
         .func = info_command},
        {.command = "wifi", .help = "wifi <ssid> [password]",
         .func = wifi_command},
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

static int network_event_handler(bool connected)
{
    ESP_LOGI(TAG, "Network %s", connected ? "connected" : "disconnected");
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);

    if (init_board() != 0 || media_sys_buildup() != 0) {
        ESP_LOGE(TAG, "Board or audio initialization failed");
        return;
    }
    init_console();
    network_init(AGORA_DEMO_WIFI_SSID, AGORA_DEMO_WIFI_PASSWORD,
                 network_event_handler);

    int64_t next_wifi_retry_us =
        esp_timer_get_time() + WIFI_RETRY_INTERVAL_US;
    int64_t next_session_retry_us = 0;
    int64_t associated_since_us = 0;

    while (true) {
        media_lib_thread_sleep(2000);
        int64_t now_us = esp_timer_get_time();
        if (!network_is_connected()) {
            if (!session_starting) {
                stop_webrtc();
            }
            next_session_retry_us = 0;
            if (now_us >= next_wifi_retry_us) {
                next_wifi_retry_us = now_us + WIFI_RETRY_INTERVAL_US;
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    if (associated_since_us == 0) {
                        associated_since_us = now_us;
                    } else if (now_us - associated_since_us >=
                               WIFI_IP_TIMEOUT_US) {
                        ESP_LOGW(TAG, "Wi-Fi has no IP; reconnecting");
                        esp_wifi_disconnect();
                        associated_since_us = 0;
                    }
                } else {
                    associated_since_us = 0;
                    esp_err_t ret = esp_wifi_connect();
                    ESP_LOGW(TAG, "Wi-Fi reconnect: %s",
                             esp_err_to_name(ret));
                }
            }
            continue;
        }

        associated_since_us = 0;
        next_wifi_retry_us = now_us + WIFI_RETRY_INTERVAL_US;
        query_webrtc();
        if (auto_start && !session_starting && webrtc_needs_restart() &&
            now_us >= next_session_retry_us) {
            stop_webrtc();
            start_session_async();
            next_session_retry_us =
                now_us + SESSION_RETRY_INTERVAL_US;
        }
    }
}
