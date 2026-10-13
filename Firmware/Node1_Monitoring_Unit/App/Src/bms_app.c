/**
 ******************************************************************************
 * @file    bms_app.c
 * @brief   Node 1 application: cooperative scheduler, BQ76952 bring-up and
 *          supervision, measurement processing and the BMS state machine.
 *
 *  Task             Period   Work
 *  measure          100 ms   BQ block reads, protections, SoC, limits, state
 *  CAN fast         100 ms   STATUS PACK LIMITS CELL_STATS CELLS x4
 *  CAN slow         500 ms   TEMPS BALANCE
 *  1 s              1000 ms  coulomb counter, balancing, ENERGY INFO BQ_STATUS
 *  display          250 ms   OLED page
 *  ALERT interrupt  event    forces an immediate measure pass
 ******************************************************************************
 */
#include "bms_app.h"
#include "bms_protection.h"
#include "bms_soc.h"
#include "bms_balancing.h"
#include "bms_can.h"
#include "bms_display.h"
#include "bms_console.h"
#include "main.h"
#include <string.h>

BmsContext_t g_bms;

/* ==========================================================================
 * BQ76952 configuration table (written and verified at every bring-up)
 * ========================================================================== */
#define MV_TO_COV_CUV(mv)   ((uint32_t)(((mv) * 10 + 253) / 506))          /* 50.6 mV */
#define AMP_TO_2MV(a)       ((uint32_t)((float)(a) * BMS_SHUNT_MOHM / 2.0f)) /* 2 mV    */
#define I8(v)               ((uint32_t)(uint8_t)(int8_t)(v))

static BQ_ConfigEntry_t s_bq_cfg[] = {
    { BQ_DM_POWER_CONFIG,       BQ_POWER_CONFIG_NO_SLEEP,   2, "Power Config"    },
    { BQ_DM_REG12_CONFIG,       0x00,                       1, "REG12 Config"    },
    { BQ_DM_CFETOFF_PIN_CONFIG, BQ_PIN_UNUSED,              1, "CFETOFF Pin"     },
    { BQ_DM_DFETOFF_PIN_CONFIG, BQ_PIN_UNUSED,              1, "DFETOFF Pin"     },
    { BQ_DM_ALERT_PIN_CONFIG,   BQ_PIN_ALERT_OD_ACTIVE_LOW, 1, "ALERT Pin"       },
    { BQ_DM_TS1_CONFIG,         BQ_PIN_THERMISTOR_CELL,     1, "TS1 (NTC1)"      },
    { BQ_DM_TS2_CONFIG,         BQ_PIN_UNUSED,              1, "TS2 (n.c.)"      },
    { BQ_DM_TS3_CONFIG,         BQ_PIN_THERMISTOR_CELL,     1, "TS3 (NTC2)"      },
    { BQ_DM_HDQ_PIN_CONFIG,     BQ_PIN_UNUSED,              1, "HDQ Pin"         },
    { BQ_DM_DCHG_PIN_CONFIG,    BQ_PIN_THERMISTOR_CELL,     1, "DCHG (NTC3)"     },
    { BQ_DM_DDSG_PIN_CONFIG,    BQ_PIN_THERMISTOR_CELL,     1, "DDSG (NTC4)"     },
    { BQ_DM_DA_CONFIGURATION,   BQ_DA_CONFIG_CV_CA,         1, "DA Config"       },
    { BQ_DM_VCELL_MODE,         0xFFFF,                     2, "Vcell Mode"      },
    { BQ_DM_CC_GAIN,            0,                          4, "CC Gain"         },
    { BQ_DM_CAPACITY_GAIN,      0,                          4, "Capacity Gain"   },
    { BQ_DM_ENABLED_PROT_A,     BQ_PROT_A_SCD | BQ_PROT_A_OCD2 | BQ_PROT_A_OCD1 |
                                BQ_PROT_A_OCC | BQ_PROT_A_COV  | BQ_PROT_A_CUV,
                                                            1, "Enabled Prot A"  },
    { BQ_DM_ENABLED_PROT_B,     BQ_PROT_B_OTINT | BQ_PROT_B_OTD | BQ_PROT_B_OTC |
                                BQ_PROT_B_UTINT | BQ_PROT_B_UTD | BQ_PROT_B_UTC,
                                                            1, "Enabled Prot B"  },
    { BQ_DM_ENABLED_PROT_C,     0x00,                       1, "Enabled Prot C"  },
    { BQ_DM_COV_THRESHOLD,      MV_TO_COV_CUV(BQ_COV_MV),   1, "COV Threshold"   },
    { BQ_DM_CUV_THRESHOLD,      MV_TO_COV_CUV(BQ_CUV_MV),   1, "CUV Threshold"   },
    { BQ_DM_OCC_THRESHOLD,      AMP_TO_2MV(BQ_OCC_A),       1, "OCC Threshold"   },
    { BQ_DM_OCC_DELAY,          127,                        1, "OCC Delay"       },
    { BQ_DM_OCD1_THRESHOLD,     AMP_TO_2MV(BQ_OCD1_A),      1, "OCD1 Threshold"  },
    { BQ_DM_OCD1_DELAY,         127,                        1, "OCD1 Delay"      },
    { BQ_DM_OCD2_THRESHOLD,     AMP_TO_2MV(BQ_OCD2_A),      1, "OCD2 Threshold"  },
    { BQ_DM_OCD2_DELAY,         3,                          1, "OCD2 Delay"      },
    { BQ_DM_SCD_THRESHOLD,      BQ_SCD_CODE,                1, "SCD Threshold"   },
    { BQ_DM_OTC_THRESHOLD,      I8(BQ_OTC_C),               1, "OTC Threshold"   },
    { BQ_DM_OTD_THRESHOLD,      I8(BQ_OTD_C),               1, "OTD Threshold"   },
    { BQ_DM_UTC_THRESHOLD,      I8(BQ_UTC_C),               1, "UTC Threshold"   },
    { BQ_DM_UTD_THRESHOLD,      I8(BQ_UTD_C),               1, "UTD Threshold"   },
    { BQ_DM_DEFAULT_ALARM_MASK, BQ_ALARM_SAFETY_MASK,       2, "Alarm Mask"      },
    { BQ_DM_BALANCING_CONFIG,   0x00,                       1, "Balancing Config"},
    { BQ_DM_BAL_MAX_CELL_TEMP,  I8(BQ_BAL_MAX_CELL_TEMP_C), 1, "Bal Max Temp"    },
};
#define BQ_CFG_COUNT (sizeof(s_bq_cfg) / sizeof(s_bq_cfg[0]))

