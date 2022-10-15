/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */

#ifndef _GLOBAL_H
#define _GLOBAL_H

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_now.h"
#include "esp_crc.h"

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA

// #define ESPNOW_WIFI_MODE WIFI_MODE_AP
// #define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP

#define ESPNOW_MAXDELAY 512
// #define LEN_ESPNOW_ETH_MAC 6

#define ESPNOW_QUEUE_SIZE 2

#define MAX_LEN_ESPNOW_SEND_RECV 120

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, s_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

#define CONFIG_ESPNOW_PMK "pmk1234567890123"
#define CONFIG_ESPNOW_LMK "lmk1234567890123"
#define CONFIG_ESPNOW_CHANNEL 1
#define CONFIG_MY_ESPNOW_SEND_COUNT 100

#define UART_BUF_SIZE (1024)
#define RD_BUF_SIZE (UART_BUF_SIZE)
static QueueHandle_t uart1_queue;

typedef enum
{
    MYAPP_ESPNOW_SEND_CB,
    MYAPP_ESPNOW_RECV_CB,
} espnow_event_id_t;

typedef enum
{
    MSG_TYPE_SCAN, // 0
    MSG_TYPE_DATA, // 1
    MSG_TYPE_END,
} espnow_msg_type_t;

typedef struct
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_event_send_cb_t;

typedef struct
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_event_recv_cb_t;

typedef union
{
    espnow_event_send_cb_t send_cb;
    espnow_event_recv_cb_t recv_cb;
} espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct
{
    espnow_event_id_t id;
    espnow_event_info_t info;
} espnow_event_t;

enum
{
    ESPNOW_DATA_BROADCAST = 0, // 0
    ESPNOW_DATA_UNICAST,       // 1
    ESPNOW_DATA_MAX,
};

typedef struct
{
    uint8_t type;     // Broadcast or unicast ESPNOW data.
    uint8_t state;    // Indicate that if has received broadcast ESPNOW data or not.
    uint16_t seq_num; // Sequence number of ESPNOW data.
    uint16_t crc;     // CRC16 value of ESPNOW data.
} __attribute__((packed)) espnow_data_t;

typedef struct
{
    uint8_t type;                       // 0- broadcaset;1-unicast
    uint8_t state;                      // Indicate that if has received broadcast ESPNOW data or not.
    uint16_t count;                     // Total count of unicast ESPNOW data to be sent.
    uint16_t delay;                     // Delay between sending two ESPNOW data, unit: ms.
    int len;                            // Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                    // Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN]; // MAC address of destination device.
} espnow_send_param_t;

/* User defined field of ESPNOW data in this example. */
// typedef struct
// {
//     uint8_t type;       // Broadcast or unicast ESPNOW data.
//     uint8_t state;      // Indicate that if has received broadcast ESPNOW data or not.
//     uint16_t seq_num;   // Sequence number of ESPNOW data.
//     uint16_t crc;       // CRC16 value of ESPNOW data.
//     uint32_t magic;     // Magic number which is used to determine which device to send unicast ESPNOW data.
//     uint8_t payload[0]; // Real payload of ESPNOW data.
// } __attribute__((packed)) espnow_data_t;

/* Parameters of sending ESPNOW data. */
// typedef struct
// {
//     bool unicast;                         // Send unicast ESPNOW data.
//     bool broadcast;                       // Send broadcast ESPNOW data.
//     uint8_t state;                        // Indicate that if has received broadcast ESPNOW data or not.
//     uint32_t magic;                       // Magic number which is used to determine which device to send unicast ESPNOW data.
//     uint16_t count;                       // Total count of unicast ESPNOW data to be sent.
//     uint16_t delay;                       // Delay between sending two ESPNOW data, unit: ms.
//     int len;                              // Length of ESPNOW data to be sent, unit: byte.
//     uint8_t *buffer;                      // Buffer pointing to ESPNOW data.
//     uint8_t dest_mac[ESP_NOW_ETH_ALEN]; // MAC address of destination device.
// } espnow_send_param_t;

// typedef struct
// {
//     uint8_t type;                       // 0- broadcaset;1-unicast
//     uint8_t serno;                      // for mult package count >1
//     uint8_t state;                      // Indicate that if has received broadcast ESPNOW data or not.
//     uint32_t magic;                     // Magic number which is used to determine which device to send unicast ESPNOW data.
//     uint16_t count;                     // Total count of unicast ESPNOW data to be sent.
//     uint16_t delay;                     // Delay between sending two ESPNOW data, unit: ms.
//     int len;                            // Length of ESPNOW data to be sent, unit: byte.
//     uint16_t totalsize;                 // total size to send
//     uint8_t *buffer;                    // Buffer pointing to ESPNOW data.
//     uint8_t dest_mac[ESP_NOW_ETH_ALEN]; // MAC address of destination device.
// } espnow_send_param_t;

#endif