/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
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
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cmsis_os.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
extern osMutexId_t mutex_id;
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
#define ESP8266_RX_Pin GPIO_PIN_5
#define ESP8266_RX_GPIO_Port GPIOD
#define ESP32_EN_Pin GPIO_PIN_4
#define ESP32_EN_GPIO_Port GPIOD
#define AD7606_CS_Pin GPIO_PIN_6
#define AD7606_CS_GPIO_Port GPIOB
#define AD7606_OS1_Pin GPIO_PIN_4
#define AD7606_OS1_GPIO_Port GPIOB
#define ESP8266_TX_Pin GPIO_PIN_6
#define ESP8266_TX_GPIO_Port GPIOD
#define AD7606_REST_Pin GPIO_PIN_7
#define AD7606_REST_GPIO_Port GPIOB
#define AD7606_CONVEST_A_Pin GPIO_PIN_3
#define AD7606_CONVEST_A_GPIO_Port GPIOB
#define AD7606_SCLK_Pin GPIO_PIN_5
#define AD7606_SCLK_GPIO_Port GPIOE
#define AD7606_OS2_Pin GPIO_PIN_3
#define AD7606_OS2_GPIO_Port GPIOE
#define AD7606_CONVEST_B_Pin GPIO_PIN_9
#define AD7606_CONVEST_B_GPIO_Port GPIOB
#define AD7606_OS0_Pin GPIO_PIN_8
#define AD7606_OS0_GPIO_Port GPIOB
#define AD7606_BUSY_Pin GPIO_PIN_3
#define AD7606_BUSY_GPIO_Port GPIOC
#define AD7606_DB7_Pin GPIO_PIN_0
#define AD7606_DB7_GPIO_Port GPIOA
#define ESP32_READY_Pin GPIO_PIN_10
#define ESP32_READY_GPIO_Port GPIOB
#define ESP32_READY_EXTI_IRQn EXTI15_10_IRQn
#define ESP32_CS_Pin GPIO_PIN_12
#define ESP32_CS_GPIO_Port GPIOB
#define ESP32_MOSI_Pin GPIO_PIN_15
#define ESP32_MOSI_GPIO_Port GPIOB
#define ESP32_SCK_Pin GPIO_PIN_13
#define ESP32_SCK_GPIO_Port GPIOB
#define ESP32_MISO_Pin GPIO_PIN_14
#define ESP32_MISO_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define USE_AD7606 1

/* 采样点数（双缓冲采集长度）：4096 点（降低以减轻 FFT/UI 负载，缓解卡顿） */
#define AD_ACQ_POINTS 4096

/* AD7606 过采样模式：
 * 25.6kHz 下必须使用 OS=0（无过采样）才能避免 BUSY 过长导致 miss≈frames、UI卡顿。 */
#define AD7606_OS_MODE 0u
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
