/**
 ******************************************************************************
 * @file    bms_config.h
 * @brief   Node 1 (Master Monitoring Unit) - every tunable parameter.
 *
 *  Pack : 16S, 8 x Nissan Leaf (Gen 1) modules, each module 2S2P
 *         Rated 66.2 Ah / 60 V nominal, measured SOH ~35 % -> ~23 Ah usable
 *  Units: mV, mA, 0.1 degC ("dC") unless the name says otherwise.
 ******************************************************************************
 */
#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

#include "bms_can_protocol.h"

#define BMS_FW_VERSION_MAJOR        1u
#define BMS_FW_VERSION_MINOR        0u

/* ==========================================================================
 * 1. PACK
 * ========================================================================== */
#define BMS_CELLS                   BMS_CELL_COUNT      /* 16S                */
#define BMS_CELL_NOMINAL_MV         3750
#define BMS_RATED_CAPACITY_AH       66.2f               /* 2P x 33.1 Ah new   */
#define BMS_INITIAL_SOH_PCT         35.0f               /* capacity test      */
#define BMS_USABLE_CAPACITY_AH      (BMS_RATED_CAPACITY_AH * BMS_INITIAL_SOH_PCT / 100.0f)

/* ==========================================================================
 * 2. MEASUREMENT HARDWARE (see PCB/SCH_bms_*.json)
 * ========================================================================== */
#define BMS_SHUNT_MOHM              0.75f   /* 100 A / 75 mV shunt            */
#define BMS_CURRENT_SIGN            (+1)    /* set -1 if shunt leads swapped  */
#define BMS_CURRENT_DEADBAND_MA     60      /* |I| below this is shown as 0   */

/* Four 10 k NTCs to VSS (BQ 18 k pull-up + "18K" 103AT-type model) on TEMP.:
 *   NTC1 = header "TS1" -> BQ pin 21 TS1
 *   NTC2 = header "TS2" -> BQ pin 23 TS3
 *   NTC3 = header "TS3" -> BQ pin 31 DCHG
 *   NTC4 = header "TS4" -> BQ pin 32 DDSG
 * Clear a bit to ignore an NTC that is not fitted (bench testing).        */
#define BMS_NTC_FITTED_MASK         0x0Fu

/* ==========================================================================
 * 3. CELL VOLTAGE WINDOW  (Leaf LMO/NMC cells, derated for SOH 35 %)
 * ========================================================================== */
#define BMS_CELL_OV_FAULT_MV        4200    /* hard trip                      */
#define BMS_CELL_OV_WARN_MV         4150    /* charge blocked                 */
#define BMS_CELL_FULL_MV            4100    /* SoC = 100 % anchor             */
#define BMS_CELL_CHG_TAPER_MV       4000    /* charge limit starts to taper   */
#define BMS_CELL_CHG_RESUME_MV      4050    /* charge re-allowed below this   */
#define BMS_CELL_DSG_TAPER_MV       3400    /* discharge limit starts taper   */
#define BMS_CELL_EMPTY_MV           3200    /* SoC = 0 %, discharge blocked   */
#define BMS_CELL_DSG_RESUME_MV      3350    /* discharge re-allowed above     */
#define BMS_CELL_UV_FAULT_MV        3000    /* hard trip                      */
#define BMS_CELL_PLAUSIBLE_MIN_MV   500     /* below -> open sense wire       */
#define BMS_CELL_PLAUSIBLE_MAX_MV   4800
#define BMS_CELL_DELTA_WARN_MV      80
#define BMS_CELL_DELTA_FAULT_MV     500
#define BMS_STACK_MISMATCH_PCT      2u      /* |stack - sum(cells)| > 2 %     */
#define BMS_STACK_MISMATCH_MIN_MV   500

/* ==========================================================================
 * 4. TEMPERATURE WINDOW (0.1 degC)
 * ========================================================================== */
#define BMS_TEMP_CHG_MIN_DC         0
#define BMS_TEMP_CHG_MAX_DC         450
#define BMS_TEMP_DSG_MIN_DC         (-100)
#define BMS_TEMP_DSG_MAX_DC         550
#define BMS_TEMP_DERATE_START_DC    450     /* discharge limit linear -> 0    */
#define BMS_TEMP_WARN_HIGH_DC       450
#define BMS_TEMP_WARN_LOW_DC        50
#define BMS_TEMP_OT_FAULT_DC        600
#define BMS_TEMP_UT_FAULT_DC        (-200)
#define BMS_TEMP_DIE_FAULT_DC       850
#define BMS_TEMP_PLAUSIBLE_MIN_DC   (-400)  /* colder -> NTC open             */
#define BMS_TEMP_PLAUSIBLE_MAX_DC   1200    /* hotter -> NTC shorted          */

/* ==========================================================================
 * 5. CURRENT (mA, magnitudes)
 * ========================================================================== */
