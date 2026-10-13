/* Node 1 host tests: CRC-8, fixed-point text, protection + limits, SoC,
 * balancing policy, and OLED page renders (written as .pgm into the cwd). */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "bms_app.h"
#include "bms_protection.h"
#include "bms_soc.h"
#include "bms_balancing.h"
#include "bms_display.h"
#include "ssd1306.h"

/* ---- HAL / dependency stubs ---- */
GPIO_TypeDef mGPIOA, mGPIOB, mGPIOC;
uint32_t g_tick = 1000;
uint32_t HAL_GetTick(void) { return g_tick; }
void HAL_Delay(uint32_t ms) { g_tick += ms; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s) { (void)p; (void)pin; (void)s; }
static uint8_t fb[1024];
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t t)
{ (void)h; (void)t; if (n == 1024) memcpy(fb, d, 1024); return HAL_OK; }
BmsContext_t g_bms;
static uint16_t bq_mask; static int bal_writes;
BQ_Result_t BQ_SetBalancing(uint16_t m) { bq_mask = m; bal_writes++; return BQ_OK; }
BQ_Result_t BQ_GetBalancing(uint16_t *m) { *m = bq_mask; return BQ_OK; }
uint32_t BQ_ErrorCount(void) { return 0; }
const char *BMS_App_StateName(BmsState_t s) { const char *n[] = {"INIT","IDLE","CHARGE","DISCHG","FAULT"}; return n[s]; }
const char *BMS_App_FaultName(uint32_t f) {
    static const char *const names[] = {"BQ_COMM","BQ_CONFIG","CELL_OV","CELL_UV","OVER_TEMP","UNDER_TEMP",
        "OC_CHARGE","OC_DISCHG","SHORT_CIRC","IMBALANCE","CELL_SENSE","NTC_SENSE","STACK_MISMATCH","BQ_PERM_FAIL"};
    for (int i = 0; i < 14; i++) if (f & (1UL << i)) return names[i];
    return f ? "UNKNOWN" : "";
}

/* same statistics as bms_app.c process_raw() */
static void set_cells(BmsContext_t *b, const uint16_t *mv, int32_t ma, const int16_t *ntc)
{
    BmsMeasurement_t *m = &b->m; uint32_t sum = 0; uint16_t mn = 0xFFFF, mx = 0;
    int16_t tmin = 32767, tmax = -32768;
    for (int i = 0; i < 16; i++) { m->cell_mv[i] = mv[i]; sum += mv[i];
        if (mv[i] < mn) { mn = mv[i]; m->cell_min_idx = i; } if (mv[i] > mx) { mx = mv[i]; m->cell_max_idx = i; } }
    m->cell_sum_mv = sum; m->cell_min_mv = mn; m->cell_max_mv = mx; m->cell_avg_mv = sum / 16; m->cell_delta_mv = mx - mn;
    m->stack_mv = (int32_t)sum + 20; m->current_ma = ma; m->die_dc = 352;
    for (int n = 0; n < 4; n++) { m->ntc_dc[n] = ntc[n];
        if (ntc[n] < BMS_TEMP_PLAUSIBLE_MIN_DC || ntc[n] > BMS_TEMP_PLAUSIBLE_MAX_DC) continue;
        if (ntc[n] < tmin) tmin = ntc[n]; if (ntc[n] > tmax) tmax = ntc[n]; }
    if (tmax < tmin) { tmin = tmax = m->die_dc; }
    m->temp_min_dc = tmin; m->temp_max_dc = tmax; m->valid = true;
}

static void write_pgm(const char *path)
{
    FILE *f = fopen(path, "wb"); fprintf(f, "P5 128 64 255\n");
    for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) {
        uint8_t on = (fb[x + (y / 8) * 128] >> (y & 7)) & 1; fputc(on ? 255 : 0, f); }
    fclose(f);
}

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } else passes++; } while (0)
int fails, passes;

