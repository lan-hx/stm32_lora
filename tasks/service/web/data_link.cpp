/**
 * @brief 数据链路层
 * @author lan
 */

#define DATA_LINK_SEMAPHORE
#include "service/web/data_link.h"

#define LORA_SEMAPHORE
#include "service/lora/lora.h"

SemaphoreHandle_t data_link_wifi_semaphore;
StaticSemaphore_t data_link_wifi_semaphore_buffer;

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

/**
 * @brief 发送buffer
 */
LoraPacket datalink_transmit_buffer;

/**
 * @brief 接收buffer
 */
LoraPacket datalink_receive_buffer;

/**
 * @brief 数据链路层事件处理
 */
void DataLinkEventLoop() {}
