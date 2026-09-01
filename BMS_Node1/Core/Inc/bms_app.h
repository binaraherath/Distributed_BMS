/**
  ******************************************************************************
  * @file    bms_app.h
  * @brief   Application layer: state machine, redundant protections, SoC,
  *          balancing policy.
  ******************************************************************************
  */
#ifndef BMS_APP_H
#define BMS_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "bq76952.h"


typedef enum {
    BMS_STATE_INIT = 0,
    BMS_STATE_IDLE,        /* healthy, |I| small                            */
    BMS_STATE_ACTIVE,      /* healthy, charging or discharging              */
    BMS_STATE_FAULT        /* latched                                        */
} BMS_State_t;

/* Fault bitfield, transmitted on CAN ID 0x107. */
typedef enum {
    BMS_FAULT_NONE          = 0x00000000u,
    /* measurement / plausibility */
    BMS_FAULT_BQ_COMMS      = 0x00000001u,  /* lost I2C to the BQ76952      */
    BMS_FAULT_BQ_CONFIG     = 0x00000002u,  /* config verify failed         */
    BMS_FAULT_CELL_IMPLAUS  = 0x00000004u,  /* cell reading out of range    */
    BMS_FAULT_TEMP_IMPLAUS  = 0x00000008u,  /* NTC open or shorted          */
    BMS_FAULT_SUM_MISMATCH  = 0x00000010u,  /* sum(cells) != stack voltage  */
    /* MCU-side protections */
    BMS_FAULT_CELL_OV       = 0x00000020u,
    BMS_FAULT_CELL_UV       = 0x00000040u,
    BMS_FAULT_OVER_TEMP     = 0x00000080u,
    BMS_FAULT_OVER_CURRENT  = 0x00000100u,
    BMS_FAULT_IMBALANCE     = 0x00000200u,
    /* BQ-side protections, mirrored */
    BMS_FAULT_BQ_COV        = 0x00001000u,
    BMS_FAULT_BQ_CUV        = 0x00002000u,
    BMS_FAULT_BQ_OCC        = 0x00004000u,
    BMS_FAULT_BQ_OCD        = 0x00008000u,
    BMS_FAULT_BQ_SCD        = 0x00010000u,
    BMS_FAULT_BQ_OT         = 0x00020000u,
    BMS_FAULT_BQ_UT         = 0x00040000u,
    /* system */
    BMS_FAULT_CAN_BUS_OFF   = 0x00100000u,
    BMS_FAULT_NOT_ARMED     = 0x00800000u   /* BMS_ARM_SAFETY_OUTPUT == 0   */
} BMS_Fault_t;

/* Non-latching warnings. These derate but never open a contactor. */
typedef enum {
    BMS_WARN_NONE           = 0x0000u,
    BMS_WARN_CELL_HIGH      = 0x0001u,
    BMS_WARN_CELL_LOW       = 0x0002u,
    BMS_WARN_TEMP_HIGH      = 0x0004u,
    BMS_WARN_TEMP_LOW       = 0x0008u,
    BMS_WARN_IMBALANCE      = 0x0010u,
    BMS_WARN_CTRL_TIMEOUT   = 0x0020u,  /* no controller heartbeat          */
    BMS_WARN_BALANCING      = 0x0040u
} BMS_Warning_t;

typedef struct {
    BMS_State_t state;
    uint32_t    faults;             /* latched                              */
    uint16_t    warnings;           /* live                                 */

    BQ_Data_t   meas;               /* last good measurement                */

    float       soc_pct;            /* 0..100                               */
    float       soh_pct;            /* placeholder, 100 until characterised  */
    float       coulomb_count_ah;   /* charge counter since last OCV reset   */

    int32_t     charge_limit_ma;    /* allowed charge current, >= 0          */
    int32_t     discharge_limit_ma; /* allowed discharge current, <= 0       */

    uint16_t    balance_mask;       /* cells currently commanded to bleed    */
    bool        balancing_allowed;

    uint32_t    last_good_meas_ms;
    uint32_t    last_ctrl_rx_ms;
    uint32_t    uptime_s;
    uint16_t    fault_debounce[8];  /* per-check debounce counters           */
} BMS_Context_t;

extern BMS_Context_t g_bms;

/* --- lifecycle ---------------------------------------------------------- */
void BMS_App_Init(void);

/** Called every BMS_MEAS_PERIOD_MS. Reads the BQ, evaluates protections,
 *  updates SoC and limits, drives the hardware safety output. */
void BMS_App_MeasureAndEvaluate(void);

/** Called every BMS_BALANCE_PERIOD_MS. */
void BMS_App_UpdateBalancing(void);

/** Called every 1000 ms. */
void BMS_App_Tick1s(void);

/** Clear latched faults. Only succeeds if no fault condition is still
 *  present -- a clear request cannot override live danger. */
bool BMS_App_ClearFaults(void);

void BMS_App_NoteControllerHeartbeat(void);
void BMS_App_SetBalancingPermission(bool allowed);

const char *BMS_App_StateName(BMS_State_t s);

#endif /* BMS_APP_H */
