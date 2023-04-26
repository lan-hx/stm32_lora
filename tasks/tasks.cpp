/**
 * @brief 任务入口、管理
 * @author lan
 */

#include "tasks.h"

#include <stdio.h>
#include <string.h>

#define LORA_IMPL
#define LORA_SEMAPHORE
#define DATA_LINK_SEMAPHORE
#define DATA_LINK_TIMER

#include "FreeRTOS.h"
#include "app/chat.h"
#include "cmsis_os.h"
#include "main.h"
#include "semphr.h"
#include "service/lora/lora.h"
#include "service/watchdog/watchdog.h"
#include "service/web/data_link.h"
#include "service/web/web.h"
#include "task.h"
#include "timers.h"

// 下面为freertos静态分配内存
// 见cmsis_os2.c

// 下面为任务静态分配内存
constexpr size_t CHAT_STACK_SIZE = 512;
constexpr size_t CHAT_STACK_SIZE_IN_WORD = CHAT_STACK_SIZE / sizeof(StackType_t);
constexpr uint32_t CHAT_PRIORITY = osPriorityNormal;
TaskHandle_t chat_handler;
StackType_t chat_stack[CHAT_STACK_SIZE_IN_WORD];
StaticTask_t chat_tcb;

constexpr size_t WEB_STACK_SIZE = 512;
constexpr size_t WEB_STACK_SIZE_IN_WORD = WEB_STACK_SIZE / sizeof(StackType_t);
constexpr uint32_t WEB_PRIORITY = osPriorityHigh;
TaskHandle_t web_handler;
StackType_t web_stack[WEB_STACK_SIZE_IN_WORD];
StaticTask_t web_tcb;

constexpr size_t LORA_STACK_SIZE = 512;
constexpr size_t LORA_STACK_SIZE_IN_WORD = LORA_STACK_SIZE / sizeof(StackType_t);
constexpr uint32_t LORA_PRIORITY = osPriorityHigh;
TaskHandle_t lora_handler;
StackType_t lora_stack[WEB_STACK_SIZE_IN_WORD];
StaticTask_t lora_tcb;

constexpr size_t WATCHDOG_STACK_SIZE = 512;
constexpr size_t WATCHDOG_STACK_SIZE_IN_WORD = WATCHDOG_STACK_SIZE / sizeof(StackType_t);
constexpr uint32_t WATCHDOG_PRIORITY = osPriorityRealtime;
TaskHandle_t watchdog_handler;
StackType_t watchdog_stack[WATCHDOG_STACK_SIZE_IN_WORD];
StaticTask_t watchdog_tcb;

void TaskInit() {
  // 防止编译器将FreeRTOS堆优化掉
  vPortFree(pvPortMalloc(8));

  // 检测栈峰值使用情况
#ifdef DEBUG
  memset(chat_stack, 0xcc, CHAT_STACK_SIZE);
  memset(web_stack, 0xcc, WEB_STACK_SIZE);
  memset(lora_stack, 0xcc, LORA_STACK_SIZE);
  memset(watchdog_stack, 0xcc, WATCHDOG_STACK_SIZE);
#endif

  printf("Before task init heap free: %u\r\n", xPortGetFreeHeapSize());  // NOLINT
  taskENTER_CRITICAL();

  // 初始化计时器

  // 初始化队列
  lora_semaphore = xSemaphoreCreateBinaryStatic(&lora_semaphore_buffer);
  UNUSED(lora_semaphore);
  // data_link_lora_semaphore = xSemaphoreCreateBinaryStatic(&data_link_lora_semaphore_buffer);
  // UNUSED(data_link_lora_semaphore);
  data_link_wifi_semaphore = xSemaphoreCreateBinaryStatic(&data_link_wifi_semaphore_buffer);
  UNUSED(data_link_wifi_semaphore);

  // 创建任务
  chat_handler =
      xTaskCreateStatic(ChatMain, "Chat App", CHAT_STACK_SIZE_IN_WORD, nullptr, CHAT_PRIORITY, chat_stack, &chat_tcb);
  configASSERT(chat_handler);

  web_handler =
      xTaskCreateStatic(WebMain, "Web Service", WEB_STACK_SIZE_IN_WORD, nullptr, WEB_PRIORITY, web_stack, &web_tcb);
  configASSERT(web_handler);

  lora_handler = xTaskCreateStatic(LoraMain, "LoRa Driver", LORA_STACK_SIZE_IN_WORD, nullptr, LORA_PRIORITY, lora_stack,
                                   &lora_tcb);
  configASSERT(lora_handler);

  watchdog_handler = xTaskCreateStatic(WatchdogMain, "Watchdog Service", WATCHDOG_STACK_SIZE_IN_WORD, nullptr,
                                       WATCHDOG_PRIORITY, watchdog_stack, &watchdog_tcb);
  configASSERT(watchdog_handler);

  taskEXIT_CRITICAL();
  printf("After task init heap free: %u\r\n", xPortGetFreeHeapSize());  // NOLINT
}
