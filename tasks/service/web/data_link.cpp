/**
 * @brief 数据链路层
 * @author lan
 */

#define DATALINK_DBG
#define NETWORK_DBG
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
  TX_Route_Packet,  // m3新增，发送路由包
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

// m3新增
/**
 * @brief 此定时器负责判断包是否超时
 */
TimerHandle_t network_route_timer;
StaticTimer_t network_route_timer_buffer;

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
 * @brief 当前Lora模块是否在传输ACK/NAK包/普通包
 */
bool is_send_ack, is_send_route;

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

// m3 新增 Route buffer是否可用

bool route_buffer_avaliable;

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
// m3新增
uint8_t datalink_route_real_buffer[MAX_REAL_LORA_PACKET_SIZE];
LoraPacket *datalink_route_buffer = (LoraPacket *)datalink_route_real_buffer;

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
// HeardList *heard_list;
HeardList HeardLists[MaxHeardListNum];

/*m3新增*/
//序列号,作为全局变量,每次发包后加1,超过255以后归0
uint8_t seq = 0;

/**
 * @brief 本机注册的服务
 * @note unused
 */
uint8_t registered_service;
// ---------------------------END------------------------------;

//上层通过调此函数注册回调函数(true=发包 false=收包)
void DataLinkRegisterService(bool func_select, LoraService service, LoraPacketCallback_t callback) {
  assert(service <= LORA_SERVICE_NUM);
  if (func_select) {
    lora_send_callback[service] = callback;
  } else {
    lora_packet_callback[service] = callback;
  }
}

//上层调用此函数说明开始收包
void DataLinkReceivePacketBegin() {
  LoraReadAsyncStart();
  is_datalink_receive = true;
}

//上层调用此函数说明截止收包
void DataLinkReceivePacketEnd() {
  LoraReadAsyncStop();
  is_datalink_receive = false;
}

//上层声明发送buffer —— 发送buffer不可用
uint32_t DataLinkDeclareTransmitBuffer() {
  if (transmit_buffer_avaliable) {
    transmit_buffer_avaliable = false;
  } else {
    return 0;
  }
  return sizeof(datalink_transmit_buffer);
}

//上层释放发送buffer —— 发送buffer又可用
uint32_t DataLinkReleaseTransmitBuffer() {
  assert(transmit_buffer_avaliable == false);
  transmit_buffer_avaliable = true;
  return sizeof(datalink_transmit_buffer);
}

//链路层发包
DataLinkError DataLinkSendPacket(LoraService service, LoraPacket *pak, uint32_t hop) {
  UNUSED(hop);
  // m3修改
  /*发包前，根据本地路由表填写中转地址信息trandfer_addr
           同时seq在这里填入，而不是上层调用发包时自己给出*/
  pak->header.settings.seq = seq;
  seq++;
  // DataLinkRoute(pak);
#ifdef DATALINK_DBG
  printf("[DataLink Route] fill DataLinkRoute()\r\n");
#endif
  //上层已经将包填到发送buffer中
  assert(pak == datalink_transmit_buffer);
  //上层已经声明了发送buffer
  assert(transmit_buffer_avaliable == false);
  // TX_Packet通知主循环进行发包
  DataLinkSignal queue_signal = TX_Packet;
  if (xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY) != pdTRUE) {
    return DataLink_Busy;
  }
  send_service_number = service;
  return DataLink_OK;
}

//超时重发的回调函数，当发包定时器到时后，就会调此函数
void DataLinkResendCallBack(TimerHandle_t xTimer) {
  //此时应处于TX_Packet_Wait状态
  assert(datalink_send_state == TX_Packet_Wait);
  datalink_send_state = TX_Init;
  printf("[Timer] Time Out\r\n");
  // TX_Retry通知主循环重发包
  DataLinkSignal queue_signal = TX_Retry;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
  portYIELD();
}

// m3新增
/*定时发送路由包，暂定5s一次*/
void NetworkRouteCallBack(TimerHandle_t xTimer) {
  printf("[Timer] Send Route Packet!\r\n");
  // TX_Route_Packet通知主循环发路由包
  DataLinkSignal queue_signal = TX_Route_Packet;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
  portYIELD();  //让出CPU
}

