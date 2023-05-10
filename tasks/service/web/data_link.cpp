/**
 * @brief 数据链路层
 * @author lan
 */

#define DATALINK_DBG
#define DATALINK_IMPL
#define DATA_LINK_SEMAPHORE
#define DATA_LINK_TIMER
#include "service/web/data_link.h"

#include <string.h>

#include "crc.h"
#include "service/lora/lora.h"
#include "stm32f1xx_hal_crc.h"
#include "utility.h"

enum DataLinkSignalEnum : uint8_t {
  RX_Packet,
  TX_Packet,
  TX_Ack,
  TX_Retry,

  TX_Init,
  TX_Wait_Lora,
  TX_Packet_Wait,
};

#define MAX_REAL_LORA_PACKET_SIZE 256

// --------------------- 信号传递使用队列 -----------------------;
/**
 * @brief 实际上考虑队列中各个信号出现的可能性,不会出现信号放不进去的情况.ACK包发送信号至多出现一次,
 *  由于接收buffer只有一个接收信号也至多出现一次.发送信号和重传信号不会同时出现,且至多出现一次.
 *  因此实际上当前实现信号队列至多同时出现三个信号
 */
QueueHandle_t data_link_queue;
StaticQueue_t data_link_queue_buffer;
DataLinkSignal data_link_queue_storage[DATALINK_QUEUE_LEN];
// ---------------------------END------------------------------;

// --------------------------semophore-------------------------;
/**
 * @brief TODO: M3涉及中心服务器通讯时使用
 */
SemaphoreHandle_t data_link_wifi_semaphore;
StaticSemaphore_t data_link_wifi_semaphore_buffer;

/**
 * @brief 这个semaphore负责通知发送函数发送过程已经结束,状态在data_link_tx_state中
 * @note 此semaphore不必要
 */
SemaphoreHandle_t data_link_tx_semaphore;
StaticSemaphore_t data_link_tx_semaphore_buffer;

/**
 * @brief 此semaphore管理receive buffer,保证buffer内包不被改变
 */
SemaphoreHandle_t data_link_rx_buffer_semaphore;
StaticSemaphore_t data_link_rx_buffer_semaphore_buffer;
// ---------------------------END------------------------------;

// ---------------------------timer----------------------------;
/**
 * @brief 此定时器负责判断包是否超时
 */
TimerHandle_t datalink_resend_timer;
StaticTimer_t datalink_resend_timer_buffer;
// ---------------------------END------------------------------;

// --------------------datalink global states------------------;
/**
 * @brief 当前是发送函数的返回值,实际上不需要
 */
DataLinkError data_link_tx_state;

/**
 * @brief 当前数据链路层普通包传输状态
 */
DataLinkSignal datalink_send_state;

/**
 * @brief 当前Lora模块是否在传输ACK/NAK包
 */
bool is_send_ack;

/**
 * @brief 当前发包的service号
 */
LoraService send_service_number;

/**
 * @brief 是否开启数据链路层接收包回调
 */
bool is_datalink_receive;

/**
 * @brief 维护数据链路层buffer状态
 */
bool transmit_buffer_avaliable;

/**
 * @brief ACK buffer是否可用
 */
bool ack_buffer_avaliable;

/**
 * @brief LoRa收到包的长度
 */
uint8_t datalink_rx_packet_length;
// ---------------------------END------------------------------;

// ------------------Callback and Buffer-----------------------;

/**
 * @brief 注册服务回调函数
 */
LoraPacketCallback_t lora_packet_callback[LORA_SERVICE_NUM] = {nullptr};
LoraPacketCallback_t lora_send_callback[LORA_SERVICE_NUM] = {nullptr};

/**
 * @brief ACK/NAK包buffer
 */
uint8_t datalink_tx_real_buffer[MAX_REAL_LORA_PACKET_SIZE];
uint8_t datalink_rx_real_buffer[MAX_REAL_LORA_PACKET_SIZE];
uint8_t datalink_ack_real_buffer[sizeof(LoraPacketHeader)];

LoraPacketHeader *datalink_ack_buffer = (LoraPacketHeader *)datalink_ack_real_buffer;

/**
 * @brief 发送buffer
 */
LoraPacket *datalink_transmit_buffer = (LoraPacket *)datalink_tx_real_buffer;

/**
 * @brief 接收buffer
 */
LoraPacket *datalink_receive_buffer = (LoraPacket *)datalink_rx_real_buffer;

/**
 * @brief heard list
 * @note unused
 */
HeardList *heard_list;

/**
 * @brief 本机注册的服务
 * @note unused
 */
uint8_t registered_service;
// ---------------------------END------------------------------;

void DataLinkRegisterService(bool func_select, LoraService service, LoraPacketCallback_t callback) {
  assert(service <= LORA_SERVICE_NUM);
  if (func_select) {
    lora_send_callback[service] = callback;
  } else {
    lora_packet_callback[service] = callback;
  }
}

