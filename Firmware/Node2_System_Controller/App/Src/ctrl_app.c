/**
 ******************************************************************************
 * @file    ctrl_app.c
 * @brief   Node 2 application (see header).
 *
 *  STANDBY --key ON (armed) + BMS OK--> PRECHARGE --current decayed--> RUN
 *     ^                                     |                          |
 *     |<-------- key OFF / stop -----------+------ STOPPING <---------+
 *  any state --fault--> FAULT (all relays open) --key OFF + fault gone--> STANDBY
 *
 *  Contactor order: close Main-, then Precharge, then Main+, then open
 *  Precharge. Opening: Charge-enable first, Main+ when current is low,
 *  Main- last. On a fault everything opens at once.
 *  After power-up, a fault or a stop, the key must be turned OFF before a
 *  new start is accepted ("armed" flag).
 ******************************************************************************
 */
#include "ctrl_app.h"
#include "ctrl_can.h"
#include "ctrl_io.h"
#include "ctrl_console.h"
#include "main.h"
#include <string.h>

CtrlContext_t g_ctrl;

static IWDG_HandleTypeDef *s_iwdg;
static uint8_t  s_latched;              /* PRECHARGE / LIMIT_VIOLATION      */
static uint32_t s_boot_ms;
static uint32_t s_last_tick_ms;
static uint32_t t_tick, t_fast, t_1s;
static bool     s_key_prev;
static uint32_t s_pre_count;
static bool     s_chg_ok_timing;
static uint32_t s_chg_ok_since_ms;
static bool     s_limit_timing;
static uint32_t s_limit_since_ms;
static bool     s_fan_on;
static uint32_t s_fan_on_since_ms;
static CtrlState_t s_prev_state;
static uint8_t  s_prev_faults;

#define OUT_RELAYS  (CTRL_OUT_MAIN_NEG | CTRL_OUT_PRECHARGE | CTRL_OUT_MAIN_POS | CTRL_OUT_CHARGE_EN)

static int32_t iabs32(int32_t v) { return (v < 0) ? -v : v; }

static void kick_watchdog(void)
{
    if (s_iwdg != NULL && s_iwdg->Instance == IWDG) (void)HAL_IWDG_Refresh(s_iwdg);
}

static void enter(CtrlState_t s, uint32_t now)
{
    if (s == g_ctrl.state) return;
    Console_Printf("state %s -> %s\r\n", Ctrl_App_StateName(g_ctrl.state), Ctrl_App_StateName(s));
    g_ctrl.state = s;
    g_ctrl.state_entry_ms = now;
    g_ctrl.step = 0;
    g_ctrl.step_ms = now;
    if (s == CTRL_STATE_FAULT || s == CTRL_STATE_PRECHARGE) g_ctrl.armed = false;
}

static void next_step(uint32_t now)
{
    g_ctrl.step++;
    g_ctrl.step_ms = now;
}

/* ==========================================================================
 * Supervision
 * ========================================================================== */

/** Fault conditions that are present right now (not latched). */
static uint8_t active_conditions(uint32_t now)
{
    const BmsView_t *b = &g_ctrl.bms;
    uint8_t f = 0;

    if (!b->online && (now - s_boot_ms) >= CTRL_STARTUP_WAIT_MS)       f |= CTRL_FLT_BMS_TIMEOUT;
    if (b->online && (b->state == BMS_STATE_FAULT || b->faults != 0u)) f |= CTRL_FLT_BMS_FAULT;
    if (b->e2e_errors >= CTRL_E2E_MAX_ERRORS)                          f |= CTRL_FLT_BMS_DATA;
    if (!Io_EstopOk())                                                 f |= CTRL_FLT_ESTOP;
    if (CtrlCan_IsBusOff())                                            f |= CTRL_FLT_CAN_BUSOFF;
    return f;
}

static bool start_permitted(void)
{
    const BmsView_t *b = &g_ctrl.bms;
    return g_ctrl.faults == 0u && b->online &&
           (b->state == BMS_STATE_IDLE || b->state == BMS_STATE_CHARGING ||
            b->state == BMS_STATE_DISCHARGING) &&
           (b->limit_flags & (BMS_LIM_CHARGE_ALLOWED | BMS_LIM_DISCHARGE_ALLOWED)) != 0u;
}

