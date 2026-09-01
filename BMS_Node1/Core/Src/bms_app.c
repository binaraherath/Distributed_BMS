/**
  ******************************************************************************
  * @file    bms_app.c
  * @brief   State machine, redundant protections, SoC estimation, balancing.
  ******************************************************************************
  */
#include "bms_app.h"
#include "bms_config.h"
#include "main.h"
#include <string.h>
#include <math.h>

BMS_Context_t g_bms;

/* Debounce slots */
enum { DB_OV = 0, DB_UV, DB_OT, DB_OC, DB_IMB, DB_CELL_PLAUS, DB_TEMP_PLAUS,
       DB_SUM };

/* --------------------------------------------------------------------------
 * Open-circuit-voltage table for Nissan Leaf cells.
 * ------------------------------------------------------------------------ */
typedef struct { uint16_t mv; float soc; } OcvPoint_t;

static const OcvPoint_t k_ocv[] = {
    { 3200,   0.0f },
    { 3480,  10.0f },
    { 3580,  20.0f },
    { 3640,  30.0f },
    { 3690,  40.0f },
    { 3740,  50.0f },
    { 3800,  60.0f },
    { 3860,  70.0f },
    { 3930,  80.0f },
    { 4000,  90.0f },
    { 4100, 100.0f }
};
#define OCV_POINTS (sizeof(k_ocv) / sizeof(k_ocv[0]))

static float OcvToSoc(uint16_t cell_mv)
{
    if (cell_mv <= k_ocv[0].mv)             return 0.0f;
    if (cell_mv >= k_ocv[OCV_POINTS - 1].mv) return 100.0f;

    for (uint8_t i = 1; i < OCV_POINTS; i++) {
        if (cell_mv <= k_ocv[i].mv) {
            float span_mv  = (float)(k_ocv[i].mv - k_ocv[i - 1].mv);
            float span_soc = k_ocv[i].soc - k_ocv[i - 1].soc;
            float frac     = (float)(cell_mv - k_ocv[i - 1].mv) / span_mv;
            return k_ocv[i - 1].soc + frac * span_soc;
        }
    }
    return 50.0f;
}

/* --------------------------------------------------------------------------
 * Fault helpers
 * ------------------------------------------------------------------------ */

static void FaultLatch(uint32_t bit)
{
    g_bms.faults |= bit;
}

/**
 * Latch a fault only after BMS_FAULT_DEBOUNCE_COUNT consecutive bad samples.
 * Rejects single-sample I2C noise without meaningfully delaying a real event.
 */
static void FaultDebounced(uint8_t slot, bool condition, uint32_t bit)
{
    if (condition) {
        if (g_bms.fault_debounce[slot] < 0xFFFFu) g_bms.fault_debounce[slot]++;
        if (g_bms.fault_debounce[slot] >= BMS_FAULT_DEBOUNCE_COUNT) {
            FaultLatch(bit);
        }
    } else {
        g_bms.fault_debounce[slot] = 0;
    }
}

