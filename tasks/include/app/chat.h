/**
 * @brief 文本聊天app
 * @author lan
 */

#ifndef LORA_TASKS_INCLUDE_APP_CHAT_H_
#define LORA_TASKS_INCLUDE_APP_CHAT_H_

#include "FreeRTOS.h"
#include "lib/BLE.h"
#include "semphr.h"
#include "timers.h"
/**
 * @brief app chat主函数
 * @param p 未使用
 */

// m3新增,定时发送普通包
extern TimerHandle_t normal_packet_send_timer;
extern StaticTimer_t normal_packet_send_timer_buffer;
void NormalPacketSendTimerCallBack(TimerHandle_t xTimer);
[[noreturn]] void ChatMain([[maybe_unused]] void *p);

#endif  // LORA_TASKS_INCLUDE_APP_CHAT_H_