void DataLinkReceivePacketBegin() {
  LoraReadAsyncStart();
  is_datalink_receive = true;
}

void DataLinkReceivePacketEnd() {
  LoraReadAsyncStop();
  is_datalink_receive = false;
}

uint32_t DataLinkDeclareTransmitBuffer() {
  if (transmit_buffer_avaliable) {
    transmit_buffer_avaliable = false;
  } else {
    return 0;
  }
  return sizeof(datalink_transmit_buffer);
}

uint32_t DataLinkReleaseTransmitBuffer() {
  assert(transmit_buffer_avaliable == false);
  transmit_buffer_avaliable = true;
  return sizeof(datalink_transmit_buffer);
}

DataLinkError DataLinkSendPacket(LoraService service, LoraPacket *pak, uint32_t hop) {
  UNUSED(hop);
  assert(pak == datalink_transmit_buffer);
  assert(transmit_buffer_avaliable == false);
  DataLinkSignal queue_signal = TX_Packet;
  if (xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY) != pdTRUE) {
    return DataLink_Busy;
  }
  send_service_number = service;
  return DataLink_OK;
}

void DataLinkResendCallBack(TimerHandle_t xTimer) {
  assert(datalink_send_state == TX_Packet_Wait);
  datalink_send_state = TX_Init;
  printf("[Timer] Time Out\r\n");
  DataLinkSignal queue_signal = TX_Retry;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
  portYIELD();
}

/**
 * @brief LoRa模块的异步接收回调函数
 *
 * @param s 数据包
 * @param len 数据包长度
 * @param state 接收状态
 */
void LoraRxCallback(const uint8_t *s, uint8_t len, LoraError state) {
  if (state != Lora_OK && state != Lora_CRCError) {
    return;
  }
  if (xSemaphoreTake(data_link_rx_buffer_semaphore, 0) == pdTRUE) {
    datalink_rx_packet_length = len;
    memset(datalink_receive_buffer, 0, MAX_REAL_LORA_PACKET_SIZE);
    memcpy(datalink_receive_buffer, s, len);
    DataLinkSignal queue_signal = RX_Packet;
    if (xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY) == pdTRUE) {
#ifdef DATALINK_DBG
      printf("[CallBack From Lora] Receive a packet.\r\n");
#endif
    } else {
      xSemaphoreGive(data_link_rx_buffer_semaphore);
    }
  }
}

/**
 * @brief 发包中断回调函数, 更新LoRa发送模块状态
 * @param[in] state 发包反馈
 * @note 确定发送状态
 */
void LoraTxCallback(LoraError state) {
  if (state == Lora_OK) {
    if (is_send_ack) {
      is_send_ack = false;
      ack_buffer_avaliable = true;
    } else {
      datalink_send_state = TX_Packet_Wait;
      xTimerStart(datalink_resend_timer, 0);
    }
#ifdef DATALINK_DBG
    printf("[CallBack From Lora] Send a packet succ.\r\n");
#endif
  } else {
    if (is_send_ack) {
      is_send_ack = false;
      DataLinkSignal queue_signal = TX_Ack;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
    } else {
      DataLinkSignal queue_signal = TX_Retry;
      datalink_send_state = TX_Init;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
    }
  }
}

/**
 * @brief 数据链路层事件处理
 */
