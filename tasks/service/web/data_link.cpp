/**
 * @brief 数据链路层
 * @author lan
 */

// #define DATALINK_DBG
// #define NETWORK_DBG
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
  RX_Packet,  // 收包
  TX_Packet,  // 发包
  TX_Ack,     // 发ack包
  TX_Retry,   // 发重传包

  TX_Init,             // 初始状态
  TX_Wait_Lora,        // 等到物理层发包
  TX_Packet_Wait,      // 等待Ack包(此时不能发下一个普通包)
  TX_Route_Packet,     // m3新增，发送路由包
  TX_Transfer_Packet,  // m3新增，转发一个包
};

/*发普通包时: TX_Init —— TX_Packet —— TX_Wait_Lora —— TX_Packet_Wait —— TX_Init
                 上层调用发包    主循环调用       Lora发包回调        收到Ack包
                SendPacket    LoraWriteAsync   TxCallBack      或超时准备重发
  发路由包时: 不控制状态，直接发送 通过is_send_route判断当前是否在发路由包，防止LoraWriteAsync的冲突
 */

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
bool is_send_ack, is_send_route, is_send_normal, is_send_transfer;

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

bool transfer_buffer_available;
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

// m3新增  发送路由包的buffer
uint8_t datalink_route_real_buffer[MAX_REAL_LORA_PACKET_SIZE];
LoraPacket *datalink_route_buffer = (LoraPacket *)datalink_route_real_buffer;
// dbq我又开了一个buffer
uint8_t datalink_transfer_real_buffer[MAX_REAL_LORA_PACKET_SIZE];
LoraPacket *datalink_transfer_buffer = (LoraPacket *)datalink_transfer_real_buffer;

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
// 序列号,作为全局变量
// 发普通包时0-1-0-1来回变化
// 发路由包默认seq=0（因为不需要ack）
uint8_t seq = 0;
// crc_value
uint32_t crc_value = 0;
/**
 * @brief 本机注册的服务
 * @note unused
 */
uint8_t registered_service;
// ---------------------------END------------------------------;

// 上层通过调此函数注册回调函数(true=发包 false=收包)
void DataLinkRegisterService(bool func_select, LoraService service, LoraPacketCallback_t callback) {
  assert(service <= LORA_SERVICE_NUM);
  if (func_select) {
    lora_send_callback[service] = callback;
  } else {
    lora_packet_callback[service] = callback;
  }
}

// 上层调用此函数说明开始收包
void DataLinkReceivePacketBegin() {
  LoraReadAsyncStart();
  is_datalink_receive = true;
}

// 上层调用此函数说明截止收包
void DataLinkReceivePacketEnd() {
  LoraReadAsyncStop();
  is_datalink_receive = false;
}

// 上层声明发送buffer —— 发送buffer不可用
uint32_t DataLinkDeclareTransmitBuffer() {
  if (transmit_buffer_avaliable) {
    transmit_buffer_avaliable = false;
  } else {
    return 0;
  }
  return sizeof(datalink_transmit_buffer);
}

// 上层释放发送buffer —— 发送buffer又可用
uint32_t DataLinkReleaseTransmitBuffer() {
  assert(transmit_buffer_avaliable == false);
  transmit_buffer_avaliable = true;
  return sizeof(datalink_transmit_buffer);
}

// 链路层发包
DataLinkError DataLinkSendPacket(LoraService service, LoraPacket *pak, uint32_t hop) {
  // printf("***DataLinkSendPacket***\r\n");
  UNUSED(hop);
  // 上层已经将包填到发送buffer中
  assert(pak == datalink_transmit_buffer);
  // 上层已经声明了发送buffer
  assert(transmit_buffer_avaliable == false);
  // m3修改
  /*发包前，填入序列号 魔数 源地址 当前结点地址*/
  pak->header.settings.seq = seq;
  seq ^= 1;
  pak->header.magic_number = LORA_MAGIC_NUMBER;
  pak->header.src_addr = LORA_ADDR;
  pak->header.curr_addr = LORA_ADDR;
  // 根据中转地址填入
  DataLinkRoute(pak);
#ifdef DATALINK_DBG
  printf("[DataLink Route] fill DataLinkRoute()\r\n");
#endif
  // TX_Packet通知主循环进行发包
  DataLinkSignal queue_signal = TX_Packet;
#ifdef DATALINK_DBG
  printf("---Push Signal TX_Packet---\r\n");
#endif
  if (xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY) != pdTRUE) {
    return DataLink_Busy;
  }

  send_service_number = service;
  return DataLink_OK;
}

