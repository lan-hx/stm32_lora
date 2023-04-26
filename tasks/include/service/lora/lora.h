/**
 * @brief LoRa wrapper module
 * @author lan
 */

#ifndef LORA_CORE_SRC_LORA_H_
#define LORA_CORE_SRC_LORA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "semphr.h"

#ifdef LORA_SEMAPHORE
extern SemaphoreHandle_t lora_semaphore;
extern StaticSemaphore_t lora_semaphore_buffer;
#endif

/**
 * @brief 初始化LoRa模块，设置中断
 * @return 1成功，0超时
 * @note 应在uart和HAL初始化完毕后调用
 * @note init失败将导致reset
 */
int LoraInit();

/**
 * @brief LoRa模块发包异步API
 * @param[in] s 待写入内容地址
 * @param[in] len 待写入内容长度
 * @return TODO
 * @note 在上一次发包未回调时再次调用是未定义行为，推荐实现为打断上次发包任务
 */
void LoraWriteAsync(const char *s, uint8_t len);

/**
 * @brief 发包中断回调函数
 * @param[in] state TODO: 发包反馈
 * @note 由用户实现
 */
void LoraTxCallbackFromISR();

/**
 * LoRa write同步系统调用
 * @param[in] s 待写入内容地址
 * @param[in] len 待写入内容长度
 * @return 写入字符数，-1为失败
 * @note 该API仅用于测试
 */
__attribute__((deprecated)) int LoraWrite(const char *s, int len);

/**
 * @brief 启用LoRa模块收包功能
 * @return TODO
 */
void LoraReadAsyncStart();

/**
 * @brief 停止LoRa模块收包功能
 * @return TODO
 * @note LoRa模块应确保stop时即使有未收完的包后续也不会callback
 */
void LoraReadAsyncStop();

/**
 * @brief 收包中断回调函数
 * @param[in] s 缓冲区地址
 * @param[in] len 数据长度，最大为256
 * @note 由用户实现
 * @note 函数返回后缓冲区`s`将不可用
 */
void LoraRxCallbackFromISR(const char *s, uint8_t len);

/**
 * LoRa read同步系统调用
 * @param[in] s 待写入缓冲区地址
 * @param[in] len 最长读入长度
 * @return 读入字符数，-1为失败
 * @note 同步API，需要阻塞，下次收包不需要回调，使该函数返回即可
 */
int LoraRead(char *s, int len);

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
#endif  // LORA_IT

#ifdef __cplusplus
}
#endif

#endif  // LORA_CORE_SRC_LORA_H_