int main(void)
{
    char buf[32];
    /* ---- CRC-8 SAE J1850 check value: "123456789" -> 0x4B ---- */
    { const uint8_t d[7] = {'3','4','5','6','7','8','9'};
      CHECK(can_crc8_j1850(0x3231, d, 7) == 0x4B);
      uint8_t f[8] = {1,2,3,4,5,6,0,0}; can_e2e_protect(0x100, f, 7);
      CHECK(can_e2e_check(0x100, f)); f[2] ^= 1; CHECK(!can_e2e_check(0x100, f));
      f[2] ^= 1; CHECK(!can_e2e_check(0x101, f)); }

    /* ---- fmt_fixed ---- */
    fmt_fixed(buf, 59843, 1000, 2); CHECK(!strcmp(buf, "59.84"));
    fmt_fixed(buf, -12345, 1000, 1); CHECK(!strcmp(buf, "-12.3"));
    fmt_fixed(buf, -5, 10, 1);      CHECK(!strcmp(buf, "-0.5"));
    fmt_fixed(buf, 3712, 1000, 3);  CHECK(!strcmp(buf, "3.712"));
    fmt_fixed(buf, 0, 1000, 2);     CHECK(!strcmp(buf, "0.00"));
    fmt_fixed(buf, 999, 1000, 2);   CHECK(!strcmp(buf, "1.00"));

    /* ---- protection ---- */
    uint16_t ok[16]; for (int i = 0; i < 16; i++) ok[i] = 3800 + (i % 3) * 5;
    int16_t t_ok[4] = {280, 290, 300, 285};
    memset(&g_bms, 0, sizeof g_bms); g_bms.bal_enabled = true; g_bms.bq_ready = true; g_bms.state = BMS_STATE_IDLE;
    Soc_Init(&g_bms);
    set_cells(&g_bms, ok, -5000, t_ok);
    Prot_ResetDebounce();
    for (int k = 0; k < 20; k++) { Prot_Evaluate(&g_bms); Prot_UpdateLimits(&g_bms); }
    CHECK(g_bms.faults == 0);
    CHECK(g_bms.chg_limit_ma == BMS_CHG_LIMIT_MA && g_bms.dsg_limit_ma == BMS_DSG_LIMIT_MA);
    CHECK(g_bms.limit_flags & BMS_LIM_CHARGE_ALLOWED);

    uint16_t ov[16]; memcpy(ov, ok, sizeof ov); ov[5] = 4210;
    set_cells(&g_bms, ov, 0, t_ok);
    Prot_Evaluate(&g_bms); Prot_Evaluate(&g_bms);
    CHECK(!(g_bms.faults & BMS_FLT_CELL_OV));             /* 2 samples: not yet */
    Prot_Evaluate(&g_bms);
    CHECK(g_bms.faults & BMS_FLT_CELL_OV);                /* 3rd sample latches */
    CHECK(g_bms.warnings & BMS_WRN_CELL_HIGH);
    CHECK(Prot_ActiveConditions(&g_bms) & BMS_FLT_CELL_OV);  /* clear refused */
    Prot_UpdateLimits(&g_bms); CHECK(g_bms.chg_limit_ma == 0 && g_bms.dsg_limit_ma == 0);
    set_cells(&g_bms, ok, 0, t_ok);
    CHECK(Prot_ActiveConditions(&g_bms) == 0);            /* clear allowed */

    /* NTC open -> TEMP_SENSE after 1 s, not UNDER_TEMP */
    g_bms.faults = 0; Prot_ResetDebounce();
    int16_t t_open[4] = {280, -450, 300, 285};
    set_cells(&g_bms, ok, 0, t_open);
    for (int k = 0; k < 9; k++) Prot_Evaluate(&g_bms);
    CHECK(g_bms.faults == 0);
    Prot_Evaluate(&g_bms);
    CHECK(g_bms.faults == BMS_FLT_TEMP_SENSE);

    /* open sense wire (0 mV) -> CELL_SENSE, not CELL_UV */
    g_bms.faults = 0; Prot_ResetDebounce();
    uint16_t ow[16]; memcpy(ow, ok, sizeof ow); ow[9] = 0;
    set_cells(&g_bms, ow, 0, t_ok);
    g_bms.m.stack_mv = 16 * 3800;   /* stack still right */
    for (int k = 0; k < 10; k++) Prot_Evaluate(&g_bms);
    CHECK((g_bms.faults & BMS_FLT_CELL_SENSE) && !(g_bms.faults & BMS_FLT_CELL_UV));

    /* BQ hardware short circuit mirrored immediately */
    g_bms.faults = 0; Prot_ResetDebounce(); set_cells(&g_bms, ok, 0, t_ok);
    g_bms.bq.status_a = BQ_SSA_SCD; Prot_Evaluate(&g_bms);
    CHECK(g_bms.faults == BMS_FLT_SHORT_CIRCUIT);
    g_bms.bq.status_a = 0; g_bms.faults = 0;

    /* limits: 50 C -> no charge, discharge derated to 50 % */
    int16_t t_hot[4] = {500, 480, 470, 460};
    set_cells(&g_bms, ok, 0, t_hot); Prot_UpdateLimits(&g_bms);
    CHECK(g_bms.chg_limit_ma == 0); CHECK(g_bms.dsg_limit_ma == 20000);
    /* charge taper at 4050 mV -> 6.5 A */
    uint16_t hi[16]; memcpy(hi, ok, sizeof hi); hi[0] = 4050;
    set_cells(&g_bms, hi, 0, t_ok); Prot_UpdateLimits(&g_bms);
    CHECK(g_bms.chg_limit_ma == 6500);
    /* discharge taper at 3300 mV -> 20 A; block at 3200, resume only > 3350 */
    uint16_t lo[16]; memcpy(lo, ok, sizeof lo); lo[3] = 3300;
    set_cells(&g_bms, lo, 0, t_ok); Prot_UpdateLimits(&g_bms); CHECK(g_bms.dsg_limit_ma == 20000);
    lo[3] = 3200; set_cells(&g_bms, lo, 0, t_ok); Prot_UpdateLimits(&g_bms); CHECK(g_bms.dsg_limit_ma == 0);
    lo[3] = 3300; set_cells(&g_bms, lo, 0, t_ok); Prot_UpdateLimits(&g_bms); CHECK(g_bms.dsg_limit_ma == 0);
    lo[3] = 3360; set_cells(&g_bms, lo, 0, t_ok); Prot_UpdateLimits(&g_bms); CHECK(g_bms.dsg_limit_ma > 0);

    /* ---- SoC ---- */
    Soc_Init(&g_bms); uint16_t mid[16]; for (int i = 0; i < 16; i++) mid[i] = 3780;
    set_cells(&g_bms, mid, 0, t_ok); Soc_Update(&g_bms);
    CHECK(fabsf(g_bms.soc_pct - 50.0f) < 0.01f);
    int64_t q = (int64_t)1000 << 32;                     /* 1000 userAh */
    Soc_OnPassedCharge(&g_bms, true, q);                  /* seed */
    Soc_OnPassedCharge(&g_bms, true, q + ((int64_t)50 << 32)); /* +0.5 Ah (50 x 10 mAh) */
    Soc_Update(&g_bms);
    CHECK(fabsf(g_bms.soc_pct - (50.0f + 0.5f / BMS_USABLE_CAPACITY_AH * 100.0f)) < 0.05f);
    Soc_OnPassedCharge(&g_bms, true, q - ((int64_t)1000 << 32)); /* impossible jump: ignored */
    Soc_Update(&g_bms);
    CHECK(fabsf(g_bms.soc_pct - (50.0f + 0.5f / BMS_USABLE_CAPACITY_AH * 100.0f)) < 0.05f);

    /* ---- balancing ---- */
    uint16_t bal[16]; for (int i = 0; i < 16; i++) bal[i] = 3900;
    bal[2] = 3950; bal[3] = 3945; bal[7] = 3930;          /* cells 3, 4, 8 high */
    memset(&g_bms, 0, sizeof g_bms); g_bms.bal_enabled = true; g_bms.bq_ready = true;
    g_bms.state = BMS_STATE_IDLE; g_bms.bal_odd_group = true;
    set_cells(&g_bms, bal, 0, t_ok); Bal_Init(); bq_mask = 0;
    Bal_Update(&g_bms); CHECK(g_bms.bal_mask == 0x0004);  /* odd group: cell 3 */
    g_tick += 31000; Bal_Update(&g_bms); CHECK(g_bms.bal_mask == 0x0088); /* even: cells 4, 8 */
    for (int k = 0; k < 200; k++) { g_tick += 1000; Bal_Update(&g_bms);
        CHECK((g_bms.bal_mask & (g_bms.bal_mask << 1)) == 0); }   /* never neighbours */
    int w0 = bal_writes; g_tick += 1000; Bal_Update(&g_bms);
    g_tick += BMS_BAL_REFRESH_MS; Bal_Update(&g_bms); CHECK(bal_writes > w0);   /* refreshed */
    set_cells(&g_bms, bal, -2000, t_ok); Bal_Update(&g_bms); CHECK(g_bms.bal_mask == 0); /* discharging */
    set_cells(&g_bms, bal, 0, t_ok); g_bms.bal_enabled = false; Bal_Update(&g_bms); CHECK(g_bms.bal_mask == 0);
    for (int i = 0; i < 16; i++) bal[i] = 3700; bal[5] = 3800; g_bms.bal_enabled = true;
    set_cells(&g_bms, bal, 0, t_ok); Bal_Update(&g_bms); CHECK(g_bms.bal_mask == 0); /* below 3.85 V floor */

    /* ---- OLED pages ---- */
    uint16_t cells[16] = {3712,3715,3709,3711,3745,3720,3718,3716,3714,3719,3721,3713,3717,3722,3710,3716};
    int16_t temps[4] = {315, 309, 320, 311};
    memset(&g_bms, 0, sizeof g_bms);
    set_cells(&g_bms, cells, -12340, temps);
    g_bms.state = BMS_STATE_DISCHARGING; g_bms.soc_pct = 76.5f; g_bms.soh_pct = 35.0f;
    g_bms.bal_enabled = true; g_bms.bal_mask = 0x0010; g_bms.uptime_s = 5025;
    SPI_HandleTypeDef spi; g_tick = 0; Display_Init(&spi);
    Display_Splash("Master Monitoring", "Starting BQ76952..."); write_pgm("oled_0_splash.pgm");
    for (int p = 0; p < 3; p++) { g_tick += BMS_DISPLAY_PAGE_MS * (p ? 1 : 0); Display_Update(&g_bms);
        char n[32]; sprintf(n, "oled_%d_page.pgm", p + 1); write_pgm(n); }
    g_bms.faults = BMS_FLT_CELL_OV | BMS_FLT_OVER_TEMP; g_bms.state = BMS_STATE_FAULT;
    g_tick += BMS_DISPLAY_PAGE_MS; Display_Update(&g_bms); write_pgm("oled_4_fault.pgm");
    g_tick += BMS_DISPLAY_PAGE_MS; Display_Update(&g_bms); write_pgm("oled_5_faultsummary.pgm");

    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
