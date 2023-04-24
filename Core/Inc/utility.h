/**
 * @brief 标准库依赖实现
 * @author lan
 */

#ifndef LORA_CORE_SRC_UTILITY_H_
#define LORA_CORE_SRC_UTILITY_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define LORA_FILE_NO 0xff

/**
 * 获取高精度时间
 * @return system uptime in us
 */
uint32_t GetHighResolutionTick();
uint64_t GetHighResolutionTick64();

/**
 * 阻塞指定的时间
 * @param us 阻塞时间，以微秒为单位，取值范围5~999
 */
#define HighResolutionDelay HighResolutionDelay64
void HighResolutionDelay64(uint32_t us);
void HighResolutionDelay32(uint32_t us);

uint32_t GetRandSeed();
uint32_t BinaryExponentialBackoff();
#ifdef __cplusplus
}
#endif

#endif  // LORA_CORE_SRC_UTILITY_H_
