/**
 ******************************************************************************
 * @file    ctrl_console.h
 * @brief   USART1 debug console for Node 2 (115200 8N1). Type "help".
 ******************************************************************************
 */
#ifndef CTRL_CONSOLE_H
#define CTRL_CONSOLE_H

#include "stm32f1xx_hal.h"
#include "ctrl_app.h"

void Console_Init(UART_HandleTypeDef *huart);
void Console_Printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void Console_Process(void);
void Console_Log(const CtrlContext_t *c);   /* 1 Hz when "log on" */

#endif /* CTRL_CONSOLE_H */
