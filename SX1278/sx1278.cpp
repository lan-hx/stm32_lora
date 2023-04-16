/**
 * @brief sx1278 impl
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
#include "sx1278.h"
#include "sx1278.h"

#include <string.h>

#include "radioConfig.h"
#include "sx1278-Hal.h"
#include "sx1278-LoRaMisc.h"
/*!
 * SX1278 registers variable
 */
uint8_t SX1278Regs[0x70];
tSX1278LR *SX1278LR;

// Default settings
tLoRaSettings LoRaSettings = {
    434000000,  // RFFrequency
    20,         // Power
    9,          // SignalBw [0: 7.8kHz, 1: 10.4 kHz, 2: 15.6 kHz, 3: 20.8 kHz, 4: 31.2 kHz,
                // 5: 41.6 kHz, 6: 62.5 kHz, 7: 125 kHz, 8: 250 kHz, 9: 500 kHz, other: Reserved]
    6,          // SpreadingFactor [6: 64, 7: 128, 8: 256, 9: 512, 10: 1024, 11: 2048, 12: 4096  chips]
    1,          // ErrorCoding [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
    true,       // CrcOn [0: OFF, 1: ON]
    false,      // ImplicitHeaderOn [0: OFF, 1: ON]
    1,          // RxSingleOn [0: Continuous, 1 Single]
    0,          // FreqHopOn [0: OFF, 1: ON]
    4,          // HopPeriod Hops every frequency hopping period symbols
    100,        // TxPacketTimeout
    100,        // RxPacketTimeout
    128,        // PayloadLength (used for implicit header mode)
};

/*!
 * Local RF buffer for communication support
 */
static uint8_t RFBuffer[RF_BUFFER_SIZE];

/*!
 * RF state machine variable
 */
uint8_t RFLRState = RFLR_STATE_IDLE;
uint32_t PacketTimeout;
uint16_t TxPacketSize = 0;
/*!
 * Rx management support variables
 */
uint16_t RxPacketSize = 0;
uint32_t RxTimeoutTimer = 0;
/*!
 * PacketTimeout Stores the Rx window time value for packet reception
 */


/*!
 * Tx management support variables
 */


uint8_t SX1278LoRaInit(void) {
  uint8_t version = 0;

  RFLRState = RFLR_STATE_IDLE;

  // SX1278LoRaSetDefaults();

  version = SX1278LR->RegVersion;

  SX1278ReadBuffer(REG_LR_OPMODE, SX1278Regs + 1, 0x70 - 1);

  SX1278WriteBuffer(REG_LR_OPMODE, SX1278Regs + 1, 0x70 - 1);
  //modified
  SX1278LoRaSetRFPower(LoRaSettings.Power);
  // SX1278LoRaSetRxPacketTimeout(50);
  // SX1278LoRaSetTxPacketTimeout(50);
  SX1278LoRaSetRxPacketTimeout(5000);
  SX1278LoRaSetTxPacketTimeout(5000);
  SX1278LoRaSetPreambleLength(128);
  SX1278LoRaSetLowDatarateOptimize(true);
  // set the RF settings
  SX1278LoRaSetRFFrequency(LoRaSettings.RFFrequency);
  SX1278LoRaSetSpreadingFactor(LoRaSettings.SpreadingFactor);  // SF6 only operates in implicit header mode.
  SX1278LoRaSetErrorCoding(LoRaSettings.ErrorCoding);
  SX1278LoRaSetPacketCrcOn(LoRaSettings.CrcOn);
  SX1278LoRaSetSignalBandwidth(LoRaSettings.SignalBw);

  SX1278LoRaSetImplicitHeaderOn(LoRaSettings.ImplicitHeaderOn);
  SX1278LoRaSetSymbTimeout(0x3FF);
  SX1278LoRaSetPayloadLength(LoRaSettings.PayloadLength);
  // SX1278LoRaSetLowDatarateOptimize( true );
  SX1278LoRaSetOpMode(RFLR_OPMODE_STANDBY);
  
    // if( LoRaSettings.RFFrequency > 860000000 )
    // {
    //     SX1278LoRaSetPAOutput( RFLR_PACONFIG_PASELECT_RFO );
    //     SX1278LoRaSetPa20dBm( false );
    //     LoRaSettings.Power = 14;
    //     SX1278LoRaSetRFPower( LoRaSettings.Power );
    // }
    // else
    // {
    //     SX1278LoRaSetPAOutput( RFLR_PACONFIG_PASELECT_PABOOST );
    //     SX1278LoRaSetPa20dBm( true );
    //     LoRaSettings.Power = 20;
    //     SX1278LoRaSetRFPower( LoRaSettings.Power );
    // } 

  return version;
}

void SX1278LoRaSetOpMode(uint8_t opMode) {
  static uint8_t opModePrev = RFLR_OPMODE_STANDBY;
  // static bool antennaSwitchTxOnPrev = true;
  // bool antennaSwitchTxOn = false;

  opModePrev = SX1278LR->RegOpMode & ~RFLR_OPMODE_MASK;

  if (opMode != opModePrev) {

    SX1278LR->RegOpMode = (SX1278LR->RegOpMode & RFLR_OPMODE_MASK) | opMode;
    SX1278LR->RegOpMode |= RFLR_OPMODE_FREQMODE_ACCESS_LF;

    SX1278Write(REG_LR_OPMODE, SX1278LR->RegOpMode);
  }
}


uint8_t SX1278LoRaGetOpMode(void) {
    SX1278Read(REG_LR_OPMODE, &SX1278LR->RegOpMode);

    return SX1278LR->RegOpMode & ~RFLR_OPMODE_MASK;
}

void SX1278LoRaStartRx( void )
{
    SX1278LoRaSetRFState( RFLR_STATE_RX_INIT );
}

void SX1278LoRaGetRxPacket(void *buffer, uint16_t *size) {
    *size = RxPacketSize;
    RxPacketSize = 0;
    memcpy((void *)buffer, (void *)RFBuffer, (size_t)*size);
}

void SX1278LoRaSetTxPacket(const void *buffer, uint16_t size) {
    TxPacketSize = size;
    memcpy((void *)RFBuffer, buffer, (size_t)TxPacketSize);

    RFLRState = RFLR_STATE_TX_INIT;
    printf("RFLRState in SX1278LoRaSetTxPacket==%d\r\n", RFLRState);
}

uint8_t SX1278LoRaGetRFState(void) { return RFLRState; }

void SX1278LoRaSetRFState(uint8_t state) { RFLRState = state; }