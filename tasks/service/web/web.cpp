/**
 * @brief 网络进程
 * @author lan
 */

#include "service/web/web.h"

#include <errno.h>
#include <stdio.h>
#include <stm32f1xx_hal.h>
#include <string.h>

#include "service/web/lora.h"
#include "sx1278.h"
#include "utility.h"
extern uint8_t RFLRState;
uint8_t buffer[256] = "hello";
uint8_t length;

void WebMain([[maybe_unused]] void *p) {
  // buffer = "hello";

  length = 6;
  LoraInit();
  // SX1278LoRaSetRFState(RFLR_STATE_RX_INIT);
  SX1278LoRaSetTxPacket(buffer, length);
  // printf("RFLRState in main==%d\r\n", RFLRState);
  while (true) {
    LoraEventLoop();
  }
}
