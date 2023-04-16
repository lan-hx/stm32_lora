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
  SX1278LoRaSetRFState(RFLR_STATE_RX_INIT);
  // SX1278LoRaSetTxPacket(buffer, length);
  // printf("RFLRState in main==%d\r\n", RFLRState);
  while (true) {
    LoraEventLoop();

    // switch (LoraEventLoop()) {
    //   case RF_CHANNEL_ACTIVITY_DETECTED:
    //     __NOP();
    //     break;
    //   case RF_CHANNEL_EMPTY:
    //     __NOP();
    //     __NOP();
    //     break;
    //   case RF_LEN_ERROR:
    //     __NOP();
    //     __NOP();
    //     __NOP();
    //     break;
    //   case RF_IDLE:
    //     __NOP();
    //     __NOP();
    //     __NOP();
    //     __NOP();
    //     break;
    //   case RF_BUSY:
    //     //  printf("RF_BUSY\r\n");
    //     break;

    //   case RF_RX_TIMEOUT:
    //     // Radio_RxTimeout();
    //     break;
    //   case RF_RX_DONE:
    //     // SX1278LoRaGetRxPacket(buffer, (uint16_t *)&length);
    //     // if (length > 0) {
    //     //   Radio_RxDone(RadioBuffer, RadioLength);
    //     // }
    //     printf("received buffer:%s", buffer);
    //     break;
    //   case RF_TX_DONE:
    //     // printf("RF_TX_DONE\r\n");
    //     // printf("%d", length);
    //     // SX1278LoRaSetRFState(RFLR_STATE_RX_INIT);
    //     break;
    //   case RF_TX_TIMEOUT:
    //     // printf("RF_TX_TIMEOUT\r\n");
    //     break;
    //   default:
    //     break;
    // }
    // printf("hellowworld\r\n");
  }
}
