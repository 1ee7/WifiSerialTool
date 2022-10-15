#include "global.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "espnow_serial";

#define PATTERN_CHR_NUM (3)

int8_t iwork_state = -1; //-1初始状态，0-广播状态，1-获取到dest mac,2-检测到对方结束广播信息

static xQueueHandle s_example_espnow_queue;

static uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t s_dest_mac[ESP_NOW_ETH_ALEN] = {0};
static uint16_t s_espnow_seq[ESPNOW_DATA_MAX] = {0, 0};

xSemaphoreHandle g_sem_task_serial_ready;

static void myapp_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    espnow_event_t evt;
    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL)
    {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    if (iwork_state == 3)
        return;

    evt.id = MYAPP_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE)
    {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void myapp_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

    if (mac_addr == NULL || data == NULL || len <= 0)
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (iwork_state == 3)
    {
        uart_write_bytes(UART_NUM_1, (const char *)data, len);

        return;
    }

    evt.id = MYAPP_ESPNOW_RECV_CB;
    if (iwork_state == 2)
    {
        if (memcmp(mac_addr, s_dest_mac, ESP_NOW_ETH_ALEN) != 0)
        {
            printf("\n have already match one , another id defined !\n");
            return;
        }
    }

    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);

    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL)
    {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE)
    {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* WiFi should start before using ESPNOW */
static void myapp_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
}

void scan_espnow_data_prepare(espnow_send_param_t *send_param, uint8_t status)
{
    espnow_data_t *buf = (espnow_data_t *)send_param->buffer;

    assert(send_param->len >= sizeof(espnow_data_t));

    buf->type = MSG_TYPE_SCAN;
    buf->state = status;

    if (status == 0 || status == 1)
        buf->seq_num = 0;

    else if (status == 2)
    {
        buf->seq_num = s_espnow_seq[buf->type]++;
    }

    buf->crc = 0;

    /* Fill all remaining bytes after the data with random values */

    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len - sizeof(buf->crc));
}

/* Parse received ESPNOW data. */
int example_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq /*, int *magic*/)
{
    espnow_data_t *buf = (espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t))
    {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    // *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len - sizeof(buf->crc));

    if (crc_cal == crc)
    {
        return buf->type;
    }

    return -1;
}
static void my_espnow_deinit(espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(s_example_espnow_queue);
    esp_now_deinit();
}
static void my_espnow_task(void *pvParameter)
{
    espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    int recv_magic = 0;
    bool is_broadcast = false;
    int ret;

    vTaskDelay(5000 / portTICK_RATE_MS);
    ESP_LOGI(TAG, "Start sending broadcast data");

    /* Start sending broadcast ESPNOW data. */
    espnow_send_param_t *send_param = (espnow_send_param_t *)pvParameter;
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK)
    {
        ESP_LOGE(TAG, "Send error");
        my_espnow_deinit(send_param);
        vTaskDelete(NULL);
    }

    uint8_t mCount = send_param->count - 1;
    printf("\n mCount is %d\n", mCount);

    while (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE)
    {
        switch (evt.id)
        {
        case MYAPP_ESPNOW_SEND_CB:
        {
            espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
            is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

            ESP_LOGD(TAG, "Send data to " MACSTR ", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

            printf("\n===================ESPNOW_SEND_CB=============================\n");
            printf("iwork_state:%d,is broadcast:%d,send_status:%d ,send_count:%d\n", iwork_state, is_broadcast, send_cb->status, send_param->count);
            printf("\n==================ESPNOW_SEND_CB============================\n");

            if (iwork_state == 0 /*&& is_broadcast&& (send_cb->status)*/) //状态0 广播状态  TODO 延时
            {
                vTaskDelay(send_param->delay / portTICK_RATE_MS);

                memcpy(send_param->dest_mac, s_broadcast_mac, ESP_NOW_ETH_ALEN);
                scan_espnow_data_prepare(send_param, 0);

                esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);
            }
            else if (iwork_state == 1 /*&& !is_broadcast && (send_cb->status)*/) //状态1 获取到mac地址
            {
                vTaskDelay(send_param->delay / portTICK_RATE_MS);
                memcpy(send_param->dest_mac, s_dest_mac, ESP_NOW_ETH_ALEN);
                scan_espnow_data_prepare(send_param, 1);
                esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);
            }
            else if (iwork_state == 2 /*&& (send_cb->status)*/ && send_param->count != 0) //状态2 获取到mac地址&& 收到对方的结束信息
            {
                vTaskDelay(send_param->delay / portTICK_RATE_MS);
                send_param->count--;
                memcpy(send_param->dest_mac, s_dest_mac, ESP_NOW_ETH_ALEN);
                scan_espnow_data_prepare(send_param, 2);
                esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);
            }
            else if (iwork_state == 3)
            {

                goto FINISH;
                // break;
            }

            ESP_LOGI(TAG, "send data to " MACSTR "", MAC2STR(send_cb->mac_addr));
            /* Send the next data after the previous data is sent. */

            // if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK)
            // {
            //     ESP_LOGE(TAG, "Send error");
            //     my_espnow_deinit(send_param);
            //     vTaskDelete(NULL);
            // }
            break;
        }
        case MYAPP_ESPNOW_RECV_CB:
        {
            espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

            ret = example_espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq); // &recv_magic);
            free(recv_cb->data);
            if (ret == MSG_TYPE_SCAN)
            {
                ESP_LOGI(TAG, "Receive %dth state:%d data from: " MACSTR ", len: %d", recv_seq, recv_state, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                /* If MAC address does not exist in peer list, add it to peer list. */
                if (esp_now_is_peer_exist(recv_cb->mac_addr) == false)
                {
                    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                    if (peer == NULL)
                    {
                        ESP_LOGE(TAG, "Malloc peer information fail");
                        my_espnow_deinit(send_param);
                        vTaskDelete(NULL);
                    }
                    memset(peer, 0, sizeof(esp_now_peer_info_t));
                    peer->channel = CONFIG_ESPNOW_CHANNEL;
                    peer->ifidx = ESPNOW_WIFI_IF;
                    peer->encrypt = true;
                    memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                    memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                    ESP_ERROR_CHECK(esp_now_add_peer(peer));
                    free(peer);
                }

                if (iwork_state == 0 && !IS_BROADCAST_ADDR(recv_cb->mac_addr))
                {
                    memcpy(s_dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                    printf("\n============ iwork state 1 ====\n");
                    iwork_state = 1;
                }
                else if (recv_state == 1 && iwork_state == 1)
                {
                    printf("\n============ iwork state 2 ====\n");
                    iwork_state = 2;
                }
                else if (iwork_state == 2 && recv_state == 2 && recv_seq == mCount)
                {
                    printf("\n============ iwork state 3 ====\n");
                    printf("\n=============000 game over 000 ============\n");

                    iwork_state = 3;

                    goto FINISH;
                }
            }
            else
            {
                ESP_LOGI(TAG, "Receive error data from: " MACSTR "", MAC2STR(recv_cb->mac_addr));
            }
            break;
        }
        default:
            ESP_LOGE(TAG, "Callback type error: %d", evt.id);
            break;
        }
    }