/* ==========================================================================
 * Module state
 * ========================================================================== */
static IWDG_HandleTypeDef *s_iwdg;
static volatile bool s_alert_irq;
static bool     s_reconfig_request;
static bool     s_bq_probe_ok;
static uint32_t s_boot_ms;
static uint32_t s_last_bq_attempt_ms;
static uint32_t s_last_task_ms;
static uint32_t s_prev_faults;
static BmsState_t s_prev_state;
static uint32_t s_prev_i2c_errors;
static uint32_t s_can_error_until_ms;
static uint32_t t_meas, t_fast, t_slow, t_1s, t_disp, t_led;

static void kick_watchdog(void)
{
    if (s_iwdg != NULL && s_iwdg->Instance == IWDG) (void)HAL_IWDG_Refresh(s_iwdg);
}

static uint32_t f32_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* ==========================================================================
 * BQ76952 bring-up and supervision
 * ========================================================================== */

/* Prove the ALERT line works: enable the FULLSCAN alarm for a moment, the
 * BQ must pull PB5 low within one measurement loop (~200 ms). */
static bool alert_self_test(void)
{
    bool     seen = false;
    uint32_t t0;

    (void)BQ_ClearAlarms(0xFFFFu);
    HAL_Delay(2);
    s_alert_irq = false;
    if (BQ_SetAlarmEnable(BQ_ALARM_SAFETY_MASK | BQ_ALARM_FULLSCAN) != BQ_OK) return false;

    t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 600u) {
        if (s_alert_irq ||
            HAL_GPIO_ReadPin(BQ_ALERT_GPIO_Port, BQ_ALERT_Pin) == GPIO_PIN_RESET) {
            seen = true;
            break;
        }
        kick_watchdog();
    }

    (void)BQ_SetAlarmEnable(BQ_ALARM_SAFETY_MASK);
    (void)BQ_ClearAlarms(0xFFFFu);
    HAL_Delay(2);
    s_alert_irq = false;

    /* The line must be released again after clearing */
    return seen && HAL_GPIO_ReadPin(BQ_ALERT_GPIO_Port, BQ_ALERT_Pin) == GPIO_PIN_SET;
}

