/**
  ******************************************************************************
  * @file    bq76952.c
  * @brief   Blocking I2C driver for the TI BQ76952.
  *
  * Protocol summary (SLUAA11B):
  *  - Direct command:  write [cmd], repeated start, read N bytes (little endian)
  *  - Subcommand:      write [0x3E][lo][hi], wait, then read 0x40.. if it
  *                     returns data. Length is at 0x61, checksum at 0x60.
  *  - RAM write:       write [0x3E][addr_lo][addr_hi][data...] then
  *                     [0x60][checksum][length]. Checksum is the 1's complement
  *                     of the byte sum of addr_lo+addr_hi+data. Length counts
  *                     the two address bytes, the data, the checksum and the
  *                     length byte itself (= nbytes + 4).
  ******************************************************************************
  */
#include "bq76952.h"
#include <string.h>

#define BQ_I2C_TIMEOUT_MS       25u
#define BQ_I2C_RETRIES          3u
#define BQ_SUBCMD_DELAY_MS      2u
#define BQ_CFGUPDATE_DELAY_MS   5u
#define BQ_DEVICE_NUMBER        0x7695u

static I2C_HandleTypeDef *s_hi2c = NULL;

/* ==========================================================================
 * Low level I2C with retry. STM32F1 I2C is famously fragile; on a repeated
 * failure we reset the peripheral rather than let the bus stay wedged.
 * ========================================================================== */

static void BQ_RecoverBus(void)
{
    HAL_I2C_DeInit(s_hi2c);
    HAL_Delay(1);
    HAL_I2C_Init(s_hi2c);
}

BQ_Status_t BQ76952_DirectRead(uint8_t cmd, uint8_t *buf, uint8_t len)
{
    if (s_hi2c == NULL || buf == NULL) return BQ_ERR_PARAM;

    for (uint8_t attempt = 0; attempt < BQ_I2C_RETRIES; attempt++) {
        if (HAL_I2C_Master_Transmit(s_hi2c, BQ_I2C_ADDR_8BIT, &cmd, 1,
                                    BQ_I2C_TIMEOUT_MS) == HAL_OK) {
            if (HAL_I2C_Master_Receive(s_hi2c, BQ_I2C_ADDR_8BIT, buf, len,
                                       BQ_I2C_TIMEOUT_MS) == HAL_OK) {
                return BQ_OK;
            }
        }
        BQ_RecoverBus();
    }
    return BQ_ERR_I2C;
}

BQ_Status_t BQ76952_DirectWrite(uint8_t cmd, const uint8_t *buf, uint8_t len)
{
    uint8_t tx[34];
    if (s_hi2c == NULL || len > 32u) return BQ_ERR_PARAM;

    tx[0] = cmd;
    if (len > 0u && buf != NULL) memcpy(&tx[1], buf, len);

    for (uint8_t attempt = 0; attempt < BQ_I2C_RETRIES; attempt++) {
        if (HAL_I2C_Master_Transmit(s_hi2c, BQ_I2C_ADDR_8BIT, tx,
                                    (uint16_t)(len + 1u),
                                    BQ_I2C_TIMEOUT_MS) == HAL_OK) {
            return BQ_OK;
        }
        BQ_RecoverBus();
    }
    return BQ_ERR_I2C;
}

/* Read a 16-bit direct command. */
static BQ_Status_t BQ_ReadU16(uint8_t cmd, uint16_t *out)
{
    uint8_t b[2];
    BQ_Status_t st = BQ76952_DirectRead(cmd, b, 2);
    if (st == BQ_OK) *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return st;
}

static BQ_Status_t BQ_ReadU8(uint8_t cmd, uint8_t *out)
{
    return BQ76952_DirectRead(cmd, out, 1);
}

/* ==========================================================================
 * Subcommands
 * ========================================================================== */

BQ_Status_t BQ76952_Subcommand(uint16_t subcmd)
{
    uint8_t d[2] = { (uint8_t)(subcmd & 0xFFu), (uint8_t)(subcmd >> 8) };
    BQ_Status_t st = BQ76952_DirectWrite(BQ_CMD_SUBCMD_LOW, d, 2);
    HAL_Delay(BQ_SUBCMD_DELAY_MS);
    return st;
}

