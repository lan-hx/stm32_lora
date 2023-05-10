/**
 * @brief 网络进程
 * @author lan
 */

#include "service/web/web.h"

#include <stdio.h>
#include <string.h>

#include "service/lora/lora.h"
#include "task.h"
#include "utility.h"

volatile uint8_t web_state_rx = Lora_Invalid, web_state_tx = Lora_Invalid;
volatile uint8_t buf_tx[256] = {0};
volatile uint8_t buf_rx[256] = {0};
volatile uint8_t rx_len = 0;

void WebMain([[maybe_unused]] void *p) {
  static uint32_t count = 0;
  uint8_t str[] = "Hello World!";
  printf("LoraReadAsyncStart() return %s\r\n", lora_error_map[LoraReadAsyncStart()]);
  sprintf((char *)buf_tx, "Hello world %u!", ++count);
  printf("LoraWriteAsync(%s) return %s\r\n", buf_tx,
         lora_error_map[LoraWriteAsync((const uint8_t *)buf_tx, strlen((char *)buf_tx), false)]);
  while (true) {
    if (web_state_tx != (uint8_t)Lora_Invalid) {
      printf("LoraTxCallback result: %s\r\n", lora_error_map[web_state_tx]);
      web_state_tx = Lora_Invalid;
      // vTaskDelay(5);
      //  HighResolutionDelay32(100);
      //sprintf((char *)buf_tx,
      //        "Hello world "
      //        "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      //        "00000000%0240u!",
      //        ++count);
      sprintf((char *)buf_tx, "Hello world %u!", ++count);
      printf("LoraWriteAsync(%s) return %s\r\n", buf_tx,
             lora_error_map[LoraWriteAsync((const uint8_t *)buf_tx, strlen((char *)buf_tx), false)]);
    }

    if (web_state_rx != (uint8_t)Lora_Invalid) {
      printf("LoraRxCallback result(%u): %s\r\n", (uint32_t)rx_len, lora_error_map[web_state_rx]);
      printf("content: %s\r\n", buf_rx);
      web_state_rx = Lora_Invalid;
    }
  }
}

void LoraTxCallback(LoraError state) { web_state_tx = state; }

void LoraRxCallback(const uint8_t *s, uint8_t len, LoraError state) {
  memset((void *)buf_rx, 0, sizeof(buf_rx));
  memcpy((void *)buf_rx, s, len);
  rx_len = len;
  web_state_rx = state;
}
