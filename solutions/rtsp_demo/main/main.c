/* RTSP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "settings.h"
#include "common.h"
#include "esp_capture.h"
#include "rtsp.h"

static const char *TAG = "RTSP_Test";

static struct {
    struct arg_str *mode;
    struct arg_str *uri;
    struct arg_end *end;
} rtsp_args;

static bool wifi_connected = false;

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

static int start(int argc, char **argv)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "Waiting for wifi connection");
        return 0;
    }
    int nerrors = arg_parse(argc, argv, (void **) &rtsp_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, rtsp_args.end, argv[0]);
        return 1;
    }
    int mode = atoi(rtsp_args.mode->sval[0]);
    const char *uri = rtsp_args.uri->sval[0];

    start_rtsp(mode, uri);
    return 0;
}

static int stop(int argc, char **argv)
{
    stop_rtsp();
    return 0;
}

static int sys(int argc, char **argv)
{
    sys_state_show();
    return 0;
}

static int init_console()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10*1024;
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

    rtsp_args.mode   = arg_str1(NULL, NULL, "<mode>", "0: Server 1: Push");
    rtsp_args.uri    = arg_str0(NULL, NULL, "<uri>", "rtsp://162.168.10.233:554/live");
    rtsp_args.end    = arg_end(3);
    const esp_console_cmd_t cmd_rtsp_start = {
        .command = "start",
        .help = "Start RTSP Server/Client\r\n",
        .func = &start,
        .argtable = &rtsp_args,
    };
    esp_console_cmd_register(&cmd_rtsp_start);

    const esp_console_cmd_t cmd_rtsp_stop = {
        .command = "stop",
        .help = "Stop RTSP Server/Client",
        .func = &stop,
    };
    esp_console_cmd_register(&cmd_rtsp_stop);
    const esp_console_cmd_t sys_cmd = {
        .command = "i",
        .help = "Show system loadings",
        .func = &sys,
    };
    esp_console_cmd_register(&sys_cmd);
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
    }
#ifdef WEBRTC_SUPPORT_OPUS
    else if (strcmp(thread_name, "aenc_0") == 0) {
        // For OPUS encoder it need huge stack, when use G711 can set it to small value
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
#endif
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "pc_task") == 0) {
        schedule_cfg->stack_size = 25 * 1024;
        schedule_cfg->priority = 18;
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
        // Enter into Room directly
        RUN_ASYNC(start, {
            if (start_rtsp(RTSP_SERVER, "rtsp://127.0.0.1:8554/live") != 0) {
                ESP_LOGE(TAG, "Fail to start RTSP server");
            }
        });
    } else {
        stop_rtsp();
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
