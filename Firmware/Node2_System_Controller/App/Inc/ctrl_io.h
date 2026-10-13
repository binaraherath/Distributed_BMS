/**
 ******************************************************************************
 * @file    ctrl_io.h
 * @brief   Node 2 hardware I/O: contactor / relay drivers, debounced key and
 *          E-stop inputs, fan PWM (TIM3_CH1) and tachometer (EXTI0), LEDs.
 ******************************************************************************
 */
#ifndef CTRL_IO_H
#define CTRL_IO_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"

void     Io_Init(TIM_HandleTypeDef *htim_fan);

/** Drive the relays from a CTRL_OUT_* bitmap (CTRL_OUT_FAN is ignored). */
void     Io_SetOutputs(uint8_t outputs);

/** Sample and debounce the inputs - call every CTRL_TICK_MS. */
void     Io_Sample(void);
bool     Io_KeyOn(void);
bool     Io_EstopOk(void);

void     Io_SetFanDuty(uint8_t percent);
void     Io_Tick1s(void);           /* converts tach pulses to rpm */
uint16_t Io_FanRpm(void);
void     Io_OnTachPulse(void);      /* EXTI0 callback              */

void     Io_SetLeds(bool status_on, bool fault_on);

#endif /* CTRL_IO_H */