/** Charge-enable relay: BMS must allow charging continuously for a while. */
static bool charge_enable(uint32_t now)
{
    if ((g_ctrl.bms.limit_flags & BMS_LIM_CHARGE_ALLOWED) == 0u) {
        s_chg_ok_timing = false;
        return false;
    }
    if (!s_chg_ok_timing) { s_chg_ok_timing = true; s_chg_ok_since_ms = now; }
    return (now - s_chg_ok_since_ms) >= CTRL_CHARGE_ENABLE_DELAY_MS;
}

/** Trip if the pack current stays above the BMS limit + margin. */
static void check_limits(uint32_t now)
{
    const BmsView_t *b = &g_ctrl.bms;
    int32_t lim    = (b->current_ma >= 0) ? b->chg_limit_ma : b->dsg_limit_ma;
    int32_t margin = lim * CTRL_LIMIT_MARGIN_PCT / 100;
    if (margin < CTRL_LIMIT_MARGIN_MIN_MA) margin = CTRL_LIMIT_MARGIN_MIN_MA;

    if (iabs32(b->current_ma) > lim + margin) {
        if (!s_limit_timing) { s_limit_timing = true; s_limit_since_ms = now; }
        else if ((now - s_limit_since_ms) >= CTRL_LIMIT_VIOLATION_MS) {
            s_latched |= CTRL_FLT_LIMIT_VIOLATION;
            Console_Printf("current %ld mA beyond BMS limit %ld mA\r\n", (long)b->current_ma, (long)lim);
        }
    } else {
        s_limit_timing = false;
    }
}

/* ==========================================================================
 * State machine (every CTRL_TICK_MS)
 * ========================================================================== */

static void state_machine(uint32_t now)
{
    const BmsView_t *b = &g_ctrl.bms;
    bool     key     = Io_KeyOn();
    bool     key_off_edge = s_key_prev && !key;
    uint32_t in_state = now - g_ctrl.state_entry_ms;
    uint32_t in_step  = now - g_ctrl.step_ms;
    uint8_t  out      = g_ctrl.outputs & OUT_RELAYS;

    s_key_prev = key;
    if (!key) {                                 /* key OFF re-arms and ends a remote stop */
        g_ctrl.armed = true;
        g_ctrl.remote_stop = false;
    }

    if (g_ctrl.faults != 0u && g_ctrl.state != CTRL_STATE_FAULT && g_ctrl.state != CTRL_STATE_INIT) {
        enter(CTRL_STATE_FAULT, now);
    }

    switch (g_ctrl.state) {
    case CTRL_STATE_INIT:
        out = 0;
        if (b->online || in_state >= CTRL_STARTUP_WAIT_MS) {
            enter(g_ctrl.faults ? CTRL_STATE_FAULT : CTRL_STATE_STANDBY, now);
        }
        break;

    case CTRL_STATE_STANDBY:
        out = 0;
        if (key && g_ctrl.armed && !g_ctrl.remote_stop && start_permitted()) {
            enter(CTRL_STATE_PRECHARGE, now);
        }
        break;

    case CTRL_STATE_PRECHARGE:
        if (!key || g_ctrl.remote_stop) {       /* abort: only the small precharge current flows */
            out = 0;
            enter(CTRL_STATE_STANDBY, now);
            break;
        }
        if (g_ctrl.step == 0u) {                /* 1. Main- */
            out = CTRL_OUT_MAIN_NEG;
            if (in_step >= CTRL_RELAY_STEP_MS) { s_pre_count = b->pack_rx_count; next_step(now); }
        } else if (g_ctrl.step == 1u) {         /* 2. Precharge, wait for the current to decay */
            out = CTRL_OUT_MAIN_NEG | CTRL_OUT_PRECHARGE;
            if (iabs32(b->current_ma) > CTRL_PRECHARGE_MAX_MA) {
                s_latched |= CTRL_FLT_PRECHARGE;
                Console_Printf("precharge over-current %ld mA\r\n", (long)b->current_ma);
            } else if (in_step >= CTRL_PRECHARGE_MIN_MS &&
                       (b->pack_rx_count - s_pre_count) >= 3u &&       /* fresh readings */
                       iabs32(b->current_ma) <= CTRL_PRECHARGE_DONE_MA) {
                Console_Printf("precharge done in %lu ms\r\n", (unsigned long)in_step);
                next_step(now);
            } else if (in_step >= CTRL_PRECHARGE_TIMEOUT_MS) {
                s_latched |= CTRL_FLT_PRECHARGE;
                Console_Printf("precharge timeout, current still %ld mA\r\n", (long)b->current_ma);
            }
        } else {                                /* 3. Main+, then drop Precharge */
            out = CTRL_OUT_MAIN_NEG | CTRL_OUT_PRECHARGE | CTRL_OUT_MAIN_POS;
            if (in_step >= CTRL_RELAY_STEP_MS) {
                out = CTRL_OUT_MAIN_NEG | CTRL_OUT_MAIN_POS;
                s_limit_timing = false;
                enter(CTRL_STATE_RUN, now);
            }
        }
        break;

    case CTRL_STATE_RUN:
        out = CTRL_OUT_MAIN_NEG | CTRL_OUT_MAIN_POS;
        if (charge_enable(now)) out |= CTRL_OUT_CHARGE_EN;
        check_limits(now);
        if (!key || g_ctrl.remote_stop) enter(CTRL_STATE_STOPPING, now);
        break;

    case CTRL_STATE_STOPPING:
        if (g_ctrl.step == 0u) {                /* charge path off, wait for low current */
            out = CTRL_OUT_MAIN_NEG | CTRL_OUT_MAIN_POS;
            if (iabs32(b->current_ma) < CTRL_STOP_CURRENT_MA || in_step >= CTRL_STOP_TIMEOUT_MS) next_step(now);
        } else {                                /* Main+ open, then Main- */
            out = CTRL_OUT_MAIN_NEG;
            if (in_step >= CTRL_RELAY_STEP_MS) { out = 0; enter(CTRL_STATE_STANDBY, now); }
        }
        break;

    case CTRL_STATE_FAULT:
    default:
        out = 0;
        /* Turning the key OFF acknowledges the fault: drop our latched
         * faults and ask the BMS to clear its own (it refuses while the
         * condition is still present). */
        if (key_off_edge) {
            s_latched = 0;
            if (b->faults != 0u) CtrlCan_SendBmsCommand(CTRL_CMD_CLEAR_FAULTS, 0);
        }
        if (g_ctrl.faults == 0u && !key) enter(CTRL_STATE_STANDBY, now);
        break;
    }

    /* A fault latched during this tick (precharge, limit) opens everything now */
    if ((s_latched & (uint8_t)~g_ctrl.faults) != 0u) {
        g_ctrl.faults |= s_latched;
        enter(CTRL_STATE_FAULT, now);
        out = 0;
    }

    g_ctrl.outputs = (uint8_t)((g_ctrl.outputs & CTRL_OUT_FAN) | out);
    Io_SetOutputs(g_ctrl.outputs);
}

