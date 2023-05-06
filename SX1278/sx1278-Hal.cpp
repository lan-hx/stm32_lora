#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "spi.h"
#include "radioConfig.h"
#include "sx1278-Hal.h"
#include "utility.h"

#define DUMMY_BYTE   0

volatile uint8_t lora_dma_running = 0;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Configure system clock to run at 16MHz
  * @param  None
  * @retval None
  */

void SX1278_hw_Reset() {
  HAL_SPI_DMAStop(&SX1278SPI);
  SPI_NSS_HIGH();
  //HAL_SPI_DeInit(&SX1278SPI);
  HAL_GPIO_WritePin(SX_RST_GPIO_Port, SX_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(SX_RST_GPIO_Port, SX_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);
  //HAL_SPI_Init(&SX1278SPI);
}

void SX1278_hw_init() {
  HAL_SPI_DMAStop(&SX1278SPI);
  SPI_NSS_HIGH();
  HAL_GPIO_WritePin(SX_RST_GPIO_Port, SX_RST_Pin, GPIO_PIN_SET);
}

/**********************************************************
* @brief  Sends a byte through the SPI interface and 
					return the byte received from the SPI bus.
* @param  byte: byte to send.
* @retval The value of the received byte.
**********************************************************/
static uint8_t SPI1_SendByte(uint8_t byte) {
  uint8_t Readbyte;
  HAL_SPI_TransmitReceive(&SX1278SPI, &byte, &Readbyte, 1, SPI_TIMEOUT);
  return Readbyte;
}
/**********************************************************
**Name:     SPIWrite
**Function: SPI Write CMD
**Input:    uint8_t address & uint8_t data
**Output:   None
**********************************************************/
void SPIWrite(uint8_t adr, uint8_t WrPara) {
  SPI_NSS_LOW();
  SPI1_SendByte(adr | 0x80);
  SPI1_SendByte(WrPara);
  SPI_NSS_HIGH();
}
/**********************************************************
**Name:     SPIRead
**Function: SPI Read CMD
**Input:    adr -> address for read
**Output:   None
**********************************************************/
uint8_t SPIRead(uint8_t adr) {
  uint8_t tmp;
  SPI_NSS_LOW();
  SPI1_SendByte(adr);
  tmp = SPI1_SendByte(DUMMY_BYTE);
  SPI_NSS_HIGH();
  return (tmp);
}
/**********************************************************
**Name:     SX1278ReadBuffer
**Function: SPI burst read mode
**Input:    adr-----address for read
**          ptr-----data buffer point for read
**          length--how many bytes for read
**Output:   None
**********************************************************/

void SX1278ReadBuffer(uint8_t adr, uint8_t *ptr, uint8_t length) {
  //uint8_t i;

  SPI_NSS_LOW();
  SPI1_SendByte(adr);

  //lora_dma_running = 1;
  HAL_SPI_Receive(&SX1278SPI, ptr, length, HAL_MAX_DELAY);
  //HAL_SPI_Receive_DMA(&SX1278SPI, ptr, length);
  //while (lora_dma_running) {}

  //for (i = 0; i < length; i++) {
  //  ptr[i] = SPI1_SendByte(DUMMY_BYTE);
  //}

  SPI_NSS_HIGH();
}
/**********************************************************
**Name:     SX1278WriteBuffer
**Function: SPI burst write mode
**Input:    adr-----address for write
**          ptr-----data buffer point for write
**          length--how many bytes for write
**Output:   none
**********************************************************/
void SX1278WriteBuffer(uint8_t adr, uint8_t *ptr, uint8_t length) {
  //uint8_t i;

  SPI_NSS_LOW();
  SPI1_SendByte(adr | 0x80);

  //lora_dma_running = 1;
  HAL_SPI_Transmit(&SX1278SPI, ptr, length, HAL_MAX_DELAY);
  //HAL_SPI_Transmit_DMA(&SX1278SPI, ptr, length);
  //while (lora_dma_running) {}

  //for (i = 0; i < length; i++) {
  //  SPI1_SendByte(ptr[i]);
  //}

  SPI_NSS_HIGH();
}

void SX1278Write(uint8_t addr, uint8_t data) {
  SX1278WriteBuffer(addr, &data, 1);
#if (SX1278_DEBUG == 1)
  printf("WRITE REG 0x%02X = 0x%02X\r\n",addr,data);
#endif
}

void SX1278Read(uint8_t addr, uint8_t *data) {
  SX1278ReadBuffer(addr, data, 1);
#if (SX1278_DEBUG == 1)
  printf("READ REG 0x%02X : 0x%02X\r\n",addr,*data);
#endif
}

void SX1278WriteFifo(uint8_t *buffer, uint8_t size) {
  SX1278WriteBuffer(0, buffer, size);
}

void SX1278ReadFifo(uint8_t *buffer, uint8_t size) {
  SX1278ReadBuffer(0, buffer, size);
}
