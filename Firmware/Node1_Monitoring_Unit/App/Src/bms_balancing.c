/**
 ******************************************************************************
 * @file    bms_balancing.c
 * @brief   Balancing policy (see header).
 ******************************************************************************
 */
#include "bms_balancing.h"

#define MASK_ODD_CELLS   0x5555u   /* cells 1,3,5..15 (bit 0 = cell 1) */
#define MASK_EVEN_CELLS  0xAAAAu   /* cells 2,4,6..16                  */

static uint16_t s_cmd_mask;        /* last mask written to the BQ      */
static uint32_t s_last_write_ms;
static uint32_t s_group_start_ms;

static uint8_t popcount16(uint16_t v)
{
    uint8_t n = 0;
    while (v) { v &= (uint16_t)(v - 1u); n++; }
    return n;
}

static void write_mask(BmsContext_t *b, uint16_t mask)
{
    if (BQ_SetBalancing(mask) == BQ_OK) {
        s_cmd_mask = mask;
        s_last_write_ms = HAL_GetTick();
    }
    /* Read back what the BQ is really doing (it may block for temperature) */
    (void)BQ_GetBalancing(&b->bal_mask);
}

void Bal_Init(void)
{
    s_cmd_mask = 0;
    s_last_write_ms = 0;
    s_group_start_ms = HAL_GetTick();
}

void Bal_Stop(BmsContext_t *b)
{
    b->bal_conditions_ok = false;
    if (s_cmd_mask != 0u || b->bal_mask != 0u) write_mask(b, 0);
}

void Bal_Update(BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    uint32_t now = HAL_GetTick();
    uint16_t want = 0, group_odd, group_even, apply;

    b->bal_conditions_ok =
        b->bal_enabled && b->bq_ready && m->valid &&
        b->faults == 0u && b->state != BMS_STATE_INIT &&
        m->current_ma >= BMS_BAL_MIN_CURRENT_MA &&
        m->temp_max_dc <= BMS_BAL_MAX_TEMP_DC &&
        m->die_dc < BMS_TEMP_DIE_FAULT_DC - 150;

    if (!b->bal_conditions_ok) {
        Bal_Stop(b);
        return;
    }

    /* Candidates: above the voltage floor and above the lowest cell by the
     * start delta (or still above the stop delta if already bleeding). */
    for (uint8_t i = 0; i < BMS_CELLS; i++) {
        bool     active = (s_cmd_mask >> i) & 1u;
        uint16_t above  = (uint16_t)(m->cell_mv[i] - m->cell_min_mv);
        uint16_t thr    = active ? BMS_BAL_STOP_DELTA_MV : BMS_BAL_START_DELTA_MV;
        if (m->cell_mv[i] >= BMS_BAL_MIN_CELL_MV && above > thr) {
            want |= (uint16_t)(1u << i);
        }
    }

    group_odd  = want & MASK_ODD_CELLS;
    group_even = want & MASK_EVEN_CELLS;

    /* Alternate groups on the timer, or early if the active one is done */
    {
        bool cur_has   = (b->bal_odd_group ? group_odd : group_even) != 0u;
        bool other_has = (b->bal_odd_group ? group_even : group_odd) != 0u;
        bool timer_up  = (now - s_group_start_ms) >= BMS_BAL_GROUP_SWAP_MS;

        if (other_has && (timer_up || !cur_has)) {
            b->bal_odd_group = !b->bal_odd_group;
            s_group_start_ms = now;
        } else if (timer_up) {
            s_group_start_ms = now;
        }
    }
    apply = b->bal_odd_group ? group_odd : group_even;

    /* Cap the number of bleeders by dropping the lowest cells first */
    while (popcount16(apply) > BMS_BAL_MAX_CELLS) {
        uint8_t  lowest = 0;
        uint16_t lowest_mv = 0xFFFFu;
        for (uint8_t i = 0; i < BMS_CELLS; i++) {
            if (((apply >> i) & 1u) && m->cell_mv[i] < lowest_mv) { lowest = i; lowest_mv = m->cell_mv[i]; }
        }
        apply &= (uint16_t)~(1u << lowest);
    }

    /* Write on change, and refresh before the BQ's 20 s interval runs out */
    if (apply != s_cmd_mask ||
        (apply != 0u && (now - s_last_write_ms) >= BMS_BAL_REFRESH_MS)) {
        write_mask(b, apply);
    } else {
        (void)BQ_GetBalancing(&b->bal_mask);
    }

    if (b->bal_mask != 0u) b->bal_seconds++;
}