FINISH:

    printf("\n================= game over ============\n");
    printf("\n================= begin uart transfer ============\n");
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(s_example_espnow_queue);

    xSemaphoreGive(g_sem_task_serial_ready);
    vTaskDelete(NULL);
}

static void myapp_espnow_init(void)
{

    espnow_send_param_t *send_param;
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_send_param_t));
    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(myapp_espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(myapp_espnow_recv_cb));

    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        ESP_LOGE(TAG, "Malloc peer information fail");
        // vSemaphoreDelete(s_espnow_queue);
        esp_now_deinit();
        return;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);

    iwork_state = 0;
    /* Initialize sending parameters. */
    send_param = malloc(sizeof(espnow_send_param_t));
    memset(send_param, 0, sizeof(espnow_send_param_t));
    if (send_param == NULL)
    {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    // send_param->unicast = false;
    // send_param->broadcast = true;

    send_param->type = 0;
    send_param->state = iwork_state;
    // send_param->magic = esp_random();
    send_param->count = 10;
    send_param->delay = 1000;
    send_param->len = sizeof(espnow_data_t);
    send_param->buffer = malloc(send_param->len);
    if (send_param->buffer == NULL)
    {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    scan_espnow_data_prepare(send_param, 0);

    xTaskCreate(my_espnow_task, "my_espnow_task", 2048, send_param, 4, NULL);

    return;
}

void espnow_sendData_pack(espnow_send_param_t *send_param_t, uint8_t *data, uint8_t type)
{
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;
    uint8_t *dtmp = (uint8_t *)malloc(RD_BUF_SIZE);

     uart_flush_input(UART_NUM_1);  //tgl debug
    for (;;)
    {
        // Waiting for UART event.
        if (xQueueReceive(uart1_queue, (void *)&event, (portTickType)portMAX_DELAY))
        {
            bzero(dtmp, RD_BUF_SIZE);
            ESP_LOGI(TAG, "uart[%d] event:", UART_NUM_1);
            switch (event.type)
            {
            // Event of UART receving data
            /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
            case UART_DATA:
                ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                uart_read_bytes(UART_NUM_1, dtmp, event.size,0);// portMAX_DELAY);
                ESP_LOGI(TAG, "[DATA EVT]:");
                // uart_write_bytes(UART_NUM_1, (const char *)dtmp, event.size);
                esp_now_send(s_dest_mac, dtmp, event.size);
                break;
            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGI(TAG, "hw fifo overflow");

                uart_flush_input(UART_NUM_1);
                xQueueReset(uart1_queue);
                break;
            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGI(TAG, "ring buffer full");
                uart_flush_input(UART_NUM_1);
                xQueueReset(uart1_queue);
                break;
            // Event of UART RX break detected
            case UART_BREAK:
                ESP_LOGI(TAG, "uart rx break");
                break;
            // Event of UART parity check error
            case UART_PARITY_ERR:
                ESP_LOGI(TAG, "uart parity error");
                break;
            // Event of UART frame error
            case UART_FRAME_ERR:
                ESP_LOGI(TAG, "uart frame error");
                break;

            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

static void myapp_serial_init()
{
 
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 200,
        .source_clk = UART_SCLK_APB,
    };
    // Install UART driver, and get the queue.
    uart_driver_install(UART_NUM_1, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 20, &uart1_queue, 0);
    uart_param_config(UART_NUM_1, &uart_config);

    // Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(UART_NUM_1, 32, 33, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_sem_task_serial_ready = xSemaphoreCreateBinary();

    myapp_wifi_init();
    myapp_espnow_init();

  

    // TODO 信号量堵塞
    xSemaphoreTake(g_sem_task_serial_ready, portMAX_DELAY);

      myapp_serial_init();
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 5, NULL);
}
