/**
 ******************************************************************************
 * @file    ctrl_app.h
 * @brief   Node 2 application: contactor / precharge state machine, fault
 *          supervision of the BMS link, fan control and command routing.
 ******************************************************************************
 */
#ifndef CTRL_APP_H
#define CTRL_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"
#include "ctrl_config.h"

/** Latest view of Node 1, decoded from CAN. */
typedef struct {
    bool       online;              /* valid BMS_STATUS within timeout      */
    uint32_t   last_status_ms;
    uint32_t   pack_rx_count;       /* increments on every valid BMS_PACK   */
    BmsState_t state;
    uint32_t   faults;
    uint16_t   warnings;
    int32_t    pack_mv;
    int32_t    current_ma;          /* + charge / - discharge               */
    uint16_t   soc_x10;
    int32_t    chg_limit_ma;
    int32_t    dsg_limit_ma;
    uint8_t    limit_flags;
    int8_t     tmax_c;
    uint8_t    e2e_errors;          /* consecutive CRC errors / frozen counters */
} BmsView_t;

/** Latest view of Node 3. */
typedef struct {
    bool     online;
    uint32_t last_rx_ms;
    uint8_t  flags;
    int8_t   rssi_dbm;
} GwView_t;

typedef struct {
    CtrlState_t state;
    uint8_t     faults;             /* CTRL_FLT_*, latched                  */
    uint8_t     warnings;           /* CTRL_WRN_*                           */
    uint8_t     outputs;            /* CTRL_OUT_*                           */
    uint8_t     inputs;             /* CTRL_IN_*                            */
    uint8_t     fan_duty;           /* %                                    */
    uint16_t    fan_rpm;
    bool        armed;              /* key seen OFF since last stop / fault */
    bool        remote_stop;        /* latched until the key goes OFF       */
    int16_t     fan_override;       /* -1 = automatic, else 0..100 %        */
    uint32_t    fan_override_until_ms;
    uint32_t    state_entry_ms;
    uint8_t     step;               /* sub-step inside PRECHARGE / STOPPING */
    uint32_t    step_ms;
    uint32_t    uptime_s;
    uint8_t     reset_cause;
    BmsView_t   bms;
    GwView_t    gw;
} CtrlContext_t;

extern CtrlContext_t g_ctrl;

void Ctrl_App_Init(CAN_HandleTypeDef *hcan, TIM_HandleTypeDef *htim_fan,
                   UART_HandleTypeDef *huart, IWDG_HandleTypeDef *hiwdg);
void Ctrl_App_Loop(void);

/* Commands (from the gateway over CAN, or the console) */
void Ctrl_App_RequestStop(void);
void Ctrl_App_ClearFaults(void);            /* also forwarded to Node 1 */
void Ctrl_App_SetBalancing(bool allow);     /* forwarded to Node 1      */
void Ctrl_App_FanOverride(int16_t duty);    /* -1 = automatic           */

const char *Ctrl_App_StateName(CtrlState_t s);
const char *Ctrl_App_FaultName(uint8_t fault_bit);

#endif /* CTRL_APP_H */
