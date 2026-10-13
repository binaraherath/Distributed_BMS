/**
 ******************************************************************************
 * @file    bms_app.h
 * @brief   Node 1 application: data model, scheduler and state machine.
 ******************************************************************************
 */
#ifndef BMS_APP_H
#define BMS_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"
#include "bms_config.h"
#include "bq76952.h"

/** Processed measurement (BMS sign convention: + charge, - discharge). */
typedef struct {
    uint16_t cell_mv[BMS_CELLS];
    int32_t  stack_mv;
    uint32_t cell_sum_mv;
    int32_t  current_ma;
    int16_t  ntc_dc[BMS_NTC_COUNT];     /* NTC1..NTC4                     */
    int16_t  die_dc;

    uint16_t cell_min_mv, cell_max_mv, cell_avg_mv, cell_delta_mv;
    uint8_t  cell_min_idx, cell_max_idx; /* 0-based                      */
    int16_t  temp_min_dc, temp_max_dc;   /* over fitted NTCs only          */
    bool     valid;
    uint32_t timestamp_ms;
} BmsMeasurement_t;

typedef struct {
    BmsState_t        state;
    uint32_t          faults;           /* latched, BMS_FLT_*              */
    uint16_t          warnings;         /* live,    BMS_WRN_*              */

    BmsMeasurement_t  m;
    BQ_Safety_t       bq;               /* last BQ safety snapshot         */
    uint16_t          bq_alarm_latched; /* OR of alarm bits since boot     */
    uint16_t          bq_device;
    uint16_t          bq_fw;
    bool              bq_ready;         /* probed and configured           */

    /* State of charge */
    float             soc_pct;
    float             soh_pct;
    float             usable_ah;
    float             remaining_ah;
    float             discharged_ah;    /* since boot                      */

    /* Limits sent to the controller */
    int32_t           chg_limit_ma;     /* >= 0                            */
    int32_t           dsg_limit_ma;     /* >= 0 (magnitude)                */
    uint8_t           limit_flags;      /* BMS_LIM_*                       */

    /* Balancing */
    uint16_t          bal_mask;         /* read back from the BQ           */
    bool              bal_enabled;      /* permission from controller/CLI  */
    bool              bal_conditions_ok;
    bool              bal_odd_group;
    uint32_t          bal_seconds;

    /* Health */
    uint32_t          last_good_meas_ms;
    uint32_t          last_ctrl_rx_ms;
    uint32_t          uptime_s;
    uint8_t           reset_cause;      /* BMS_RST_*                       */
    bool              alert_pin_ok;
} BmsContext_t;

extern BmsContext_t g_bms;

/* --- called from main.c ------------------------------------------------- */
void BMS_App_Init(I2C_HandleTypeDef *hi2c, CAN_HandleTypeDef *hcan,
                  SPI_HandleTypeDef *hspi, UART_HandleTypeDef *huart,
                  IWDG_HandleTypeDef *hiwdg);
void BMS_App_Loop(void);

/** ALERT pin EXTI (call from HAL_GPIO_EXTI_Callback). */
void BMS_App_OnAlertIrq(void);

/* --- commands (CAN / console) ------------------------------------------- */
bool BMS_App_ClearFaults(void);
void BMS_App_SetBalancing(bool enable);
void BMS_App_NoteControllerHeartbeat(void);
void BMS_App_RequestReconfigure(void);

const char *BMS_App_StateName(BmsState_t s);
/** Short name of the lowest set fault bit, "" if none. */
const char *BMS_App_FaultName(uint32_t fault_bit);

#endif /* BMS_APP_H */
