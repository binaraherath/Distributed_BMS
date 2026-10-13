/**
 ******************************************************************************
 * @file    bms_protection.c
 * @brief   Fault detection, warnings and current limits (see header).
 ******************************************************************************
 */
#include "bms_protection.h"

enum {
    DB_OV = 0, DB_UV, DB_OT, DB_UT, DB_OCC, DB_OCD, DB_IMB,
    DB_CELL_SENSE, DB_TEMP_SENSE, DB_STACK, DB_COUNT
};

static uint8_t s_db[DB_COUNT];
static bool    s_chg_blocked;
static bool    s_dsg_blocked;

/* ==========================================================================
 * Condition checks (shared by Evaluate and ActiveConditions)
 * ========================================================================== */

static bool any_cell_implausible(const BmsMeasurement_t *m)
{
    for (uint8_t i = 0; i < BMS_CELLS; i++) {
        if (m->cell_mv[i] < BMS_CELL_PLAUSIBLE_MIN_MV ||
            m->cell_mv[i] > BMS_CELL_PLAUSIBLE_MAX_MV) return true;
    }
    return false;
}

static bool any_ntc_implausible(const BmsMeasurement_t *m)
{
    for (uint8_t i = 0; i < BMS_NTC_COUNT; i++) {
        if ((BMS_NTC_FITTED_MASK & (1u << i)) == 0u) continue;
        if (m->ntc_dc[i] < BMS_TEMP_PLAUSIBLE_MIN_DC ||
            m->ntc_dc[i] > BMS_TEMP_PLAUSIBLE_MAX_DC) return true;
    }
    return false;
}

static bool stack_mismatch(const BmsMeasurement_t *m)
{
    int32_t diff  = m->stack_mv - (int32_t)m->cell_sum_mv;
    int32_t limit = (int32_t)(m->cell_sum_mv * BMS_STACK_MISMATCH_PCT / 100u);
    if (limit < BMS_STACK_MISMATCH_MIN_MV) limit = BMS_STACK_MISMATCH_MIN_MV;
    if (diff < 0) diff = -diff;
    return diff > limit;
}

/** Raw (un-debounced) MCU-side conditions as fault bits. */
static uint32_t mcu_conditions(const BmsMeasurement_t *m)
{
    uint32_t f = 0;

    if (m->cell_max_mv >= BMS_CELL_OV_FAULT_MV)                 f |= BMS_FLT_CELL_OV;
    if (m->cell_min_mv <= BMS_CELL_UV_FAULT_MV &&
        m->cell_min_mv >= BMS_CELL_PLAUSIBLE_MIN_MV)            f |= BMS_FLT_CELL_UV;
    if (m->temp_max_dc >= BMS_TEMP_OT_FAULT_DC ||
        m->die_dc      >= BMS_TEMP_DIE_FAULT_DC)                f |= BMS_FLT_OVER_TEMP;
    if (m->temp_min_dc <= BMS_TEMP_UT_FAULT_DC)                 f |= BMS_FLT_UNDER_TEMP;
    if (m->current_ma  >=  BMS_OC_CHG_FAULT_MA)                 f |= BMS_FLT_OC_CHARGE;
    if (m->current_ma  <= -BMS_OC_DSG_FAULT_MA)                 f |= BMS_FLT_OC_DISCHARGE;
    if (m->cell_delta_mv >= BMS_CELL_DELTA_FAULT_MV)            f |= BMS_FLT_IMBALANCE;
    if (any_cell_implausible(m))                                f |= BMS_FLT_CELL_SENSE;
    if (any_ntc_implausible(m))                                 f |= BMS_FLT_TEMP_SENSE;
    if (stack_mismatch(m))                                      f |= BMS_FLT_STACK_MISMATCH;
    return f;
}

/** BQ76952 safety status mapped onto our fault bits (already debounced
 *  inside the BQ by its own protection delays). */