void DataLinkEventLoop() {
  DataLinkSignal opt;
  bool crc_state = false;
  datalink_send_state = TX_Init;
  is_send_ack = false;
  send_service_number = LORA_SERVICE_UNAVALIABLE;
  is_datalink_receive = false;
  transmit_buffer_avaliable = true;
  ack_buffer_avaliable = true;
  uint32_t retry_count = 0;
  xSemaphoreGive(data_link_rx_buffer_semaphore);

  while (true) {
    xQueueReceive(data_link_queue, &opt, portMAX_DELAY);
#ifdef DATALINK_DBG
    printf("[DataLink MainLoop] running, operation = %d\r\n", opt);
#endif
    switch (opt) {
      case RX_Packet: {
        if (datalink_receive_buffer->header.dest_addr == LORA_ADDR &&
            datalink_receive_buffer->header.magic_number == LORA_MAGIC_NUMBER) {
          if (datalink_rx_packet_length < 8 || datalink_rx_packet_length != datalink_receive_buffer->header.length) {
#ifdef DATALINK_DBG
            printf("[DataLink MainLoop] warning: Packet length not correct!\r\n");
#endif
          }
          // 计算crc
          crc_state = false;
          uint8_t packet_crc = datalink_receive_buffer->header.crc;
          datalink_receive_buffer->header.crc = 0;
          uint32_t crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_receive_buffer,
                                                 (datalink_receive_buffer->header.length + 3) / 4);
          uint8_t crc_xor =
              (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
          if (crc_xor == packet_crc) {
            crc_state = true;
          } else {
#ifdef DATALINK_DBG
            printf("[DataLink MainLoop] Datalink crc incorrect!\r\n");
#endif
          }

          if (datalink_receive_buffer->header.settings.ack) {
            // ack包
            if (crc_state && datalink_send_state == TX_Packet_Wait &&
                datalink_receive_buffer->header.settings.seq == datalink_transmit_buffer->header.settings.seq) {
              datalink_send_state = TX_Init;
              xTimerStop(datalink_resend_timer, 0);
              lora_send_callback[send_service_number](nullptr, DataLink_OK);
              send_service_number = LORA_SERVICE_UNAVALIABLE;
            }
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            // nak包
          } else if (datalink_receive_buffer->header.settings.nak) {
            if (crc_state && datalink_send_state == TX_Packet_Wait &&
                datalink_receive_buffer->header.settings.seq == datalink_transmit_buffer->header.settings.seq) {
              datalink_send_state = TX_Init;
              DataLinkSignal queue_signal = TX_Retry;
              xTimerStop(datalink_resend_timer, 0);
              xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
              printf("[DataLink MainLoop] Receive a NAK packet, retransmit!\r\n");
#endif
            }
            xSemaphoreGive(data_link_rx_buffer_semaphore);
          } else {
            // 其他包
            if (!ack_buffer_avaliable) {
              DataLinkSignal queue_signal = RX_Packet;
              xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
              break;
            }
            ack_buffer_avaliable = false;
            if (crc_state) {
              uint8_t service_number = datalink_receive_buffer->header.settings.service;
              if (is_datalink_receive && service_number < LORA_SERVICE_NUM &&
                  lora_packet_callback[service_number] != nullptr) {
                lora_packet_callback[service_number](datalink_receive_buffer, 0);
              }
              // 发送ack
              *datalink_ack_buffer = {datalink_receive_buffer->header.src_addr,
                                      {0, 0},
                                      LORA_ADDR,
                                      LORA_MAGIC_NUMBER,
                                      {datalink_receive_buffer->header.settings.seq, true, false, false, 0, 0},
                                      sizeof(LoraPacketHeader),
                                      0};
            } else {
              // 发送nak
              *datalink_ack_buffer = {datalink_receive_buffer->header.src_addr,
                                      {0, 0},
                                      LORA_ADDR,
                                      LORA_MAGIC_NUMBER,
                                      {datalink_receive_buffer->header.settings.seq, false, false, true, 0, 0},
                                      sizeof(LoraPacketHeader),
                                      0};
            }
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            DataLinkSignal queue_signal = TX_Ack;
            xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          }
        } else {
          xSemaphoreGive(data_link_rx_buffer_semaphore);
        }
        break;
      }
      case TX_Packet: {
        if (datalink_send_state == TX_Init) {
          if (is_send_ack) {
            DataLinkSignal queue_signal = TX_Packet;
            xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
            break;
          }
          datalink_send_state = TX_Wait_Lora;
          retry_count = 0;
          memset(((uint8_t *)datalink_transmit_buffer) + datalink_transmit_buffer->header.length, 0,
                 (((uint32_t)datalink_transmit_buffer->header.length + 3) & 0xfffffffc) -
                     (uint32_t)datalink_transmit_buffer->header.length);
          uint32_t crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_transmit_buffer,
                                                 (datalink_transmit_buffer->header.length + 3) / 4);
          uint8_t crc_xor =
              (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
          datalink_transmit_buffer->header.crc = crc_xor;
          LoraWriteAsync((const uint8_t *)datalink_transmit_buffer, datalink_transmit_buffer->header.length, false);
        } else {
          lora_send_callback[send_service_number](nullptr, DataLink_Busy);
          send_service_number = LORA_SERVICE_UNAVALIABLE;
        }
        break;
      }
      case TX_Ack: {
        assert(is_send_ack == false);
        if (datalink_send_state == TX_Wait_Lora) {
          DataLinkSignal queue_signal = TX_Ack;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          break;
        }
        uint32_t crc_value =
            HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_ack_buffer, (datalink_ack_buffer->length + 3) / 4);
        uint8_t crc_xor =
            (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
        datalink_ack_buffer->crc = crc_xor;
        is_send_ack = true;
        LoraWriteAsync((const uint8_t *)datalink_ack_buffer, sizeof(LoraPacketHeader), true);
        break;
      }
      case TX_Retry: {
        assert(datalink_send_state == TX_Init);
        if (retry_count > DATA_LINK_RETRY - 1) {
          lora_send_callback[send_service_number](nullptr, DataLink_TxFailed);
          send_service_number = LORA_SERVICE_UNAVALIABLE;
          break;
        }
        if (is_send_ack) {
          DataLinkSignal queue_signal = TX_Retry;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          break;
        }
        datalink_send_state = TX_Wait_Lora;
        retry_count++;
#ifdef DATALINK_DBG
        printf("[DataLink MainLoop] Retransmit retry = %d\r\n", retry_count);
#endif
        LoraWriteAsync((const uint8_t *)datalink_transmit_buffer, sizeof(LoraPacketHeader), false);
        break;
      }
      default: {
      } break;
    }
  }
}