// 超时重发的回调函数，当发包定时器到时后，就会调此函数
void DataLinkResendCallBack(TimerHandle_t xTimer) {
  // 此时应处于TX_Packet_Wait状态
  assert(datalink_send_state == TX_Packet_Wait);
  datalink_send_state = TX_Init;
#ifdef DATALINK_DBG
  printf("[Timer] Time Out\r\n");
#endif
  // TX_Retry通知主循环重发包
  DataLinkSignal queue_signal = TX_Retry;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
  printf("---Push Signal TX_Retry---\r\n");
#endif
  portYIELD();
}

// m3新增
/*定时发送路由包，暂定5s一次*/
void NetworkRouteCallBack(TimerHandle_t xTimer) {
  DataLinkHeardListTick();  // 清除本地路由表的过期项
  // TX_Route_Packet通知主循环发路由包
  DataLinkSignal queue_signal = TX_Route_Packet;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
  printf("---Push Signal TX_Route_Packet---\r\n");
#endif
  portYIELD();  // 让出CPU
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
#ifdef DATALINK_DBG
    printf("---Push Signal RX_Packet---\r\n");
#endif
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
// 发送 ACK包/路由包/普通包 成功/失败 都加了输出
void LoraTxCallback(LoraError state) {
  // 发送成功
  if (state == Lora_OK) {
    // 发送的是ACK包
    if (is_send_ack) {
#ifdef DATALINK_DBG
      printf("[LoraTxCallback]Send an ack packet successfully!\r\n");
#endif
      is_send_ack = false;
      // ack buffer变成可用,即可以再发其他ack包
      ack_buffer_avaliable = true;
    }
    // 发送的是路由包
    else if (is_send_route) {
#ifdef DATALINK_DBG
      printf("[LoraTxCallback]Send a route packet successfully!\r\n");
#endif
      is_send_route = false;
      // route buffer变成可用,即可以发送新的路由包了
      route_buffer_avaliable = true;
      // 开启路由定时器,为了周期性发送路由包
      xTimerStart(network_route_timer, 0);
    }
    // 发送的是转发包
    else if (is_send_transfer) {
#ifdef DATALINK_DBG
      printf("[LoraTxCallback]Send a transfer packet successfully!\r\n");
#endif
      is_send_transfer = false;
      transfer_buffer_available = true;
    }
    // 发送的是普通包
    else {
#ifdef DATALINK_DBG
      printf("[LoraTxCallback]Send a normal packet successfully!\r\n");
#endif
      is_send_normal = false;
      datalink_send_state = TX_Packet_Wait;  // 状态变为等待Ack
      // 开启定时器,为了重传
      xTimerStart(datalink_resend_timer, 0);
    }
#ifdef DATALINK_DBG
    printf("[CallBack From Lora] Send a packet succ.\r\n");
#endif
  }
  // 发送失败
  else {
    // 发送ACK包失败
    if (is_send_ack) {
      is_send_ack = false;
      // TX_Ack通知主循环重发Ack包
      DataLinkSignal queue_signal = TX_Ack;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
      printf("---Push Signal TX_Ack---\r\n");
      printf("[LoraTxCallback]Failed to send an ack packet!\r\n");
#endif
    }
    // 发送路由包失败
    else if (is_send_route) {
      is_send_route = false;
      // TX_Route_Packet通知主循环发送路由包
      DataLinkSignal queue_signal = TX_Route_Packet;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
      printf("---Push Signal TX_Route_Packet---\r\n");
      printf("[LoraTxCallback]Failed to send a route packet!\r\n");
#endif
    }
    // 发送的是转发包
    else if (is_send_transfer) {
      is_send_transfer = false;
      // TX_Route_Packet通知主循环发送路由包
      DataLinkSignal queue_signal = TX_Transfer_Packet;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
      printf("---Push Signal TX_Transfer_Packet---\r\n");
      printf("[LoraTxCallback]Failed to Send a transfer packet!\r\n");
#endif
    }
    // 发送普通包失败
    else {
      is_send_normal = false;
      // TX_Retry通知主循环重传
      DataLinkSignal queue_signal = TX_Retry;
      datalink_send_state = TX_Init;
      xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
      printf("---Push Signal TX_Retry---\r\n");
      printf("[LoraTxCallback]Failed to send a normal packet!\r\n");
#endif
    }
  }
}

const HeardList *DataLinkGetHeardList() { return HeardLists; }

// m3新增函数
/*供上层调用，调用此函数后，该结点开始定期向外界转发路由包*/
void NetworkBeginRoute() {
  // printf("**{NetWork Route Start}**!\r\n");
  DataLinkSignal queue_signal = TX_Route_Packet;
  xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
  // printf("---Push Signal TX_Route_Packet---\r\n");
}

// m3新增函数
/*根据本地的路由信息，填写路由包*/
void PrepareRoutePacket(LoraPacket *route_packet) {
  // 路由包的序列号都为0
  route_packet->header.settings.seq = 0;
  // 填入源地址  目标地址(FF表示广播)  包的长度=最大长度  当前地址
  route_packet->header.dest_addr = 0xFF;
  route_packet->header.src_addr = LORA_ADDR;
  route_packet->header.curr_addr = LORA_ADDR;
  route_packet->header.magic_number = LORA_MAGIC_NUMBER;
  route_packet->header.transfer_addr[0] = route_packet->header.transfer_addr[1] = 0;
  NetworkLinkStatePacket *link_state_pack = (NetworkLinkStatePacket *)(route_packet->content);
  memset(link_state_pack, 0, MAX_LORA_CONTENT_LENGTH);
  // 向路由包填入内容，遍历本地的路由表
  link_state_pack->sender_addr = LORA_ADDR;
  // link_state_pack->registered_service = LORA_SERVICE_LINK_STATE;
  int num = 0;
  for (int i = 0; i < MaxHeardListNum; i++) {
    if (HeardLists[i].tick > 0 && HeardLists[i].cost == 1) {  // 不经中转
      link_state_pack->neighbor[num].next_addr = HeardLists[i].addr;
      link_state_pack->neighbor[num].next_next_addr = 0xFF;
      link_state_pack->neighbor[num].registered_service = LORA_SERVICE_LINK_STATE;
      num++;
    } else if (HeardLists[i].tick > 0 && HeardLists[i].cost == 2) {  // 经一跳中转
      link_state_pack->neighbor[num].next_addr = HeardLists[i].next_addr;
      link_state_pack->neighbor[num].next_next_addr = HeardLists[i].addr;
      link_state_pack->neighbor[num].registered_service = LORA_SERVICE_LINK_STATE;
      num++;
    }
    // cost=3的无需在填入路由包，转发给邻居了
    link_state_pack->num = num;
  }
  // 填入包的长度 = 包头长度+ 2个uint8_t(num sender_addr) + (路由条目数*3)个uint8_t
  route_packet->header.length = sizeof(LoraPacketHeader) + 2 * sizeof(uint8_t) + num * sizeof(uint8_t) * 3;
}

// m3新增函数  填写中转地址，如果找不到中转地址则返回0，找到返回1
/*函数作用：根据本地路由表信息，在LoraPacket里面填入中转地址信息transfer_addr
qx修改:transfer_addr数组最多两个元素，只记中转地址，不计目标地址*/
bool DataLinkRoute(LoraPacket *pak) {
  for (auto a : HeardLists) {
    if (a.addr == pak->header.dest_addr) {
      if (a.cost == 1) {  // 不需中转
        pak->header.transfer_addr[0] = 0xFF;
        pak->header.transfer_addr[1] = 0xFF;
      }
      if (a.cost == 2) {  // 经一跳中转
        pak->header.transfer_addr[0] = a.next_addr;
        pak->header.transfer_addr[1] = 0xFF;
      }
      if (a.cost == 3) {  // 经两跳中转
        pak->header.transfer_addr[0] = a.next_addr;
        pak->header.transfer_addr[1] = a.next_next_addr;
      }
      return 1;
    }
  }
  return 0;
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
          if (pack->neighbor[i].next_addr != 0xFF && pack->neighbor[i].next_addr != LORA_ADDR) {
            HeardLists[j].tick = HeardListTicks;
            HeardLists[j].addr = pack->neighbor[i].next_addr;
            HeardLists[j].next_addr = pack->sender_addr;
            HeardLists[j].cost = 2;
            HeardLists[j].registered_service = pack->neighbor[i].registered_service;
            HeardListOverstack = 0;
            break;
          } else if (pack->neighbor[i].next_next_addr != 0xFF && pack->neighbor[i].next_next_addr != LORA_ADDR) {
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
      //      assert(HeardListOverstack < 1);
    }
  }
}