/* ==========================================================================
 * Fan
 * ========================================================================== */

static void fan_control(uint32_t now)
{
    const BmsView_t *b = &g_ctrl.bms;
    int32_t duty;

    if (g_ctrl.fan_override >= 0 && (int32_t)(g_ctrl.fan_override_until_ms - now) <= 0) {
        g_ctrl.fan_override = -1;
    }

    if (g_ctrl.fan_override >= 0) {
        duty = g_ctrl.fan_override;
    } else if (!b->online) {
        duty = 100;                             /* no temperature data: cool anyway */
    } else {
        int32_t t = b->tmax_c;
        if (t >= CTRL_FAN_START_C)     s_fan_on = true;
        else if (t <= CTRL_FAN_STOP_C) s_fan_on = false;

        if (!s_fan_on)                  duty = 0;
        else if (t >= CTRL_FAN_FULL_C)  duty = 100;
        else if (t <= CTRL_FAN_START_C) duty = CTRL_FAN_MIN_DUTY;
        else duty = CTRL_FAN_MIN_DUTY + (100 - CTRL_FAN_MIN_DUTY) * (t - CTRL_FAN_START_C) /
                                        (CTRL_FAN_FULL_C - CTRL_FAN_START_C);

        if ((b->limit_flags & BMS_LIM_BALANCING_ACTIVE) && duty < CTRL_FAN_BALANCING_DUTY) {
            duty = CTRL_FAN_BALANCING_DUTY;
        }
        if ((b->faults & BMS_FLT_OVER_TEMP) || (b->warnings & BMS_WRN_TEMP_HIGH)) duty = 100;
    }

    if (duty > 0 && g_ctrl.fan_duty == 0u) s_fan_on_since_ms = now;
    g_ctrl.fan_duty = (uint8_t)duty;
    Io_SetFanDuty(g_ctrl.fan_duty);
    if (duty > 0) g_ctrl.outputs |= CTRL_OUT_FAN; else g_ctrl.outputs &= (uint8_t)~CTRL_OUT_FAN;
}

