/**
 ******************************************************************************
 * @file    bms_console.h
 * @brief   USART1 debug console (115200 8N1, PCB "USART" header).
 *          Non-blocking output through an interrupt-driven ring buffer and a
 *          small command line for bench testing. Type "help".
 ******************************************************************************
 */
#ifndef BMS_CONSOLE_H
#define BMS_CONSOLE_H

#include "stm32f1xx_hal.h"
#include "bms_app.h"

void Console_Init(UART_HandleTypeDef *huart);
void Console_Printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Parse and run a pending command line (main loop). */
void Console_Process(void);

/** 1 Hz status line when "log on" (main loop). */
void Console_Log(const BmsContext_t *b);

#endif /* BMS_CONSOLE_H */
