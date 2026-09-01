/**
  ******************************************************************************
  * @file    bms_can.h
  * @brief   CAN transmit/receive layer for the monitoring node.
  ******************************************************************************
  */
#ifndef BMS_CAN_H
#define BMS_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"

/** Configure filters, start the peripheral, enable RX interrupts. */
HAL_StatusTypeDef BMS_CAN_Init(CAN_HandleTypeDef *hcan);

/** 100 ms group: summary, all 16 cell voltages, stats, fault status. */
void BMS_CAN_SendFast(void);

/** 500 ms group: temperatures and current limits. */
void BMS_CAN_SendSlow(void);

/** 1 s: heartbeat with uptime and firmware version. */
void BMS_CAN_SendHeartbeat(void);

/** Send the fault frame immediately, outside the normal schedule. */
void BMS_CAN_SendFaultsNow(void);

/** Poll RX FIFO 0. Call from the main loop; keeps ISR work minimal. */
void BMS_CAN_Poll(void);

/** True if the controller has gone bus-off. */
bool BMS_CAN_IsBusOff(void);

#endif /* BMS_CAN_H */
