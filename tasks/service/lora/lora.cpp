/**
 * @brief LoRa wrapper module
 * @author lan
 */

#define LORA_SEMAPHORE
#define LORA_IMPL
#define LORA_IT
#include "service/lora/lora.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "main.h"
#include "semphr.h"
#include "service/web/config.h"
#include "sx1278-Hal.h"
#include "sx1278.h"
#include "tim.h"
#include "utility.h"
#define PHY_DEBUG
// 注意：SPI的大量读写请使用DMA实现，DMA读写过程中使用yield让出CPU

enum LoraSignalEnum : uint8_t {
  INVALID,
  RX_SINGLE,
  RX_START,
  RX_STOP,
  TX,
  TIMER,
  IFS_TIMER,
  BACKOFF_TIMER,
  BUSY_TIMER,
  RX_DONE,
  TX_DONE,
  D0,
  // D1,
  LORA_SIGNAL_NUMBER,
};
constexpr const char *lora_signal_map[LORA_SIGNAL_NUMBER] = {"INVALID",    "RX_SINGLE", "RX_START",  "RX_STOP",
                                                             "TX",         "TIMER",     "IFS_TIMER", "BACKOFF_TIMER",
                                                             "BUSY_TIMER", "RX_DONE",   "TX_DONE",   "D0"};

QueueHandle_t lora_queue;
StaticQueue_t lora_queue_buffer;
LoraSignal lora_queue_storage[LORA_QUEUE_LEN];
SemaphoreHandle_t lora_sync_api_semaphore;
StaticSemaphore_t lora_sync_api_semaphore_buffer;

LoraSignal lora_global_signal = INVALID;

typedef struct LoraFlags {
  bool init : 1;
  bool rx_single : 1;
  bool rx_continuous : 1;
  bool tx_single : 1;
  bool pending_tx : 1;
  bool ifs_timer : 1;
  bool backoff_timer : 1;
  bool busy_timer : 1;
  bool backoff : 1;
} LoraFlags;
LoraFlags flag = {false, false, false, false, false, false, false, false, false};

uint8_t backoff_count = 0;
uint8_t tx_length = 0;
bool tx_ack = false;
bool crc_error = false;
LoraError LoraSyncAPIGlobalError = Lora_OK;
uint8_t *rx_sync_buffer;
int rx_sync_buffer_len;
extern uint8_t TXBuffer[RF_BUFFER_SIZE];
extern uint8_t RXBuffer[RF_BUFFER_SIZE];

extern uint8_t SX1278Regs[0x70];
extern tSX1278LR *SX1278LR;
extern tLoRaSettings LoRaSettings;
extern tRFLRStates RFLRState;

// ---------------- APIs ------------------------

LoraError LoraInit() {
  SX1278_hw_Reset();
  // SX1278_hw_init();
#ifndef PHY_DEBUG
  printf("Configuring LoRa module\r\n");
#endif
  SX1278LR = (tSX1278LR *)SX1278Regs;

  // sx1278SetLoRaOn
  SX1278LoRaSetOpMode(RFLR_OPMODE_SLEEP);  // in sleep mode
  HAL_Delay(15);

  // low freq?
  SX1278LR->RegOpMode = (SX1278LR->RegOpMode & RFLR_OPMODE_LONGRANGEMODE_MASK) | RFLR_OPMODE_LONGRANGEMODE_ON;

  SX1278Write(REG_LR_OPMODE, SX1278LR->RegOpMode);

  SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);
  //// RxDone               RxTimeout                   FhssChangeChannel           CadDone
  // SX1278LR->RegDioMapping1 =
  //     RFLR_DIOMAPPING1_DIO0_00 | RFLR_DIOMAPPING1_DIO1_00 | RFLR_DIOMAPPING1_DIO2_00 | RFLR_DIOMAPPING1_DIO3_00;
  //// CadDetected          ModeReady
  // SX1278LR->RegDioMapping2 = RFLR_DIOMAPPING2_DIO4_00 | RFLR_DIOMAPPING2_DIO5_00;
  // SX1278WriteBuffer(REG_LR_DIOMAPPING1, &SX1278LR->RegDioMapping1, 2);

  SX1278ReadBuffer(REG_LR_OPMODE, SX1278Regs + 1, 0x70 - 1);

  SX1278LoRaInit();

  flag.init = true;
  return Lora_OK;
}

