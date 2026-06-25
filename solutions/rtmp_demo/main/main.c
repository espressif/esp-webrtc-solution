/* RTMP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdbool.h>
#include <string.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "settings.h"
#include "common.h"
#include "esp_capture.h"
#include "rtmp.h"

static const char *TAG = "RTMP_Test";

static struct {
    struct arg_str *url;
    struct arg_end *end;
} rtmp_args;

static bool wifi_connected = false;

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

static const char *get_start_url(void)
{
    if (rtmp_args.url->count > 0) {
        return rtmp_args.url->sval[0];
    }
    return RTMP_PUSH_URL;
}

static int start(int argc, char **argv)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "Waiting for wifi connection");
        return 0;
    }
    int nerrors = arg_parse(argc, argv, (void **) &rtmp_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, rtmp_args.end, argv[0]);
        return 1;
    }
    return start_rtmp(get_start_url());
}

static int stop(int argc, char **argv)
{
    stop_rtmp();
    return 0;
}

static int sys(int argc, char **argv)
{
    sys_state_show();
    return 0;
}

static int wifi_cli(int argc, char **argv)
{
    if (argc < 2) {
        ESP_LOGW(TAG, "Usage: wifi <ssid> [password]");
        return -1;
    }
    char *ssid = argv[1];
    char *password = argc > 2 ? argv[2] : NULL;
    return network_connect_wifi(ssid, password);
}

static int init_console()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10 * 1024;
    repl_config.task_priority = 22;
    repl_config.max_cmdline_length = 1024;
    // install console REPL environment
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif

    rtmp_args.url = arg_str0(NULL, NULL, "<url>", "rtmp://host[:port]/app/stream");
    rtmp_args.end = arg_end(2);
    const esp_console_cmd_t cmd_rtmp_start = {
        .command = "start",
        .help = "Start RTMP push",
        .func = &start,
        .argtable = &rtmp_args,
    };
    esp_console_cmd_register(&cmd_rtmp_start);

    const esp_console_cmd_t cmd_rtmp_stop = {
        .command = "stop",
        .help = "Stop RTMP push",
        .func = &stop,
    };
    esp_console_cmd_register(&cmd_rtmp_stop);
    const esp_console_cmd_t sys_cmd = {
        .command = "i",
        .help = "Show system loadings",
        .func = &sys,
    };
    esp_console_cmd_register(&sys_cmd);
    const esp_console_cmd_t wifi_cmd = {
        .command = "wifi",
        .help = "wifi ssid psw",
        .func = &wifi_cli,
    };
    esp_console_cmd_register(&wifi_cmd);
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
#endif
    } else if (strcmp(thread_name, "aenc_0") == 0) {
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    } else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "rtmp_push") == 0) {
        schedule_cfg->stack_size = 8 * 1024;
        schedule_cfg->priority = 20;
        schedule_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "start") == 0) {
        schedule_cfg->stack_size = 6 * 1024;
    }
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    media_lib_thread_cfg_t cfg = {
        .stack_size = schedule_cfg->stack_size,
        .priority = schedule_cfg->priority,
        .core_id = schedule_cfg->core_id,
    };
    schedule_cfg->stack_in_ext = true;
    thread_scheduler(name, &cfg);
    schedule_cfg->stack_size = cfg.stack_size;
    schedule_cfg->priority = cfg.priority;
    schedule_cfg->core_id = cfg.core_id;
}

static int network_event_handler(bool connected)
{
    wifi_connected = connected;
    if (connected) {
        if (RTMP_PUSH_URL[0] == '\0') {
            ESP_LOGW(TAG, "RTMP_PUSH_URL is empty, use `start <url>` from console");
            return 0;
        }
        RUN_ASYNC(start, {
            if (start_rtmp(RTMP_PUSH_URL) != 0) {
                ESP_LOGE(TAG, "Fail to start RTMP push");
            }
        });
    } else {
        stop_rtmp();
    }
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);
    init_board();
    media_sys_buildup();
    init_console();
    network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler);
}
