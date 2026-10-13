/* Node 2 contactor / precharge state machine, simulated against a DC-link
 * plant model. Run with any argument for a verbose transition log. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "ctrl_app.h"
#include "ctrl_can.h"
#include "ctrl_io.h"
#include "ctrl_console.h"

uint32_t g_tick = 0;
uint32_t HAL_GetTick(void) { return g_tick; }
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h) { (void)h; return HAL_OK; }
/* ---- IO stubs ---- */
static bool key, estop_ok = true; static uint8_t relays; static uint8_t fan;
void Io_Init(TIM_HandleTypeDef *h) { (void)h; }
void Io_SetOutputs(uint8_t o) { relays = o & 0x0F; }
void Io_Sample(void) {}
bool Io_KeyOn(void) { return key; }
bool Io_EstopOk(void) { return estop_ok; }
void Io_SetFanDuty(uint8_t p) { fan = p; }
void Io_Tick1s(void) {}
uint16_t Io_FanRpm(void) { return fan ? 1200 : 0; }
void Io_OnTachPulse(void) {}
void Io_SetLeds(bool a, bool b) { (void)a; (void)b; }
/* ---- CAN stubs ---- */
static int bms_clear_cmds, bms_bal_cmds; static bool busoff;
HAL_StatusTypeDef CtrlCan_Init(CAN_HandleTypeDef *h) { (void)h; return HAL_OK; }
void CtrlCan_Process(void) {}
void CtrlCan_SendStatus(const CtrlContext_t *c) { (void)c; }
void CtrlCan_SendInfo(const CtrlContext_t *c) { (void)c; }
void CtrlCan_SendBmsCommand(uint8_t cmd, uint8_t arg) { (void)arg; if (cmd == CTRL_CMD_CLEAR_FAULTS) bms_clear_cmds++; else bms_bal_cmds++; }
bool CtrlCan_IsBusOff(void) { return busoff; }
bool CtrlCan_TakeErrorFlag(void) { return false; }
/* ---- console ---- */
static int verbose = 0;
void Console_Init(UART_HandleTypeDef *h) { (void)h; }
void Console_Printf(const char *fmt, ...) { if (verbose) { va_list ap; va_start(ap, fmt); printf("  [%6u] ", g_tick); vprintf(fmt, ap); va_end(ap);} }
void Console_Process(void) {}
void Console_Log(const CtrlContext_t *c) { (void)c; }

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s (t=%u state=%s relays=0x%X)\n", __LINE__, #c, g_tick, Ctrl_App_StateName(g_ctrl.state), relays); fails++; } else passes++; } while (0)
static int fails, passes, invariant_fails;

/* ---- simple plant: BMS view + precharge current decay ---- */
static bool bms_online = true; static double cap_v = 0; static double load_a = 0;
static uint32_t last_pack = 0;
static void plant_step(void)
{
    BmsView_t *b = &g_ctrl.bms;
    b->online = bms_online;
    if (!bms_online) return;
    /* DC-link: 60 V pack, 50 ohm precharge resistor, 2 mF, tau = 100 ms */
    double i = 0;
    if ((relays & CTRL_OUT_MAIN_NEG) && (relays & CTRL_OUT_MAIN_POS)) { cap_v = 60; i = load_a; }
    else if ((relays & CTRL_OUT_MAIN_NEG) && (relays & CTRL_OUT_PRECHARGE)) {
        /* 50 ohm precharge resistor feeding 2 mF + a resistive load (60 V / load_a) */
        double r_load = load_a > 0 ? 60.0 / load_a : 1e9;
        i = (60 - cap_v) / 50.0;
        cap_v += (i - cap_v / r_load) / 0.002 * 0.001;
    }
    else { cap_v *= 0.999; }
    if (g_tick - last_pack >= 100) { last_pack = g_tick; b->pack_rx_count++; b->current_ma = (int32_t)(-i * 1000); }
}
static void invariants(void)
{
    /* Main+ never without Main-; precharge never together with charge enable; FAULT => all open */
    if ((relays & CTRL_OUT_MAIN_POS) && !(relays & CTRL_OUT_MAIN_NEG)) invariant_fails++;
    if ((relays & CTRL_OUT_PRECHARGE) && (relays & CTRL_OUT_CHARGE_EN)) invariant_fails++;
    if (g_ctrl.state == CTRL_STATE_FAULT && relays != 0) invariant_fails++;
}
static void run_ms(uint32_t ms) { for (uint32_t k = 0; k < ms; k++) { g_tick++; plant_step(); Ctrl_App_Loop(); invariants(); } }
static void healthy_bms(void)
{
    BmsView_t *b = &g_ctrl.bms;
    b->state = BMS_STATE_IDLE; b->faults = 0; b->warnings = 0; b->e2e_errors = 0;
    b->limit_flags = BMS_LIM_CHARGE_ALLOWED | BMS_LIM_DISCHARGE_ALLOWED;
    b->chg_limit_ma = 12000; b->dsg_limit_ma = 40000; b->tmax_c = 25;
}

