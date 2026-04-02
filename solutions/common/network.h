/* Network

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Network connect callback
 */
typedef int (*network_connect_cb)(bool connected);

/**
 * @brief  Initialize network
 *
 * @param[in]  ssid      Wifi ssid
 * @param[in]  password  Wifi password
 * @param[in]  cb        Network connect callback
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to initialized
 */
int network_init(const char *ssid, const char *password, network_connect_cb cb);

/**
 * @brief  Get current network mac
 *
 * @param[out]  mac  Network mac to store
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to initialized
 */
int network_get_mac(uint8_t mac[6]);

/**
 * @brief  Check network connected or not
 *
 * @return
 *      - true   Network connected
 *      - false  Network disconnected
 */
bool network_is_connected(void);

/**
 * @brief  Connect wifi manually
 *
 * @param[in]  ssid      Wifi ssid
 * @param[in]  password  Wifi password
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to connect
 */
int network_connect_wifi(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
