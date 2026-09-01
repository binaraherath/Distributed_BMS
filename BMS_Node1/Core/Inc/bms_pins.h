/**
  ******************************************************************************
  * @file    bms_pins.h
  * @brief   Pin assignments for the monitoring node.
  ******************************************************************************
  */
#ifndef BMS_PINS_H
#define BMS_PINS_H

#include "stm32f1xx_hal.h"

/* --- Onboard LED (Blue Pill PC13, ACTIVE LOW) --------------------------- */
#define HB_LED_Pin              GPIO_PIN_13
#define HB_LED_GPIO_Port        GPIOC

/* --- Hardware safety interlock to the system controller ----------------- */
#define BMS_OK_Pin              GPIO_PIN_12
#define BMS_OK_GPIO_Port        GPIOB

/* --- Fault indicator LED ------------------------------------------------ */
#define FAULT_LED_Pin           GPIO_PIN_13
#define FAULT_LED_GPIO_Port     GPIOB

/* --- BQ76952 ALERT, active low, EXTI falling edge ----------------------- */
#define BQ_ALERT_Pin            GPIO_PIN_5
#define BQ_ALERT_GPIO_Port      GPIOB
#define BQ_ALERT_EXTI_IRQn      EXTI9_5_IRQn

/* --- SSD1306 OLED on SPI1 (PA5 SCK, PA7 MOSI) --------------------------- */
#define OLED_CS_Pin             GPIO_PIN_4
#define OLED_CS_GPIO_Port       GPIOA
#define OLED_DC_Pin             GPIO_PIN_3
#define OLED_DC_GPIO_Port       GPIOA
#define OLED_RST_Pin            GPIO_PIN_2
#define OLED_RST_GPIO_Port      GPIOA

/* --- Peripheral pin summary (for reference, configured by CubeMX) -------
 *   I2C1   SCL  PB6    SDA  PB7      -> BQ76952
 *   CAN1   RX   PB8    TX   PB9      -> ISO1042 
 *   USART1 TX   PA9    RX   PA10     -> debug console 115200 8N1
 *   SPI1   SCK  PA5    MOSI PA7      -> SSD1306
 * ---------------------------------------------------------------------- */

#endif /* BMS_PINS_H */