static bool bq_bring_up(bool wake)
{
    BQ_Result_t r;
    size_t bad = 0;

    g_bms.bq_ready = false;
    kick_watchdog();

    if (wake) {
        BQ_WakePulse(BQ_RST_SHUT_PULSE_MS);
        HAL_Delay(250);                     /* device boot + first scan */
        kick_watchdog();
    }

    r = BQ_Probe(&g_bms.bq_device, &g_bms.bq_fw);
    if (r != BQ_OK) {
        s_bq_probe_ok = false;
        Console_Printf("BQ76952 probe failed: %s (dev 0x%04X)\r\n", BQ_ResultName(r), g_bms.bq_device);
        return false;
    }
    s_bq_probe_ok = true;

    r = BQ_WriteConfig(s_bq_cfg, BQ_CFG_COUNT, &bad);
    kick_watchdog();
    if (r != BQ_OK) {
        Console_Printf("BQ76952 config failed at '%s': %s\r\n",
                       (bad < BQ_CFG_COUNT) ? s_bq_cfg[bad].name : "?", BQ_ResultName(r));
        return false;
    }

    (void)BQ_Subcommand(BQ_SUB_SLEEP_DISABLE);
    (void)BQ_SetBalancing(0);
    g_bms.alert_pin_ok = alert_self_test();
    g_bms.bq_ready = true;
    g_bms.last_good_meas_ms = HAL_GetTick();

    Console_Printf("BQ76952 ready: fw 0x%04X, %u settings verified, ALERT line %s\r\n",
                   g_bms.bq_fw, (unsigned)BQ_CFG_COUNT, g_bms.alert_pin_ok ? "OK" : "NOT RESPONDING");
    return true;
}

static void invalidate_measurement(void)
{
    g_bms.m.valid      = false;
    g_bms.chg_limit_ma = 0;
    g_bms.dsg_limit_ma = 0;
    g_bms.limit_flags  = 0;
}

static void supervise_bq(uint32_t now)
{
    if (s_reconfig_request) {
        s_reconfig_request = false;
        Bal_Stop(&g_bms);
        g_bms.bq_ready = false;
        s_last_bq_attempt_ms = now - BMS_BQ_RETRY_MS;
    }
    if (g_bms.bq_ready) return;
    if ((now - s_last_bq_attempt_ms) < BMS_BQ_RETRY_MS) return;

    s_last_bq_attempt_ms = now;
    invalidate_measurement();
    if (bq_bring_up(true)) return;

    if ((now - s_boot_ms) > BMS_BQ_STARTUP_TIMEOUT_MS) {
        g_bms.faults |= s_bq_probe_ok ? BMS_FLT_BQ_CONFIG : BMS_FLT_BQ_COMM;
    }
}

/* ==========================================================================
 * Measurement
 * ========================================================================== */

static void process_raw(const BQ_Measurement_t *r, uint32_t now)
{
    BmsMeasurement_t *m = &g_bms.m;
    const int16_t ntc[BMS_NTC_COUNT] = { r->ts1_dc, r->ts3_dc, r->dchg_dc, r->ddsg_dc };
    uint32_t sum = 0;
    uint16_t mn = 0xFFFFu, mx = 0;
    int16_t  tmin = 32767, tmax = -32768;
    int32_t  i;

    for (uint8_t c = 0; c < BMS_CELLS; c++) {
        uint16_t v = (r->cell_mv[c] < 0) ? 0u : (uint16_t)r->cell_mv[c];
        m->cell_mv[c] = v;
        sum += v;
        if (v < mn) { mn = v; m->cell_min_idx = c; }
        if (v > mx) { mx = v; m->cell_max_idx = c; }
    }
    m->cell_sum_mv   = sum;
    m->cell_min_mv   = mn;
    m->cell_max_mv   = mx;
    m->cell_avg_mv   = (uint16_t)(sum / BMS_CELLS);
    m->cell_delta_mv = (uint16_t)(mx - mn);
    m->stack_mv      = r->stack_mv;

    i = r->current_ma * BMS_CURRENT_SIGN;
    if (i > -BMS_CURRENT_DEADBAND_MA && i < BMS_CURRENT_DEADBAND_MA) i = 0;
    m->current_ma = i;

    m->die_dc = r->die_dc;
    for (uint8_t n = 0; n < BMS_NTC_COUNT; n++) {
        m->ntc_dc[n] = ntc[n];
        if ((BMS_NTC_FITTED_MASK & (1u << n)) == 0u) continue;
        if (ntc[n] < BMS_TEMP_PLAUSIBLE_MIN_DC || ntc[n] > BMS_TEMP_PLAUSIBLE_MAX_DC) continue;
        if (ntc[n] < tmin) tmin = ntc[n];
        if (ntc[n] > tmax) tmax = ntc[n];
    }
    if (tmax < tmin) { tmin = r->die_dc; tmax = r->die_dc; }   /* no usable NTC */
    m->temp_min_dc  = tmin;
    m->temp_max_dc  = tmax;
    m->valid        = true;
    m->timestamp_ms = now;
}