// 打印本地路由包，为了调试
void PrintRouteTable(void) {
  //  for (int i = 0; i < 4; i++) {
  //    printf("HeardLists[%d],addr =%x, next_addr = %x, next_next_addr = %x,cost=%d,tick = %d \r\n", i,
  //    HeardLists[i].addr,
  //           HeardLists[i].next_addr, HeardLists[i].next_next_addr, HeardLists[i].cost, HeardLists[i].tick);
  //  }
}
// 打印发送的路由包，为了调试
void PrintRoutePacket(void) {
  //  printf("---------------Route Packet Information----------------\r\n");
  //  NetworkLinkStatePacket *pak = (NetworkLinkStatePacket *)datalink_route_buffer->content;
  //  printf("    route packet entry num = %d\r\n", pak->num);
  //  for (int i = 0; i < pak->num; i++) {
  //    printf("    next_addr = %X  next_next_addr = %X\r\n", pak->neighbor[i].next_addr,
  //    pak->neighbor[i].next_next_addr);
  //  }
  //  printf("------------------------------------------------------\r\n");
}
/**
 * @brief 数据链路层事件处理
 */
void DataLinkEventLoop() {
  DataLinkSignal opt;
  bool crc_state = false;
  datalink_send_state = TX_Init;
  is_send_ack = false, is_send_route = false, is_send_normal = false, is_send_transfer = false;
  //  send_service_number = LORA_SERVICE_UNAVALIABLE;
  send_service_number = LORA_SERVICE_LINK_STATE;
  is_datalink_receive = false;

  transmit_buffer_avaliable = true, ack_buffer_avaliable = true, route_buffer_avaliable = true,
  transfer_buffer_available = true;
  uint8_t retry_count = 0;
  xSemaphoreGive(data_link_rx_buffer_semaphore);
  while (true) {
    xQueueReceive(data_link_queue, &opt, portMAX_DELAY);
#ifdef DATALINK_DBG
    printf("---queue_num = %d\r\n", uxQueueMessagesWaiting(data_link_queue));
    printf("---Fetch a Signal!,%d\r\n", opt);
    printf("[DataLink MainLoop] running, operation = %d\r\n", opt);
#endif
    switch (opt) {
      // 收包逻辑:
      /* 目的地址是自己，则直接发 —— m2已实现
         中转地址是自己 —— 转发这个包(需要修改包头的curr_addr为自己)
         广播包 —— 根据路由包更新本地路由包 NetworkUpdateHeardList(NetworkLinkStatePacket *pack)
         其他情况 —— 丢弃(类似于m2对目的地址不是自己的包的处理)
       */
      case RX_Packet: {
        // 上一跳的结点不是拒绝的才会接受这个包
        if (datalink_receive_buffer->header.magic_number == LORA_MAGIC_NUMBER &&
            datalink_receive_buffer->header.curr_addr != REJECTED_LORA_ADDR) {
          if (datalink_rx_packet_length < 8 || datalink_rx_packet_length != datalink_receive_buffer->header.length) {
#ifdef DATALINK_DBG
            printf("[DataLink MainLoop] warning: Packet length not correct!\r\n");
#endif
          }
          // CRC校验，校验通过:crc_state=true,否则crc_state=false
          //                                 检验不通过有输出
          // CRC校验过不去
          crc_state = true;
          uint8_t packet_crc = datalink_receive_buffer->header.crc;
          datalink_receive_buffer->header.crc = 0;
          crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_receive_buffer,
                                        (datalink_receive_buffer->header.length + 3) / 4);
          uint8_t crc_xor =
              (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
          if (crc_xor == packet_crc) {
            crc_state = true;
          } else {
            //printf("[DataLink MainLoop] Datalink crc incorrect!\r\n");
          }
          // 收到目的地址是自己的包，这部分和m2一样
          if (datalink_receive_buffer->header.dest_addr == LORA_ADDR) {
            // 收到ack包
            if (datalink_receive_buffer->header.settings.ack) {
#ifdef DATALINK_DBG
              printf("[datalink]RECEIVE A ACK\r\n");
#endif
              if (crc_state && datalink_send_state == TX_Packet_Wait &&
                  datalink_receive_buffer->header.settings.seq == datalink_transmit_buffer->header.settings.seq) {
                // 回到TX_Init状态
                datalink_send_state = TX_Init;
                // 停止重传定时器
                xTimerStop(datalink_resend_timer, 0);
                // 收到ack包，会调上层的发包回调，send_state = DataLink_OK
                lora_send_callback[send_service_number](nullptr, DataLink_OK);
                // printf("---lora_send_callback:DataLink_OK---!\r\n");
                send_service_number = LORA_SERVICE_UNAVALIABLE;
              }
              xSemaphoreGive(data_link_rx_buffer_semaphore);
            }
            // 收到nack包
            else if (datalink_receive_buffer->header.settings.nak) {
              if (crc_state && datalink_send_state == TX_Packet_Wait &&
                  datalink_receive_buffer->header.settings.seq == datalink_transmit_buffer->header.settings.seq) {
                // 回到TX_Init状态
                datalink_send_state = TX_Init;
                // 停止重传定时器
                xTimerStop(datalink_resend_timer, 0);
                // 直接重传
                DataLinkSignal queue_signal = TX_Retry;
                xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);

#ifdef DATALINK_DBG
                printf("[DataLink MainLoop] Receive a NAK packet, retransmit!\r\n");
#endif
              }
              xSemaphoreGive(data_link_rx_buffer_semaphore);
            }
            // 收到普通包
            else {
              // 此时ack_buffer不可用(即上一个要发送的ack还没有发送,则一会儿再收这个包)
              if (!ack_buffer_avaliable) {
                DataLinkSignal queue_signal = RX_Packet;
                xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
                printf("---Push Signal RX_Packet---\r\n");
#endif
                break;
              }
              ack_buffer_avaliable = false;
              // 如果路由表里还没有目标地址的项，之后再发送
              if (crc_state) {  // 校验通过发送ACK
                uint8_t service_number = datalink_receive_buffer->header.settings.service;
                if (is_datalink_receive && service_number < LORA_SERVICE_NUM &&
                    lora_packet_callback[service_number] != nullptr) {
                  lora_packet_callback[service_number](datalink_receive_buffer, 0);
                }
                datalink_ack_buffer->dest_addr = datalink_receive_buffer->header.src_addr;
                datalink_ack_buffer->transfer_addr[0] = datalink_ack_buffer->transfer_addr[1] = 0;
                datalink_ack_buffer->curr_addr = LORA_ADDR;

                datalink_ack_buffer->src_addr = LORA_ADDR;
                datalink_ack_buffer->magic_number = LORA_MAGIC_NUMBER;
                datalink_ack_buffer->settings = {
                    datalink_receive_buffer->header.settings.seq, true, false, false, 0, 0};
                datalink_ack_buffer->length = sizeof(LoraPacketHeader);
                datalink_ack_buffer->crc = 0;
              } else {  // 校验未通过发送nak
                // 发送nak
                datalink_ack_buffer->dest_addr = datalink_receive_buffer->header.src_addr;
                datalink_ack_buffer->transfer_addr[0] = datalink_ack_buffer->transfer_addr[1] = 0;
                datalink_ack_buffer->curr_addr = LORA_ADDR;
                datalink_ack_buffer->src_addr = LORA_ADDR;
                datalink_ack_buffer->magic_number = LORA_MAGIC_NUMBER;
                datalink_ack_buffer->settings = {
                    datalink_receive_buffer->header.settings.seq, false, false, true, 0, 0};
                datalink_ack_buffer->length = sizeof(LoraPacketHeader);
                datalink_ack_buffer->crc = 0;
              }
              xSemaphoreGive(data_link_rx_buffer_semaphore);
              // 如果ACK包目前填不了中转地址，则先不回这个ACK，把ack_buffer让出来
              if (!DataLinkRoute((LoraPacket *)datalink_ack_buffer)) {
                ack_buffer_avaliable = true;
                break;
              }
              // 压入TX_Ack，来发送ACK包
              DataLinkSignal queue_signal = TX_Ack;
              xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
              printf("---Push Signal TX_Ack---\r\n");
#endif
            }
          }
          // 收到中转地址是自己的包，转发
          else if (datalink_receive_buffer->header.transfer_addr[0] == LORA_ADDR ||
                   datalink_receive_buffer->header.transfer_addr[1] == LORA_ADDR) {
            // 如果此时我的中转buffer不可用,则把RX_Packet压回去，过一会儿再收包
            if (!transfer_buffer_available) {
              DataLinkSignal queue_signal = RX_Packet;
              xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
              printf("---Push Signal RX_Packet---\r\n");
              printf(".......................\r\n");
#endif
              break;
            }
            transfer_buffer_available = false;
            // 拷贝这个包到转发队列
            memcpy(datalink_transfer_buffer, datalink_receive_buffer, datalink_receive_buffer->header.length);
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            DataLinkSignal queue_signal = TX_Transfer_Packet;
            xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
            printf("---Push Signal TX_Transfer_Packet---\r\n");
#endif
            printf("[DataLink MainLoop] Receive a packet needed to transfer,dest = %X,ACK = %X!",
                   datalink_receive_buffer->header.dest_addr, datalink_receive_buffer->header.settings.ack);

          }
          // 收到广播的路由包
          else if (datalink_receive_buffer->header.dest_addr == 0xFF) {
            // 根据收到的路由包，更新本地的路由表
            NetworkUpdateHeardList((NetworkLinkStatePacket *)datalink_receive_buffer->content);
            xSemaphoreGive(data_link_rx_buffer_semaphore);
            // 打印路由表
#ifdef DATALINK_DBG
            printf("Receive a route packet!\r\n");
            printf("------------------------------------\r\n");
            PrintRouteTable();
            printf("------------------------------------\r\n");
#endif
          }
          // 其他情况不作处理,释放接受buffer的锁(不再管这个包)
          else {
            xSemaphoreGive(data_link_rx_buffer_semaphore);
          }
        } else {
          xSemaphoreGive(data_link_rx_buffer_semaphore);
        }
        break;
      }
      // 发包逻辑
      case TX_Packet: {
        // printf("In TX_Packet!\r\n");
        if (datalink_send_state == TX_Init) {
          /*m3修改*/
          // 如果该结点目前正在往外发ack包/路由包，则此时不能发包，把Tx_Packet信号压回队列，之后再发
          if (is_send_ack || is_send_route || is_send_transfer) {
            DataLinkSignal queue_signal = TX_Packet;
            xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
            printf("---Push Signal TX_Packet---\r\n");
#endif
            break;
          }
          // 长度为0的包不发
          if (datalink_transmit_buffer->header.length == 0) {
            break;
          }
          // 如果路由表里还没有目标地址的项，把TX_Packet信号压回队列，之后再发送
          if (!DataLinkRoute(datalink_transmit_buffer)) {
            // DataLinkSignal queue_signal = TX_Packet;
            // xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
            lora_send_callback[send_service_number](nullptr, DataLink_Unreachable);
            // printf("---lora_send_callback:DataLink_Unreachable---!\r\n");
            send_service_number = LORA_SERVICE_UNAVALIABLE;
            break;
          }
          datalink_send_state = TX_Wait_Lora;
          retry_count = 0;
          // 新增
          datalink_transmit_buffer->header.crc = 0;
          // 确保发送的32字节的整数倍,不是的话后面填0
          memset(((uint8_t *)datalink_transmit_buffer) + datalink_transmit_buffer->header.length, 0,
                 (((uint32_t)datalink_transmit_buffer->header.length + 3) & 0xfffffffc) -
                     (uint32_t)datalink_transmit_buffer->header.length);
          // 计算crc并填入
          crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_transmit_buffer,
                                        (datalink_transmit_buffer->header.length + 3) / 4);
          uint8_t crc_xor =
              (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
          datalink_transmit_buffer->header.crc = crc_xor;
          // 发包
          is_send_normal = true;

          printf(
              "[DataLinkMainLoop]Send a normal packet!  src=%X  dst=%X  curr=%X  transfer[0]=%X transfer[1] = %X seq = "
              "%d length = %d\r\n ",
              datalink_transmit_buffer->header.src_addr, datalink_transmit_buffer->header.dest_addr,
              datalink_transmit_buffer->header.curr_addr, datalink_transmit_buffer->header.transfer_addr[0],
              datalink_transmit_buffer->header.transfer_addr[1], datalink_transmit_buffer->header.settings.seq,
              datalink_transmit_buffer->header.length);

          LoraWriteAsync((const uint8_t *)datalink_transmit_buffer, datalink_transmit_buffer->header.length, false);
        } else {
          // 如果调用发包时，上一个包还没有搞定，调上层回调，send_state = DataLink_Busy

          lora_send_callback[send_service_number](nullptr, DataLink_Busy);
          // printf("---lora_send_callback:DataLink_Busy---!\r\n");
          send_service_number = LORA_SERVICE_UNAVALIABLE;
        }
        break;
      }
      // m3新增,发送路由包
      case TX_Route_Packet: {
        // 如果route buffer还不可用，说明上一个路由包还没成功发送,则把TX_Route_Packet信号压回去，一会儿再发路由包
        if (!route_buffer_avaliable) {
          DataLinkSignal queue_signal = TX_Route_Packet;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
          printf("---Push Signal TX_Route_Packet---\r\n");
#endif
          break;
        }
        // 如果结点这时正在发送一个普通包/ACK/转发包,则把TX_Route_Packet信号压回去，一会儿再发路由包
        if (is_send_ack || is_send_normal || is_send_transfer) {
          DataLinkSignal queue_signal = TX_Route_Packet;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
          printf("---Push Signal TX_Route_Packet---\r\n");
#endif
          break;
        }
        // route buffer不可用
        route_buffer_avaliable = false;
        //  准备路由包
        PrepareRoutePacket(datalink_route_buffer);
        // 填写CRC
        datalink_route_buffer->header.crc = 0;
        memset(((uint8_t *)datalink_route_buffer) + datalink_route_buffer->header.length, 0,
               (((uint32_t)datalink_route_buffer->header.length + 3) & 0xfffffffc) -
                   (uint32_t)datalink_route_buffer->header.length);
        crc_value =
            HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_route_buffer, (datalink_route_buffer->header.length + 3) / 4);
        uint8_t crc_xor =
            (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
        datalink_route_buffer->header.crc = crc_xor;
        // 当前正在发路由包
        is_send_route = true;
        // 打印发的路由包（调试用）
        //        printf("[DataLink MainLoop]Send a route packet!  src=%X  dst=%X  length=%d\r\n",
        //               datalink_route_buffer->header.src_addr, datalink_route_buffer->header.dest_addr,
        //               datalink_route_buffer->header.length);
        PrintRoutePacket();
        // 调用物理层异步发包
        LoraWriteAsync((const uint8_t *)datalink_route_buffer, datalink_route_buffer->header.length, false);
        break;
      }
      case TX_Transfer_Packet: {
        assert(is_send_transfer == false);
        // 如果正在发normal/ack/route包，则把TX_Transfer_Packet信号压回去，一会儿再转发这个包
        if (is_send_normal || is_send_ack || is_send_route) {
          DataLinkSignal queue_signal = TX_Transfer_Packet;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
          printf("---Push Signal TX_Route_Packet---\r\n");
#endif
          break;
        }
        // 转发的时候只需要改curr_addr为自己的地址
        datalink_transfer_buffer->header.curr_addr = LORA_ADDR;
        // 重新计算CRC
        datalink_transfer_buffer->header.crc = 0;
        memset(((uint8_t *)datalink_transfer_buffer) + datalink_transfer_buffer->header.length, 0,
               (((uint32_t)datalink_transfer_buffer->header.length + 3) & 0xfffffffc) -
                   (uint32_t)datalink_transfer_buffer->header.length);
        crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_transfer_buffer,
                                      (datalink_transfer_buffer->header.length + 3) / 4);
        uint8_t crc_xor =
            (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
        datalink_transfer_buffer->header.crc = crc_xor;
        // 调用物理层异步发包
        is_send_transfer = true;
        LoraWriteAsync((const uint8_t *)datalink_transfer_buffer, datalink_transfer_buffer->header.length, false);
        break;
      }
        // 发送ACK包
      case TX_Ack: {
        assert(is_send_ack == false);
        if (is_send_route || is_send_normal || is_send_transfer) {
          DataLinkSignal queue_signal = TX_Ack;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
          printf("---Push Signal TX_Ack---\r\n");
#endif
          break;
        }
        // 填CRC
        datalink_ack_buffer->crc = 0;
        memset(((uint8_t *)datalink_ack_buffer) + datalink_ack_buffer->length, 0,
               (((uint32_t)datalink_ack_buffer->length + 3) & 0xfffffffc) - (uint32_t)datalink_ack_buffer->length);
        crc_value = HAL_CRC_Calculate(&hcrc, (uint32_t *)datalink_ack_buffer, (datalink_ack_buffer->length + 3) / 4);
        uint8_t crc_xor =
            (crc_value & 0xFF) ^ ((crc_value >> 8) & 0xFF) ^ ((crc_value >> 16) & 0xFF) ^ ((crc_value >> 24) & 0xFF);
        datalink_ack_buffer->crc = crc_xor;
        is_send_ack = true;
        printf("[DataLink MainLoop]Send an ack packet!  src=%X  dst=%X  seq=%d  length=%d\r\n",
               datalink_ack_buffer->src_addr, datalink_ack_buffer->dest_addr, datalink_ack_buffer->settings.seq,
               datalink_ack_buffer->length);
        LoraWriteAsync((const uint8_t *)datalink_ack_buffer, sizeof(LoraPacketHeader), true);
        break;
      }
      // 重传
      case TX_Retry: {
        assert(datalink_send_state == TX_Init);
        // 超过重传次数 直接调回调
        if (retry_count > DATA_LINK_RETRY - 1) {
          // 如果重传超过最大次数，调上层回调send_state = DataLink_TxFailed
          // lora_send_callback[send_service_number](nullptr, DataLink_TxFailed);
          printf("---lora_send_callback:DataLink_TxFailed---!\r\n");
          send_service_number = LORA_SERVICE_UNAVALIABLE;
          break;
        }
        // 正在发ack包或者路由包，把TX_Retry信号压回去，一会儿再重传
        if (is_send_ack || is_send_route || is_send_normal || is_send_transfer) {
          DataLinkSignal queue_signal = TX_Retry;
          xQueueSend(data_link_queue, &queue_signal, portMAX_DELAY);
#ifdef DATALINK_DBG
          printf("---Push Signal TX_Retry---\r\n");
#endif
          break;
        }
        datalink_send_state = TX_Wait_Lora;
        retry_count++;
#ifdef DATALINK_DBG
        printf("[DataLink MainLoop] Retransmit retry = %d\r\n", retry_count);
        printf(
            "[DataLinkMainLoop]Send a retry packet!  retry_count=%d  src=%X  dst=%X  curr=%X  transfer[0]=%X "
            "transfer[1]=%X  seq = %d "
            " length=%d  \r\n",
            retry_count, datalink_transmit_buffer->header.src_addr, datalink_transmit_buffer->header.dest_addr,
            datalink_transmit_buffer->header.curr_addr, datalink_transmit_buffer->header.transfer_addr[0],
            datalink_transmit_buffer->header.transfer_addr[1], datalink_transmit_buffer->header.settings.seq,
            datalink_transmit_buffer->header.length);
#endif
        // 发送重传包 也认为是在发送一个普通包
        is_send_normal = true;
        LoraWriteAsync((const uint8_t *)datalink_transmit_buffer, datalink_transmit_buffer->header.length, false);
        break;
      }
      default: {
        break;
      }
    }
  }
}

