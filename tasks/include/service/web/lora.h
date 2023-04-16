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
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"

#ifdef LORA_SEMAPHORE
extern SemaphoreHandle_t lora_semaphore;
extern StaticSemaphore_t lora_semaphore_buffer;
#endif
typedef enum {
  RF_IDLE,
  RF_BUSY,
  RF_RX_DONE,
  RF_RX_TIMEOUT,
  RF_TX_DONE,
  RF_TX_TIMEOUT,
  RF_LEN_ERROR,
  RF_CHANNEL_EMPTY,
  RF_CHANNEL_ACTIVITY_DETECTED,
} tRFProcessReturnCodes;
/**
 * @brief 初始化lora模块，设置中断
 * @return 1成功，0超时
 * @note 应在uart和HAL初始化完毕后调用
 */
int LoraInit();

/**
 * lora write系统调用
 * @param[in] s 待写入内容地址
 * @param[in] len 待写入内容长度
 * @return 写入字符数，-1为失败
 * @note 注意实现需要调整模式
 */
int LoraWrite(char *s, int len);

int LoraWriteAsync(char *s, int len, void (*callback)());

/**
 * @brief 收包中断回调函数
 * @note 由用户实现
 */
void LoraRxCallbackFromISR();

/**
 * lora read系统调用
 * @param[in] s 缓冲区地址
 * @param[in] len 最长读入长度
 * @return 读入字符数，-1为失败
 * @note impl should assert there IS a package waiting to be received
 */
int LoraRead(char *s, int len);
void LoraD0CallbackFromISR();
int LoraEventLoop();
#ifdef LORA_IMPL
/**
 * @brief D0中断回调函数
 */
void LoraD0CallbackFromISR();

#endif  // LORA_IMPL

#ifdef __cplusplus
}
#endif

#endif  // LORA_CORE_SRC_LORA_H_
