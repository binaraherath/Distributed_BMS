/**
  ******************************************************************************
  * @file    bq76952.h
  * @brief   Blocking I2C driver for the TI BQ76952 battery monitor.
  ******************************************************************************
  */
#ifndef BQ76952_H
#define BQ76952_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"
#include "bms_config.h"
#include "bq76952_regs.h"

typedef enum {
    BQ_OK = 0,
    BQ_ERR_I2C,          /* HAL transfer failed / NAK                     */
    BQ_ERR_TIMEOUT,
    BQ_ERR_VERIFY,       /* read-back after write did not match           */
    BQ_ERR_DEVICE_ID,    /* DEVICE_NUMBER was not 0x7695                  */
    BQ_ERR_CFGUPDATE,    /* device would not enter/leave CONFIG_UPDATE    */
    BQ_ERR_PARAM
} BQ_Status_t;

/** Snapshot of everything the driver measures in one pass. */
typedef struct {
    uint16_t cell_mv[BMS_CELL_COUNT];    /* per-cell voltage, mV            */
    uint32_t stack_mv;                   /* stack voltage from BQ, mV       */
    uint32_t cell_sum_mv;                /* sum of cells, mV                */
    int32_t  current_ma;                 /* + = charge, - = discharge       */
    int16_t  temp_dc[BMS_THERMISTOR_COUNT]; /* deci-degC                    */
    int16_t  internal_temp_dc;           /* BQ die temperature, deci-degC   */

    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint8_t  cell_min_idx;               /* 0-based                         */
    uint8_t  cell_max_idx;
    uint16_t cell_delta_mv;

    uint8_t  safety_status_a;
    uint8_t  safety_status_b;
    uint8_t  safety_status_c;
    uint16_t battery_status;
    uint16_t balancing_mask;             /* cells currently bleeding        */

    bool     valid;                      /* whole snapshot is trustworthy   */
    uint32_t timestamp_ms;
} BQ_Data_t;

/* --- lifecycle ---------------------------------------------------------- */

void        BQ76952_Attach(I2C_HandleTypeDef *hi2c);

/** Probe the device: reads DEVICE_NUMBER and checks it is 0x7695. */
BQ_Status_t BQ76952_Probe(void);

/**
 * Full configuration: enters CONFIG_UPDATE, writes calibration, pin
 * assignments, cell count and protection thresholds, verifies each write by
 * reading it back, then exits CONFIG_UPDATE.
 * Returns BQ_ERR_VERIFY if any register did not take the written value.
 */
BQ_Status_t BQ76952_Configure(void);

/* --- measurement -------------------------------------------------------- */

/** One complete measurement pass. Populates *out. */
BQ_Status_t BQ76952_ReadAll(BQ_Data_t *out);

/* --- balancing ---------------------------------------------------------- */

/**
 * Set which cells bleed. Bit 0 = cell 1 ... bit 15 = cell 16.
 * Adjacent bits are silently masked off by the caller because
 * the BQ cannot bleed neighbouring cells at the same time.
 */
BQ_Status_t BQ76952_SetBalancing(uint16_t cell_mask);
BQ_Status_t BQ76952_GetBalancing(uint16_t *cell_mask);

/* --- low level (exposed for bring-up and debugging) --------------------- */
BQ_Status_t BQ76952_DirectRead(uint8_t cmd, uint8_t *buf, uint8_t len);
BQ_Status_t BQ76952_DirectWrite(uint8_t cmd, const uint8_t *buf, uint8_t len);
BQ_Status_t BQ76952_Subcommand(uint16_t subcmd);
BQ_Status_t BQ76952_SubcommandRead(uint16_t subcmd, uint8_t *buf, uint8_t len);
BQ_Status_t BQ76952_RamWrite(uint16_t addr, uint32_t value, uint8_t nbytes);
BQ_Status_t BQ76952_RamWriteFloat(uint16_t addr, float value);
BQ_Status_t BQ76952_RamRead(uint16_t addr, uint8_t *buf, uint8_t nbytes);

const char *BQ76952_StatusName(BQ_Status_t s);

#endif /* BQ76952_H */
