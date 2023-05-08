/**
 * @brief LoRa wrapper module
 * @author lan
 */

#ifndef LORA_CORE_SRC_LORA_H_
#define LORA_CORE_SRC_LORA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"

#ifdef LORA_IMPL
typedef uint8_t LoraSignal;
constexpr uint32_t DIFS = 100;
constexpr uint32_t SIFS = 50;
#endif

#ifdef LORA_SEMAPHORE
constexpr uint32_t LORA_QUEUE_LEN = 5;
extern QueueHandle_t lora_queue;
extern StaticQueue_t lora_queue_buffer;
extern LoraSignal lora_queue_storage[LORA_QUEUE_LEN];
extern SemaphoreHandle_t lora_sync_api_semaphore;
extern StaticSemaphore_t lora_sync_api_semaphore_buffer;
#endif

// extern enum LoraSignal lora_global_signal;

#ifdef __cplusplus
enum LoraErrorEnum : uint8_t {
#else
enum LoraErrorEnum {
#endif
  Lora_OK,  // 没有错误
  Lora_Invalid,
  Lora_NotInitialized,         // LoRa模块没有初始化
  Lora_TxBusy,                 // 缓冲区内已经有发包请求或正在发包
  Lora_RxBusy,                 // 缓冲区内已经有同步收包请求或正在同步收包（仅用于同步API）
  Lora_TxExceedMaxTry,         // 超过最大尝试发送次数
  Lora_RxBufferNotEnough,      // 提供的缓冲区大小不足（仅用于同步API）
  Lora_RxAsyncAlreadyStarted,  // 连续接收已开启
  Lora_RxAsyncAlreadyStopped,  // 连续接收已关闭
  Lora_CRCError,               // 收到包，但是CRC校验失败
  // Lora_RxCancel, // 用户取消了连续接收过程（仅用于同步API）
};
typedef uint8_t LoraError;
extern LoraError LoraSyncAPIGlobalError;

/**
 * @brief 初始化LoRa模块，设置中断
 * @return 是否成功
 * @note 应在uart和HAL初始化完毕后调用
 * @note init失败将导致reset
 */
LoraError LoraInit();

/**
 * @brief LoRa模块发包异步API
 * @param[in] s 待写入内容地址
 * @param[in] len 待写入内容长度
 * @return TODO
 * @note 在上一次发包未回调时再次调用是未定义行为，推荐实现为打断上次发包任务
 */
LoraError LoraWriteAsync(const uint8_t *s, uint8_t len, bool is_ack);

/**
 * @brief 发包中断回调函数
 * @param[in] state TODO: 发包反馈
 * @note 由用户实现
 */
void LoraTxCallback(LoraError state);

/**
 * LoRa write同步系统调用
 * @param[in] s 待写入内容地址
 * @param[in] len 待写入内容长度
 * @return 写入字符数，-1为失败
 * @note 该API仅用于测试
 */
__attribute__((deprecated)) int LoraWrite(const uint8_t *s, int len);

/**
 * @brief 启用LoRa模块收包功能
 * @return TODO
 */
LoraError LoraReadAsyncStart();

/**
 * @brief 停止LoRa模块收包功能
 * @return TODO
 * @note LoRa模块应确保stop时即使有未收完的包后续也不会callback
 */
LoraError LoraReadAsyncStop();

/**
 * @brief 收包中断回调函数
 * @param[in] s 缓冲区地址
 * @param[in] len 数据长度，最大为256
 * @param[in] state 接收状态
 * @note 由用户实现
 * @note 函数返回后缓冲区`s`将不可用
 */
void LoraRxCallback(const uint8_t *s, uint8_t len, LoraError state);

/**
 * LoRa read同步系统调用
 * @param[in] s 待写入缓冲区地址
 * @param[in] len 最长读入长度
 * @return 读入字符数，-1为失败
 * @note 同步API，需要阻塞，下次收包不需要回调，使该函数返回即可
 */
int LoraRead(uint8_t *s, int len);

#ifdef LORA_IMPL
/**
 * @brief LoRa服务主函数
 * @param p 未使用
 */
[[noreturn]] void LoraMain([[maybe_unused]] void *p);
#endif  // LORA_IMPL

#ifdef LORA_IT
/**
 * @brief D0中断回调函数
 */
void LoraD0CallbackFromISR();
/**
 * @brief D1中断回调函数
 */
void LoraD1CallbackFromISR();
/**
 * @brief 定时器中断回调函数
 */
void LoraTimerCallbackFromISR();
#endif  // LORA_IT

#ifdef __cplusplus
}
#endif

#endif  // LORA_CORE_SRC_LORA_H_
