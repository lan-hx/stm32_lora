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
#include "timers.h"

#ifdef DATALINK_IMPL
typedef uint8_t DataLinkSignal;
#endif

#ifdef DATA_LINK_SEMAPHORE
constexpr uint32_t DATALINK_QUEUE_LEN = 5;
extern QueueHandle_t data_link_queue;
extern StaticQueue_t data_link_queue_buffer;
extern DataLinkSignal data_link_queue_storage[DATALINK_QUEUE_LEN];
extern SemaphoreHandle_t data_link_rx_buffer_semaphore;
extern StaticSemaphore_t data_link_rx_buffer_semaphore_buffer;
extern SemaphoreHandle_t data_link_wifi_semaphore;
extern StaticSemaphore_t data_link_wifi_semaphore_buffer;
extern SemaphoreHandle_t data_link_tx_semaphore;
extern StaticSemaphore_t data_link_tx_semaphore_buffer;
#endif

#ifdef __cplusplus
enum DataLinkErrorEnum : uint8_t {
#else
enum LoraErrorEnum {
#endif
  DataLink_OK,        // 没有错误
  DataLink_Busy,      // 队列已满
  DataLink_TxFailed,  // 超过最大尝试发送次数发送失败
  DataLink_Unknow,
};
typedef uint8_t DataLinkError;

#ifdef DATA_LINK_TIMER
extern TimerHandle_t datalink_resend_timer;
extern StaticTimer_t datalink_resend_timer_buffer;
#endif

// m3新增,网络层路由定时器
extern TimerHandle_t network_route_timer;
extern StaticTimer_t network_route_timer_buffer;

/**
 * @brief 发送buffer，需要加锁
 */
extern LoraPacket *datalink_transmit_buffer;

// 原则：谁获取，谁释放。不是用户API，仅限在网络服务进程内调用。
/**
 * @brief 在调用数据链路层的发送函数之前, 需要首先调用该函数申请buffer的访问权限
 * @note 使用semaphore实现更好
 * @return uint32_t 如果成功返回buffer的大小,失败返回0
 */
uint32_t DataLinkDeclareTransmitBuffer();

/**
 * @brief 在数据链路层发送完成之后调用该函数释放buffer
 * @note 使用samaphore实现更好
 * @return uint32_t 如果成功返回buffer的大小,失败返回0
 */
uint32_t DataLinkReleaseTransmitBuffer();

/* ---------- Heard List Algorithm ---------- */

/**
 * @brief heard list节点
 * @note TODO: 决定是否静态分配
 * @note TODO: 暂未确定
 */
#define HeardListTicks 100
#define HeardListTickReduce 10
#define MaxHeardListNum 10
typedef struct HeardList {
  uint8_t addr;            // 目的地址
  uint8_t next_addr;       // 发送到目的地址的下一跳地址
  uint8_t next_next_addr;  // 发送到目的地址的第二跳地址
  uint8_t cost;            // 总花费
  uint8_t tick;            // 上一次更新
  uint8_t registered_service;
  // struct HeardList *next;
} HeardList;
extern HeardList HeardLists[MaxHeardListNum];

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
bool DataLinkRoute(LoraPacket *pak);

/**
 * @brief heard list清除过期项
 */
void DataLinkHeardListTick();

/* ---------- Heard List Algorithm End ---------- */

typedef void (*LoraPacketCallback_t)(const LoraPacket *, DataLinkError);

/**
 * @brief 发包函数,每次调用前确保已经收到上一次发送的回调函数
 * @param service 调用发送的服务号
 * @param pak 待发送的数据包
 * @param hop 当前处于第几跳
 * @return TODO 发送状态 RTT
 * @note 使用wifi方式发包时hop始终为0
 * @note 发包时借助发包函数阻塞，发包后阻塞直至ack/timeout
 * @note 有些包不需要ack
 */
DataLinkError DataLinkSendPacket(LoraService service, LoraPacket *pak, uint32_t hop = 0);

/**
 * 注册各个服务的回调函数
 * @param func_select 如果是true表示发送,false表示接收功能
 * @param service 服务号，不应大于等于`LORA_SERVICE_NUM`
 * @param callback 回调函数，nullptr为直接丢包
 * @note 服务号大于等于`MIN_LORA_TRANSPORT_SERVICE`应为传输层的包装函数
 * @note 可以覆盖
 * @note 主线程也要初始化broadcast的服务
 */
void DataLinkRegisterService(bool func_select, LoraService service, LoraPacketCallback_t callback);

/**
 * 接收数据包，做一些检查，并中转/ack+按照服务类型分发/丢弃
 * @return TODO 接收状态
 * @note 有些包不需要ack
 * @note 需要实现`LoraRxCallback`，使用freertos锁，采用中断方式接收数据包
 * @note 由主线程中物理层（Lora）和WiFi处理函数调用
 * @note 注：由于数据包可能来自于lora或wifi，所以主线程事件循环需要FreeRTOS事件组
 */
uint32_t DataLinkReceivePacket(uint8_t *pack, uint8_t len, NetworkType network_type);

/**
 * @brief 数据链路层开始接收包
 *
 */
void DataLinkReceivePacketBegin();

/**
 * @brief 数据链路层停止接受包
 *
 */
void DataLinkReceivePacketEnd();

/**
 * @brief 发送超时定时器回调函数
 *
 * @param xTimer
 */
void DataLinkResendCallBack(TimerHandle_t xTimer);

// m3新增
/**
 * @brief 网络层定时发送路由包
 *
 * @param xTimer
 */
void NetworkRouteCallBack(TimerHandle_t xTimer);

// m3新增函数
/*供上层调用，调用此函数后，该结点开始定期向外界转发路由包*/
void NetworkBeginRoute();

// m3新增函数
/*根据本地的路由信息，填写路由包*/
void PrepareRoutePacket(LoraPacket *route_packet);

// m3新增
/*根据收到的路由包，更新本地路由信息*/
void NetworkUpdateHeardList(NetworkLinkStatePacket *pack);

// 打印路由表,为了调试
void PrintRouteTable(void);

void PrintRoutePacket(void);

#ifdef DATALINK_IMPL
void DataLinkEventLoop();
#endif

#ifdef __cplusplus
}
#endif

#endif  // LORA_TASKS_SERVICE_WEB_DATA_LINK_H_
