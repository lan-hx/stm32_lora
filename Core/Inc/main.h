/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SX_D0_Pin GPIO_PIN_1
#define SX_D0_GPIO_Port GPIOA
#define SX_D0_EXTI_IRQn EXTI1_IRQn
#define SX_D1_Pin GPIO_PIN_2
#define SX_D1_GPIO_Port GPIOA
#define SX_D1_EXTI_IRQn EXTI2_IRQn
#define SX_NSS_Pin GPIO_PIN_4
#define SX_NSS_GPIO_Port GPIOA
#define SX_RST_Pin GPIO_PIN_0
#define SX_RST_GPIO_Port GPIOB
#define RAND_SEED_Pin GPIO_PIN_8
#define RAND_SEED_GPIO_Port GPIOA
#define DEBUG_Pin GPIO_PIN_11
#define DEBUG_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
