
#ifndef __SX1278_HAL_H__
#define __SX1278_HAL_H__



/*!
 * DIO state read functions mapping
 */


#define RED_LED_OFF()           GPIO_WriteBit(GPIOA, GPIO_Pin_4, RESET)
#define RED_LED_ON()            GPIO_WriteBit(GPIOA, GPIO_Pin_4, SET)
#define RED_LED_TOGGLE()        GPIO_ToggleBits(GPIOA, GPIO_Pin_4)

#define GREEN_LED_OFF()         GPIO_WriteBit(GPIOA, GPIO_Pin_5, RESET)
#define GREEN_LED_ON()          GPIO_WriteBit(GPIOA, GPIO_Pin_5, SET)
#define GREEN_LED_TOGGLE()      GPIO_ToggleBits(GPIOA, GPIO_Pin_5)
    
#define BL_ON()                 GPIO_WriteBit(GPIOB, GPIO_Pin_3, SET)
#define BL_OFF()                GPIO_WriteBit(GPIOB, GPIO_Pin_3, RESET)
#define BL_TOGGLE()             GPIO_ToggleBits(GPIOB, GPIO_Pin_3)


// RXTX pin control see errata note
//#define RXTX( txEnable )                            SX1278WriteRxTx( txEnable );

#define GET_TICK_COUNT( )                            HAL_GetTick() 
#define TICK_RATE_MS( ms )                          ( ms )

typedef enum
{
    RADIO_RESET_OFF,
    RADIO_RESET_ON,
}tRadioResetState;

void SX1278_hw_Reset(void);
void SX1278_hw_init(void);
void Init_SPI(void);
void SPIWrite(uint8_t adr, uint8_t WrPara);
uint8_t SPIRead(uint8_t adr);

/*!
 * \brief Initializes the radio interface I/Os
 */
void SX1278InitIo( void );

/*!
 * \brief Set the radio reset pin state
 * 
 * \param state New reset pin state
 */
void SX1278SetReset( uint8_t state );

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
inline uint8_t SX1278ReadDio0( void );

#define DIO0                    SX1278ReadDio0( )
#define DIO1                    0//SX1278ReadDio1( )
#define DIO2                    0//SX1278ReadDio2( )
#define DIO3                    0//SX1278ReadDio3( )
#define DIO4                    0//SX1278ReadDio4( )
#endif //__SX1278_HAL_H__