/* ==========================================================================
 * 1 s housekeeping and LEDs
 * ========================================================================== */

static void set_warning(uint8_t bit, bool on)
{
    if (on) g_ctrl.warnings |= bit; else g_ctrl.warnings &= (uint8_t)~bit;
}

static void task_1s(uint32_t now)
{
    const BmsView_t *b = &g_ctrl.bms;
    bool fan_fail = false;

    g_ctrl.uptime_s = now / 1000u;
    Io_Tick1s();
    g_ctrl.fan_rpm = Io_FanRpm();

#if (CTRL_FAN_HAS_TACH == 1)
    fan_fail = g_ctrl.fan_duty >= CTRL_FAN_MIN_DUTY &&
               (now - s_fan_on_since_ms) >= CTRL_FAN_SPINUP_MS &&
               g_ctrl.fan_rpm < CTRL_FAN_FAIL_RPM;
#endif
    set_warning(CTRL_WRN_FAN_FAIL, fan_fail);
    set_warning(CTRL_WRN_GW_OFFLINE, !g_ctrl.gw.online);
    set_warning(CTRL_WRN_CHARGE_BLOCKED, g_ctrl.state == CTRL_STATE_RUN &&
                                         (b->limit_flags & BMS_LIM_CHARGE_ALLOWED) == 0u);
    set_warning(CTRL_WRN_DERATED, (b->warnings & (BMS_WRN_CELL_HIGH | BMS_WRN_CELL_LOW |
                                  BMS_WRN_TEMP_HIGH | BMS_WRN_TEMP_LOW | BMS_WRN_CURRENT_HIGH)) != 0u);
    set_warning(CTRL_WRN_REMOTE_STOP, g_ctrl.remote_stop);
    set_warning(CTRL_WRN_FAN_OVERRIDE, g_ctrl.fan_override >= 0);
}

static void update_leds(uint32_t now)
{
    bool status, fault;

    switch (g_ctrl.state) {
        case CTRL_STATE_RUN:     status = true;                          break;
        case CTRL_STATE_STANDBY: status = ((now / 500u) & 1u) != 0u;     break;
        case CTRL_STATE_FAULT:   status = false;                         break;
        default:                 status = ((now / 100u) & 1u) != 0u;     break;
    }
    if (g_ctrl.state == CTRL_STATE_FAULT) fault = true;
    else fault = (g_ctrl.warnings & (uint8_t)~CTRL_WRN_GW_OFFLINE) != 0u && ((now / 500u) & 1u);
    Io_SetLeds(status, fault);
}

/* ==========================================================================
 * Public
 * ========================================================================== */

void Ctrl_App_Init(CAN_HandleTypeDef *hcan, TIM_HandleTypeDef *htim_fan,
                   UART_HandleTypeDef *huart, IWDG_HandleTypeDef *hiwdg)
{
    memset(&g_ctrl, 0, sizeof(g_ctrl));
    g_ctrl.state = CTRL_STATE_INIT;
    g_ctrl.fan_override = -1;
    s_iwdg = hiwdg;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))  g_ctrl.reset_cause |= BMS_RST_POWER_ON;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))  g_ctrl.reset_cause |= BMS_RST_PIN;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) g_ctrl.reset_cause |= BMS_RST_WATCHDOG;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  g_ctrl.reset_cause |= BMS_RST_SOFTWARE;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    if (g_ctrl.reset_cause & BMS_RST_WATCHDOG) g_ctrl.warnings |= CTRL_WRN_WDG_RESET;

    Io_Init(htim_fan);
    Console_Init(huart);
    Console_Printf("\r\n=== Distributed BMS | Node 2 System Controller | fw %u.%u ===\r\n",
                   CTRL_FW_VERSION_MAJOR, CTRL_FW_VERSION_MINOR);
    Console_Printf("reset cause 0x%02X, relays open, waiting for Node 1\r\n", g_ctrl.reset_cause);

    if (CtrlCan_Init(hcan) != HAL_OK) Console_Printf("CAN start FAILED\r\n");

    s_boot_ms = HAL_GetTick();
    g_ctrl.state_entry_ms = s_boot_ms;
    s_last_tick_ms = s_boot_ms;
    s_prev_state = g_ctrl.state;
}