#define BMS_CHG_LIMIT_MA            12000L  /* ~0.5 C of usable capacity      */
#define BMS_DSG_LIMIT_MA            40000L
#define BMS_CHG_TAPER_END_MA        1000L   /* limit at BMS_CELL_FULL_MV      */
#define BMS_OC_CHG_FAULT_MA         20000L  /* MCU trip (debounced 1 s)       */
#define BMS_OC_DSG_FAULT_MA         60000L
#define BMS_STATE_CURRENT_MA        500L    /* IDLE <-> CHARGING/DISCHARGING  */

/* ==========================================================================
 * 6. BQ76952 HARDWARE PROTECTION LAYER (backs up the MCU checks)
 * ========================================================================== */
#define BQ_COV_MV                   4200    /* 50.6 mV steps                  */
#define BQ_CUV_MV                   3000
#define BQ_OCC_A                    20      /* 2 mV steps across the shunt    */
#define BQ_OCD1_A                   60
#define BQ_OCD2_A                   80
#define BQ_SCD_CODE                 5u      /* 5 = 100 mV = 133 A @ 0.75 mOhm */
#define BQ_OTC_C                    50
#define BQ_OTD_C                    60
#define BQ_UTC_C                    0
#define BQ_UTD_C                    (-10)
#define BQ_BAL_MAX_CELL_TEMP_C      50      /* BQ stops balancing above this  */

/* ==========================================================================
 * 7. CELL BALANCING (host controlled, external NPN + 18 R / 5 W bleeders)
 *    Adjacent cells cannot bleed together with the external NPN circuit,
 *    so odd and even cells take turns.
 * ========================================================================== */
#define BMS_BAL_MIN_CELL_MV         3850    /* only bleed cells above this    */
#define BMS_BAL_START_DELTA_MV      25      /* start when > min + 25 mV       */
#define BMS_BAL_STOP_DELTA_MV       10      /* stop when < min + 10 mV        */
#define BMS_BAL_MAX_TEMP_DC         450
#define BMS_BAL_MIN_CURRENT_MA      (-1000L)/* no balancing while discharging */
#define BMS_BAL_GROUP_SWAP_MS       30000u  /* odd/even alternation           */
#define BMS_BAL_REFRESH_MS          10000u  /* < BQ Cell Balance Interval 20 s */
#define BMS_BAL_MAX_CELLS           8u

/* ==========================================================================
 * 8. STATE OF CHARGE
 * ========================================================================== */
#define BMS_SOC_REST_CURRENT_MA     500L
#define BMS_SOC_REST_TIME_MS        (30UL * 60UL * 1000UL)
#define BMS_SOC_OCV_BLEND           0.20f   /* weight of OCV at each re-anchor */
#define BMS_SOC_LOW_WARN_PCT        10.0f

/* ==========================================================================
 * 9. FAULT DEBOUNCE (in 100 ms measurement cycles)
 * ========================================================================== */
#define BMS_DEBOUNCE_FAST           3u      /* 300 ms: voltage, temperature   */
#define BMS_DEBOUNCE_SLOW           10u     /* 1 s: current, plausibility     */

/* ==========================================================================
 * 10. TIMING (ms)
 * ========================================================================== */
#define BMS_TASK_MEASURE_MS         100u
#define BMS_TASK_BALANCE_MS         1000u
#define BMS_TASK_DISPLAY_MS         250u
#define BMS_DISPLAY_PAGE_MS         3000u
#define BMS_TASK_LOG_MS             1000u
#define BMS_BQ_COMM_TIMEOUT_MS      500u    /* no good read -> BQ_COMM fault  */
#define BMS_BQ_STARTUP_TIMEOUT_MS   5000u   /* give up waiting in INIT        */
#define BMS_BQ_RETRY_MS             2000u   /* re-probe / re-configure period */
#define BMS_CTRL_TIMEOUT_MS         CAN_TIMEOUT_CTRL_MS

/* ==========================================================================
 * 11. BOARD HARDWARE (Master Monitoring Unit PCB, STM32F103C8T6)
 * ========================================================================== */
#define BQ_I2C_ADDR_8BIT            0x10u   /* 7-bit 0x08 (factory default)   */
#define BQ_I2C_SCL_PORT             GPIOB
#define BQ_I2C_SCL_PIN              GPIO_PIN_6
#define BQ_I2C_SDA_PORT             GPIOB
#define BQ_I2C_SDA_PIN              GPIO_PIN_7
#define BQ_RST_SHUT_PULSE_MS        50u     /* < 1 s: wake / reset, never shutdown */

/* ==========================================================================
 * 12. FEATURE SWITCHES
 * ========================================================================== */
#define BMS_USE_OLED                1
#define BMS_USE_CONSOLE             1       /* USART1 115200 8N1 CLI          */
#define BMS_USE_IWDG                1       /* set 0 only while debugging     */

#endif /* BMS_CONFIG_H */