/**
 * @brief LoRa模块的异步接收回调函数
 *
 * @param s 数据包
 * @param len 数据包长度
 * @param state 接收状态
 */

// Lora收到一个包后,将包拷贝到接收buffer,同时RX_Packet通知主循环收包
//    这时候拿着data_link_rx_buffer_semaphore锁,等主循环收完包就放锁
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
//
void LoraTxCallback(LoraError state) {
  //发送成功
  if (state == Lora_OK) {
    //发送的是ACK包
    if (is_send_ack) {
      is_send_ack = false;
      // ack buffer变成可用,即可以再发其他ack包
      ack_buffer_avaliable = true;
    } else if (is_send_route) {
      is_send_route = false;
      // route buffer变成可用,即可以发送新的路由包了
      route_buffer_avaliable = true;
      //开启路由定时器,为了周期性发送路由包
      xTimerStart(network_route_timer, 0);
    }
    //发送的是普通包
    else {
      datalink_send_state = TX_Packet_Wait;
      //开启定时器,为了重传
      xTimerStart(datalink_resend_timer, 0);
    }
#ifdef DATALINK_DBG
    printf("[CallBack From Lora] Send a packet succ.\r\n");
#endif
  }
  //发送失败
  else {
    //发送ACK包失败
    if (is_send_ack) {
      is_send_ack = false;
      // TX_Ack通知主循环重发Ack包
      DataLinkSignal queue_signal = TX_Ack;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
    }
    //发送路由包失败
    else if (is_send_route) {
      is_send_route = false;
      // TX_Route_Packet通知主循环发送路由包
      DataLinkSignal queue_signal = TX_Route_Packet;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
    }
    //发送普通包失败
    else {
      // TX_Retry通知主循环重传
      DataLinkSignal queue_signal = TX_Retry;
      datalink_send_state = TX_Init;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
    }
  }
}

const HeardList *DataLinkGetHeardList() { return HeardLists; }

// m3新增函数
/*供上层调用，调用此函数后，该结点开始定期向外界转发路由包*/
void NetworkBeginRoute() {
  DataLinkSignal queue_signal = TX_Route_Packet;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
}

// m3新增函数
/*根据本地的路由信息，填写路由包*/
void PrepareRoutePacket(LoraPacket *route_packet) {
  //填入序列号seq
  route_packet->header.settings.seq = seq;
  seq++;
  //填入源地址  目标地址(FF表示广播)  包的长度=最大长度  当前地址
  route_packet->header.dest_addr = 0xFF;
  route_packet->header.src_addr = LORA_ADDR;
  route_packet->header.curr_addr = LORA_ADDR;

  NetworkLinkStatePacket link_state_pack;
  //向路由包填入内容，遍历本地的路由表
  link_state_pack.sender_addr = LORA_ADDR;
  // link_state_pack->registered_service = LORA_SERVICE_LINK_STATE;
  int num = 0;
  for (int i = 0; i < MaxHeardListNum; i++) {
    if (HeardLists[i].tick > 0 && HeardLists[i].cost == 1) {  //不经中转
      link_state_pack.neighbor[num].next_addr = HeardLists[i].addr;
      link_state_pack.neighbor[num].next_next_addr = 0xFF;
      link_state_pack.neighbor[num].registered_service = LORA_SERVICE_LINK_STATE;
      num++;
    } else if (HeardLists[i].tick > 0 && HeardLists[i].cost == 2) {  //经一跳中转
      link_state_pack.neighbor[num].next_addr = HeardLists[i].next_addr;
      link_state_pack.neighbor[num].next_next_addr = HeardLists[i].addr;
      link_state_pack.neighbor[num].registered_service = LORA_SERVICE_LINK_STATE;
      num++;
    }
    // cost=3的无需在填入路由包，转发给邻居了
    link_state_pack.num = num;
  }
  //把link_state_pack的内容拷贝到路由包的content中
  memset(route_packet->content, 0, MAX_LORA_CONTENT_LENGTH);
  memcpy(route_packet->content, &link_state_pack, num * sizeof(uint8_t) * 3 + 2 * sizeof(uint8_t));

  //填入包的长度 = 包头长度+ 2个uint8_t(num sender_addr) + (路由条目数*3)个uint8_t
  route_packet->header.length = sizeof(LoraPacketHeader) + 2 * sizeof(uint8_t) + num * sizeof(uint8_t) * 3;
}