LoraError LoraWriteAsync(const uint8_t *s, uint8_t len, bool is_ack) {
  // 不允许在中断中操作网卡
  assert(xPortIsInsideInterrupt() == pdFALSE);

  if (flag.init) {
    if (flag.pending_tx) {
      return Lora_TxBusy;
    }
    memcpy(TXBuffer, s, len);
    tx_length = len;
    tx_ack = is_ack;

    // signal
    LoraSignal signal = TX;
    assert(xQueueSendToBack(lora_queue, &signal, 0) == pdPASS);
    return Lora_OK;
  }
  return Lora_NotInitialized;
}

__attribute__((weak)) void LoraRxCallback(const uint8_t *s, uint8_t len, LoraError state) {
  UNUSED(s);
  UNUSED(len);
  UNUSED(state);
}

__attribute__((weak)) void LoraTxCallback(LoraError state) { UNUSED(state); }

int LoraWrite(const uint8_t *s, int len) {
  assert(len >= 0);
  // 不允许在中断中操作网卡
  assert(xPortIsInsideInterrupt() == pdFALSE);

  LoraSyncAPIGlobalError = Lora_OK;
  if (flag.init) {
    if (flag.pending_tx) {
      LoraSyncAPIGlobalError = Lora_TxBusy;
      return -1;
    }
    memcpy(TXBuffer, s, len);
    tx_length = len;

    // signal
    LoraSignal signal = TX;
    assert(xQueueSendToBack(lora_queue, &signal, 0) == pdPASS && "signal queue full!");
    xSemaphoreTake(lora_sync_api_semaphore, portMAX_DELAY);
    return LoraSyncAPIGlobalError == Lora_OK ? len : -1;
  }
  LoraSyncAPIGlobalError = Lora_NotInitialized;
  return -1;
}

LoraError LoraReadAsyncStart() {
  // 不允许在中断中操作网卡
  assert(xPortIsInsideInterrupt() == pdFALSE);

  if (flag.rx_continuous) {
    return Lora_RxAsyncAlreadyStarted;
  }
  // signal
  LoraSignal signal = RX_START;
  assert(xQueueSendToBack(lora_queue, &signal, 0) == pdPASS && "signal queue full!");
  return Lora_OK;
}

LoraError LoraReadAsyncStop() {
  // 不允许在中断中操作网卡
  assert(xPortIsInsideInterrupt() == pdFALSE);

  if (!flag.rx_continuous) {
    return Lora_RxAsyncAlreadyStopped;
  }
  // signal
  LoraSignal signal = RX_STOP;
  assert(xQueueSendToBack(lora_queue, &signal, 0) == pdPASS && "signal queue full!");
  return Lora_OK;
}

int LoraRead(uint8_t *s, int len) {
  assert(len >= 0);
  // 不允许在中断中操作网卡
  assert(xPortIsInsideInterrupt() == pdFALSE);

  LoraSyncAPIGlobalError = Lora_OK;
  if (flag.init) {
    if (flag.rx_single) {
      LoraSyncAPIGlobalError = Lora_RxBusy;
      return -1;
    }

    rx_sync_buffer = s;
    rx_sync_buffer_len = len;

    // signal
    LoraSignal signal = RX_SINGLE;
    assert(xQueueSendToBack(lora_queue, &signal, 0) == pdPASS && "signal queue full!");
    xSemaphoreTake(lora_sync_api_semaphore, portMAX_DELAY);

    rx_sync_buffer = nullptr;
    rx_sync_buffer_len = 0;
    return LoraSyncAPIGlobalError == Lora_OK ? len : -1;
  }
  LoraSyncAPIGlobalError = Lora_NotInitialized;
  return -1;
}

// -------------- helper func -----------------------

static void LoraTimerOneShot(uint16_t us) {
  assert(htim3.State == HAL_TIM_STATE_READY && "timer is running!");
  // change period
  __HAL_TIM_SET_AUTORELOAD(&htim3, us);
  __HAL_TIM_CLEAR_IT(&htim3, TIM_IT_UPDATE);
  HAL_TIM_Base_Start_IT(&htim3);
}