static void update_state(void)
{
    BmsState_t s = g_bms.state;
    int32_t    i = g_bms.m.current_ma;

    if (g_bms.faults != 0u)                       s = BMS_STATE_FAULT;
    else if (!g_bms.bq_ready || !g_bms.m.valid)   s = BMS_STATE_INIT;
    else if (i >  BMS_STATE_CURRENT_MA)           s = BMS_STATE_CHARGING;
    else if (i < -BMS_STATE_CURRENT_MA)           s = BMS_STATE_DISCHARGING;
    else if (i > -BMS_STATE_CURRENT_MA / 2 && i < BMS_STATE_CURRENT_MA / 2) s = BMS_STATE_IDLE;
    else if (s == BMS_STATE_INIT || s == BMS_STATE_FAULT) s = BMS_STATE_IDLE;

    g_bms.state = s;
}

static void task_measure(uint32_t now)
{
    BQ_Measurement_t raw;
    bool ok = false;

    s_last_task_ms = now;

    if (g_bms.bq_ready) {
        ok = (BQ_ReadMeasurements(&raw) == BQ_OK) && (BQ_ReadSafety(&g_bms.bq) == BQ_OK);
    }

    if (ok) {
        process_raw(&raw, now);
        g_bms.last_good_meas_ms = now;

        /* RAM configuration is lost after a full BQ reset -> write it again */
        if (g_bms.bq.battery_status & BQ_BAT_POR) {
            Console_Printf("BQ76952 reset detected - re-configuring\r\n");
            s_reconfig_request = true;
        }
        /* Release the ALERT line; the causes stay visible in Safety Status */
        if (g_bms.bq.alarm_status != 0u) {
            g_bms.bq_alarm_latched |= g_bms.bq.alarm_status;
            (void)BQ_ClearAlarms(g_bms.bq.alarm_status);
        }

        Prot_Evaluate(&g_bms);
        Soc_Update(&g_bms);
        Prot_UpdateLimits(&g_bms);
    } else if (g_bms.bq_ready && (now - g_bms.last_good_meas_ms) > BMS_BQ_COMM_TIMEOUT_MS) {
        Console_Printf("BQ76952 communication lost\r\n");
        g_bms.faults  |= BMS_FLT_BQ_COMM;
        g_bms.bq_ready = false;
        s_last_bq_attempt_ms = now;
        invalidate_measurement();
    }

    update_state();
}

/* ==========================================================================
 * 1 s housekeeping
 * ========================================================================== */

static void set_warning(uint16_t bit, bool on)
{
    if (on) g_bms.warnings |= bit; else g_bms.warnings &= (uint16_t)~bit;
}

