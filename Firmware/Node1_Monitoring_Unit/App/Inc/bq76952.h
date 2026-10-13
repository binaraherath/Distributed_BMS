/**
 ******************************************************************************
 * @file    bq76952.h
 * @brief   Blocking I2C driver for the TI BQ76952 16S battery monitor.
 *
 *  - Direct commands are read in blocks (one I2C transaction for all 16
 *    cells + stack + current) so the snapshot is time-consistent.
 *  - Subcommands wait for completion by polling 0x3E/0x3F and verify the
 *    transfer-buffer checksum, as described in TRM section 3.1.
 *  - Data-memory writes are verified by reading them back.
 *  - A wedged bus is recovered with 9 SCL clocks + STOP (STM32F1 errata).
 ******************************************************************************
 */
#ifndef BQ76952_H
#define BQ76952_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "stm32f1xx_hal.h"
#include "bq76952_regs.h"

#define BQ_CELLS            16u

typedef enum {
    BQ_OK = 0,
    BQ_ERR_I2C,         /* NAK / bus error                                */
    BQ_ERR_TIMEOUT,     /* subcommand did not complete                    */
    BQ_ERR_CHECKSUM,    /* transfer-buffer checksum mismatch              */
    BQ_ERR_VERIFY,      /* data-memory read-back differs                  */
    BQ_ERR_DEVICE_ID,   /* DEVICE_NUMBER != 0x7695                        */
    BQ_ERR_CFGUPDATE,   /* could not enter / leave CONFIG_UPDATE          */
    BQ_ERR_PARAM
} BQ_Result_t;

/** Raw measurement snapshot, already scaled to mV / mA / 0.1 degC. */
typedef struct {
    int16_t  cell_mv[BQ_CELLS];
    int32_t  stack_mv;
    int32_t  pack_mv;
    int32_t  current_ma;        /* BQ sign: + = charge                     */
    int16_t  die_dc;            /* internal temperature                    */
    int16_t  ts1_dc;
    int16_t  ts3_dc;
    int16_t  dchg_dc;
    int16_t  ddsg_dc;
} BQ_Measurement_t;

/** Safety / status registers. */
typedef struct {
    uint8_t  alert_a, status_a;
    uint8_t  alert_b, status_b;
    uint8_t  alert_c, status_c;
    uint16_t battery_status;
    uint16_t alarm_status;
    uint8_t  fet_status;
} BQ_Safety_t;

/** One data-memory setting for BQ_WriteConfig(). */
typedef struct {
    uint16_t    addr;
    uint32_t    value;          /* raw little-endian bits (floats via memcpy) */
    uint8_t     size;           /* 1, 2 or 4 bytes                         */
    const char *name;           /* for the debug log                       */
} BQ_ConfigEntry_t;

/* --- lifecycle ---------------------------------------------------------- */
void        BQ_Init(I2C_HandleTypeDef *hi2c, GPIO_TypeDef *rst_port, uint16_t rst_pin);

/** Short high pulse on RST_SHUT (< 1 s): wakes the device from SLEEP /
 *  DEEPSLEEP and resets its digital core. Never long enough to SHUTDOWN. */
void        BQ_WakePulse(uint32_t pulse_ms);

BQ_Result_t BQ_Probe(uint16_t *device_number, uint16_t *fw_version);

/** Enter CONFIG_UPDATE, write + verify every entry, exit CONFIG_UPDATE.
 *  On failure *failed_index (if not NULL) gets the offending entry. */
BQ_Result_t BQ_WriteConfig(const BQ_ConfigEntry_t *table, size_t count,
                           size_t *failed_index);

/* --- measurement -------------------------------------------------------- */
BQ_Result_t BQ_ReadMeasurements(BQ_Measurement_t *m);
BQ_Result_t BQ_ReadSafety(BQ_Safety_t *s);

/** Accumulated passed charge (DASTATUS6). q_raw is in userAh x 2^32. */
BQ_Result_t BQ_ReadPassedCharge(int64_t *q_raw, uint32_t *time_s);

/* --- alarm / ALERT pin -------------------------------------------------- */
BQ_Result_t BQ_ClearAlarms(uint16_t bits);
BQ_Result_t BQ_SetAlarmEnable(uint16_t mask);

/* --- balancing ---------------------------------------------------------- */
BQ_Result_t BQ_SetBalancing(uint16_t cell_mask);    /* bit0 = cell 1     */
BQ_Result_t BQ_GetBalancing(uint16_t *cell_mask);

/* --- low level ---------------------------------------------------------- */
BQ_Result_t BQ_DirectRead(uint8_t cmd, uint8_t *buf, uint16_t len);
BQ_Result_t BQ_DirectWrite(uint8_t cmd, const uint8_t *buf, uint16_t len);
BQ_Result_t BQ_Subcommand(uint16_t sub);
BQ_Result_t BQ_SubcommandRead(uint16_t sub, uint8_t *buf, uint8_t len);
BQ_Result_t BQ_RamRead(uint16_t addr, uint8_t *buf, uint8_t len);
BQ_Result_t BQ_RamWrite(uint16_t addr, const uint8_t *data, uint8_t len);

/** Clock the bus free and re-initialise the I2C peripheral. */
void        BQ_BusRecover(void);

uint32_t    BQ_ErrorCount(void);
const char *BQ_ResultName(BQ_Result_t r);

#endif /* BQ76952_H */
