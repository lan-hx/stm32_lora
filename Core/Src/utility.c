/**
 * @brief 标准库依赖实现
 * @author lan
 */

#include "utility.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "lib/printf.h"
#include "main.h"
#include "service/lora/lora.h"
#include "task.h"
#include "usart.h"

void __assert_func(const char *file, int line, const char *func, const char *msg) {
  if (xPortIsInsideInterrupt() == pdFALSE) {
    printf("Assertion failed in function %s in file:%lu. msg: %s\r\n", func, file, line, msg);  // NOLINT
  }
  while (1) {
  }
}

volatile uint32_t uart_dma_busy = 0;  // uart的DMA模块是否正在运行

// void HAL_UART_TxHalfCpltCallback(UART_HandleTypeDef *huart) {
//   UNUSED(huart);
// }
//
// void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
//  if (huart == &huart1) {
//    uart_dma_busy = 0;
//  }
//}

static int UartWrite(char *ptr, int len) {
  return HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY) == HAL_OK ? len : -1;
  // uart_dma_busy = 1;
  // if (HAL_UART_Transmit_DMA(&huart1, (uint8_t *)ptr, len) == HAL_OK) {
  //   while (uart_dma_busy) {
  //   }
  //   return len;
  // }
  // uart_dma_busy = 0;
  // return -1;
}

__attribute__((used)) int _write(int file, char *ptr, int len) {
  if (file == stdout->_file) {
    assert_param(len >= 0);
    int ret;
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
      configASSERT(xPortIsInsideInterrupt() == pdFALSE);
      ret = UartWrite(ptr, len);
    }
    // 保证原子性
    else if (xPortIsInsideInterrupt() == pdTRUE) {
      assert(false && "no printf in interrupt!");
      uint32_t status = taskENTER_CRITICAL_FROM_ISR();
      ret = UartWrite(ptr, len);
      taskEXIT_CRITICAL_FROM_ISR(status);
    } else {
      taskENTER_CRITICAL();
      ret = UartWrite(ptr, len);
      taskEXIT_CRITICAL();
    }
    // if (uart_dma_busy) {
    //   return -1;
    // }
    // uart_dma_busy = 1;
    // HAL_UART_Transmit_DMA(&huart1, (uint8_t *)ptr, len);
    //
    // while (uart_dma_busy) {}
    // return len;
    return ret;
  }
  // if (file == LORA_FILE_NO) {
  //   return LoraWrite(ptr, len);
  // }
  return -1;
}
__attribute__((used)) int _read(int file, char *ptr, int len) {
  UNUSED(file);
  UNUSED(ptr);
  UNUSED(len);
  // if (file == LORA_FILE_NO) {
  //   return LoraRead(ptr, len);
  // }
  return -1;
}
__attribute__((used)) int _open(char *path, int flags, ...) {
  UNUSED(flags);
  if (strcmp(path, "lora") == 0) {
    return LORA_FILE_NO;
  }
  /* Pretend like we always fail */
  return -1;
}

// 应该与HAL库计时工具同步
extern TIM_HandleTypeDef htim4;
uint32_t GetHighResolutionTick() { return HAL_GetTick() * 1000 + __HAL_TIM_GET_COUNTER(&htim4); }
uint64_t GetHighResolutionTick64() { return HAL_GetTick() * 1000 + __HAL_TIM_GET_COUNTER(&htim4); }

void HighResolutionDelay64(uint32_t us) {
  configASSERT(us >= 5 && us < 1000);
  //  为什么-1：不准，简单处理
  uint64_t dest = GetHighResolutionTick64() + us - 1;
  while (GetHighResolutionTick64() < dest) {
  }
}

void HighResolutionDelay32(uint32_t us) {
  configASSERT(us >= 5 && us < 1000);
  //  为什么-1：不准，简单处理
  uint32_t dest = GetHighResolutionTick() + us - 1;
  while (GetHighResolutionTick() < dest) {
  }
}
uint32_t GetRandSeed() { return 3176273894; }

__attribute__((used)) void *malloc(size_t size) _NOTHROW { return pvPortMalloc(size); }
__attribute__((used)) void free(void *p) _NOTHROW { vPortFree(p); }

#undef printf
#undef sprintf
int printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = printf_(fmt, args);
  va_end(args);
  return ret;
}
int sprintf(char *str, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = sprintf_(str, fmt, args);
  va_end(args);
  return ret;
}
