
#ifndef __SX1278_HAL_H__
#define __SX1278_HAL_H__

#include "main.h"

/*!
 * DIO state read functions mapping
 */

typedef enum
{
    RADIO_RESET_OFF,
    RADIO_RESET_ON,
}tRadioResetState;

extern volatile uint8_t lora_dma_running;

__attribute((always_inline)) inline void SPI_NSS_LOW() {
  HAL_GPIO_WritePin(SX_NSS_GPIO_Port, SX_NSS_Pin, GPIO_PIN_RESET);
}
__attribute((always_inline)) inline void SPI_NSS_HIGH() {
  HAL_GPIO_WritePin(SX_NSS_GPIO_Port, SX_NSS_Pin, GPIO_PIN_SET);
}

void SX1278_hw_Reset(void);
void SX1278_hw_init(void);
void Init_SPI(void);
void SPIWrite(uint8_t adr, uint8_t WrPara);
uint8_t SPIRead(uint8_t adr);

/*!
 * \brief Writes the radio register at the specified address
 *
 * \param [IN]: addr Register address
 * \param [IN]: data New register value
 */
void SX1278Write( uint8_t addr, uint8_t data );

/*!
 * \brief Reads the radio register at the specified address
 *
 * \param [IN]: addr Register address
 * \param [OUT]: data Register value
 */
void SX1278Read( uint8_t addr, uint8_t *data );

/*!
 * \brief Writes multiple radio registers starting at address
 *
 * \param [IN] addr   First Radio register address
 * \param [IN] buffer Buffer containing the new register's values
 * \param [IN] size   Number of registers to be written
 */

void SX1278WriteBuffer(uint8_t adr, uint8_t *ptr, uint8_t length);


/*!
 * \brief Reads multiple radio registers starting at address
 *
 * \param [IN] addr First Radio register address
 * \param [OUT] buffer Buffer where to copy the registers data
 * \param [IN] size Number of registers to be read
 */

void SX1278ReadBuffer(uint8_t adr, uint8_t *ptr, uint8_t length);


/*!
 * \brief Writes the buffer contents to the radio FIFO
 *
 * \param [IN] buffer Buffer containing data to be put on the FIFO.
 * \param [IN] size Number of bytes to be written to the FIFO
 */
void SX1278WriteFifo( uint8_t *buffer, uint8_t size );

/*!
 * \brief Reads the contents of the radio FIFO
 *
 * \param [OUT] buffer Buffer where to copy the FIFO read data.
 * \param [IN] size Number of bytes to be read from the FIFO
 */
void SX1278ReadFifo( uint8_t *buffer, uint8_t size );

/*!
 * \brief Gets the SX1278 DIO0 hardware pin status
 *
 * \retval status Current hardware pin status [1, 0]
 */
__attribute((always_inline)) inline uint8_t SX1278ReadDio0( void ) {
  return HAL_GPIO_ReadPin(SX_D0_GPIO_Port, SX_D0_Pin);
}

#endif //__SX1278_HAL_H__
