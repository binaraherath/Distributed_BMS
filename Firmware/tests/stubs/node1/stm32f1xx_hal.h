#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int id; } GPIO_TypeDef;
extern GPIO_TypeDef mGPIOA, mGPIOB, mGPIOC;
#define GPIOA (&mGPIOA)
#define GPIOB (&mGPIOB)
#define GPIOC (&mGPIOC)
#define GPIO_PIN_2 0x0004u
#define GPIO_PIN_3 0x0008u
#define GPIO_PIN_4 0x0010u
#define GPIO_PIN_5 0x0020u
#define GPIO_PIN_6 0x0040u
#define GPIO_PIN_7 0x0080u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
typedef struct { int d; } SPI_HandleTypeDef;
typedef struct { int d; } I2C_HandleTypeDef;
typedef struct { int d; } CAN_HandleTypeDef;
typedef struct { int d; } UART_HandleTypeDef;
typedef struct { void *Instance; } IWDG_HandleTypeDef;
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t t);