// m3新增函数
/*函数作用：根据本地路由表信息，在LoraPacket里面填入中转地址信息transfer_addr
  qx修改:transfer_addr数组最多两个元素，只记中转地址，不计目标地址*/
void DataLinkRoute(LoraPacket *pak) {
  for (auto a : HeardLists) {
    if (a.addr == pak->header.dest_addr) {
      if (a.cost == 1) {  //不需中转
        pak->header.transfer_addr[0] = 0xFF;
        pak->header.transfer_addr[1] = 0xFF;
      }
      if (a.cost == 2) {  //经一跳中转
        pak->header.transfer_addr[0] = a.next_addr;
        pak->header.transfer_addr[1] = 0xFF;
      }
      if (a.cost == 3) {  //经两跳中转
        pak->header.transfer_addr[0] = a.next_addr;
        pak->header.transfer_addr[1] = a.next_next_addr;
      }
    }
  }
}

void DataLinkHeardListTick() {
  for (auto a : HeardLists) {
    if (a.tick > 0) {
      a.tick -= HeardListTickReduce;
    }
  }
}

// m3新增
/*根据收到的路由包，更新本地路由信息*/
void NetworkUpdateHeardList(NetworkLinkStatePacket *pack) {
  int HeardListOverstack = 1;
  int inlist = 0;
  for (int j = 0; j < MaxHeardListNum; j++) {
    if (HeardLists[j].tick > 0 && HeardLists[j].addr == pack->sender_addr) {
      HeardLists[j].tick = HeardListTicks;
      HeardLists[j].addr = pack->sender_addr;
      HeardLists[j].next_addr = pack->sender_addr;
      HeardLists[j].cost = 1;
      //   HeardLists[j].registered_service = pack->registered_service;
      inlist = 1;
      HeardListOverstack = 0;
      break;
    }
  }
  if (!inlist) {
    for (int j = 0; j < MaxHeardListNum; j++) {
      if (HeardLists[j].tick == 0) {
        HeardLists[j].tick = HeardListTicks;
        HeardLists[j].addr = pack->sender_addr;
        HeardLists[j].next_addr = pack->sender_addr;
        HeardLists[j].cost = 1;
        // HeardLists[j].registered_service = pack->registered_service;
        HeardListOverstack = 0;
        break;
      }
    }
  }
  if (HeardListOverstack) {
    assert(HeardListOverstack < 1);
  }

  for (int i = 0; i < pack->num; i++) {
    inlist = 0;
    HeardListOverstack = 1;
    for (int j = 0; j < MaxHeardListNum; j++) {
      if (HeardLists[j].tick > 0 && HeardLists[j].addr == pack->neighbor[i].next_addr) {
        if (HeardLists[j].cost == 1) {
          inlist = 1;
          HeardListOverstack = 0;
          break;
        }
        HeardLists[j].tick = HeardListTicks;
        HeardLists[j].addr = pack->neighbor[i].next_addr;
        HeardLists[j].next_addr = pack->sender_addr;
        HeardLists[j].cost = 2;
        HeardLists[j].registered_service = pack->neighbor[i].registered_service;
        inlist = 1;
        HeardListOverstack = 0;
        break;
      }
      if (HeardLists[j].tick > 0 && HeardLists[j].addr == pack->neighbor[i].next_next_addr) {
        if (HeardLists[j].cost < 3) {
          inlist = 1;
          HeardListOverstack = 0;
          break;
        }
        HeardLists[j].tick = HeardListTicks;
        HeardLists[j].addr = pack->neighbor[i].next_next_addr;
        HeardLists[j].next_addr = pack->sender_addr;
        HeardLists[j].next_next_addr = pack->neighbor[i].next_addr;
        HeardLists[j].cost = 3;
        HeardLists[j].registered_service = pack->neighbor[i].registered_service;
        inlist = 1;
        HeardListOverstack = 0;
        break;
      }
    }
    if (!inlist) {
      for (int j = 0; j < MaxHeardListNum; j++) {
        if (HeardLists[j].tick == 0) {
          if (pack->neighbor[i].next_next_addr != 0xFF) {
            HeardLists[j].tick = HeardListTicks;
            HeardLists[j].addr = pack->neighbor[i].next_addr;
            HeardLists[j].next_addr = pack->sender_addr;
            HeardLists[j].cost = 2;
            HeardLists[j].registered_service = pack->neighbor[i].registered_service;
            HeardListOverstack = 0;
            break;
          } else {
            HeardLists[j].tick = HeardListTicks;
            HeardLists[j].addr = pack->neighbor[i].next_next_addr;
            HeardLists[j].next_addr = pack->sender_addr;
            HeardLists[j].next_next_addr = pack->neighbor[i].next_addr;
            HeardLists[j].cost = 3;
            HeardLists[j].registered_service = pack->neighbor[i].registered_service;
            inlist = 1;
            HeardListOverstack = 0;
            break;
          }
        }
      }
    }
    if (HeardListOverstack) {
      assert(HeardListOverstack < 1);
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
  // m3新增
  route_buffer_avaliable = true;
  uint32_t retry_count = 0;
  xSemaphoreGive(data_link_rx_buffer_semaphore);

  while (true) {
    xQueueReceive(data_link_queue, &opt, portMAX_DELAY);
#ifdef DATALINK_DBG
    printf("[DataLink MainLoop] running, operation = %d\r\n", opt);
#endif
    switch (opt) {
        //收包逻辑:
        /* 目的地址是自己，则直接发 —— m2已实现
           中转地址是自己 —— 转发这个包(需要修改包头的curr_addr为自己)
           广播包 —— 根据路由包更新本地路由包 NetworkUpdateHeardList(NetworkLinkStatePacket *pack)
           其他情况 —— 丢弃(类似于m2对目的地址不是自己的包的处理)
         */
      case RX_Packet: {
        //上一跳的结点不是拒绝的才会接受这个包
        if (datalink_receive_buffer->header.magic_number == LORA_MAGIC_NUMBER &&
            datalink_receive_buffer->header.curr_addr != REJECTED_LORA_ADDR) {
          if (datalink_rx_packet_length < 8 || datalink_rx_packet_length != datalink_receive_buffer->header.length) {
#ifdef DATALINK_DBG
            printf("[DataLink MainLoop] warning: Packet length not correct!\r\n");
#endif
          }
          // CRC校验，校验通过:crc_state=true,否则crc_state=false
          //                                 检验不通过有输出
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
          //收到目的地址是自己的包，这部分和m2一样
          if (datalink_receive_buffer->header.dest_addr == LORA_ADDR) {
            //收到ack包
            if (datalink_receive_buffer->header.settings.ack) {
              if (crc_state && datalink_send_state == TX_Packet_Wait &&
                  datalink_receive_buffer->header.settings.seq == datalink_transmit_buffer->header.settings.seq) {
                datalink_send_state = TX_Init;
                xTimerStop(datalink_resend_timer, 0);
                lora_send_callback[send_service_number](nullptr, DataLink_OK);
                send_service_number = LORA_SERVICE_UNAVALIABLE;
              }
              xSemaphoreGive(data_link_rx_buffer_semaphore);
            }
            //收到nack包
            else if (datalink_receive_buffer->header.settings.nak) {
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
            }
            //收到普通包
            else {
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
                datalink_ack_buffer->dest_addr = datalink_receive_buffer->header.src_addr;
                datalink_ack_buffer->curr_addr = LORA_ADDR;
                datalink_ack_buffer->src_addr = LORA_ADDR;
                datalink_ack_buffer->magic_number = LORA_MAGIC_NUMBER;
                datalink_ack_buffer->settings.seq = datalink_receive_buffer->header.settings.seq;
                datalink_ack_buffer->settings.ack = true;
                datalink_ack_buffer->length = sizeof(LoraPacketHeader);
                /**datalink_ack_buffer = {datalink_receive_buffer->header.src_addr,
                                        {0, 0},
                                        LORA_ADDR,
                                        LORA_MAGIC_NUMBER,
                                        {datalink_receive_buffer->header.settings.seq, true, false, false, 0, 0},
                                        sizeof(LoraPacketHeader),
                                        0};*/
              } else {
                // 发送nak
                datalink_ack_buffer->dest_addr = datalink_receive_buffer->header.src_addr;
                datalink_ack_buffer->curr_addr = LORA_ADDR;
                datalink_ack_buffer->src_addr = LORA_ADDR;
                datalink_ack_buffer->magic_number = LORA_MAGIC_NUMBER;
                datalink_ack_buffer->settings.seq = datalink_receive_buffer->header.settings.seq;
                datalink_ack_buffer->settings.nak = true;
                datalink_ack_buffer->length = sizeof(LoraPacketHeader);
                /**datalink_ack_buffer = {datalink_receive_buffer->header.src_addr,
                                        {0, 0},
                                        LORA_ADDR,
                                        LORA_MAGIC_NUMBER,
                                        {datalink_receive_buffer->header.settings.seq, false, false, true, 0, 0},
                                        sizeof(LoraPacketHeader),
                                        0};*/
              }
              xSemaphoreGive(data_link_rx_buffer_semaphore);
              DataLinkSignal queue_signal = TX_Ack;
              xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
            }
          }
          //收到中转地址是自己的包，转发
          else if (datalink_receive_buffer->header.transfer_addr[0] == LORA_ADDR ||
                   datalink_receive_buffer->header.transfer_addr[1] == LORA_ADDR) {
            //如果此时我的发送buffer不可用,则把RX_Packet压回去，过一会儿再收包
            if (!transmit_buffer_avaliable) {
              DataLinkSignal queue_signal = RX_Packet;
              xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
              break;
            }
            //拷贝这个包到发送队列
            memcpy(datalink_transmit_buffer, datalink_receive_buffer, datalink_receive_buffer->header.length);
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            //修改curr_addr的地址为自己
            datalink_transmit_buffer->header.curr_addr = LORA_ADDR;
#ifdef NETWORK_DBG
            printf("[DataLink MainLoop] Receive a packet needed to transfer,dest = %X!\r\n",
                   datalink_receive_buffer->header.dest_addr);
#endif
            //发包
            DataLinkSignal queue_signal = TX_Packet;
            xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          }
          //收到广播的路由包
          else if (datalink_receive_buffer->header.dest_addr == 0xFF) {
            //根据收到的路由包，更新本地的路由表
#ifdef NETWORK_DBG
            printf("[DataLink MainLoop] Receive a route packet from %X!\r\n", datalink_receive_buffer->header.src_addr);
#endif
            NetworkLinkStatePacket link_state_pak;
            memcpy(&link_state_pak, datalink_receive_buffer->content, datalink_receive_buffer->header.length);
            NetworkUpdateHeardList(&link_state_pak);
            xSemaphoreGive(data_link_rx_buffer_semaphore);
          }
          //其他情况不作处理,释放接受buffer的锁(不再管这个包)
          else {
            xSemaphoreGive(data_link_rx_buffer_semaphore);
          }
          break;
        }
      }
      //发包逻辑
      case TX_Packet: {
        printf("[DataLink MainLoop]:Send a normal packet!\r\n");
        if (datalink_send_state == TX_Init) {
          /*m3修改*/
          //如果该结点目前正在往外发ack包/路由包，则此时不能发包，把Tx_Packet信号压回队列，之后再发
          if (is_send_ack || is_send_route) {
            DataLinkSignal queue_signal = TX_Packet;
            xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
            break;
          }
          datalink_send_state = TX_Wait_Lora;
          retry_count = 0;
          //新增
          datalink_transmit_buffer->header.curr_addr = LORA_ADDR;
          //确保发送的32字节的整数倍,不是的话后面填0
          memset(((uint8_t *)datalink_transmit_buffer) + datalink_transmit_buffer->header.length, 0,
                 (((uint32_t)datalink_transmit_buffer->header.length + 3) & 0xfffffffc) -
                     (uint32_t)datalink_transmit_buffer->header.length);
          //计算crc并填入
          uint32_t crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_transmit_buffer,
                                                 (datalink_transmit_buffer->header.length + 3) / 4);
          uint8_t crc_xor =
              (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
          datalink_transmit_buffer->header.crc = crc_xor;
          printf("[Packet] src:%X  dst:%X  transfer[0]:%X  transfer[1]:%X  curr:%X \r\n",
                 datalink_transmit_buffer->header.src_addr, datalink_transmit_buffer->header.dest_addr,
                 datalink_transmit_buffer->header.transfer_addr[0], datalink_transmit_buffer->header.transfer_addr[1],
                 datalink_transmit_buffer->header.curr_addr);
          printf("Before LoraWriteAsync!\r\n");
          //发包
          LoraWriteAsync((const uint8_t *)datalink_transmit_buffer, datalink_transmit_buffer->header.length, false);
          printf("End LoraWriteAsync!\r\n");
        } else {
          lora_send_callback[send_service_number](nullptr, DataLink_Busy);
          send_service_number = LORA_SERVICE_UNAVALIABLE;
        }
        break;
      }
      // m3新增,发送路由包
      case TX_Route_Packet: {
        //如果route buffer还不可用，说明上一个路由包还没成功发送,则把TX_Route_Packet信号压回去，一会儿再发路由包
        if (!route_buffer_avaliable) {
          DataLinkSignal queue_signal = TX_Route_Packet;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          break;
        }
        //如果结点这时正在发送一个普通包,则把TX_Route_Packet信号压回去，一会儿再发路由包
        if (datalink_send_state == TX_Wait_Lora) {
          DataLinkSignal queue_signal = TX_Route_Packet;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          break;
        }
        if (is_send_ack) {  //如果该结点正在往外发ACK包，则暂时不发路由包，把信号放回队列，之后再发
          DataLinkSignal queue_signal = TX_Route_Packet;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
          break;
        }
        printf("[DataLink MainLoop]Prepare sending a route packet!\r\n");
        datalink_send_state = TX_Wait_Lora;
        //准备路由包
        LoraPacket route_packet;
        PrepareRoutePacket(&route_packet);
        // route buffer不可用
        route_buffer_avaliable = false;
        //把route_packet放入路由buffer中
        memcpy(datalink_route_buffer, &route_packet, route_packet.header.length);
        //填充包的尾部，使长度是32bit的整数倍（为了crc的计算）
        memset(((uint8_t *)datalink_route_buffer) + datalink_route_buffer->header.length, 0,
               (((uint32_t)datalink_route_buffer->header.length + 3) & 0xfffffffc) -
                   (uint32_t)datalink_route_buffer->header.length);
        //计算crc并填入
        uint32_t crc_value =
            HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_route_buffer, (datalink_route_buffer->header.length + 3) / 4);
        uint8_t crc_xor =
            (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
        datalink_route_buffer->header.crc = crc_xor;
        //当前正在发路由包
        is_send_route = true;
#ifdef NETWORK_DBG
        printf("[DataLink MainLoop] Sending a route packet from me!\r\n");
#endif
        //调用物理层异步发包
        LoraWriteAsync((const uint8_t *)datalink_route_buffer, datalink_route_buffer->header.length, false);
        break;
      }
      //发送ACK包 —— 没改
      case TX_Ack: {
        assert(is_send_ack == false);
        if (datalink_send_state == TX_Wait_Lora || is_send_route) {
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
      //重传 —— 没改
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
        break;
      }
    }
  }
}
