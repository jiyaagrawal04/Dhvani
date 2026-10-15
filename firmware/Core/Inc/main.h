/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "stm32u5xx_hal.h"
#include "stm32u5xx_nucleo.h"
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DAC_B2_Pin GPIO_PIN_2
#define DAC_B2_GPIO_Port GPIOE
#define DAC_B3_Pin GPIO_PIN_3
#define DAC_B3_GPIO_Port GPIOE
#define DAC_B4_Pin GPIO_PIN_4
#define DAC_B4_GPIO_Port GPIOE
#define DAC_B5_Pin GPIO_PIN_5
#define DAC_B5_GPIO_Port GPIOE
#define DAC_B6_Pin GPIO_PIN_6
#define DAC_B6_GPIO_Port GPIOE
#define DS18B20_Pin GPIO_PIN_12
#define DS18B20_GPIO_Port GPIOF
#define DAC_B7_Pin GPIO_PIN_7
#define DAC_B7_GPIO_Port GPIOE
#define DAC_B8_Pin GPIO_PIN_8
#define DAC_B8_GPIO_Port GPIOE
#define DAC_B9_Pin GPIO_PIN_9
#define DAC_B9_GPIO_Port GPIOE
#define DAC_B10_Pin GPIO_PIN_10
#define DAC_B10_GPIO_Port GPIOE
#define DAC_B11_Pin GPIO_PIN_11
#define DAC_B11_GPIO_Port GPIOE
#define DAC_B0_Pin GPIO_PIN_0
#define DAC_B0_GPIO_Port GPIOE
#define DAC_B1_Pin GPIO_PIN_1
#define DAC_B1_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