BQ_Status_t BQ76952_SubcommandRead(uint16_t subcmd, uint8_t *buf, uint8_t len)
{
    BQ_Status_t st = BQ76952_Subcommand(subcmd);
    if (st != BQ_OK) return st;
    return BQ76952_DirectRead(BQ_CMD_TRANSFER_BUFFER, buf, len);
}

/* ==========================================================================
 * Data-memory (RAM) access
 * ========================================================================== */

static uint8_t BQ_Checksum(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(~sum);
}

static BQ_Status_t BQ_RamWriteRaw(uint16_t addr, const uint8_t *data,
                                  uint8_t nbytes)
{
    uint8_t payload[6];
    uint8_t tail[2];
    BQ_Status_t st;

    if (nbytes == 0u || nbytes > 4u) return BQ_ERR_PARAM;

    payload[0] = (uint8_t)(addr & 0xFFu);
    payload[1] = (uint8_t)(addr >> 8);
    memcpy(&payload[2], data, nbytes);

    st = BQ76952_DirectWrite(BQ_CMD_SUBCMD_LOW, payload,
                             (uint8_t)(2u + nbytes));
    if (st != BQ_OK) return st;

    tail[0] = BQ_Checksum(payload, (uint8_t)(2u + nbytes));
    tail[1] = (uint8_t)(nbytes + 4u);

    st = BQ76952_DirectWrite(BQ_CMD_BUFFER_CHECKSUM, tail, 2);
    HAL_Delay(BQ_SUBCMD_DELAY_MS);
    return st;
}

BQ_Status_t BQ76952_RamWrite(uint16_t addr, uint32_t value, uint8_t nbytes)
{
    uint8_t d[4];
    d[0] = (uint8_t)(value & 0xFFu);
    d[1] = (uint8_t)((value >> 8) & 0xFFu);
    d[2] = (uint8_t)((value >> 16) & 0xFFu);
    d[3] = (uint8_t)((value >> 24) & 0xFFu);
    return BQ_RamWriteRaw(addr, d, nbytes);
}

BQ_Status_t BQ76952_RamWriteFloat(uint16_t addr, float value)
{
    uint8_t d[4];
    memcpy(d, &value, 4);       /* Cortex-M3 is little endian, as is the BQ */
    return BQ_RamWriteRaw(addr, d, 4);
}

BQ_Status_t BQ76952_RamRead(uint16_t addr, uint8_t *buf, uint8_t nbytes)
{
    uint8_t a[2] = { (uint8_t)(addr & 0xFFu), (uint8_t)(addr >> 8) };
    BQ_Status_t st = BQ76952_DirectWrite(BQ_CMD_SUBCMD_LOW, a, 2);
    if (st != BQ_OK) return st;
    HAL_Delay(BQ_SUBCMD_DELAY_MS);
    return BQ76952_DirectRead(BQ_CMD_TRANSFER_BUFFER, buf, nbytes);
}


static BQ_Status_t BQ_RamWriteVerified(uint16_t addr, uint32_t value,
                                       uint8_t nbytes)
{
    uint8_t rb[4] = {0};
    uint32_t readback = 0;
    BQ_Status_t st;

    st = BQ76952_RamWrite(addr, value, nbytes);
    if (st != BQ_OK) return st;

    st = BQ76952_RamRead(addr, rb, nbytes);
    if (st != BQ_OK) return st;

    for (uint8_t i = 0; i < nbytes; i++) readback |= ((uint32_t)rb[i]) << (8u * i);

    /* Mask the comparison to the written width. */
    if (nbytes < 4u) {
        uint32_t mask = (1UL << (8u * nbytes)) - 1UL;
        if ((readback & mask) != (value & mask)) return BQ_ERR_VERIFY;
    } else if (readback != value) {
        return BQ_ERR_VERIFY;
    }
    return BQ_OK;
}

static BQ_Status_t BQ_RamWriteFloatVerified(uint16_t addr, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, 4);
    if (BQ76952_RamWriteFloat(addr, value) != BQ_OK) return BQ_ERR_I2C;
    {
        uint8_t rb[4] = {0};
        uint32_t back = 0;
        if (BQ76952_RamRead(addr, rb, 4) != BQ_OK) return BQ_ERR_I2C;
        for (uint8_t i = 0; i < 4u; i++) back |= ((uint32_t)rb[i]) << (8u * i);
        if (back != bits) return BQ_ERR_VERIFY;
    }
    return BQ_OK;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

