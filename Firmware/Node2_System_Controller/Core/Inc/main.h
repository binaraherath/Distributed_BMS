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
#include "stm32f1xx_hal.h"

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
#define LED_STATUS_Pin GPIO_PIN_13
#define LED_STATUS_GPIO_Port GPIOC
#define FAN_TACH_Pin GPIO_PIN_0
#define FAN_TACH_GPIO_Port GPIOB
#define FAN_TACH_EXTI_IRQn EXTI0_IRQn
#define LED_FAULT_Pin GPIO_PIN_1
#define LED_FAULT_GPIO_Port GPIOB
#define KEY_SW_Pin GPIO_PIN_10
#define KEY_SW_GPIO_Port GPIOB
#define ESTOP_LOOP_Pin GPIO_PIN_11
#define ESTOP_LOOP_GPIO_Port GPIOB
#define RLY_MAIN_NEG_Pin GPIO_PIN_12
#define RLY_MAIN_NEG_GPIO_Port GPIOB
#define RLY_PRECHARGE_Pin GPIO_PIN_13
#define RLY_PRECHARGE_GPIO_Port GPIOB
#define RLY_MAIN_POS_Pin GPIO_PIN_14
#define RLY_MAIN_POS_GPIO_Port GPIOB
#define RLY_CHARGE_EN_Pin GPIO_PIN_15
#define RLY_CHARGE_EN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