void Ctrl_App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    CtrlCan_Process();

    if ((now - t_tick) >= CTRL_TICK_MS) {
        t_tick = now;
        s_last_tick_ms = now;
        Io_Sample();
        g_ctrl.inputs = (uint8_t)((Io_KeyOn()        ? CTRL_IN_KEY_ON     : 0u) |
                                  (Io_EstopOk()      ? CTRL_IN_ESTOP_OK   : 0u) |
                                  (g_ctrl.bms.online ? CTRL_IN_BMS_ONLINE : 0u) |
                                  (g_ctrl.gw.online  ? CTRL_IN_GW_ONLINE  : 0u));
        g_ctrl.faults = (uint8_t)(s_latched | active_conditions(now));
        state_machine(now);
    }

    if (g_ctrl.state != s_prev_state || g_ctrl.faults != s_prev_faults) {
        if ((g_ctrl.faults & ~s_prev_faults) != 0u) {
            Console_Printf("FAULT: %s (0x%02X)\r\n",
                           Ctrl_App_FaultName((uint8_t)(g_ctrl.faults & ~s_prev_faults)), g_ctrl.faults);
        }
        s_prev_state  = g_ctrl.state;
        s_prev_faults = g_ctrl.faults;
        CtrlCan_SendStatus(&g_ctrl);            /* report changes at once */
    }

    if ((now - t_fast) >= CAN_PERIOD_FAST_MS) {
        t_fast = now;
        fan_control(now);
        CtrlCan_SendStatus(&g_ctrl);
    }

    if ((now - t_1s) >= CAN_PERIOD_INFO_MS) {
        t_1s = now;
        task_1s(now);
        CtrlCan_SendInfo(&g_ctrl);
        Console_Log(&g_ctrl);
    }

    update_leds(now);
    Console_Process();

    if ((HAL_GetTick() - s_last_tick_ms) < 200u) kick_watchdog();
}

void Ctrl_App_RequestStop(void)
{
    g_ctrl.remote_stop = true;
    Console_Printf("remote stop requested - turn the key OFF to re-arm\r\n");
}

void Ctrl_App_ClearFaults(void)
{
    s_latched = 0;
    g_ctrl.warnings &= (uint8_t)~CTRL_WRN_WDG_RESET;
    CtrlCan_SendBmsCommand(CTRL_CMD_CLEAR_FAULTS, 0);
    Console_Printf("clear faults sent (key must be OFF to leave FAULT)\r\n");
}

void Ctrl_App_SetBalancing(bool allow)
{
    CtrlCan_SendBmsCommand(CTRL_CMD_SET_BALANCING, allow ? 1u : 0u);
    Console_Printf("balancing %s\r\n", allow ? "allowed" : "blocked");
}

void Ctrl_App_FanOverride(int16_t duty)
{
    if (duty < 0) {
        g_ctrl.fan_override = -1;
        Console_Printf("fan: automatic\r\n");
        return;
    }
    if (duty > 100) duty = 100;
    g_ctrl.fan_override = duty;
    g_ctrl.fan_override_until_ms = HAL_GetTick() + CTRL_FAN_OVERRIDE_MAX_MS;
    Console_Printf("fan override %d %% for 10 min\r\n", duty);
}

const char *Ctrl_App_StateName(CtrlState_t s)
{
    switch (s) {
        case CTRL_STATE_INIT:      return "INIT";
        case CTRL_STATE_STANDBY:   return "STANDBY";
        case CTRL_STATE_PRECHARGE: return "PRECHARGE";
        case CTRL_STATE_RUN:       return "RUN";
        case CTRL_STATE_STOPPING:  return "STOPPING";
        default:                   return "FAULT";
    }
}

const char *Ctrl_App_FaultName(uint8_t f)
{
    static const char *const names[] = {
        "BMS_TIMEOUT", "BMS_FAULT", "BMS_DATA", "PRECHARGE", "ESTOP", "LIMIT_VIOLATION", "CAN_BUSOFF"
    };
    for (uint8_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (f & (1u << i)) return names[i];
    }
    return f ? "UNKNOWN" : "";
}