void BQ76952_Attach(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;
}

BQ_Status_t BQ76952_Probe(void)
{
    uint8_t b[2] = {0};
    uint16_t devnum;

    if (s_hi2c == NULL) return BQ_ERR_PARAM;

    if (HAL_I2C_IsDeviceReady(s_hi2c, BQ_I2C_ADDR_8BIT, 3, 50) != HAL_OK)
        return BQ_ERR_I2C;

    if (BQ76952_SubcommandRead(BQ_SUB_DEVICE_NUMBER, b, 2) != BQ_OK)
        return BQ_ERR_I2C;

    devnum = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return (devnum == BQ_DEVICE_NUMBER) ? BQ_OK : BQ_ERR_DEVICE_ID;
}

static BQ_Status_t BQ_EnterConfigUpdate(void)
{
    uint16_t bs = 0;
    if (BQ76952_Subcommand(BQ_SUB_SET_CFGUPDATE) != BQ_OK) return BQ_ERR_I2C;
    HAL_Delay(BQ_CFGUPDATE_DELAY_MS);
    if (BQ_ReadU16(BQ_CMD_BATTERY_STATUS, &bs) != BQ_OK) return BQ_ERR_I2C;
    return (bs & BQ_BATSTAT_CFGUPDATE) ? BQ_OK : BQ_ERR_CFGUPDATE;
}

static BQ_Status_t BQ_ExitConfigUpdate(void)
{
    uint16_t bs = 0;
    if (BQ76952_Subcommand(BQ_SUB_EXIT_CFGUPDATE) != BQ_OK) return BQ_ERR_I2C;
    HAL_Delay(BQ_CFGUPDATE_DELAY_MS);
    if (BQ_ReadU16(BQ_CMD_BATTERY_STATUS, &bs) != BQ_OK) return BQ_ERR_I2C;
    return (bs & BQ_BATSTAT_CFGUPDATE) ? BQ_ERR_CFGUPDATE : BQ_OK;
}

