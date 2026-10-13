/**
 ******************************************************************************
 * @file    bms_balancing.h
 * @brief   Host-controlled passive balancing through the BQ76952 internal
 *          switches driving external MMBT2222A + 18 R / 5 W bleeders.
 *
 *  With the external NPN circuit two neighbouring cells share a VC filter
 *  resistor, so neighbours cannot bleed together: odd cells (1,3,5..) and
 *  even cells (2,4,6..) take turns every BMS_BAL_GROUP_SWAP_MS.
 ******************************************************************************
 */
#ifndef BMS_BALANCING_H
#define BMS_BALANCING_H

#include "bms_app.h"

void Bal_Init(void);

/** Called every BMS_TASK_BALANCE_MS. */
void Bal_Update(BmsContext_t *b);

/** Switch all bleeders off now (fault, loss of permission). */
void Bal_Stop(BmsContext_t *b);

#endif /* BMS_BALANCING_H */
