/**
  ******************************************************************************
  * @file    bms_config.h
  * @brief   Single place for every tunable parameter of the monitoring node.
  ******************************************************************************
  */
#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

/* ==========================================================================
 * 1. PACK TOPOLOGY
 * ========================================================================== */
#define BMS_CELL_COUNT              16u     /* 16S                             */
#define BMS_THERMISTOR_COUNT        5u      /* TS1..TS3 + DCHG + DDSG          */
#define BMS_PACK_DESIGN_CAPACITY_AH 66.0f   /* Leaf module = 2S2P              */

#define BMS_SHUNT_MILLIOHM          0.75f

/* ==========================================================================
 * 2. BQ76952 INTERFACE
 * ========================================================================== */
/* 7-bit address 0x08. HAL takes the 8-bit form, so 0x08 << 1 = 0x10. */
#define BQ_I2C_ADDR_8BIT            0x10u

/* Scaling of the direct-command results. Confirm both on the bench during
 * bring-up (step 6 in README.md) and correct here if needed. */
#define BQ_STACK_V_LSB_MV           10.0f   /* Stack/PACK voltage LSB          */
#define BQ_CURRENT_LSB_MA           10.0f   /* CC2 LSB.                        */

/* ==========================================================================
 * 3. PROTECTION LIMITS  (MCU-side, redundant to the BQ's own protections)
 * ========================================================================== */

/* --- Cell voltage (mV). ---------------------------------------------------- */
#define BMS_CELL_OV_WARN_MV         4150    /* stop charging (soft)            */
#define BMS_CELL_OV_FAULT_MV        4250    /* MCU hard fault                  */
#define BMS_CELL_UV_WARN_MV         3000    /* stop discharging (soft)         */
#define BMS_CELL_UV_FAULT_MV        2700    /* MCU hard fault                  */
#define BMS_CELL_PLAUSIBLE_MIN_MV   500     /* below this = broken sense wire   */
#define BMS_CELL_PLAUSIBLE_MAX_MV   5000    /* above this = broken sense wire   */

/* --- Temperature ----------------------------------------------------------- */
#define BMS_TEMP_CHG_MIN_DC         0       /* no charging below 0.0 C         */
#define BMS_TEMP_CHG_MAX_DC         450     /* no charging above 45.0 C        */
#define BMS_TEMP_DSG_MIN_DC        -200     /* no discharging below -20.0 C    */
#define BMS_TEMP_DSG_MAX_DC         550     /* no discharging above 55.0 C     */
#define BMS_TEMP_FAULT_DC           650     /* 65.0 C -> hard fault            */
#define BMS_TEMP_PLAUSIBLE_MIN_DC  -400
#define BMS_TEMP_PLAUSIBLE_MAX_DC   1200

/* --- Current --------------------------------------------------------------- */
#define BMS_CURRENT_CHG_LIMIT_MA    60000L      /*  60 A charge                */
#define BMS_CURRENT_DSG_LIMIT_MA   -120000L     /* 120 A discharge             */
#define BMS_CURRENT_FAULT_MA        150000L     /* |I| above this = hard fault */

/* --- Pack-level --------------------------------------------------------- */
#define BMS_CELL_DELTA_WARN_MV      120     /* imbalance warning               */
#define BMS_CELL_DELTA_FAULT_MV     500     /* imbalance hard fault            */

/* Consecutive bad samples before a fault latches. Rejects single-sample
 * noise without meaningfully delaying a real event (3 x 100 ms = 300 ms). */
#define BMS_FAULT_DEBOUNCE_COUNT    3u

/* ==========================================================================
 * 4. CELL BALANCING 
 * ========================================================================== */
#define BMS_BAL_ENABLE_ABOVE_MV     3900    /* only balance near the top       */
#define BMS_BAL_START_DELTA_MV      30      /* cell above min by this -> bleed */
#define BMS_BAL_STOP_DELTA_MV       15      /* hysteresis                      */
#define BMS_BAL_MAX_TEMP_DC         450     /* stop balancing above 45.0 C     */
#define BMS_BAL_MAX_CURRENT_MA      2000L   /* only balance at rest/slow chg   */
#define BMS_BAL_SLOT_MS             30000u  /* odd/even alternation period     */

/* ==========================================================================
 * 5. TIMING 
 * ========================================================================== */
#define BMS_TICK_MS                 10u
#define BMS_MEAS_PERIOD_MS          100u
#define BMS_CAN_FAST_PERIOD_MS      100u
#define BMS_CAN_SLOW_PERIOD_MS      500u
#define BMS_CAN_HEARTBEAT_MS        1000u
#define BMS_OLED_PERIOD_MS          250u
#define BMS_BALANCE_PERIOD_MS       1000u

#define BMS_BQ_COMMS_TIMEOUT_MS     500u    /* no good BQ read -> fault        */
#define BMS_CAN_RX_TIMEOUT_MS       2000u   /* no controller heartbeat -> warn */

/* ==========================================================================
 * 6. CAN IDs (11-bit standard)
 *    0x1xx = monitoring node TX,  0x2xx = system controller TX
 * ========================================================================== */
#define CAN_ID_PACK_SUMMARY         0x100u  /* 100 ms */
#define CAN_ID_CELLS_1_4            0x101u  /* 100 ms */
#define CAN_ID_CELLS_5_8            0x102u  /* 100 ms */
#define CAN_ID_CELLS_9_12           0x103u  /* 100 ms */
#define CAN_ID_CELLS_13_16          0x104u  /* 100 ms */
#define CAN_ID_TEMPERATURES         0x105u  /* 500 ms */
#define CAN_ID_CELL_STATS           0x106u  /* 100 ms */
#define CAN_ID_FAULT_STATUS         0x107u  /* 100 ms + immediately on change */
#define CAN_ID_LIMITS               0x108u  /* 500 ms */
#define CAN_ID_HEARTBEAT            0x109u  /* 1000 ms */

#define CAN_ID_CTRL_HEARTBEAT       0x200u  /* RX from system controller */
#define CAN_ID_CTRL_COMMAND         0x201u  /* RX: clear faults, balance enable */

/* ==========================================================================
 * 7. FEATURE SWITCHES
 * ========================================================================== */
#define BMS_USE_OLED                1       /* set 0 until the display is wired */
#define BMS_USE_UART_DEBUG          1       /* 115200 8N1 on USART1             */
#define BMS_USE_IWDG                1       /* leave 1 except when debugging    */

#define BMS_ARM_SAFETY_OUTPUT       0

#endif /* BMS_CONFIG_H */
