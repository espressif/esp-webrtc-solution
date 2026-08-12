/* Network

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <string.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_event.h"
#include "network.h"

#ifdef CONFIG_NETWORK_USE_ETHERNET
#include "esp_eth.h"
#include "ethernet_init.h"
#else
#include <esp_wifi.h>
#endif

#define TAG "NETWORK"

static bool               network_connected = false;
static network_connect_cb connect_cb;

static void network_set_connected(bool connected)
{
    if (network_connected != connected) {
        network_connected = connected;
        if (connect_cb) {
            connect_cb(connected);
        }
    }
}

bool network_is_connected(void)
{
    return network_connected;
}

#ifndef CONFIG_NETWORK_USE_ETHERNET

static wifi_config_t wifi_config;

#define PART_NAME     "wifi-set"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PSW_KEY  "psw"

static bool load_from_nvs(void)
{
    nvs_handle_t wifi_nvs = 0;
    bool load_ok = false;
    do {
        esp_err_t ret = nvs_open(PART_NAME, NVS_READWRITE, &wifi_nvs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fail to open nvs ret %d", ret);
            break;
        }
        size_t size = sizeof(wifi_config.sta.ssid);
        ret = nvs_get_str(wifi_nvs, WIFI_SSID_KEY, (char*)(wifi_config.sta.ssid), &size);
        if (ret != ESP_OK) {
            break;
        }
        wifi_config.sta.ssid[size] = '\0';
        size = sizeof(wifi_config.sta.password);
        ret = nvs_get_str(wifi_nvs, WIFI_PSW_KEY, (char*)(wifi_config.sta.password), &size);
        if (ret != ESP_OK) {
            break;
        }
        wifi_config.sta.password[size] = '\0';
        load_ok = true;
    } while (0);
    if (wifi_nvs) {
        nvs_close(wifi_nvs);
    }
    return load_ok;
}

static void store_to_nvs(void)
{
    nvs_handle_t wifi_nvs = 0;
    do {
        esp_err_t ret = nvs_open(PART_NAME, NVS_READWRITE, &wifi_nvs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fail to open nvs ret %d", ret);
            break;
        }
        ret = nvs_set_str(wifi_nvs, WIFI_SSID_KEY, (char*)(wifi_config.sta.ssid));
        if (ret != ESP_OK) {
            break;
        }
        ret = nvs_set_str(wifi_nvs, WIFI_PSW_KEY, (char*)(wifi_config.sta.password));
        if (ret != ESP_OK) {
            break;
        }
    } while (0);
    if (wifi_nvs) {
        nvs_close(wifi_nvs);
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        network_set_connected(false);
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        network_set_connected(true);
        store_to_nvs();
    }
}

int network_init(const char *ssid, const char *password, network_connect_cb cb)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if (load_from_nvs()) {
        ESP_LOGI(TAG, "Force to use wifi config from nvs");
    } else {
        if (ssid) {
            memcpy(wifi_config.sta.ssid, ssid, strlen(ssid) + 1);
        }
        if (password) {
            memcpy(wifi_config.sta.password, password, strlen(password) + 1);
        }
    }
    connect_cb = cb;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return 0;
}

int network_get_mac(uint8_t mac[6])
{
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    return 0;
}

int network_connect_wifi(const char *ssid, const char *password)
{
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if (ssid) {
        memcpy(wifi_config.sta.ssid, ssid, strlen(ssid) + 1);
    }
    if (password) {
        memcpy(wifi_config.sta.password, password, strlen(password) + 1);
    }
    network_connected = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    return 0;
}

#else
static esp_eth_handle_t *eth_handles = NULL;
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = { 0 };
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Ethernet Link Up");
            ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            network_set_connected(false);
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    network_set_connected(true);
}

int network_init(const char *ssid, const char *password, network_connect_cb cb)
{
    // Initialize Ethernet driver
    uint8_t eth_port_cnt = 0;
    ESP_ERROR_CHECK(ethernet_init_all(&eth_handles, &eth_port_cnt));

    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1) {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    } else {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
        // esp-netif configuration parameters for each interface (name, priority, etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        for (int i = 0; i < eth_port_cnt; i++) {
            itoa(i, num_str, 10);
            strcat(strcpy(if_key_str, "ETH_"), num_str);
            strcat(strcpy(if_desc_str, "eth"), num_str);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i * 5;
            esp_netif_t *eth_netif = esp_netif_new(&cfg_spi);

            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[i])));
        }
    }

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
    connect_cb = cb;
    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }
    return 0;
}

int network_get_mac(uint8_t mac[6])
{
    if (eth_handles) {
        esp_eth_ioctl(eth_handles[0], ETH_CMD_G_MAC_ADDR, mac);
    }
    return 0;
}

int network_connect_wifi(const char *ssid, const char *password)
{
    ESP_LOGE(TAG, "Using ethernet now not support wifi config");
    return 0;
}

#endif
