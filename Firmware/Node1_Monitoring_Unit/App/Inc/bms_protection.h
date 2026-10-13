/**
 ******************************************************************************
 * @file    bms_protection.h
 * @brief   Two-layer protection: debounced MCU checks + BQ76952 hardware
 *          protections mirrored into one latched fault word. Also derives
 *          the charge / discharge current limits sent to the controller.
 ******************************************************************************
 */
#ifndef BMS_PROTECTION_H
#define BMS_PROTECTION_H

#include "bms_app.h"

/** Warning bits owned by this module (the rest are set by the app). */
#define PROT_WARN_MASK  (BMS_WRN_CELL_HIGH | BMS_WRN_CELL_LOW | BMS_WRN_TEMP_HIGH | \
                         BMS_WRN_TEMP_LOW  | BMS_WRN_IMBALANCE | BMS_WRN_CURRENT_HIGH | \
                         BMS_WRN_BQ_ALERT  | BMS_WRN_SOC_LOW)

void     Prot_ResetDebounce(void);

/** Run after every good measurement: latches faults, refreshes warnings. */
void     Prot_Evaluate(BmsContext_t *b);

/** Fault bits whose condition is present right now (no debounce).
 *  Used to refuse a clear request while danger is still there. */
uint32_t Prot_ActiveConditions(const BmsContext_t *b);

/** Charge / discharge limits with temperature and voltage tapering. */
void     Prot_UpdateLimits(BmsContext_t *b);

#endif /* BMS_PROTECTION_H */
