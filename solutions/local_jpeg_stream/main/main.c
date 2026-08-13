/* Local JPEG Stream Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_webrtc.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "webrtc_utils_time.h"
#include "esp_cpu.h"
#include "settings.h"
#include "media_lib_netif.h"
#include "common.h"
#include "esp_capture.h"
#include "esp_heap_caps.h"
#include "cipher_bench.h"
#include "esp_peer_default.h"

static const char *TAG = "JPEG_Stream";

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

static void log_mem(const char *stage)
{
    ESP_LOGI(TAG, "[%s] MEM Avail:%d, IRam:%d, PSRam:%d",
             stage,
             (int)esp_get_free_heap_size(),
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static int start_cli(int argc, char **argv)
{
    start_webrtc(NULL);
    return 0;
}

static int stop_cli(int argc, char **argv)
{
    RUN_ASYNC(leave, { stop_webrtc(); });
    return 0;
}

static int cmd_cli(int argc, char **argv)
{
    send_cmd(argc > 1 ? argv[1] : "ring");
    return 0;
}

static int assert_cli(int argc, char **argv)
{
    *(int *)0 = 0;
    return 0;
}

static int sys_cli(int argc, char **argv)
{
    // Same as other solutions: memory + task stats
    sys_state_show();
    query_webrtc();
    return 0;
}

static int wifi_cli(int argc, char **argv)
{
    if (argc < 1) {
        return -1;
    }
    char *ssid = argv[1];
    char *password = argc > 2 ? argv[2] : NULL;
    return network_connect_wifi(ssid, password);
}

static int capture_to_player_cli(int argc, char **argv)
{
    return test_capture_to_player();
}

static int measure_cli(int argc, char **argv)
{
    void measure_enable(bool enable);
    measure_enable(true);
    media_lib_thread_sleep(1500);
    measure_enable(false);
    return 0;
}

static int cipher_cli(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return cipher_bench_run();
}

static int dtls_cli(int argc, char **argv)
{
    if (argc < 2) {
        ESP_LOGI(TAG, "Usage: dtls <auto|aes|chacha> then stop/reconnect browser");
        return 0;
    }
    esp_peer_dtls_cipher_pref_t pref;
    if (strcmp(argv[1], "auto") == 0) {
        pref = ESP_PEER_DTLS_CIPHER_AUTO;
    } else if (strcmp(argv[1], "aes") == 0) {
        pref = ESP_PEER_DTLS_CIPHER_AES_GCM;
    } else if (strcmp(argv[1], "chacha") == 0) {
        pref = ESP_PEER_DTLS_CIPHER_CHACHA;
    } else {
        ESP_LOGE(TAG, "Unknown pref '%s' (use auto|aes|chacha)", argv[1]);
        return -1;
    }
    if (esp_peer_set_dtls_cipher_pref(pref) != 0) {
        return -1;
    }
    ESP_LOGI(TAG, "DTLS cipher pref set to %s — stop/reconnect to renegotiate", argv[1]);
    return 0;
}

static int init_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10 * 1024;
    repl_config.task_priority = 22;
    repl_config.max_cmdline_length = 1024;
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

    esp_console_cmd_t cmds[] = {
        {
            .command = "start",
            .help = "Start signaling and webrtc.\r\n",
            .func = start_cli,
        },
        {
            .command = "stop",
            .help = "Stop signaling and webrtc.\n",
            .func = stop_cli,
        },
        {
            .command = "cmd",
            .help = "Send command (ring/accept/deny)\n",
            .func = cmd_cli,
        },
        {
            .command = "i",
            .help = "Show memory and system status\r\n",
            .func = sys_cli,
        },
        {
            .command = "assert",
            .help = "Assert system\r\n",
            .func = assert_cli,
        },
        {
            .command = "rec2play",
            .help = "Play capture content\n",
            .func = capture_to_player_cli,
        },
        {
            .command = "wifi",
            .help = "wifi ssid psw\r\n",
            .func = wifi_cli,
        },
        {
            .command = "m",
            .help = "measure system loading\r\n",
            .func = measure_cli,
        },
        {
            .command = "cipher",
            .help = "Bench AES-GCM vs ChaCha20-Poly1305 (measure_start/stop + MB/s)\r\n",
            .func = cipher_cli,
        },
        {
            .command = "dtls",
            .help = "dtls [auto|aes|chacha] — set DTLS cipher pref for next handshake\r\n",
            .func = dtls_cli,
        },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
#endif
    } else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "pc_task") == 0) {
        schedule_cfg->stack_size = 25 * 1024;
        schedule_cfg->priority = 18;
        schedule_cfg->core_id = 1;
    } else if (strcmp(thread_name, "pc_send") == 0) {
        schedule_cfg->stack_size = 8 * 1024;
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "start") == 0) {
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

static char *get_network_ip(void)
{
    media_lib_ipv4_info_t ip_info;
    media_lib_netif_get_ipv4_info(MEDIA_LIB_NET_TYPE_STA, &ip_info);
    return media_lib_ipv4_ntoa(&ip_info.ip);
}

static int network_event_handler(bool connected)
{
    if (connected) {
        RUN_ASYNC(start, {
            log_mem("before start_webrtc");
            int ret = start_webrtc(NULL);
            if (ret == 0) {
                log_mem("server ready");
                // Stop heap leak tracing started in app_main and dump records
                sys_state_heap_trace(false);
                ESP_LOGI(TAG, "Use browser to enter https://%s/webrtc/test for JPEG stream test", get_network_ip());
            } else {
                log_mem("start_webrtc failed");
                sys_state_heap_trace(false);
            }
        });
    } else {
        stop_webrtc();
    }
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
#if CONFIG_IDF_TARGET_ESP32S31
    /* S31 has partial hardware GCM and is ~7x faster than software ChaCha
     * with internal DMA buffers. User can still override using `dtls chacha`. */
    esp_peer_set_dtls_cipher_pref(ESP_PEER_DTLS_CIPHER_AES_GCM);
#endif
    // Trace allocations from boot path until HTTP/WebRTC server is ready
    // sys_state_heap_trace(true);
    log_mem("app_main enter");

    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);

    init_board();
    log_mem("after init_board");

    media_sys_buildup();
    log_mem("after media_sys_buildup");

    init_console();
    log_mem("after init_console");

    network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler);
    log_mem("after network_init");

    while (1) {
        media_lib_thread_sleep(2000);
        query_webrtc();
    }
}
