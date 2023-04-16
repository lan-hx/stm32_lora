/**
 * @brief LoRa wrapper module
 * @author lan
 */

#define LORA_SEMAPHORE
#define LORA_IMPL
#include "service/web/lora.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "SX1278/include/radioConfig.h"
#include "main.h"
#include "semphr.h"
#include "service/web/config.h"
#include "spi.h"
#include "sx1278-Hal.h"
#include "sx1278.h"
#include "task.h"

// 注意：SPI的大量读写请使用DMA实现，DMA读写过程中使用yield让出CPU

// 信号量
// 用于DMA访问，启动DMA时take，DMA结束时give

SemaphoreHandle_t lora_semaphore;
StaticSemaphore_t lora_semaphore_buffer;
extern uint16_t RxPacketSize;
// static int8_t RxPacketSnrEstimate;
// static double RxPacketRssiValue;
// static uint8_t RxGain = 1;
extern uint32_t RxTimeoutTimer;
extern uint8_t SX1278Regs[0x70];
extern tSX1278LR *SX1278LR;
extern tLoRaSettings LoRaSettings;
extern uint8_t RFLRState;
extern uint16_t TxPacketSize;
extern uint32_t PacketTimeout;
extern uint8_t RFBuffer[RF_BUFFER_SIZE];
int LoraInit() {
  //接口
  SX1278_hw_Reset();
  SX1278_hw_init();
  printf("Configuring LoRa module\r\n");

  SX1278LR = (tSX1278LR *)SX1278Regs;

  // sx1278SetLoRaOn
  SX1278LoRaSetOpMode(RFLR_OPMODE_SLEEP);  // in sleep mode
  HAL_Delay(15);

  SX1278LR->RegOpMode = (SX1278LR->RegOpMode & RFLR_OPMODE_LONGRANGEMODE_MASK) | RFLR_OPMODE_LONGRANGEMODE_ON;

  SX1278Write(REG_LR_OPMODE, SX1278LR->RegOpMode);

  SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);
  // RxDone               RxTimeout                   FhssChangeChannel           CadDone

  SX1278LR->RegDioMapping1 =
      RFLR_DIOMAPPING1_DIO0_00 | RFLR_DIOMAPPING1_DIO1_00 | RFLR_DIOMAPPING1_DIO2_00 | RFLR_DIOMAPPING1_DIO3_00;
  // CadDetected          ModeReady
  SX1278LR->RegDioMapping2 = RFLR_DIOMAPPING2_DIO4_00 | RFLR_DIOMAPPING2_DIO5_00;
  SX1278WriteBuffer(REG_LR_DIOMAPPING1, &SX1278LR->RegDioMapping1, 2);

  SX1278ReadBuffer(REG_LR_OPMODE, SX1278Regs + 1, 0x70 - 1);

  SX1278LoRaInit();

  return 0;
}

__attribute__((weak)) void LoraRxCallbackFromISR() {}

int LoraWrite(char *s, int len) {
  configASSERT(len >= 0);
  // 不允许在中断中操作网卡
  configASSERT(xPortIsInsideInterrupt() == pdFALSE);
  UNUSED(s);
  UNUSED(len);
  return -1;
}
int LoraRead(char *s, int len) {
  configASSERT(len >= 0);
  // 不允许在中断中操作网卡
  configASSERT(xPortIsInsideInterrupt() == pdFALSE);
  UNUSED(s);
  UNUSED(len);
  return -1;
}

