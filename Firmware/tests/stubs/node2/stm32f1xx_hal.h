#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum { HAL_OK = 0, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { int d; } CAN_HandleTypeDef;
typedef struct { int d; } TIM_HandleTypeDef;
typedef struct { int d; } UART_HandleTypeDef;
typedef struct { void *Instance; } IWDG_HandleTypeDef;
#define IWDG ((void *)0x40003000)
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h);
#define RCC_FLAG_PORRST 1
#define RCC_FLAG_PINRST 2
#define RCC_FLAG_IWDGRST 3
#define RCC_FLAG_SFTRST 4
#define __HAL_RCC_GET_FLAG(f) (0)
#define __HAL_RCC_CLEAR_RESET_FLAGS() do {} while (0)