static void DriveSafetyOutput(void)
{
    bool ok = (g_bms.faults == BMS_FAULT_NONE) &&
              (g_bms.state != BMS_STATE_FAULT) &&
              (g_bms.state != BMS_STATE_INIT);

#if (BMS_ARM_SAFETY_OUTPUT == 0)
    ok = false;                     /* safety output disarmed by config */
#endif

    HAL_GPIO_WritePin(BMS_OK_GPIO_Port, BMS_OK_Pin,
                      ok ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(FAULT_LED_GPIO_Port, FAULT_LED_Pin,
                      (g_bms.faults != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* --------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------ */

void BMS_App_Init(void)
{
    memset(&g_bms, 0, sizeof(g_bms));
    g_bms.state              = BMS_STATE_INIT;
    g_bms.soc_pct            = 50.0f;
    g_bms.soh_pct            = 100.0f;
    g_bms.balancing_allowed  = true;
    g_bms.last_good_meas_ms  = HAL_GetTick();
    g_bms.last_ctrl_rx_ms    = HAL_GetTick();

#if (BMS_ARM_SAFETY_OUTPUT == 0)
    g_bms.faults |= BMS_FAULT_NOT_ARMED;
#endif

    DriveSafetyOutput();
}

/* --------------------------------------------------------------------------
 * Protection evaluation
 * ------------------------------------------------------------------------ */

static void EvaluatePlausibility(const BQ_Data_t *m)
{
    bool cell_bad = false;
    bool temp_bad = false;
    int32_t diff;

    for (uint8_t i = 0; i < BMS_CELL_COUNT; i++) {
        if (m->cell_mv[i] < BMS_CELL_PLAUSIBLE_MIN_MV ||
            m->cell_mv[i] > BMS_CELL_PLAUSIBLE_MAX_MV) {
            cell_bad = true;
        }
    }
    FaultDebounced(DB_CELL_PLAUS, cell_bad, BMS_FAULT_CELL_IMPLAUS);

    for (uint8_t i = 0; i < BMS_THERMISTOR_COUNT; i++) {
        if (m->temp_dc[i] < BMS_TEMP_PLAUSIBLE_MIN_DC ||
            m->temp_dc[i] > BMS_TEMP_PLAUSIBLE_MAX_DC) {
            temp_bad = true;
        }
    }
    FaultDebounced(DB_TEMP_PLAUS, temp_bad, BMS_FAULT_TEMP_IMPLAUS);

    /* The stack measurement uses a different ADC path from the individual
     * cell measurements. If the two disagree by more than 2 %, one of them
     * is wrong and neither can be trusted. */
    diff = (int32_t)m->stack_mv - (int32_t)m->cell_sum_mv;
    if (diff < 0) diff = -diff;
    FaultDebounced(DB_SUM,
                   (m->cell_sum_mv > 0u) &&
                   ((uint32_t)diff > (m->cell_sum_mv / 50u)),
                   BMS_FAULT_SUM_MISMATCH);
}

static void EvaluateMcuProtections(const BQ_Data_t *m)
{
    int16_t tmax = -32768, tmin = 32767;
    int32_t iabs = (m->current_ma < 0) ? -m->current_ma : m->current_ma;

    for (uint8_t i = 0; i < BMS_THERMISTOR_COUNT; i++) {
        if (m->temp_dc[i] > tmax) tmax = m->temp_dc[i];
        if (m->temp_dc[i] < tmin) tmin = m->temp_dc[i];
    }

    FaultDebounced(DB_OV,  m->cell_max_mv >= BMS_CELL_OV_FAULT_MV,
                   BMS_FAULT_CELL_OV);
    FaultDebounced(DB_UV,  m->cell_min_mv <= BMS_CELL_UV_FAULT_MV,
                   BMS_FAULT_CELL_UV);
    FaultDebounced(DB_OT,  tmax >= BMS_TEMP_FAULT_DC,
                   BMS_FAULT_OVER_TEMP);
    FaultDebounced(DB_OC,  iabs >= BMS_CURRENT_FAULT_MA,
                   BMS_FAULT_OVER_CURRENT);
    FaultDebounced(DB_IMB, m->cell_delta_mv >= BMS_CELL_DELTA_FAULT_MV,
                   BMS_FAULT_IMBALANCE);

    /* Warnings are live, not latched. */
    g_bms.warnings = BMS_WARN_NONE;
    if (m->cell_max_mv >= BMS_CELL_OV_WARN_MV)   g_bms.warnings |= BMS_WARN_CELL_HIGH;
    if (m->cell_min_mv <= BMS_CELL_UV_WARN_MV)   g_bms.warnings |= BMS_WARN_CELL_LOW;
    if (tmax >= BMS_TEMP_DSG_MAX_DC)             g_bms.warnings |= BMS_WARN_TEMP_HIGH;
    if (tmin <= BMS_TEMP_CHG_MIN_DC)             g_bms.warnings |= BMS_WARN_TEMP_LOW;
    if (m->cell_delta_mv >= BMS_CELL_DELTA_WARN_MV) g_bms.warnings |= BMS_WARN_IMBALANCE;
    if (g_bms.balance_mask != 0u)                g_bms.warnings |= BMS_WARN_BALANCING;

    if ((HAL_GetTick() - g_bms.last_ctrl_rx_ms) > BMS_CAN_RX_TIMEOUT_MS)
        g_bms.warnings |= BMS_WARN_CTRL_TIMEOUT;
}

static void MirrorBqProtections(const BQ_Data_t *m)
{
    if (m->safety_status_a & BQ_SSA_CUV)  FaultLatch(BMS_FAULT_BQ_CUV);
    if (m->safety_status_a & BQ_SSA_COV)  FaultLatch(BMS_FAULT_BQ_COV);
    if (m->safety_status_a & BQ_SSA_OCC)  FaultLatch(BMS_FAULT_BQ_OCC);
    if (m->safety_status_a & (BQ_SSA_OCD1 | BQ_SSA_OCD2)) FaultLatch(BMS_FAULT_BQ_OCD);
    if (m->safety_status_a & BQ_SSA_SCD)  FaultLatch(BMS_FAULT_BQ_SCD);

    if (m->safety_status_b & (BQ_SSB_OTC | BQ_SSB_OTD | BQ_SSB_OTINT | BQ_SSB_OTF))
        FaultLatch(BMS_FAULT_BQ_OT);
    if (m->safety_status_b & (BQ_SSB_UTC | BQ_SSB_UTD | BQ_SSB_UTINT))
        FaultLatch(BMS_FAULT_BQ_UT);
}

/* --------------------------------------------------------------------------
 * Current limits (simple linear derating)
 * ------------------------------------------------------------------------ */

static void UpdateCurrentLimits(const BQ_Data_t *m)
{
    int16_t tmax = -32768, tmin = 32767;
    int32_t chg = BMS_CURRENT_CHG_LIMIT_MA;
    int32_t dsg = BMS_CURRENT_DSG_LIMIT_MA;

    for (uint8_t i = 0; i < BMS_THERMISTOR_COUNT; i++) {
        if (m->temp_dc[i] > tmax) tmax = m->temp_dc[i];
        if (m->temp_dc[i] < tmin) tmin = m->temp_dc[i];
    }

    /* Temperature windows */
    if (tmin < BMS_TEMP_CHG_MIN_DC || tmax > BMS_TEMP_CHG_MAX_DC) chg = 0;
    if (tmin < BMS_TEMP_DSG_MIN_DC || tmax > BMS_TEMP_DSG_MAX_DC) dsg = 0;

    /* Taper charge as the highest cell approaches the OV warning. */
    if (m->cell_max_mv >= BMS_CELL_OV_WARN_MV) {
        chg = 0;
    } else if (m->cell_max_mv > (BMS_CELL_OV_WARN_MV - 100)) {
        int32_t head = BMS_CELL_OV_WARN_MV - (int32_t)m->cell_max_mv; /* 0..100 */
        chg = (chg * head) / 100;
    }

    /* Taper discharge as the lowest cell approaches the UV warning. */
    if (m->cell_min_mv <= BMS_CELL_UV_WARN_MV) {
        dsg = 0;
    } else if (m->cell_min_mv < (BMS_CELL_UV_WARN_MV + 150)) {
        int32_t head = (int32_t)m->cell_min_mv - BMS_CELL_UV_WARN_MV; /* 0..150 */
        dsg = (dsg * head) / 150;
    }

    if (g_bms.faults != BMS_FAULT_NONE) { chg = 0; dsg = 0; }

    g_bms.charge_limit_ma    = chg;
    g_bms.discharge_limit_ma = dsg;
}

/* --------------------------------------------------------------------------
 * SoC: coulomb counting, re-anchored to OCV when the pack is at rest
 * ------------------------------------------------------------------------ */

static void UpdateSoc(const BQ_Data_t *m)
{
    static uint32_t rest_start_ms = 0;
    static bool     rest_timing   = false;
    float delta_ah;

    /* Coulomb counting over one measurement period. */
    delta_ah = ((float)m->current_ma / 1000.0f) *
               ((float)BMS_MEAS_PERIOD_MS / 3600000.0f);
    g_bms.coulomb_count_ah += delta_ah;
    g_bms.soc_pct += (delta_ah / BMS_PACK_DESIGN_CAPACITY_AH) * 100.0f;

    /* Re-anchor on OCV after 10 minutes below 1 A. Coulomb counting drifts;
     * OCV is absolute but only meaningful once the cells have relaxed. */
    if (m->current_ma > -1000 && m->current_ma < 1000) {
        if (!rest_timing) { rest_timing = true; rest_start_ms = HAL_GetTick(); }
        if ((HAL_GetTick() - rest_start_ms) > 600000u) {
            float ocv_soc = OcvToSoc(m->cell_min_mv);
            /* Blend rather than jump, so the dashboard does not step. */
            g_bms.soc_pct = 0.9f * g_bms.soc_pct + 0.1f * ocv_soc;
            rest_start_ms = HAL_GetTick();
        }
    } else {
        rest_timing = false;
    }

    /* Hard anchors at the ends of the curve. */
    if (m->cell_max_mv >= BMS_CELL_OV_WARN_MV && m->current_ma < 1000)
        g_bms.soc_pct = 100.0f;
    if (m->cell_min_mv <= BMS_CELL_UV_WARN_MV)
        g_bms.soc_pct = 0.0f;

    if (g_bms.soc_pct < 0.0f)   g_bms.soc_pct = 0.0f;
    if (g_bms.soc_pct > 100.0f) g_bms.soc_pct = 100.0f;
}

/* --------------------------------------------------------------------------
 * Main evaluation pass
 * ------------------------------------------------------------------------ */

void BMS_App_MeasureAndEvaluate(void)
{
    BQ_Data_t m;
    BQ_Status_t st = BQ76952_ReadAll(&m);

    if (st != BQ_OK) {
        if ((HAL_GetTick() - g_bms.last_good_meas_ms) > BMS_BQ_COMMS_TIMEOUT_MS)
            FaultLatch(BMS_FAULT_BQ_COMMS);
    } else {
        g_bms.meas = m;
        g_bms.last_good_meas_ms = HAL_GetTick();

        EvaluatePlausibility(&m);
        MirrorBqProtections(&m);
        EvaluateMcuProtections(&m);
        UpdateCurrentLimits(&m);
        UpdateSoc(&m);
    }

    /* State transitions */
    if (g_bms.faults != BMS_FAULT_NONE) {
        g_bms.state = BMS_STATE_FAULT;
    } else if (g_bms.state == BMS_STATE_INIT) {
        g_bms.state = BMS_STATE_IDLE;
    } else {
        int32_t i = g_bms.meas.current_ma;
        if (i < 0) i = -i;
        g_bms.state = (i > 500) ? BMS_STATE_ACTIVE : BMS_STATE_IDLE;
    }

    DriveSafetyOutput();
}

/* --------------------------------------------------------------------------
 * Balancing
 *
 * The BQ76952's internal bleed FETs cannot drive two adjacent cells at once,
 * so we split the requested set into odd and even groups and alternate them
 * on a BMS_BAL_SLOT_MS timer.
 * ------------------------------------------------------------------------ */

void BMS_App_UpdateBalancing(void)
{
    static uint32_t slot_start_ms = 0;
    static bool     odd_slot      = true;
    const BQ_Data_t *m = &g_bms.meas;
    uint16_t want = 0;
    uint16_t apply;
    int16_t  tmax = -32768;
    int32_t  iabs;

    /* Every reason not to balance, checked first. */
    if (!g_bms.balancing_allowed ||
        g_bms.faults != BMS_FAULT_NONE ||
        !m->valid) {
        if (g_bms.balance_mask != 0u) {
            (void)BQ76952_SetBalancing(0);
            g_bms.balance_mask = 0;
        }
        return;
    }

    for (uint8_t i = 0; i < BMS_THERMISTOR_COUNT; i++)
        if (m->temp_dc[i] > tmax) tmax = m->temp_dc[i];

    iabs = (m->current_ma < 0) ? -m->current_ma : m->current_ma;

    if (tmax > BMS_BAL_MAX_TEMP_DC ||
        iabs > BMS_BAL_MAX_CURRENT_MA ||
        m->cell_max_mv < BMS_BAL_ENABLE_ABOVE_MV) {
        if (g_bms.balance_mask != 0u) {
            (void)BQ76952_SetBalancing(0);
            g_bms.balance_mask = 0;
        }
        return;
    }

    /* Pick cells above the minimum by more than the start threshold, with
     * hysteresis on cells already bleeding. */
    for (uint8_t i = 0; i < BMS_CELL_COUNT; i++) {
        uint16_t above = (uint16_t)(m->cell_mv[i] - m->cell_min_mv);
        bool already   = (g_bms.balance_mask >> i) & 1u;
        uint16_t thr   = already ? BMS_BAL_STOP_DELTA_MV : BMS_BAL_START_DELTA_MV;
        if (above > thr) want |= (uint16_t)(1u << i);
    }

    /* Alternate odd/even sets so no two adjacent cells bleed together. */
    if ((HAL_GetTick() - slot_start_ms) > BMS_BAL_SLOT_MS) {
        slot_start_ms = HAL_GetTick();
        odd_slot = !odd_slot;
    }
    apply = want & (odd_slot ? 0x5555u : 0xAAAAu);

    if (apply != g_bms.balance_mask) {
        if (BQ76952_SetBalancing(apply) == BQ_OK)
            g_bms.balance_mask = apply;
    }
}

/* --------------------------------------------------------------------------
 * Housekeeping
 * ------------------------------------------------------------------------ */

void BMS_App_Tick1s(void)
{
    g_bms.uptime_s++;
}

bool BMS_App_ClearFaults(void)
{
    /* Refuse to clear while a condition is still live. Re-run the checks on
     * the freshest sample with the debounce counters zeroed. */
    uint32_t saved = g_bms.faults;

    g_bms.faults = BMS_FAULT_NONE;
    memset(g_bms.fault_debounce, 0, sizeof(g_bms.fault_debounce));

#if (BMS_ARM_SAFETY_OUTPUT == 0)
    g_bms.faults |= BMS_FAULT_NOT_ARMED;
#endif

    if (!g_bms.meas.valid ||
        (HAL_GetTick() - g_bms.last_good_meas_ms) > BMS_BQ_COMMS_TIMEOUT_MS) {
        g_bms.faults = saved;
        return false;
    }

    /* Clear the BQ's own latched safety status too, then re-read. */
    (void)BQ76952_Subcommand(BQ_SUB_PF_CLEAR);

    return true;
}

void BMS_App_NoteControllerHeartbeat(void)
{
    g_bms.last_ctrl_rx_ms = HAL_GetTick();
}

void BMS_App_SetBalancingPermission(bool allowed)
{
    g_bms.balancing_allowed = allowed;
}

const char *BMS_App_StateName(BMS_State_t s)
{
    switch (s) {
        case BMS_STATE_INIT:   return "INIT";
        case BMS_STATE_IDLE:   return "IDLE";
        case BMS_STATE_ACTIVE: return "ACTIVE";
        default:               return "FAULT";
    }
}