int main(int argc, char **argv)
{
    (void)argv;
    CAN_HandleTypeDef hc; TIM_HandleTypeDef ht; UART_HandleTypeDef hu; IWDG_HandleTypeDef hw = { IWDG };
    verbose = argc > 1;
    Ctrl_App_Init(&hc, &ht, &hu, &hw);
    healthy_bms();

    /* 1. power-up with key ON: must not start until the key is cycled */
    key = true;
    run_ms(1000); CHECK(g_ctrl.state == CTRL_STATE_STANDBY); CHECK(relays == 0);
    key = false; run_ms(100); key = true;
    run_ms(50);  CHECK(g_ctrl.state == CTRL_STATE_PRECHARGE); CHECK(relays == CTRL_OUT_MAIN_NEG);
    run_ms(100); CHECK(relays == (CTRL_OUT_MAIN_NEG | CTRL_OUT_PRECHARGE));
    run_ms(1500); CHECK(g_ctrl.state == CTRL_STATE_RUN);
    CHECK(relays & CTRL_OUT_MAIN_POS); CHECK(!(relays & CTRL_OUT_PRECHARGE));
    run_ms(2100); CHECK(relays & CTRL_OUT_CHARGE_EN);           /* charge enable after 2 s */

    /* 2. BMS blocks charging -> charge relay opens at once, contactors stay */
    g_ctrl.bms.limit_flags = BMS_LIM_DISCHARGE_ALLOWED; run_ms(20);
    CHECK(!(relays & CTRL_OUT_CHARGE_EN)); CHECK(g_ctrl.state == CTRL_STATE_RUN);
    healthy_bms();

    /* 3. key OFF under 20 A load: waits up to 2 s, then Main+ then Main- */
    load_a = 20; run_ms(200); key = false; run_ms(50);
    CHECK(g_ctrl.state == CTRL_STATE_STOPPING); CHECK(relays == (CTRL_OUT_MAIN_NEG | CTRL_OUT_MAIN_POS));
    run_ms(2000); CHECK(relays == CTRL_OUT_MAIN_NEG);
    run_ms(150); CHECK(g_ctrl.state == CTRL_STATE_STANDBY); CHECK(relays == 0);
    load_a = 0;

    /* 4. BMS fault while running -> everything opens within one tick */
    key = true; run_ms(2000); CHECK(g_ctrl.state == CTRL_STATE_RUN);
    g_ctrl.bms.faults = BMS_FLT_CELL_OV; g_ctrl.bms.state = BMS_STATE_FAULT; run_ms(10);
    CHECK(g_ctrl.state == CTRL_STATE_FAULT); CHECK(relays == 0);
    healthy_bms(); run_ms(500);
    CHECK(g_ctrl.state == CTRL_STATE_FAULT);                     /* key still ON: stays in FAULT */
    g_ctrl.bms.faults = BMS_FLT_CELL_OV; g_ctrl.bms.state = BMS_STATE_FAULT;
    int c0 = bms_clear_cmds; key = false; run_ms(20);
    CHECK(bms_clear_cmds == c0 + 1);                             /* key OFF acknowledges -> clear sent */
    CHECK(g_ctrl.state == CTRL_STATE_FAULT);                     /* BMS still faulted */
    healthy_bms(); run_ms(20); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);
    key = true; run_ms(2000); CHECK(g_ctrl.state == CTRL_STATE_RUN);

    /* 5. BMS heartbeat lost */
    bms_online = false; g_ctrl.bms.online = false; run_ms(10);
    CHECK(g_ctrl.state == CTRL_STATE_FAULT); CHECK(g_ctrl.faults & CTRL_FLT_BMS_TIMEOUT);
    bms_online = true; key = false; run_ms(50); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);

    /* 6. E-stop opens the pack from RUN */
    key = true; run_ms(2000); CHECK(g_ctrl.state == CTRL_STATE_RUN);
    estop_ok = false; run_ms(10); CHECK(g_ctrl.state == CTRL_STATE_FAULT && relays == 0);
    estop_ok = true; key = false; run_ms(50); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);

    /* 7. precharge fails: heavy load stays on the precharge resistor */
    load_a = 30; cap_v = 0; key = true; run_ms(300);
    CHECK(g_ctrl.state == CTRL_STATE_PRECHARGE);
    run_ms(5200); CHECK(g_ctrl.state == CTRL_STATE_FAULT); CHECK(g_ctrl.faults & CTRL_FLT_PRECHARGE);
    load_a = 0; key = false; run_ms(50); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);
    CHECK(!(g_ctrl.faults & CTRL_FLT_PRECHARGE));                 /* key OFF cleared the latch */

    /* 8. remote stop: stops, and no restart until the key is cycled */
    cap_v = 0; key = true; run_ms(2000); CHECK(g_ctrl.state == CTRL_STATE_RUN);
    Ctrl_App_RequestStop(); run_ms(300); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);
    run_ms(2000); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);
    key = false; run_ms(50); key = true; run_ms(2000); CHECK(g_ctrl.state == CTRL_STATE_RUN);

    /* 9. discharge current beyond limit + margin for 3 s -> trip */
    load_a = 50; run_ms(2900); CHECK(g_ctrl.state == CTRL_STATE_RUN);
    run_ms(200); CHECK(g_ctrl.state == CTRL_STATE_FAULT); CHECK(g_ctrl.faults & CTRL_FLT_LIMIT_VIOLATION);
    load_a = 0; key = false; run_ms(50); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);

    /* 10. start refused while the BMS is not ready */
    g_ctrl.bms.state = BMS_STATE_INIT; key = true; run_ms(500); CHECK(g_ctrl.state == CTRL_STATE_STANDBY);
    key = false; healthy_bms(); run_ms(50);

    /* 11. fan curve */
    g_ctrl.bms.tmax_c = 20; run_ms(200); CHECK(fan == 0);
    g_ctrl.bms.tmax_c = 31; run_ms(200); CHECK(fan > 30 && fan < 40);
    g_ctrl.bms.tmax_c = 28; run_ms(200); CHECK(fan == 30);          /* hysteresis keeps it on */
    g_ctrl.bms.tmax_c = 26; run_ms(200); CHECK(fan == 0);
    g_ctrl.bms.tmax_c = 50; run_ms(200); CHECK(fan == 100);
    g_ctrl.bms.tmax_c = 20; g_ctrl.bms.limit_flags |= BMS_LIM_BALANCING_ACTIVE; run_ms(200); CHECK(fan == 40);
    Ctrl_App_FanOverride(70); run_ms(200); CHECK(fan == 70);
    g_tick += CTRL_FAN_OVERRIDE_MAX_MS; run_ms(200); CHECK(fan == 40);
    bms_online = false; g_ctrl.bms.online = false; run_ms(200); CHECK(fan == 100);

    CHECK(invariant_fails == 0);
    printf("%d passed, %d failed, invariant violations %d\n", passes, fails, invariant_fails);
    return fails != 0;
}
