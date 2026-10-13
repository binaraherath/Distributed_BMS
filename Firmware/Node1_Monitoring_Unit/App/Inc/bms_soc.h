/**
 ******************************************************************************
 * @file    bms_soc.h
 * @brief   State of charge: OCV start-up value, coulomb counting from the
 *          BQ76952 charge accumulator, OCV re-anchoring after long rest.
 ******************************************************************************
 */
#ifndef BMS_SOC_H
#define BMS_SOC_H

#include "bms_app.h"

void  Soc_Init(BmsContext_t *b);

/** After every measurement (100 ms). Seeds SoC from OCV on the first call,
 *  integrates current when the BQ accumulator is unavailable, handles rest
 *  re-anchoring and the full / empty anchors. */
void  Soc_Update(BmsContext_t *b);

/** Once per second with the DASTATUS6 accumulator (userAh x 2^32). */
void  Soc_OnPassedCharge(BmsContext_t *b, bool valid, int64_t q_raw);

float Soc_FromOcv(uint16_t cell_mv);

#endif /* BMS_SOC_H */