static uint32_t bq_conditions(const BQ_Safety_t *s)
{
    uint32_t f = 0;

    if (s->status_a & BQ_SSA_CUV)                               f |= BMS_FLT_CELL_UV;
    if (s->status_a & BQ_SSA_COV)                               f |= BMS_FLT_CELL_OV;
    if (s->status_a & BQ_SSA_OCC)                               f |= BMS_FLT_OC_CHARGE;
    if (s->status_a & (BQ_SSA_OCD1 | BQ_SSA_OCD2))              f |= BMS_FLT_OC_DISCHARGE;
    if (s->status_a & BQ_SSA_SCD)                               f |= BMS_FLT_SHORT_CIRCUIT;

    if (s->status_b & (BQ_SSB_OTC | BQ_SSB_OTD | BQ_SSB_OTINT | BQ_SSB_OTF))
                                                                f |= BMS_FLT_OVER_TEMP;
    if (s->status_b & (BQ_SSB_UTC | BQ_SSB_UTD | BQ_SSB_UTINT)) f |= BMS_FLT_UNDER_TEMP;

    if (s->status_c & (BQ_SSC_OCD3 | BQ_SSC_OCDL))              f |= BMS_FLT_OC_DISCHARGE;
    if (s->status_c & BQ_SSC_SCDL)                              f |= BMS_FLT_SHORT_CIRCUIT;
    if (s->status_c & BQ_SSC_COVL)                              f |= BMS_FLT_CELL_OV;

    if (s->battery_status & BQ_BAT_PF)                          f |= BMS_FLT_BQ_PERMANENT_FAIL;
    return f;
}

/* ==========================================================================
 * Public
 * ========================================================================== */

void Prot_ResetDebounce(void)
{
    for (uint8_t i = 0; i < DB_COUNT; i++) s_db[i] = 0;
}

static bool debounce(uint8_t slot, bool condition, uint8_t limit)
{
    if (!condition) { s_db[slot] = 0; return false; }
    if (s_db[slot] < 0xFFu) s_db[slot]++;
    return s_db[slot] >= limit;
}

void Prot_Evaluate(BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    uint32_t c = mcu_conditions(m);
    uint32_t latch = 0;
    uint16_t w = 0;

    /* Layer 1: MCU checks, debounced */
    if (debounce(DB_OV,         c & BMS_FLT_CELL_OV,        BMS_DEBOUNCE_FAST)) latch |= BMS_FLT_CELL_OV;
    if (debounce(DB_UV,         c & BMS_FLT_CELL_UV,        BMS_DEBOUNCE_FAST)) latch |= BMS_FLT_CELL_UV;
    if (debounce(DB_OT,         c & BMS_FLT_OVER_TEMP,      BMS_DEBOUNCE_FAST)) latch |= BMS_FLT_OVER_TEMP;
    if (debounce(DB_UT,         c & BMS_FLT_UNDER_TEMP,     BMS_DEBOUNCE_FAST)) latch |= BMS_FLT_UNDER_TEMP;
    if (debounce(DB_OCC,        c & BMS_FLT_OC_CHARGE,      BMS_DEBOUNCE_SLOW)) latch |= BMS_FLT_OC_CHARGE;
    if (debounce(DB_OCD,        c & BMS_FLT_OC_DISCHARGE,   BMS_DEBOUNCE_SLOW)) latch |= BMS_FLT_OC_DISCHARGE;
    if (debounce(DB_IMB,        c & BMS_FLT_IMBALANCE,      BMS_DEBOUNCE_SLOW)) latch |= BMS_FLT_IMBALANCE;
    if (debounce(DB_CELL_SENSE, c & BMS_FLT_CELL_SENSE,     BMS_DEBOUNCE_SLOW)) latch |= BMS_FLT_CELL_SENSE;
    if (debounce(DB_TEMP_SENSE, c & BMS_FLT_TEMP_SENSE,     BMS_DEBOUNCE_SLOW)) latch |= BMS_FLT_TEMP_SENSE;
    if (debounce(DB_STACK,      c & BMS_FLT_STACK_MISMATCH, BMS_DEBOUNCE_SLOW)) latch |= BMS_FLT_STACK_MISMATCH;

    /* Layer 2: BQ76952 hardware protections */
    latch |= bq_conditions(&b->bq);
    b->faults |= latch;

    /* Live warnings */
    if (m->cell_max_mv >= BMS_CELL_OV_WARN_MV)                   w |= BMS_WRN_CELL_HIGH;
    if (m->cell_min_mv <= BMS_CELL_EMPTY_MV)                     w |= BMS_WRN_CELL_LOW;
    if (m->temp_max_dc >= BMS_TEMP_WARN_HIGH_DC)                 w |= BMS_WRN_TEMP_HIGH;
    if (m->temp_min_dc <= BMS_TEMP_WARN_LOW_DC)                  w |= BMS_WRN_TEMP_LOW;
    if (m->cell_delta_mv >= BMS_CELL_DELTA_WARN_MV)              w |= BMS_WRN_IMBALANCE;
    if ((m->current_ma > 0 && m->current_ma > b->chg_limit_ma + b->chg_limit_ma / 10 + 500) ||
        (m->current_ma < 0 && -m->current_ma > b->dsg_limit_ma + b->dsg_limit_ma / 10 + 500))
                                                                 w |= BMS_WRN_CURRENT_HIGH;
    if ((b->bq.alert_a | b->bq.alert_b | b->bq.alert_c) != 0u)   w |= BMS_WRN_BQ_ALERT;
    if (b->soc_pct <= BMS_SOC_LOW_WARN_PCT)                      w |= BMS_WRN_SOC_LOW;

    b->warnings = (uint16_t)((b->warnings & ~PROT_WARN_MASK) | w);
}