BQ_Status_t BQ76952_Configure(void)
{
    BQ_Status_t st;
    float cc_gain, capacity_gain;

    st = BQ_EnterConfigUpdate();
    if (st != BQ_OK) return st;

    /* --- Cell count ----------------------------------------------------- */
    st = BQ_RamWriteVerified(BQ_REG_VCELL_MODE, 0x0000u, 2);
    if (st != BQ_OK) goto fail;

    /* --- Current calibration for the 0.75 mOhm shunt --------------------
     * CC Gain      = 7.4768 / R_sense[mOhm]
     * Capacity Gain = CC Gain * 298261.6178                               */
    cc_gain       = 7.4768f / BMS_SHUNT_MILLIOHM;      /* = 9.96907 */
    capacity_gain = cc_gain * 298261.6178f;            /* = 2973391 */

    st = BQ_RamWriteFloatVerified(BQ_REG_CC_GAIN, cc_gain);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteFloatVerified(BQ_REG_CAPACITY_GAIN, capacity_gain);
    if (st != BQ_OK) goto fail;

    /* --- Measurement units ----------------------------------------------
     * 0x06 matches TI's reference driver: temperature reported in 0.1 K.
     * See the current-range warning in bms_config.h before changing this. */
    st = BQ_RamWriteVerified(BQ_REG_DA_CONFIGURATION, 0x06u, 1);
    if (st != BQ_OK) goto fail;

    /* --- Five thermistors -----------------------------------------------
     * TS1, TS2, TS3 are dedicated. The other two NTCs sit on DCHG and DDSG,
     * which have to be re-assigned from their default FET-driver function. */
    st = BQ_RamWriteVerified(BQ_REG_TS1_CONFIG,  BQ_PIN_CFG_THERMISTOR, 1);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteVerified(BQ_REG_TS2_CONFIG,  BQ_PIN_CFG_THERMISTOR, 1);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteVerified(BQ_REG_TS3_CONFIG,  BQ_PIN_CFG_THERMISTOR, 1);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteVerified(BQ_REG_DCHG_PIN_CONFIG, BQ_PIN_CFG_THERMISTOR, 1);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteVerified(BQ_REG_DDSG_PIN_CONFIG, BQ_PIN_CFG_THERMISTOR, 1);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteVerified(BQ_REG_HDQ_PIN_CONFIG,  BQ_PIN_CFG_UNUSED, 1);
    if (st != BQ_OK) goto fail;

    /* ALERT pin as an active-low interrupt to the STM32. */
    st = BQ_RamWriteVerified(BQ_REG_ALERT_PIN_CONFIG, BQ_PIN_CFG_ALERT_IRQ, 1);
    if (st != BQ_OK) goto fail;

    /* --- Protection thresholds ------------------------------------------
     * CUV/COV are 1-byte registers with a 50.6 mV LSB.
     * OCC/OCD are 1-byte registers with a 2 mV LSB measured ACROSS THE SHUNT,
     * so a current threshold converts as:  mV = I[A] * R[mOhm] / 1000 * 1000
     *                                         = I[A] * 0.75 mV per amp.    */
    st = BQ_RamWriteVerified(BQ_REG_CUV_THRESHOLD,
                             (uint32_t)(BMS_CELL_UV_WARN_MV / 50.6f), 1);
    if (st != BQ_OK) goto fail;
    st = BQ_RamWriteVerified(BQ_REG_COV_THRESHOLD,
                             (uint32_t)(BMS_CELL_OV_WARN_MV / 50.6f), 1);
    if (st != BQ_OK) goto fail;

    /* 60 A charge  -> 60 * 0.75 = 45 mV -> 45/2 = 22 counts (44 mV = 58.7 A) */
    st = BQ_RamWriteVerified(BQ_REG_OCC_THRESHOLD,
        (uint32_t)((BMS_CURRENT_CHG_LIMIT_MA / 1000.0f) * BMS_SHUNT_MILLIOHM / 2.0f), 1);
    if (st != BQ_OK) goto fail;

    /* 120 A discharge -> 90 mV -> 45 counts */
    st = BQ_RamWriteVerified(BQ_REG_OCD1_THRESHOLD,
        (uint32_t)((-BMS_CURRENT_DSG_LIMIT_MA / 1000.0f) * BMS_SHUNT_MILLIOHM / 2.0f), 1);
    if (st != BQ_OK) goto fail;

    /* --- Leave configuration mode --------------------------------------- */
    st = BQ_ExitConfigUpdate();
    if (st != BQ_OK) return st;

    /* Start with all balancing off. */
    (void)BQ76952_SetBalancing(0x0000u);
    return BQ_OK;

fail:
    (void)BQ_ExitConfigUpdate();   /* never leave the device stuck in CFGUPDATE */
    return st;
}

/* ==========================================================================
 * Measurement
 * ========================================================================== */

/* Temperature direct commands return 0.1 K with DA Configuration = 0x06.
 * deci-degC = raw - 2732 */
static int16_t BQ_KelvinToDeciC(uint16_t raw)
{
    return (int16_t)((int32_t)raw - 2732);
}

