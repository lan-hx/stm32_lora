/**
 * @brief 数据链路层
 * @author lan
 */

#ifndef LORA_TASKS_SERVICE_WEB_DATA_LINK_H_
#define LORA_TASKS_SERVICE_WEB_DATA_LINK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "config.h"
#include "semphr.h"

/**
 * TODO
 */
#ifdef DATA_LINK_SEMAPHORE
// extern SemaphoreHandle_t data_link_lora_semaphore;
// extern StaticSemaphore_t data_link_lora_semaphore_buffer;
extern SemaphoreHandle_t data_link_wifi_semaphore;
extern StaticSemaphore_t data_link_wifi_semaphore_buffer;
#endif

#ifdef DATA_LINK_TIMER
// TODO: add software timer
#endif

/**
 * @brief 发送buffer，需要加锁
 */
extern LoraPacket datalink_transmit_buffer;

/**
 * @brief 接收buffer，需要加锁
 */
extern LoraPacket datalink_receive_buffer;

// 原则：谁获取，谁释放。不是用户API，仅限在网络服务进程内调用。
uint32_t DataLinkDeclareTransmitBuffer();
uint32_t DataLinkReleaseTransmitBuffer();
uint32_t DataLinkDeclareReceiveBuffer();
uint32_t DataLinkReleaseReceiveBuffer();

/* ---------- Heard List Algorithm ---------- */

/**
 * @brief heard list节点
 * @note TODO: 决定是否静态分配
 * @note TODO: 暂未确定
 */
typedef struct HeardList {
  uint8_t addr;
  // ...
  // uint32_t RTT;
  // uint8_t prev_addr;
  // uint32_t cost;
  // uint8_t cost;
  uint8_t seq;
  uint8_t tick;
  uint8_t registered_service;
  // struct HeardList *prev;
  struct HeardList *next;
} HeardList;

/**
 * 获取HeardList
 * @return heard list
 */
const HeardList *DataLinkGetHeardList();

/**
 * @brief 向周围节点广播，加入/更新网络
 * @param link_state_pack link state packet
 * @note 无需ack
 */
void DataLinkBroadcast(NetworkLinkStatePacket *link_state_pack);

/**
 * @brief 根据HeardList填入数据包中转站信息
 * @param pak 待发送的数据包
 */
void DataLinkRoute(LoraPacket *pak);

/**
 * @brief heard list清除过期项
 */
void DataLinkHeardListTick();

/* ---------- Heard List Algorithm End ---------- */

typedef void (*LoraPacketCallback_t)(LoraPacket *);

/**
 * @brief 发包
 * @param pak 待发送的数据包
 * @param hop 当前处于第几跳
 * @return TODO 发送状态 RTT
 * @note 使用wifi方式发包时hop始终为0
 * @note 发包时借助发包函数阻塞，发包后阻塞直至ack/timeout
 * @note 有些包不需要ack
 */
uint32_t DataLinkSendPacket(LoraPacket *pak, uint32_t hop = 0);

/**
 * 注册各个服务的回调函数
 * @param service 服务号，不应大于等于`LORA_SERVICE_NUM`
 * @param callback 回调函数，nullptr为直接丢包
 * @note 服务号大于等于`MIN_LORA_TRANSPORT_SERVICE`应为传输层的包装函数
 * @note 可以覆盖
 * @note 主线程也要初始化broadcast的服务
 */
void DataLinkRegisterService(LoraService service, LoraPacketCallback_t callback);

/**
 * 接收数据包，做一些检查，并中转/ack+按照服务类型分发/丢弃
 * @return TODO 接收状态
 * @note 有些包不需要ack
 * @note 需要实现`LoraRxCallbackFromISR`，使用freertos锁，采用中断方式接收数据包
 * @note 由主线程中物理层（Lora）和WiFi处理函数调用
 * @note 注：由于数据包可能来自于lora或wifi，所以主线程事件循环需要FreeRTOS事件组
 */
uint32_t DataLinkReceivePacket(uint8_t *pack, uint8_t len, NetworkType network_type);

#ifdef __cplusplus
}
#endif

#endif  // LORA_TASKS_SERVICE_WEB_DATA_LINK_H_