void LoraD0CallbackFromISR() {}
int LoraEventLoop() {
  uint32_t result = RF_BUSY;
  // printf("result:%d\r\n", RFLRState);
  switch (RFLRState) {
    case RFLR_STATE_IDLE:
      break;
    case RFLR_STATE_RX_INIT:

      SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);

      SX1278LR->RegIrqFlagsMask = RFLR_IRQFLAGS_RXTIMEOUT |
                                  // RFLR_IRQFLAGS_RXDONE |
                                  // RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                  RFLR_IRQFLAGS_VALIDHEADER | RFLR_IRQFLAGS_TXDONE | RFLR_IRQFLAGS_CADDONE |
                                  // RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                  RFLR_IRQFLAGS_CADDETECTED;
      SX1278Write(REG_LR_IRQFLAGSMASK, SX1278LR->RegIrqFlagsMask);

      SX1278LR->RegHopPeriod = 255;

      SX1278Write(REG_LR_HOPPERIOD, SX1278LR->RegHopPeriod);

      // RxDone                    RxTimeout                   FhssChangeChannel           CadDone
      SX1278LR->RegDioMapping1 =
          RFLR_DIOMAPPING1_DIO0_00 | RFLR_DIOMAPPING1_DIO1_00 | RFLR_DIOMAPPING1_DIO2_00 | RFLR_DIOMAPPING1_DIO3_00;
      // CadDetected               ModeReady
      SX1278LR->RegDioMapping2 = RFLR_DIOMAPPING2_DIO4_00 | RFLR_DIOMAPPING2_DIO5_00;

      SX1278WriteBuffer(REG_LR_DIOMAPPING1, &SX1278LR->RegDioMapping1, 2);

      SX1278LoRaSetOpMode(RFLR_OPMODE_RECEIVER_SINGLE);

      memset(RFBuffer, 0, (size_t)RF_BUFFER_SIZE);

      PacketTimeout = LoRaSettings.RxPacketTimeout;
      RxTimeoutTimer = GET_TICK_COUNT();
      RFLRState = RFLR_STATE_RX_RUNNING;
      break;
    case RFLR_STATE_RX_RUNNING:
      // printf("state==RFLR_STATE_RX_RUNNING\r\n");
      if (HAL_GPIO_ReadPin(DIO0_IOPORT, DIO0_PIN))  // RxDone
      {
        printf("RX_DONE\r\n");
        printf("received!\r\n");
        RxTimeoutTimer = GET_TICK_COUNT();

        // Clear Irq
        SX1278Write(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_RXDONE);
        RFLRState = RFLR_STATE_RX_DONE;
      }

      if (LoRaSettings.RxSingleOn)  // Rx single mode
      {
        uint8_t intern = GET_TICK_COUNT() - RxTimeoutTimer;
        if (intern > PacketTimeout) {
          printf("intern:%d,PacketTimeout:%d", intern, PacketTimeout);
          RFLRState = RFLR_STATE_RX_TIMEOUT;
        }
      }
      break;
    case RFLR_STATE_RX_DONE:
      SX1278Read(REG_LR_IRQFLAGS, &SX1278LR->RegIrqFlags);
      if ((SX1278LR->RegIrqFlags & RFLR_IRQFLAGS_PAYLOADCRCERROR) == RFLR_IRQFLAGS_PAYLOADCRCERROR) {
        // Clear Irq
        SX1278Write(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_PAYLOADCRCERROR);

        if (LoRaSettings.RxSingleOn)  // Rx single mode
        {
          RFLRState = RFLR_STATE_RX_INIT;
        } else {
          RFLRState = RFLR_STATE_RX_RUNNING;
        }
        break;
      }

      SX1278LR->RegFifoAddrPtr = SX1278LR->RegFifoRxBaseAddr;
      SX1278Write(REG_LR_FIFOADDRPTR, SX1278LR->RegFifoAddrPtr);

      if (LoRaSettings.ImplicitHeaderOn) {
        RxPacketSize = SX1278LR->RegPayloadLength;
        SX1278ReadFifo(RFBuffer, SX1278LR->RegPayloadLength);
      } else {
        SX1278Read(REG_LR_NBRXBYTES, &SX1278LR->RegNbRxBytes);
        RxPacketSize = SX1278LR->RegNbRxBytes;
        SX1278ReadFifo(RFBuffer, SX1278LR->RegNbRxBytes);
      }

      if (LoRaSettings.RxSingleOn)  // Rx single mode
      {
        RFLRState = RFLR_STATE_RX_INIT;
      } else  // Rx continuous mode
      {
        RFLRState = RFLR_STATE_RX_RUNNING;
      }
      result = RF_RX_DONE;

      break;

    case RFLR_STATE_RX_TIMEOUT:
      // printf("state==RFLR_STATE_RX_TIMEOUT\r\n");
      RFLRState = RFLR_STATE_RX_INIT;
      result = RF_RX_TIMEOUT;
      break;
    case RFLR_STATE_TX_INIT:
      // printf("state==init\r\n");
      SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);

      SX1278LR->RegIrqFlagsMask = RFLR_IRQFLAGS_RXTIMEOUT | RFLR_IRQFLAGS_RXDONE | RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                  RFLR_IRQFLAGS_VALIDHEADER |
                                  // RFLR_IRQFLAGS_TXDONE |
                                  RFLR_IRQFLAGS_CADDONE | RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL | RFLR_IRQFLAGS_CADDETECTED;
      SX1278LR->RegHopPeriod = 0;

      SX1278Write(REG_LR_HOPPERIOD, SX1278LR->RegHopPeriod);
      SX1278Write(REG_LR_IRQFLAGSMASK, SX1278LR->RegIrqFlagsMask);

      // Initializes the payload size
      SX1278LR->RegPayloadLength = TxPacketSize;
      SX1278Write(REG_LR_PAYLOADLENGTH, SX1278LR->RegPayloadLength);

      SX1278LR->RegFifoTxBaseAddr = 0x00;  // Full buffer used for Tx
      SX1278Write(REG_LR_FIFOTXBASEADDR, SX1278LR->RegFifoTxBaseAddr);

      SX1278LR->RegFifoAddrPtr = SX1278LR->RegFifoTxBaseAddr;
      SX1278Write(REG_LR_FIFOADDRPTR, SX1278LR->RegFifoAddrPtr);

      // Write payload buffer to LORA modem
      SX1278WriteFifo(RFBuffer, SX1278LR->RegPayloadLength);
      // TxDone               RxTimeout                   FhssChangeChannel          ValidHeader
      SX1278LR->RegDioMapping1 =
          RFLR_DIOMAPPING1_DIO0_01 | RFLR_DIOMAPPING1_DIO1_00 | RFLR_DIOMAPPING1_DIO2_00 | RFLR_DIOMAPPING1_DIO3_01;
      // PllLock              Mode Ready
      SX1278LR->RegDioMapping2 = RFLR_DIOMAPPING2_DIO4_01 | RFLR_DIOMAPPING2_DIO5_00;

      SX1278WriteBuffer(REG_LR_DIOMAPPING1, &SX1278LR->RegDioMapping1, 2);

      SX1278LoRaSetOpMode(RFLR_OPMODE_TRANSMITTER);

      printf("buffer: %s", (char *)RFBuffer);
      printf("payload_length:%d\r\n", SX1278LR->RegPayloadLength);

      RFLRState = RFLR_STATE_TX_RUNNING;
      // printf("state==init\r\n");
      break;
    case RFLR_STATE_TX_RUNNING:
      uint8_t ssss;
      SX1278Read(REG_LR_PAYLOADLENGTH, &ssss);
      // printf("sizeOfBuffer:%d\r\n", ssss);
      // printf("state==running\r\n");
      if (HAL_GPIO_ReadPin(DIO0_IOPORT, DIO0_PIN))  // TxDone
      {
        // Clear Irq
        SX1278Write(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_TXDONE);
        RFLRState = RFLR_STATE_TX_DONE;
      }

      break;
    case RFLR_STATE_TX_DONE:
      // printf("state==done\r\n");
      // optimize the power consumption by switching off the transmitter as soon as the packet has been sent
      SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);

      HAL_Delay(500);
      RFLRState = RFLR_STATE_TX_INIT;
      result = RF_TX_DONE;
      break;

    default:
      break;
  }
  return result;
}
// int LoraInit() {}