BQ_Status_t BQ76952_ReadAll(BQ_Data_t *out)
{
    static const uint8_t temp_cmds[BMS_THERMISTOR_COUNT] = {
        BQ_CMD_TS1_TEMPERATURE,
        BQ_CMD_TS2_TEMPERATURE,
        BQ_CMD_TS3_TEMPERATURE,
        BQ_CMD_DCHG_TEMPERATURE,
        BQ_CMD_DDSG_TEMPERATURE
    };
    uint16_t raw;
    uint32_t sum = 0;
    BQ_Status_t st;

    if (out == NULL) return BQ_ERR_PARAM;
    out->valid = false;

    /* --- cell voltages (mV, direct) ------------------------------------- */
    out->cell_min_mv = 0xFFFFu;
    out->cell_max_mv = 0u;
    out->cell_min_idx = 0u;
    out->cell_max_idx = 0u;

    for (uint8_t i = 0; i < BMS_CELL_COUNT; i++) {
        st = BQ_ReadU16(BQ_CMD_CELL_VOLTAGE(i + 1u), &raw);
        if (st != BQ_OK) return st;
        out->cell_mv[i] = raw;
        sum += raw;
        if (raw < out->cell_min_mv) { out->cell_min_mv = raw; out->cell_min_idx = i; }
        if (raw > out->cell_max_mv) { out->cell_max_mv = raw; out->cell_max_idx = i; }
    }
    out->cell_sum_mv  = sum;
    out->cell_delta_mv = (uint16_t)(out->cell_max_mv - out->cell_min_mv);

    /* --- stack voltage --------------------------------------------------- */
    st = BQ_ReadU16(BQ_CMD_STACK_VOLTAGE, &raw);
    if (st != BQ_OK) return st;
    out->stack_mv = (uint32_t)(raw * BQ_STACK_V_LSB_MV);

    /* --- current --------------------------------------------------------- */
    st = BQ_ReadU16(BQ_CMD_CC2_CURRENT, &raw);
    if (st != BQ_OK) return st;
    out->current_ma = (int32_t)((int16_t)raw * BQ_CURRENT_LSB_MA);

    /* --- temperatures ---------------------------------------------------- */
    for (uint8_t i = 0; i < BMS_THERMISTOR_COUNT; i++) {
        st = BQ_ReadU16(temp_cmds[i], &raw);
        if (st != BQ_OK) return st;
        out->temp_dc[i] = BQ_KelvinToDeciC(raw);
    }
    st = BQ_ReadU16(BQ_CMD_INT_TEMPERATURE, &raw);
    if (st != BQ_OK) return st;
    out->internal_temp_dc = BQ_KelvinToDeciC(raw);

    /* --- status ---------------------------------------------------------- */
    st = BQ_ReadU8(BQ_CMD_SAFETY_STATUS_A, &out->safety_status_a);
    if (st != BQ_OK) return st;
    st = BQ_ReadU8(BQ_CMD_SAFETY_STATUS_B, &out->safety_status_b);
    if (st != BQ_OK) return st;
    st = BQ_ReadU8(BQ_CMD_SAFETY_STATUS_C, &out->safety_status_c);
    if (st != BQ_OK) return st;
    st = BQ_ReadU16(BQ_CMD_BATTERY_STATUS, &out->battery_status);
    if (st != BQ_OK) return st;

    (void)BQ76952_GetBalancing(&out->balancing_mask);

    out->timestamp_ms = HAL_GetTick();
    out->valid = true;
    return BQ_OK;
}

/* ==========================================================================
 * Balancing
 * ========================================================================== */

BQ_Status_t BQ76952_SetBalancing(uint16_t cell_mask)
{
    uint8_t payload[4];
    uint8_t tail[2];
    BQ_Status_t st;

    payload[0] = (uint8_t)(BQ_SUB_CB_ACTIVE_CELLS & 0xFFu);
    payload[1] = (uint8_t)(BQ_SUB_CB_ACTIVE_CELLS >> 8);
    payload[2] = (uint8_t)(cell_mask & 0xFFu);
    payload[3] = (uint8_t)(cell_mask >> 8);

    st = BQ76952_DirectWrite(BQ_CMD_SUBCMD_LOW, payload, 4);
    if (st != BQ_OK) return st;

    tail[0] = BQ_Checksum(payload, 4);
    tail[1] = 6u;               /* 2 addr + 2 data + checksum + length */

    st = BQ76952_DirectWrite(BQ_CMD_BUFFER_CHECKSUM, tail, 2);
    HAL_Delay(BQ_SUBCMD_DELAY_MS);
    return st;
}

BQ_Status_t BQ76952_GetBalancing(uint16_t *cell_mask)
{
    uint8_t b[2] = {0};
    BQ_Status_t st = BQ76952_SubcommandRead(BQ_SUB_CB_ACTIVE_CELLS, b, 2);
    if (st == BQ_OK && cell_mask != NULL)
        *cell_mask = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return st;
}

const char *BQ76952_StatusName(BQ_Status_t s)
{
    switch (s) {
        case BQ_OK:            return "OK";
        case BQ_ERR_I2C:       return "I2C";
        case BQ_ERR_TIMEOUT:   return "TIMEOUT";
        case BQ_ERR_VERIFY:    return "VERIFY";
        case BQ_ERR_DEVICE_ID: return "DEVID";
        case BQ_ERR_CFGUPDATE: return "CFGUPD";
        default:               return "PARAM";
    }
}
