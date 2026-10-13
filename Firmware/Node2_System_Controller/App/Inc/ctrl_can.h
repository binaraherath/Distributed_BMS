/**
 ******************************************************************************
 * @file    ctrl_can.h
 * @brief   Node 2 CAN layer: decodes Node 1 and gateway frames into the
 *          controller context (with E2E checks), sends CTRL_STATUS / INFO
 *          and commands to Node 1.
 ******************************************************************************
 */
#ifndef CTRL_CAN_H
#define CTRL_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"
#include "ctrl_app.h"

HAL_StatusTypeDef CtrlCan_Init(CAN_HandleTypeDef *hcan);

/** Decode received frames into g_ctrl (main loop context). */
void CtrlCan_Process(void);

void CtrlCan_SendStatus(const CtrlContext_t *c);    /* 100 ms  */
void CtrlCan_SendInfo(const CtrlContext_t *c);      /* 1000 ms */
void CtrlCan_SendBmsCommand(uint8_t cmd, uint8_t arg);

bool CtrlCan_IsBusOff(void);
bool CtrlCan_TakeErrorFlag(void);

#endif /* CTRL_CAN_H */