uint32_t Prot_ActiveConditions(const BmsContext_t *b)
{
    if (!b->m.valid) return 0;
    return mcu_conditions(&b->m) | bq_conditions(&b->bq);
}

void Prot_UpdateLimits(BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    int32_t chg = BMS_CHG_LIMIT_MA;
    int32_t dsg = BMS_DSG_LIMIT_MA;
    uint8_t flags = 0;

    /* Temperature windows */
    if (m->temp_min_dc < BMS_TEMP_CHG_MIN_DC || m->temp_max_dc > BMS_TEMP_CHG_MAX_DC) chg = 0;
    if (m->temp_min_dc < BMS_TEMP_DSG_MIN_DC || m->temp_max_dc > BMS_TEMP_DSG_MAX_DC) {
        dsg = 0;
    } else if (m->temp_max_dc > BMS_TEMP_DERATE_START_DC) {
        dsg = dsg * (BMS_TEMP_DSG_MAX_DC - m->temp_max_dc) /
              (BMS_TEMP_DSG_MAX_DC - BMS_TEMP_DERATE_START_DC);
    }

    /* Charge: taper towards full, block at the warning level (hysteresis) */
    if (m->cell_max_mv >= BMS_CELL_OV_WARN_MV)      s_chg_blocked = true;
    else if (m->cell_max_mv < BMS_CELL_CHG_RESUME_MV) s_chg_blocked = false;

    if (s_chg_blocked) {
        chg = 0;
    } else if (m->cell_max_mv >= BMS_CELL_FULL_MV) {
        if (chg > BMS_CHG_TAPER_END_MA) chg = BMS_CHG_TAPER_END_MA;
    } else if (m->cell_max_mv > BMS_CELL_CHG_TAPER_MV && chg > BMS_CHG_TAPER_END_MA) {
        chg = BMS_CHG_TAPER_END_MA + (chg - BMS_CHG_TAPER_END_MA) *
              (BMS_CELL_FULL_MV - (int32_t)m->cell_max_mv) /
              (BMS_CELL_FULL_MV - BMS_CELL_CHG_TAPER_MV);
    }

    /* Discharge: taper towards empty, block at empty (hysteresis) */
    if (m->cell_min_mv <= BMS_CELL_EMPTY_MV)        s_dsg_blocked = true;
    else if (m->cell_min_mv > BMS_CELL_DSG_RESUME_MV) s_dsg_blocked = false;

    if (s_dsg_blocked) {
        dsg = 0;
    } else if (m->cell_min_mv < BMS_CELL_DSG_TAPER_MV) {
        dsg = dsg * ((int32_t)m->cell_min_mv - BMS_CELL_EMPTY_MV) /
              (BMS_CELL_DSG_TAPER_MV - BMS_CELL_EMPTY_MV);
    }

    if (b->faults != 0u || !m->valid) { chg = 0; dsg = 0; }
    if (chg < 0) chg = 0;
    if (dsg < 0) dsg = 0;

    if (chg > 0)                                 flags |= BMS_LIM_CHARGE_ALLOWED;
    if (dsg > 0)                                 flags |= BMS_LIM_DISCHARGE_ALLOWED;
    if (b->bal_mask != 0u)                       flags |= BMS_LIM_BALANCING_ACTIVE;
    if (b->bal_enabled)                          flags |= BMS_LIM_BALANCING_ENABLED;
    if (m->cell_max_mv >= BMS_CELL_FULL_MV)      flags |= BMS_LIM_PACK_FULL;
    if (m->cell_min_mv <= BMS_CELL_EMPTY_MV)     flags |= BMS_LIM_PACK_EMPTY;

    b->chg_limit_ma = chg;
    b->dsg_limit_ma = dsg;
    b->limit_flags  = flags;
}
