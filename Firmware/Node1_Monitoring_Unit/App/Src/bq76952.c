/**
 ******************************************************************************
 * @file    bq76952.c
 * @brief   Blocking I2C driver for the TI BQ76952 (see bq76952.h).
 *
 *  Protocol (TRM SLUUBY2B section 3.1):
 *   Direct read   : [S addr+W cmd Sr addr+R data.. P], little endian, the
 *                   command address auto-increments for block reads.
 *   Subcommand    : write lo/hi to 0x3E/0x3F, poll 0x3E/0x3F until it reads
 *                   back the same value, read length (0x61), data (0x40..)
 *                   and checksum (0x60) - checksum = ~(lo + hi + data).
 *   Subcmd write  : write [lo hi data..] from 0x3E in one transfer, then
 *                   [checksum, length] to 0x60 (length = data + 4).
 ******************************************************************************
 */
#include "bq76952.h"
#include "bms_config.h"
#include <string.h>

#define I2C_TIMEOUT_MS          20u
#define I2C_RETRIES             3u
#define I2C_ERR_BEFORE_RECOVER  3u
#define SUBCMD_TIMEOUT_MS       25u
#define SUBCMD_WRITE_DELAY_MS   2u
#define CFGUPDATE_TIMEOUT_MS    100u
#define TEMP_KELVIN_OFFSET_DK   2732    /* 273.2 K in 0.1 K               */

