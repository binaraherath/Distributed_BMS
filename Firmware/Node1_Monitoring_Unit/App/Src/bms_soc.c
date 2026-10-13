/**
 ******************************************************************************
 * @file    bms_soc.c
 * @brief   State of charge estimation (see header).
 ******************************************************************************
 */
#include "bms_soc.h"

/* Rested OCV of an aged Leaf LMO/NMC cell, mapped onto the usable window
 * 3.20 V (0 %) .. 4.10 V (100 %). To be refined with the week-14 tests. */
typedef struct { uint16_t mv; float soc; } OcvPoint_t;

static const OcvPoint_t k_ocv[] = {
    { 3200,   0.0f }, { 3450,   5.0f }, { 3550,  10.0f }, { 3620,  20.0f },
    { 3680,  30.0f }, { 3730,  40.0f }, { 3780,  50.0f }, { 3840,  60.0f },
    { 3900,  70.0f }, { 3960,  80.0f }, { 4030,  90.0f }, { 4100, 100.0f }
};
#define OCV_POINTS  (sizeof(k_ocv) / sizeof(k_ocv[0]))

/* 1 userAh = 10 mAh with DA Configuration USER_AMPS = centiamps */
#define USER_AH_TO_AH   0.01f

static bool     s_seeded;
static bool     s_passq_valid;
static int64_t  s_passq_prev;
static bool     s_resting;
static uint32_t s_rest_start_ms;
static uint32_t s_last_ms;

float Soc_FromOcv(uint16_t cell_mv)
{
    if (cell_mv <= k_ocv[0].mv)              return 0.0f;
    if (cell_mv >= k_ocv[OCV_POINTS - 1].mv) return 100.0f;

    for (uint8_t i = 1; i < OCV_POINTS; i++) {
        if (cell_mv <= k_ocv[i].mv) {
            float f = (float)(cell_mv - k_ocv[i - 1].mv) /
                      (float)(k_ocv[i].mv - k_ocv[i - 1].mv);
            return k_ocv[i - 1].soc + f * (k_ocv[i].soc - k_ocv[i - 1].soc);
        }
    }
    return 50.0f;
}

static void apply_delta_ah(BmsContext_t *b, float delta_ah)
{
    b->soc_pct += (delta_ah / b->usable_ah) * 100.0f;
    if (delta_ah < 0.0f) b->discharged_ah += -delta_ah;
}

void Soc_Init(BmsContext_t *b)
{
    b->soh_pct       = BMS_INITIAL_SOH_PCT;
    b->usable_ah     = BMS_USABLE_CAPACITY_AH;
    b->soc_pct       = 0.0f;
    b->remaining_ah  = 0.0f;
    b->discharged_ah = 0.0f;
    s_seeded = false;
    s_passq_valid = false;
    s_resting = false;
    s_last_ms = HAL_GetTick();
}

void Soc_Update(BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    uint32_t now = HAL_GetTick();
    int32_t  iabs = (m->current_ma < 0) ? -m->current_ma : m->current_ma;

    if (!m->valid) return;

    /* The node is powered from the CAN harness, so it boots with the pack
     * at rest: the first reading is a good OCV starting point. */
    if (!s_seeded) {
        b->soc_pct = Soc_FromOcv(m->cell_avg_mv);
        s_seeded = true;
        s_last_ms = now;
        return;
    }

    /* Fallback integration while the BQ accumulator is not available */
    if (!s_passq_valid) {
        float dt_h = (float)(now - s_last_ms) / 3600000.0f;
        apply_delta_ah(b, ((float)m->current_ma / 1000.0f) * dt_h);
    }
    s_last_ms = now;

    /* Re-anchor to OCV after a long rest (blend, so the dashboard does not jump) */
    if (iabs < BMS_SOC_REST_CURRENT_MA) {
        if (!s_resting) { s_resting = true; s_rest_start_ms = now; }
        if ((now - s_rest_start_ms) >= BMS_SOC_REST_TIME_MS) {
            float ocv = Soc_FromOcv(m->cell_avg_mv);
            b->soc_pct = (1.0f - BMS_SOC_OCV_BLEND) * b->soc_pct + BMS_SOC_OCV_BLEND * ocv;
            s_rest_start_ms = now;
        }
    } else {
        s_resting = false;
    }

    /* Hard anchors at the ends of the usable window */
    if (m->cell_max_mv >= BMS_CELL_FULL_MV && m->current_ma < BMS_CHG_TAPER_END_MA) b->soc_pct = 100.0f;
    if (m->cell_min_mv <= BMS_CELL_EMPTY_MV && m->current_ma <= 0)                   b->soc_pct = 0.0f;

    if (b->soc_pct < 0.0f)   b->soc_pct = 0.0f;
    if (b->soc_pct > 100.0f) b->soc_pct = 100.0f;
    b->remaining_ah = b->usable_ah * b->soc_pct / 100.0f;
}

void Soc_OnPassedCharge(BmsContext_t *b, bool valid, int64_t q_raw)
{
    if (!valid) { s_passq_valid = false; return; }

    if (s_passq_valid && s_seeded) {
        float delta = ((float)(q_raw - s_passq_prev) / 4294967296.0f) * USER_AH_TO_AH;
        delta *= (float)BMS_CURRENT_SIGN;
        /* A jump over 1 Ah in one second is impossible (3600 A): the BQ
         * accumulator was reset, so just re-seed. */
        if (delta < 1.0f && delta > -1.0f) apply_delta_ah(b, delta);
    }
    s_passq_prev  = q_raw;
    s_passq_valid = true;
}