void LoraTimerCallbackFromISR() {
  HAL_TIM_Base_Stop(&htim3);  // Kill the Timer
  if (flag.init) {
    LoraSignal signal = TIMER;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    assert(xQueueSendToBackFromISR(lora_queue, &signal, &xHigherPriorityTaskWoken) == pdPASS && "signal queue full!");
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

void LoraD0CallbackFromISR() {
  if (flag.init) {
    LoraSignal signal = D0;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    assert(xQueueSendToBackFromISR(lora_queue, &signal, &xHigherPriorityTaskWoken) == pdPASS && "signal queue full!");
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

void LoraD1CallbackFromISR() {}

static inline uint32_t LoraBackoffHelper(uint32_t times) { return rand() % (1u << (times + 5)); }

static inline uint32_t LoraAckBackoffHelper() { return rand() % MAX_ACK_BACKOFF_TIME; }

static inline uint32_t LoraIsBusy() {
  assert(RFLRState == RFLR_STATE_RX);
  SX1278Read(REG_LR_MODEMSTAT, &SX1278LR->RegModemStat);
  assert(SX1278LR->RegModemStat & 0x4 && "try to detect busy on state non-rx");
  // if (SX1278LR->RegModemStat & 0x1) printf("[DEBUG] busy!\r\n");
  return SX1278LR->RegModemStat & 0xB;
}

static inline void LoraHandleTxCallback(LoraError state) {
  assert(flag.pending_tx && RFLRState == RFLR_STATE_TX);
  if (flag.tx_single) {
    LoraSyncAPIGlobalError = state;
    xSemaphoreGive(lora_sync_api_semaphore);
    flag.tx_single = false;
  } else {
    LoraTxCallback(state);
  }

  flag.pending_tx = false;
  flag.backoff = false;
  backoff_count = 0;
}

static inline bool LoraBackoffIncrement() {
  if (flag.backoff) {
    ++backoff_count;
  }
  flag.backoff = true;
  if (backoff_count == MAX_BACKOFF_COUNT) {
    LoraHandleTxCallback(Lora_TxExceedMaxTry);
    return true;
  }
  return false;
}

static inline void LoraStartTimer() {
  assert(!flag.busy_timer);
  assert(flag.pending_tx && !flag.ifs_timer && !flag.backoff_timer);
  if (flag.backoff) {
    flag.backoff_timer = true;
    LoraTimerOneShot(tx_ack ? (SIFS + LoraAckBackoffHelper()) : (DIFS + LoraBackoffHelper(backoff_count)));
  } else {
    flag.ifs_timer = true;
    LoraTimerOneShot(tx_ack ? SIFS : DIFS);
  }
}

static inline void LoraHandleTimer() {
  // backoff
  if (flag.ifs_timer || flag.backoff_timer) {
    assert(flag.pending_tx);
    LoraBackoffIncrement();
    // tx failed will be handled in rx_donw
  }
  // stop timer
  HAL_TIM_Base_Stop(&htim3);
  __HAL_TIM_SET_COUNTER(&htim3, 0);
  __HAL_TIM_CLEAR_IT(&htim3, TIM_IT_UPDATE);
  flag.ifs_timer = flag.backoff_timer = flag.busy_timer = false;
  // start timer
  if (flag.pending_tx) {
    LoraStartTimer();
  }
}

static void RxInit() {
  SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);

  // clear irq
  SX1278LR->RegIrqFlags = 0;
  SX1278Write(REG_LR_IRQFLAGS, 0xff);

  SX1278LR->RegIrqFlagsMask = RFLR_IRQFLAGS_RXTIMEOUT |
                              // RFLR_IRQFLAGS_RXDONE |
                              // RFLR_IRQFLAGS_PAYLOADCRCERROR |
                              // RFLR_IRQFLAGS_VALIDHEADER |
                              RFLR_IRQFLAGS_TXDONE | RFLR_IRQFLAGS_CADDONE | RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                              RFLR_IRQFLAGS_CADDETECTED;
  SX1278Write(REG_LR_IRQFLAGSMASK, SX1278LR->RegIrqFlagsMask);

  //  RxDone                   | RxTimeout                | FhssChangeChannel        | ValidHeader
  SX1278LR->RegDioMapping1 =
      RFLR_DIOMAPPING1_DIO0_00 | RFLR_DIOMAPPING1_DIO1_00 | RFLR_DIOMAPPING1_DIO2_00 | RFLR_DIOMAPPING1_DIO3_01;
  //                         CadDetected              | ModeReady
  SX1278LR->RegDioMapping2 = RFLR_DIOMAPPING2_DIO4_00 | RFLR_DIOMAPPING2_DIO5_00;
  SX1278WriteBuffer(REG_LR_DIOMAPPING1, &SX1278LR->RegDioMapping1, 2);

  assert(LoRaSettings.RxSingleOn == false);
  SX1278LR->RegFifoAddrPtr = SX1278LR->RegFifoRxBaseAddr;
  SX1278Write(REG_LR_FIFOADDRPTR, SX1278LR->RegFifoAddrPtr);
  SX1278LoRaSetOpMode(RFLR_OPMODE_RECEIVER);

  SX1278Read(REG_LR_MODEMSTAT, &SX1278LR->RegModemStat);
  while (!(SX1278LR->RegModemStat & 0x4)) {
    SX1278Read(REG_LR_MODEMSTAT, &SX1278LR->RegModemStat);
  }

  if (flag.pending_tx) {
    assert(!flag.ifs_timer && !flag.backoff_timer && !flag.busy_timer);
    LoraStartTimer();
  }
}

static void TxInit() {
  assert(flag.pending_tx == true);
  SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);
  assert(SX1278LoRaGetOpMode() == RFLR_OPMODE_STANDBY);

  // clear irq
  SX1278LR->RegIrqFlags = 0;
  SX1278Write(REG_LR_IRQFLAGS, 0xff);

  SX1278LR->RegIrqFlagsMask = RFLR_IRQFLAGS_RXTIMEOUT | RFLR_IRQFLAGS_RXDONE | RFLR_IRQFLAGS_PAYLOADCRCERROR |
                              RFLR_IRQFLAGS_VALIDHEADER |
                              // RFLR_IRQFLAGS_TXDONE |
                              RFLR_IRQFLAGS_CADDONE | RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL | RFLR_IRQFLAGS_CADDETECTED;
  SX1278Write(REG_LR_IRQFLAGSMASK, SX1278LR->RegIrqFlagsMask);

  // Initializes the payload size
  SX1278LR->RegPayloadLength = tx_length;
  SX1278Write(REG_LR_PAYLOADLENGTH, SX1278LR->RegPayloadLength);

  SX1278LR->RegFifoTxBaseAddr = 0x00;  // Full buffer used for Tx
  SX1278Write(REG_LR_FIFOTXBASEADDR, SX1278LR->RegFifoTxBaseAddr);

  SX1278LR->RegFifoAddrPtr = SX1278LR->RegFifoTxBaseAddr;
  SX1278Write(REG_LR_FIFOADDRPTR, SX1278LR->RegFifoAddrPtr);

  // Write payload buffer to LORA modem
  SX1278WriteFifo(TXBuffer, SX1278LR->RegPayloadLength);
  //  TxDone                   | RxTimeout                | FhssChangeChannel        | ValidHeader
  SX1278LR->RegDioMapping1 =
      RFLR_DIOMAPPING1_DIO0_01 | RFLR_DIOMAPPING1_DIO1_00 | RFLR_DIOMAPPING1_DIO2_00 | RFLR_DIOMAPPING1_DIO3_01;
  //                         PllLock                  | Mode Ready
  SX1278LR->RegDioMapping2 = RFLR_DIOMAPPING2_DIO4_01 | RFLR_DIOMAPPING2_DIO5_00;
  SX1278WriteBuffer(REG_LR_DIOMAPPING1, &SX1278LR->RegDioMapping1, 2);

  SX1278LoRaSetOpMode(RFLR_OPMODE_TRANSMITTER);
}

static void standby() {
  // clear irq
  SX1278LR->RegIrqFlags = 0;
  SX1278Write(REG_LR_IRQFLAGS, 0xff);

  SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);
}

int LoraEventLoop(LoraSignal signal) {
  if (signal == INVALID) {
    return 0;
  }
  assert(flag.init == true);
  assert((uint32_t)flag.ifs_timer + (uint32_t)flag.backoff_timer + (uint32_t)flag.busy_timer <= 1u);

  switch (RFLRState) {
    case RFLR_STATE_IDLE: {
      assert(!flag.pending_tx && !flag.tx_single && !flag.rx_continuous && !flag.rx_single && !flag.ifs_timer &&
             !flag.backoff && !flag.backoff_timer && !flag.busy_timer);
      switch (signal) {
        case RX_SINGLE: {
          flag.rx_single = true;
          RxInit();
          RFLRState = RFLR_STATE_RX;
          break;
        }
        case RX_START: {
          flag.rx_continuous = true;
          RxInit();
          RFLRState = RFLR_STATE_RX;
          break;
        }
        case RX_STOP: {
          flag.rx_continuous = false;
#ifndef PHY_DEBUG
          printf("warning: lora driver received rx stop request when idle\r\n");
#endif
          break;
        }
        case TX: {
          flag.pending_tx = true;
          assert(flag.backoff == false && backoff_count == 0);
          RxInit();
          RFLRState = RFLR_STATE_RX;
          break;
        }
        default: {
          assert(false && "unexpected signal");
        }
      }
      break;
    }
    case RFLR_STATE_RX: {
      assert((flag.rx_continuous || flag.rx_single || flag.pending_tx));
      switch (signal) {
        case RX_SINGLE: {
          assert(!flag.rx_single);
          flag.rx_single = true;
          break;
        }
        case RX_START: {
#ifndef PHY_DEBUG
          printf("warning: lora driver received rx start request but rx already start\r\n");
#endif
          assert(!flag.rx_continuous);
          flag.rx_continuous = true;
          break;
        }
        case RX_STOP: {
          assert(flag.rx_continuous);
          flag.rx_continuous = false;
          if (!flag.rx_single && !flag.pending_tx) {
            standby();
            RFLRState = RFLR_STATE_IDLE;
          }
          break;
        }
        case TX: {
          assert(!flag.pending_tx);
          flag.pending_tx = true;
          assert(flag.backoff == false && backoff_count == 0);
          // start timer
          LoraStartTimer();
          break;
        }
        case IFS_TIMER: {
          assert(flag.pending_tx && flag.ifs_timer);
          flag.ifs_timer = false;
          uint32_t busy = LoraIsBusy();
          if (busy & 0x1) {
            if (LoraBackoffIncrement()) {
              if (!flag.rx_continuous && !flag.rx_single) {
                standby();
                RFLRState = RFLR_STATE_IDLE;
              } else {
                // start busy timer
                flag.busy_timer = true;
                LoraTimerOneShot(BUSY_TIMER_INTERVAL);
              }
            } else {
              // start busy timer
              flag.busy_timer = true;
              LoraTimerOneShot(BUSY_TIMER_INTERVAL);
            }
          } else {
            TxInit();
            RFLRState = RFLR_STATE_TX;
          }
          break;
        }
        case BACKOFF_TIMER: {
          assert(flag.pending_tx && flag.backoff_timer);
          flag.backoff_timer = false;
          uint32_t busy = LoraIsBusy();
          if (busy & 0x1) {
            if (LoraBackoffIncrement()) {
              if (!flag.rx_continuous && !flag.rx_single) {
                standby();
                RFLRState = RFLR_STATE_IDLE;
              } else {
                // start busy timer
                flag.busy_timer = true;
                LoraTimerOneShot(BUSY_TIMER_INTERVAL);
              }
            } else {
              // start busy timer
              flag.busy_timer = true;
              LoraTimerOneShot(BUSY_TIMER_INTERVAL);
            }
          } else {
            TxInit();
            RFLRState = RFLR_STATE_TX;
          }
          break;
        }
        case BUSY_TIMER: {
          assert(flag.pending_tx && flag.busy_timer);
          flag.busy_timer = false;
          uint32_t busy = LoraIsBusy();
          if (busy & 0x1) {
            // start busy timer
            flag.busy_timer = true;
            LoraTimerOneShot(BUSY_TIMER_INTERVAL);
          } else {
            LoraStartTimer();
          }
          break;
        }
        case RX_DONE: {
          SX1278Read(REG_LR_IRQFLAGS, &SX1278LR->RegIrqFlags);
          // clear irq
          SX1278Write(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_RXDONE);

          // SX1278Read(REG_LR_IRQFLAGS, &SX1278LR->RegIrqFlags);

          // check crc
          if ((SX1278LR->RegIrqFlags & RFLR_IRQFLAGS_PAYLOADCRCERROR) == RFLR_IRQFLAGS_PAYLOADCRCERROR) {
            // Clear Irq
            SX1278Write(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_PAYLOADCRCERROR);
            crc_error = true;
          } else {
            crc_error = false;
          }

          // read to buffer
          SX1278Read(REG_LR_FIFORXCURRENTADDR, &SX1278LR->RegFifoRxCurrentAddr);
          // SX1278LR->RegFifoAddrPtr = SX1278LR->RegFifoRxBaseAddr;
          SX1278LR->RegFifoAddrPtr = SX1278LR->RegFifoRxCurrentAddr;
          SX1278Write(REG_LR_FIFOADDRPTR, SX1278LR->RegFifoAddrPtr);
          SX1278Read(REG_LR_NBRXBYTES, &SX1278LR->RegNbRxBytes);
          if (flag.rx_single) {
            if (rx_sync_buffer_len < SX1278LR->RegNbRxBytes) {
              LoraSyncAPIGlobalError = Lora_RxBufferNotEnough;
            } else {
              SX1278ReadFifo(rx_sync_buffer, SX1278LR->RegNbRxBytes);
            }
          } else {
            SX1278ReadFifo(RXBuffer, SX1278LR->RegNbRxBytes);
          }

          if (flag.rx_single) {
            if (LoraSyncAPIGlobalError == Lora_OK) {
              LoraSyncAPIGlobalError = crc_error ? Lora_CRCError : Lora_OK;
            } else if (flag.rx_continuous) {
              // fallback
              LoraRxCallback(RXBuffer, SX1278LR->RegNbRxBytes, crc_error ? Lora_CRCError : Lora_OK);
            }
            xSemaphoreGive(lora_sync_api_semaphore);
            flag.rx_single = false;
          } else if (flag.rx_continuous) {
            LoraRxCallback(RXBuffer, SX1278LR->RegNbRxBytes, crc_error ? Lora_CRCError : Lora_OK);
          }

          LoraHandleTimer();

          assert(SX1278LoRaGetOpMode() == RFLR_OPMODE_RECEIVER);
          if (!flag.rx_continuous && !flag.pending_tx) {
            standby();
            RFLRState = RFLR_STATE_IDLE;
          }
          break;
        }
        default: {
          assert(false && "unexpected signal");
        }
      }
      break;
    }
    case RFLR_STATE_TX: {
      assert(flag.pending_tx && !flag.ifs_timer && !flag.backoff_timer && !flag.busy_timer);
      switch (signal) {
        case RX_SINGLE: {
          assert(!flag.rx_single);
          flag.rx_single = true;
          break;
        }
        case RX_START: {
          assert(!flag.rx_continuous);
          flag.rx_continuous = true;
          break;
        }
        case RX_STOP: {
          assert(flag.rx_continuous);
          flag.rx_continuous = false;
          break;
        }
        case TX: {
          assert(!flag.pending_tx);
          assert(false);
          break;
        }
        case TX_DONE: {
          // clear irq
          SX1278Write(REG_LR_IRQFLAGS, RFLR_IRQFLAGS_TXDONE);

          LoraHandleTxCallback(Lora_OK);

          if (flag.rx_continuous || flag.rx_single) {
            RxInit();
            RFLRState = RFLR_STATE_RX;
          } else {
            standby();
            RFLRState = RFLR_STATE_IDLE;
          }
          break;
        }
        default: {
          assert(false && "unexpected signal");
        }
      }
      break;
    }
    default: {
      assert(false && "unexpected lora state");
    }
  }
  return 0;
}

void LoraMain([[maybe_unused]] void *p) {
  LoraInit();
  while (true) {
    while (xQueueReceive(lora_queue, &lora_global_signal, portMAX_DELAY) != pdPASS) {
    }

    // translate DIO signal
    if (lora_global_signal == D0) {
      static_assert(RFLR_STATE_COUNT == 3);
      static constexpr LoraSignal signal_map[RFLR_STATE_COUNT] = {INVALID, RX_DONE, TX_DONE};
      lora_global_signal = signal_map[RFLRState];
    }

    // translate timer signal
    if (lora_global_signal == TIMER) {
      assert((uint32_t)flag.ifs_timer + (uint32_t)flag.backoff_timer + (uint32_t)flag.busy_timer <= 1u);
      if (!flag.ifs_timer && !flag.backoff_timer && !flag.busy_timer) {
#ifndef PHY_DEBUG
        printf("[DEBUG] warning: timer interrupt but timer is disabled.\r\n");
#endif
      }
      if (flag.ifs_timer) {
        lora_global_signal = IFS_TIMER;
      } else if (flag.backoff_timer) {
        lora_global_signal = BACKOFF_TIMER;
      } else if (flag.busy_timer) {
        lora_global_signal = BUSY_TIMER;
      } else {
        lora_global_signal = INVALID;
      }
    }

    if (lora_global_signal != 0 && lora_global_signal != BUSY_TIMER) {
      assert(lora_global_signal < LORA_SIGNAL_NUMBER);
#ifndef PHY_DEBUG
      printf("[DEBUG] signal: %s\r\n", lora_signal_map[lora_global_signal]);
#endif
    }

    LoraSignal signal_backup = lora_global_signal;
    LoraEventLoop(lora_global_signal);
    assert(lora_global_signal == signal_backup && "error: signal overlapped!");
    lora_global_signal = INVALID;
  }
}