static I2C_HandleTypeDef *s_i2c;
static GPIO_TypeDef      *s_rst_port;
static uint16_t           s_rst_pin;
static uint32_t           s_err_count;
static uint8_t            s_err_run;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static inline int16_t le_i16(const uint8_t *b)
{
    return (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static inline uint32_t le_u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline int16_t kelvin_to_dc(const uint8_t *b)
{
    return (int16_t)(le_i16(b) - TEMP_KELVIN_OFFSET_DK);
}

static uint8_t checksum(const uint8_t *d, uint8_t n)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum = (uint8_t)(sum + d[i]);
    return (uint8_t)~sum;
}

static void delay_us(uint32_t us)
{
    /* Rough busy wait - only used for I2C bus recovery clocks. */
    volatile uint32_t n = us * (SystemCoreClock / 4000000u);
    while (n--) { __NOP(); }
}

static void note_error(void)
{
    s_err_count++;
    if (++s_err_run >= I2C_ERR_BEFORE_RECOVER) {
        s_err_run = 0;
        BQ_BusRecover();
    }
}

/* ==========================================================================
 * Low level
 * ========================================================================== */

void BQ_Init(I2C_HandleTypeDef *hi2c, GPIO_TypeDef *rst_port, uint16_t rst_pin)
{
    s_i2c      = hi2c;
    s_rst_port = rst_port;
    s_rst_pin  = rst_pin;
    s_err_count = 0;
    s_err_run   = 0;
    if (s_rst_port != NULL) {
        HAL_GPIO_WritePin(s_rst_port, s_rst_pin, GPIO_PIN_RESET);
    }
}

void BQ_WakePulse(uint32_t pulse_ms)
{
    if (s_rst_port == NULL) return;
    if (pulse_ms > 500u) pulse_ms = 500u;   /* >= 1 s would force SHUTDOWN */
    HAL_GPIO_WritePin(s_rst_port, s_rst_pin, GPIO_PIN_SET);
    HAL_Delay(pulse_ms);
    HAL_GPIO_WritePin(s_rst_port, s_rst_pin, GPIO_PIN_RESET);
}

void BQ_BusRecover(void)
{
    GPIO_InitTypeDef g = {0};

    if (s_i2c == NULL) return;
    (void)HAL_I2C_DeInit(s_i2c);

    /* Drive both lines as open-drain GPIO and clock out a stuck slave. */
    HAL_GPIO_WritePin(BQ_I2C_SCL_PORT, BQ_I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BQ_I2C_SDA_PORT, BQ_I2C_SDA_PIN, GPIO_PIN_SET);
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin   = BQ_I2C_SCL_PIN;
    HAL_GPIO_Init(BQ_I2C_SCL_PORT, &g);
    g.Pin   = BQ_I2C_SDA_PIN;
    HAL_GPIO_Init(BQ_I2C_SDA_PORT, &g);

    for (uint8_t i = 0; i < 9u; i++) {
        HAL_GPIO_WritePin(BQ_I2C_SCL_PORT, BQ_I2C_SCL_PIN, GPIO_PIN_RESET);
        delay_us(5);
        HAL_GPIO_WritePin(BQ_I2C_SCL_PORT, BQ_I2C_SCL_PIN, GPIO_PIN_SET);
        delay_us(5);
    }
    /* STOP condition: SDA low -> high while SCL is high */
    HAL_GPIO_WritePin(BQ_I2C_SDA_PORT, BQ_I2C_SDA_PIN, GPIO_PIN_RESET);
    delay_us(5);
    HAL_GPIO_WritePin(BQ_I2C_SCL_PORT, BQ_I2C_SCL_PIN, GPIO_PIN_SET);
    delay_us(5);
    HAL_GPIO_WritePin(BQ_I2C_SDA_PORT, BQ_I2C_SDA_PIN, GPIO_PIN_SET);
    delay_us(5);

    /* STM32F1 errata: the BUSY flag can stay set - reset the whole block. */
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    (void)HAL_I2C_Init(s_i2c);          /* MspInit restores AF open-drain */
}

BQ_Result_t BQ_DirectRead(uint8_t cmd, uint8_t *buf, uint16_t len)
{
    if (s_i2c == NULL || buf == NULL || len == 0u) return BQ_ERR_PARAM;

    for (uint8_t attempt = 0; attempt < I2C_RETRIES; attempt++) {
        if (HAL_I2C_Mem_Read(s_i2c, BQ_I2C_ADDR_8BIT, cmd, I2C_MEMADD_SIZE_8BIT,
                             buf, len, I2C_TIMEOUT_MS) == HAL_OK) {
            s_err_run = 0;
            return BQ_OK;
        }
        note_error();
    }
    return BQ_ERR_I2C;
}

BQ_Result_t BQ_DirectWrite(uint8_t cmd, const uint8_t *buf, uint16_t len)
{
    if (s_i2c == NULL || buf == NULL || len == 0u) return BQ_ERR_PARAM;

    for (uint8_t attempt = 0; attempt < I2C_RETRIES; attempt++) {
        if (HAL_I2C_Mem_Write(s_i2c, BQ_I2C_ADDR_8BIT, cmd, I2C_MEMADD_SIZE_8BIT,
                              (uint8_t *)buf, len, I2C_TIMEOUT_MS) == HAL_OK) {
            s_err_run = 0;
            return BQ_OK;
        }
        note_error();
    }
    return BQ_ERR_I2C;
}

/* ==========================================================================
 * Subcommands
 * ========================================================================== */

BQ_Result_t BQ_Subcommand(uint16_t sub)
{
    uint8_t d[2] = { (uint8_t)(sub & 0xFFu), (uint8_t)(sub >> 8) };
    return BQ_DirectWrite(BQ_CMD_SUBCMD_LOW, d, 2);
}

static BQ_Result_t wait_subcommand(uint16_t sub)
{
    uint8_t  rb[2];
    uint32_t t0 = HAL_GetTick();

    do {
        if (BQ_DirectRead(BQ_CMD_SUBCMD_LOW, rb, 2) == BQ_OK &&
            rb[0] == (uint8_t)(sub & 0xFFu) && rb[1] == (uint8_t)(sub >> 8)) {
            return BQ_OK;
        }
    } while ((HAL_GetTick() - t0) < SUBCMD_TIMEOUT_MS);

    return BQ_ERR_TIMEOUT;
}

BQ_Result_t BQ_SubcommandRead(uint16_t sub, uint8_t *buf, uint8_t len)
{
    uint8_t data[34];
    uint8_t length, chk, n;
    BQ_Result_t r;

    if (buf == NULL || len == 0u || len > 32u) return BQ_ERR_PARAM;

    if ((r = BQ_Subcommand(sub)) != BQ_OK) return r;
    if ((r = wait_subcommand(sub)) != BQ_OK) return r;

    /* Length and checksum are read separately (reading them together can
     * trigger a buffer auto-increment - TRM 3.1). */
    if ((r = BQ_DirectRead(BQ_CMD_BUFFER_LENGTH, &length, 1)) != BQ_OK) return r;
    if (length < 5u || length > 36u) return BQ_ERR_CHECKSUM;
    n = (uint8_t)(length - 4u);
    if (n < len) return BQ_ERR_CHECKSUM;

    data[0] = (uint8_t)(sub & 0xFFu);
    data[1] = (uint8_t)(sub >> 8);
    if ((r = BQ_DirectRead(BQ_CMD_TRANSFER_BUFFER, &data[2], n)) != BQ_OK) return r;
    if ((r = BQ_DirectRead(BQ_CMD_BUFFER_CHECKSUM, &chk, 1)) != BQ_OK) return r;
    if (checksum(data, (uint8_t)(n + 2u)) != chk) {
        s_err_count++;
        return BQ_ERR_CHECKSUM;
    }

    memcpy(buf, &data[2], len);
    return BQ_OK;
}

/* Subcommand (or data-memory address) with a data payload. */
static BQ_Result_t subcommand_write(uint16_t sub, const uint8_t *data, uint8_t len)
{
    uint8_t tx[34];
    uint8_t tail[2];
    BQ_Result_t r;

    if (data == NULL || len == 0u || len > 32u) return BQ_ERR_PARAM;

    tx[0] = (uint8_t)(sub & 0xFFu);
    tx[1] = (uint8_t)(sub >> 8);
    memcpy(&tx[2], data, len);

    if ((r = BQ_DirectWrite(BQ_CMD_SUBCMD_LOW, tx, (uint16_t)(len + 2u))) != BQ_OK) return r;

    tail[0] = checksum(tx, (uint8_t)(len + 2u));
    tail[1] = (uint8_t)(len + 4u);
    r = BQ_DirectWrite(BQ_CMD_BUFFER_CHECKSUM, tail, 2);
    HAL_Delay(SUBCMD_WRITE_DELAY_MS);
    return r;
}

BQ_Result_t BQ_RamRead(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return BQ_SubcommandRead(addr, buf, len);
}

BQ_Result_t BQ_RamWrite(uint16_t addr, const uint8_t *data, uint8_t len)
{
    return subcommand_write(addr, data, len);
}

/* ==========================================================================
 * Probe and configuration
 * ========================================================================== */

BQ_Result_t BQ_Probe(uint16_t *device_number, uint16_t *fw_version)
{
    uint8_t b[4];
    uint16_t dn;
    BQ_Result_t r;

    if ((r = BQ_SubcommandRead(BQ_SUB_DEVICE_NUMBER, b, 2)) != BQ_OK) return r;
    dn = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    if (device_number != NULL) *device_number = dn;
    if (dn != BQ_DEVICE_NUMBER) return BQ_ERR_DEVICE_ID;

    if (fw_version != NULL) {
        /* FW_VERSION: [0..1] device no. (BE) [2..3] firmware (BE) [4..5] build */
        *fw_version = 0;
        if (BQ_SubcommandRead(BQ_SUB_FW_VERSION, b, 4) == BQ_OK) {
            *fw_version = (uint16_t)(((uint16_t)b[2] << 8) | b[3]);
        }
    }
    return BQ_OK;
}

static BQ_Result_t wait_cfgupdate(bool want_set)
{
    uint8_t  b[2];
    uint32_t t0 = HAL_GetTick();

    do {
        if (BQ_DirectRead(BQ_CMD_BATTERY_STATUS, b, 2) == BQ_OK) {
            bool set = (b[0] & BQ_BAT_CFGUPDATE) != 0u;
            if (set == want_set) return BQ_OK;
        }
        HAL_Delay(2);
    } while ((HAL_GetTick() - t0) < CFGUPDATE_TIMEOUT_MS);

    return BQ_ERR_CFGUPDATE;
}

BQ_Result_t BQ_WriteConfig(const BQ_ConfigEntry_t *table, size_t count,
                           size_t *failed_index)
{
    BQ_Result_t r;

    if (table == NULL) return BQ_ERR_PARAM;

    if ((r = BQ_Subcommand(BQ_SUB_SET_CFGUPDATE)) != BQ_OK) return r;
    HAL_Delay(3);                                   /* 2000 us typical */
    if ((r = wait_cfgupdate(true)) != BQ_OK) return r;

    for (size_t i = 0; i < count; i++) {
        const BQ_ConfigEntry_t *e = &table[i];
        uint8_t wr[4], rd[4] = {0};

        if (e->size == 0u || e->size > 4u) { r = BQ_ERR_PARAM; }
        else {
            for (uint8_t k = 0; k < e->size; k++) wr[k] = (uint8_t)(e->value >> (8u * k));
            r = BQ_RamWrite(e->addr, wr, e->size);
            if (r == BQ_OK) r = BQ_RamRead(e->addr, rd, e->size);
            if (r == BQ_OK && memcmp(wr, rd, e->size) != 0) r = BQ_ERR_VERIFY;
        }

        if (r != BQ_OK) {
            if (failed_index != NULL) *failed_index = i;
            (void)BQ_Subcommand(BQ_SUB_EXIT_CFGUPDATE); /* never leave it stuck */
            HAL_Delay(2);
            return r;
        }
    }

    if ((r = BQ_Subcommand(BQ_SUB_EXIT_CFGUPDATE)) != BQ_OK) return r;
    HAL_Delay(2);                                   /* 1000 us typical */
    return wait_cfgupdate(false);
}

/* ==========================================================================
 * Measurement
 * ========================================================================== */

BQ_Result_t BQ_ReadMeasurements(BQ_Measurement_t *m)
{
    uint8_t v[BQ_BLOCK_MEAS_LEN];
    uint8_t t[BQ_BLOCK_TEMP_LEN];
    BQ_Result_t r;

    if (m == NULL) return BQ_ERR_PARAM;

    /* 0x14..0x3B: 16 cells, stack, PACK, LD, CC2 in one transaction */
    if ((r = BQ_DirectRead(BQ_BLOCK_MEAS_START, v, sizeof(v))) != BQ_OK) return r;
    for (uint8_t i = 0; i < BQ_CELLS; i++) m->cell_mv[i] = le_i16(&v[2u * i]);
    m->stack_mv   = (int32_t)le_i16(&v[0x34 - BQ_BLOCK_MEAS_START]) * BQ_USER_V_MV;
    m->pack_mv    = (int32_t)le_i16(&v[0x36 - BQ_BLOCK_MEAS_START]) * BQ_USER_V_MV;
    m->current_ma = (int32_t)le_i16(&v[0x3A - BQ_BLOCK_MEAS_START]) * BQ_USER_A_MA;

    /* 0x68..0x7B: internal + multifunction pin temperatures (0.1 K) */
    if ((r = BQ_DirectRead(BQ_BLOCK_TEMP_START, t, sizeof(t))) != BQ_OK) return r;
    m->die_dc  = kelvin_to_dc(&t[BQ_CMD_INT_TEMPERATURE  - BQ_BLOCK_TEMP_START]);
    m->ts1_dc  = kelvin_to_dc(&t[BQ_CMD_TS1_TEMPERATURE  - BQ_BLOCK_TEMP_START]);
    m->ts3_dc  = kelvin_to_dc(&t[BQ_CMD_TS3_TEMPERATURE  - BQ_BLOCK_TEMP_START]);
    m->dchg_dc = kelvin_to_dc(&t[BQ_CMD_DCHG_TEMPERATURE - BQ_BLOCK_TEMP_START]);
    m->ddsg_dc = kelvin_to_dc(&t[BQ_CMD_DDSG_TEMPERATURE - BQ_BLOCK_TEMP_START]);
    return BQ_OK;
}

BQ_Result_t BQ_ReadSafety(BQ_Safety_t *s)
{
    uint8_t b[BQ_BLOCK_SAFETY_LEN];
    uint8_t w[2];
    BQ_Result_t r;

    if (s == NULL) return BQ_ERR_PARAM;

    if ((r = BQ_DirectRead(BQ_BLOCK_SAFETY_START, b, sizeof(b))) != BQ_OK) return r;
    s->alert_a  = b[0]; s->status_a = b[1];
    s->alert_b  = b[2]; s->status_b = b[3];
    s->alert_c  = b[4]; s->status_c = b[5];

    if ((r = BQ_DirectRead(BQ_CMD_BATTERY_STATUS, w, 2)) != BQ_OK) return r;
    s->battery_status = (uint16_t)le_i16(w);
    if ((r = BQ_DirectRead(BQ_CMD_ALARM_STATUS, w, 2)) != BQ_OK) return r;
    s->alarm_status = (uint16_t)le_i16(w);
    return BQ_DirectRead(BQ_CMD_FET_STATUS, &s->fet_status, 1);
}

BQ_Result_t BQ_ReadPassedCharge(int64_t *q_raw, uint32_t *time_s)
{
    uint8_t b[12];
    BQ_Result_t r = BQ_SubcommandRead(BQ_SUB_DASTATUS6, b, sizeof(b));
    if (r != BQ_OK) return r;

    /* [0..3] integer userAh (I4), [4..7] fraction (U4), [8..11] seconds */
    if (q_raw != NULL) {
        *q_raw = ((int64_t)(int32_t)le_u32(&b[0]) * 4294967296LL) + (int64_t)le_u32(&b[4]);
    }
    if (time_s != NULL) *time_s = le_u32(&b[8]);
    return BQ_OK;
}

/* ==========================================================================
 * Alarm and balancing
 * ========================================================================== */

BQ_Result_t BQ_ClearAlarms(uint16_t bits)
{
    uint8_t d[2] = { (uint8_t)(bits & 0xFFu), (uint8_t)(bits >> 8) };
    return BQ_DirectWrite(BQ_CMD_ALARM_STATUS, d, 2);
}

BQ_Result_t BQ_SetAlarmEnable(uint16_t mask)
{
    uint8_t d[2] = { (uint8_t)(mask & 0xFFu), (uint8_t)(mask >> 8) };
    return BQ_DirectWrite(BQ_CMD_ALARM_ENABLE, d, 2);
}

BQ_Result_t BQ_SetBalancing(uint16_t cell_mask)
{
    uint8_t d[2] = { (uint8_t)(cell_mask & 0xFFu), (uint8_t)(cell_mask >> 8) };
    return subcommand_write(BQ_SUB_CB_ACTIVE_CELLS, d, 2);
}

BQ_Result_t BQ_GetBalancing(uint16_t *cell_mask)
{
    uint8_t b[2];
    BQ_Result_t r = BQ_SubcommandRead(BQ_SUB_CB_ACTIVE_CELLS, b, 2);
    if (r == BQ_OK && cell_mask != NULL) *cell_mask = (uint16_t)le_i16(b);
    return r;
}

uint32_t BQ_ErrorCount(void)
{
    return s_err_count;
}

const char *BQ_ResultName(BQ_Result_t r)
{
    switch (r) {
        case BQ_OK:            return "OK";
        case BQ_ERR_I2C:       return "I2C";
        case BQ_ERR_TIMEOUT:   return "TIMEOUT";
        case BQ_ERR_CHECKSUM:  return "CHECKSUM";
        case BQ_ERR_VERIFY:    return "VERIFY";
        case BQ_ERR_DEVICE_ID: return "DEVICE_ID";
        case BQ_ERR_CFGUPDATE: return "CFGUPDATE";
        default:               return "PARAM";
    }
}