static void task_1s(uint32_t now)
{
    int64_t  q = 0;
    uint32_t errs = BQ_ErrorCount();

    g_bms.uptime_s = now / 1000u;

    if (g_bms.bq_ready) {
        bool ok = BQ_ReadPassedCharge(&q, NULL) == BQ_OK;
        Soc_OnPassedCharge(&g_bms, ok, q);
        Bal_Update(&g_bms);
    }

    if (BmsCan_TakeErrorFlag()) s_can_error_until_ms = now + 10000u;

    set_warning(BMS_WRN_CTRL_LOST, (now - g_bms.last_ctrl_rx_ms) > BMS_CTRL_TIMEOUT_MS);
    set_warning(BMS_WRN_BALANCING, g_bms.bal_mask != 0u);
    set_warning(BMS_WRN_ALERT_PIN, g_bms.bq_ready && !g_bms.alert_pin_ok);
    set_warning(BMS_WRN_CAN_ERROR, (int32_t)(s_can_error_until_ms - now) > 0);
    set_warning(BMS_WRN_I2C_RETRY, errs != s_prev_i2c_errors);
    s_prev_i2c_errors = errs;
}

static void task_leds(uint32_t now)
{
    uint32_t period = (g_bms.state == BMS_STATE_INIT) ? 100u : 500u;
    bool red;

    if ((now - t_led) >= period) {
        t_led = now;
        HAL_GPIO_TogglePin(LED_MCU_GPIO_Port, LED_MCU_Pin);          /* heartbeat */
    }

    /* Red: solid = latched fault, 1 Hz blink = warning (active low LED) */
    if (g_bms.faults != 0u) red = true;
    else if ((g_bms.warnings & (uint16_t)~(BMS_WRN_BALANCING | BMS_WRN_CTRL_LOST)) != 0u) red = ((now / 500u) & 1u) != 0u;
    else red = false;
    HAL_GPIO_WritePin(LED_ERROR_GPIO_Port, LED_ERROR_Pin, red ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ==========================================================================
 * Public
 * ========================================================================== */

void BMS_App_Init(I2C_HandleTypeDef *hi2c, CAN_HandleTypeDef *hcan,
                  SPI_HandleTypeDef *hspi, UART_HandleTypeDef *huart,
                  IWDG_HandleTypeDef *hiwdg)
{
    float cc_gain = 7.4768f / BMS_SHUNT_MOHM;               /* TRM 13.2.2.1 */

    memset(&g_bms, 0, sizeof(g_bms));
    g_bms.state         = BMS_STATE_INIT;
    g_bms.bal_enabled   = true;
    g_bms.bal_odd_group = true;
    s_iwdg = hiwdg;

    /* Reset cause, then clear the flags for next time */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))  g_bms.reset_cause |= BMS_RST_POWER_ON;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))  g_bms.reset_cause |= BMS_RST_PIN;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) g_bms.reset_cause |= BMS_RST_WATCHDOG;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  g_bms.reset_cause |= BMS_RST_SOFTWARE;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    if (g_bms.reset_cause & BMS_RST_WATCHDOG) g_bms.warnings |= BMS_WRN_WDG_RESET;

    Console_Init(huart);
    Console_Printf("\r\n=== Distributed BMS | Node 1 Master Monitoring Unit | fw %u.%u ===\r\n",
                   BMS_FW_VERSION_MAJOR, BMS_FW_VERSION_MINOR);
    Console_Printf("reset cause 0x%02X, type 'help' for commands\r\n", g_bms.reset_cause);

#if (BMS_USE_OLED == 1)
    Display_Init(hspi);
    Display_Splash("Master Monitoring", "Starting BQ76952...");
#else
    (void)hspi;
#endif

    for (size_t k = 0; k < BQ_CFG_COUNT; k++) {
        if (s_bq_cfg[k].addr == BQ_DM_CC_GAIN)       s_bq_cfg[k].value = f32_bits(cc_gain);
        if (s_bq_cfg[k].addr == BQ_DM_CAPACITY_GAIN) s_bq_cfg[k].value = f32_bits(cc_gain * 298261.6178f);
    }

    Soc_Init(&g_bms);
    Bal_Init();
    Prot_ResetDebounce();
    BQ_Init(hi2c, BQ_RST_SHUT_GPIO_Port, BQ_RST_SHUT_Pin);

    if (BmsCan_Init(hcan) != HAL_OK) Console_Printf("CAN start FAILED\r\n");
    else Console_Printf("CAN 500 kbit/s running\r\n");

    s_boot_ms = HAL_GetTick();
    s_last_bq_attempt_ms = s_boot_ms;
    (void)bq_bring_up(true);

    g_bms.last_ctrl_rx_ms = HAL_GetTick();
    s_last_task_ms = HAL_GetTick();
    s_prev_state = g_bms.state;
}

