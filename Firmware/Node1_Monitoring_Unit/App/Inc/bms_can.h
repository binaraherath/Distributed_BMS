/**
 ******************************************************************************
 * @file    bms_can.h
 * @brief   Node 1 CAN layer: frame packing (bms_can_protocol.h), a TX queue
 *          drained from the mailbox-empty interrupt, and command reception
 *          from the System Controller with E2E checks.
 ******************************************************************************
 */
#ifndef BMS_CAN_H
#define BMS_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"
#include "bms_app.h"

HAL_StatusTypeDef BmsCan_Init(CAN_HandleTypeDef *hcan);

void BmsCan_SendFast(const BmsContext_t *b);     /* 100 ms group  */
void BmsCan_SendSlow(const BmsContext_t *b);     /* 500 ms group  */
void BmsCan_SendInfo(const BmsContext_t *b);     /* 1000 ms group */
void BmsCan_SendStatusNow(const BmsContext_t *b);

/** Handle received frames (main loop context). */
void BmsCan_Process(void);

/** True once if an error-passive / bus-off event happened since last call. */
bool BmsCan_TakeErrorFlag(void);

uint32_t BmsCan_DroppedFrames(void);

#endif /* BMS_CAN_H */
