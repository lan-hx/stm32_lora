/**
 * @brief 数据链路层
 * @author lan
 */

#define DATALINK_IMPL
#define DATA_LINK_SEMAPHORE
#include "service/web/data_link.h"

#define LORA_SEMAPHORE
#include <string.h>

#include "crc.h"
#include "service/lora/lora.h"
#include "stm32f1xx_hal_crc.h"
#include "timers.h"

SemaphoreHandle_t data_link_wifi_semaphore;
StaticSemaphore_t data_link_wifi_semaphore_buffer;

// 这个semaphore负责通知发送函数发送过程已经结束,状态在data_link_tx_state中
SemaphoreHandle_t data_link_tx_semaphore;
StaticSemaphore_t data_link_tx_semaphore_buffer;

DataLinkError data_link_tx_state;

// LoRa模块的发送状态
DataLinkSignal datalink_send_state;
uint32_t retry_count;
bool is_send_ack;

// LoRa模块的接收状态
bool is_datalink_receive;

SemaphoreHandle_t data_link_rx_buffer_semaphore[2];
StaticSemaphore_t data_link_rx_buffer_semaphore_buffer[2];

TimerHandle_t datalink_resend_timer;
StaticTimer_t datalink_resend_timer_buffer;

void DataLinkResendCallBack(TimerHandle_t xTimer) {}

/**
 * 本机注册的服务
 */
uint8_t registered_service;

/**
 * @brief heard list
 */
HeardList *heard_list;

/**
 * @brief 注册服务回调函数
 */
LoraPacketCallback_t lora_packet_callback[LORA_SERVICE_NUM] = {nullptr};

LoraPacketHeader datalink_ack_buffer;

/**
 * @brief 发送buffer
 */
LoraPacket datalink_transmit_buffer;

/**
 * @brief 接收buffer
 */
LoraPacket datalink_receive_buffer;

DataLinkError DataLinkSendPacket(LoraPacket *pak, uint32_t hop = 0) {
  DataLinkSignal send_signal = RX;
  BaseType_t queue_error = xQueueSend(data_link_queue, &send_signal, 0);
  if (queue_error != pdTRUE) {
    return DataLink_Busy;
  }
  data_link_tx_state = DataLink_TxFailed;
  xSemaphoreTake(data_link_tx_semaphore, portMAX_DELAY);
  return data_link_tx_state;
}

void DataLinkRegisterService(LoraService service, LoraPacketCallback_t callback) {
  assert(service <= LORA_SERVICE_NUM);
  lora_packet_callback[service] = callback;
}

void DataLinkReceivePacketBegin() {
  LoraReadAsyncStart();
  is_datalink_receive = true;
}

void DataLinkReceivePacketEnd() {
  LoraReadAsyncStop();
  is_datalink_receive = false;
}

/**
 * @brief LoRa模块的异步接收回调函数
 *
 * @param s 数据包
 * @param len 数据包长度
 * @param state 接收状态
 */
void LoraRxCallback(const uint8_t *s, uint8_t len, LoraError state) {
  if (xSemaphoreTake(data_link_rx_buffer_semaphore, 0) == pdTRUE) {
    memset(&datalink_receive_buffer, 0, MAX_LORA_PACKET_SIZE);
    memcpy(&datalink_receive_buffer, s, len);
  }
  if (state != Lora_OK && state != Lora_CRCError) {
    return;
  }
  DataLinkSignal send_signal = TX;
  if (xQueueSendFromISR(data_link_queue, &send_signal, NULL) == pdTRUE) {
  } else {
  }
}

/**
 * @brief 发包中断回调函数, 更新LoRa发送模块状态
 * @param[in] state 发包反馈
 * @note 确定发送状态
 */
void LoraTxCallback(LoraError state) {
  if (state == Lora_OK) {
  }
}

/**
 * @brief 数据链路层事件处理
 */
void DataLinkEventLoop() {
  DataLinkSignal opt;
  bool crc_state;
  bool ack_buffer_avaliable = true;
  is_datalink_receive = false;
  is_send_ack = false;
  while (true) {
    xQueueReceive(data_link_queue, &opt, portMAX_DELAY);
    switch (opt) {
      case RX:
        if (datalink_receive_buffer.header.dest_addr == LORA_ADDR &&
            datalink_receive_buffer.header.magic_number == LORA_MAGIC_NUMBER) {
          // 计算crc
          crc_state = false;
          uint8_t packet_crc = datalink_receive_buffer.header.crc;
          datalink_receive_buffer.header.crc = 0;
          uint32_t crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)&datalink_receive_buffer,
                                                 (datalink_receive_buffer.header.length + 3) / 4);
          uint8_t crc_xor =
              (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
          if (crc_xor == packet_crc) {
            crc_state = true;
          }

          if (datalink_receive_buffer.header.settings.ack) {
            // ack包
            if (crc_state && datalink_send_state == TX_Packet_Wait &&
                datalink_receive_buffer.header.settings.seq == datalink_transmit_buffer.header.settings.seq) {
              data_link_tx_state = DataLink_OK;
              datalink_send_state = TX_Init;
              xSemaphoreGive(data_link_tx_semaphore);
            }
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            // nak包
          } else if (datalink_receive_buffer.header.settings.nak) {
            if (crc_state && datalink_send_state == TX_Packet_Wait &&
                datalink_receive_buffer.header.settings.seq == datalink_transmit_buffer.header.settings.seq) {
              DataLinkSignal tx_signal = TX_Retry;
              xQueueSend(data_link_queue, &tx_signal, portMAX_DELAY);
            }
            xSemaphoreGive(data_link_rx_buffer_semaphore);
          } else {
            // 其他包
            if (!ack_buffer_avaliable) {
              break;
            }
            ack_buffer_avaliable = false;
            if (crc_state) {
              uint8_t service_number = datalink_receive_buffer.header.settings.service;
              if (is_datalink_receive && lora_packet_callback[service_number] != nullptr) {
                lora_packet_callback[service_number](&datalink_receive_buffer);
              }
              // 发送ack
              datalink_ack_buffer = {datalink_receive_buffer.header.src_addr,
                                     {0, 0},
                                     LORA_ADDR,
                                     LORA_MAGIC_NUMBER,
                                     {datalink_receive_buffer.header.settings.seq, true, false, false, 0, 0},
                                     sizeof(LoraPacketHeader),
                                     0};
            } else {
              // 发送nak
              datalink_ack_buffer = {datalink_receive_buffer.header.src_addr,
                                     {0, 0},
                                     LORA_ADDR,
                                     LORA_MAGIC_NUMBER,
                                     {datalink_receive_buffer.header.settings.seq, false, false, true, 0, 0},
                                     sizeof(LoraPacketHeader),
                                     0};
            }
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            DataLinkSignal tx_signal = TX_Ack;
            while (xQueueSend(data_link_queue, &tx_signal, portMAX_DELAY) != pdTRUE) {
            }
          }
        } else {
          xSemaphoreGive(data_link_rx_buffer_semaphore);
        }
        break;
      case TX:
        if (datalink_send_state == TX_Init) {
        } else {
        }
        break;
      case TX_Ack:
        if (datalink_send_state != TX_Wait_Lora) {
          LoraWriteAsync((const uint8_t *)datalink_ack_buffer, sizeof(LoraPacketHeader), true);
        }

        break;
      case TX_Retry:

        break;
      default:

        break;
    }
  }
}