void BMS_App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    if (s_alert_irq) {                          /* safety event: read now */
        s_alert_irq = false;
        t_meas = now - BMS_TASK_MEASURE_MS;
    }

    supervise_bq(now);
    now = HAL_GetTick();

    if ((now - t_meas) >= BMS_TASK_MEASURE_MS) {
        t_meas = now;
        task_measure(now);
    }

    if (g_bms.faults != s_prev_faults || g_bms.state != s_prev_state) {
        uint32_t new_faults = g_bms.faults & ~s_prev_faults;
        if (new_faults != 0u) {
            Bal_Stop(&g_bms);
            Console_Printf("FAULT latched: %s (0x%06lX)\r\n", BMS_App_FaultName(new_faults),
                           (unsigned long)g_bms.faults);
        }
        s_prev_faults = g_bms.faults;
        s_prev_state  = g_bms.state;
        BmsCan_SendStatusNow(&g_bms);
    }

    if ((now - t_fast) >= CAN_PERIOD_FAST_MS) { t_fast = now; BmsCan_SendFast(&g_bms); }
    if ((now - t_slow) >= CAN_PERIOD_SLOW_MS) { t_slow = now; BmsCan_SendSlow(&g_bms); }
    if ((now - t_1s)   >= CAN_PERIOD_INFO_MS) {
        t_1s = now;
        task_1s(now);
        BmsCan_SendInfo(&g_bms);
        Console_Log(&g_bms);
    }

#if (BMS_USE_OLED == 1)
    if ((now - t_disp) >= BMS_TASK_DISPLAY_MS) { t_disp = now; Display_Update(&g_bms); }
#endif

    task_leds(now);
    BmsCan_Process();
    Console_Process();

    /* Refresh only while the measurement task is alive. A stuck loop resets
     * the MCU; the controller then sees the heartbeat stop and opens the
     * contactors. */
    if ((HAL_GetTick() - s_last_task_ms) < 500u) kick_watchdog();
}

void BMS_App_OnAlertIrq(void)
{
    s_alert_irq = true;
}

bool BMS_App_ClearFaults(void)
{
    uint32_t still = Prot_ActiveConditions(&g_bms);

    if (!g_bms.bq_ready) still |= s_bq_probe_ok ? BMS_FLT_BQ_CONFIG : BMS_FLT_BQ_COMM;
    if ((HAL_GetTick() - g_bms.last_good_meas_ms) > BMS_BQ_COMM_TIMEOUT_MS) still |= BMS_FLT_BQ_COMM;

    g_bms.faults = still;
    g_bms.warnings &= (uint16_t)~BMS_WRN_WDG_RESET;
    g_bms.bq_alarm_latched = 0;
    Prot_ResetDebounce();
    update_state();

    Console_Printf("clear request: %s\r\n", still ? "refused, condition still present" : "all faults cleared");
    return still == 0u;
}

void BMS_App_SetBalancing(bool enable)
{
    g_bms.bal_enabled = enable;
    if (!enable) Bal_Stop(&g_bms);
}

void BMS_App_NoteControllerHeartbeat(void)
{
    g_bms.last_ctrl_rx_ms = HAL_GetTick();
}

void BMS_App_RequestReconfigure(void)
{
    s_reconfig_request = true;
}

const char *BMS_App_StateName(BmsState_t s)
{
    switch (s) {
        case BMS_STATE_INIT:        return "INIT";
        case BMS_STATE_IDLE:        return "IDLE";
        case BMS_STATE_CHARGING:    return "CHARGE";
        case BMS_STATE_DISCHARGING: return "DISCHG";
        default:                    return "FAULT";
    }
}

const char *BMS_App_FaultName(uint32_t fault_bits)
{
    static const char *const names[] = {
        "BQ_COMM", "BQ_CONFIG", "CELL_OV", "CELL_UV", "OVER_TEMP", "UNDER_TEMP",
        "OC_CHARGE", "OC_DISCHG", "SHORT_CIRC", "IMBALANCE", "CELL_SENSE",
        "NTC_SENSE", "STACK_MISMATCH", "BQ_PERM_FAIL"
    };
    for (uint8_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (fault_bits & (1UL << i)) return names[i];
    }
    return fault_bits ? "UNKNOWN" : "";
}
