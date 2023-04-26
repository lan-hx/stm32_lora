/**
 * @brief 网络层
 * @author lan
 */

#ifndef LORA_TASKS_SERVICE_WEB_NETWORK_H_
#define LORA_TASKS_SERVICE_WEB_NETWORK_H_

#include "service/web/config.h"
#include "service/web/data_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 网络层buffer，收发共用
 */
// #ifdef SERVICE_NETWORK_IMPL
// extern uint8_t network_buffer[MAX_NETWORK_PACKET];
// #else // SERVICE_NETWORK_IMPL
// extern const uint8_t network_buffer[MAX_NETWORK_PACKET];
// #endif // SERVICE_NETWORK_IMPL

/**
 * @brief 声明buffer用于发送数据
 * 若没有接收到任何数据包，暂停接收过程，否则阻塞
 * @return buffer
 */
// uint8_t *NetworkSendBufferDeclare();

/**
 * @brief 交出buffer使用权
 */
// void NetworkSendBufferRelease();

/* ---------- Link State Algorithm ---------- */

/**
 * cost计算函数
 * @param hop 已经经过多少跳
 * @param sum_RTT sum of RTT
 * @return cost
 */
__attribute((always_inline, deprecated)) inline uint32_t DataLinkHeardListGetCost(uint8_t hop, uint32_t sum_RTT) {
  return 2000 * hop + sum_RTT;
}

/**
 * @brief 根据收到的链路层广播包更新heard list
 * @param pack 收到的link state包
 * @note 不需要在这个函数内跑dijstra
 */
void NetworkUpdateHeardList(NetworkLinkStatePacket *pack);

/**
 * @brief 根据heard list计算并更新路由信息
 */
void NetworkCalculateHeardList();

#ifdef __cplusplus
}
#endif

#endif  // LORA_TASKS_SERVICE_WEB_NETWORK_H_
