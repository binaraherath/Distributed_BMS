/**
 ******************************************************************************
 * @file    bms_can_protocol.h
 * @brief   Distributed BMS - CAN message map shared by all three nodes.
 *
 *  Single source of truth for IDs, periods, scaling, bit fields and the
 *  end-to-end (E2E) protection used on the isolated CAN bus.
 *  Included by:  Node 1 (STM32, C), Node 2 (STM32, C), Node 3 (ESP32, C++).
 *
 *  Bus      : CAN 2.0A, 11-bit IDs, 500 kbit/s, all frames DLC = 8
 *  Endian   : little endian for every multi-byte field
 *  ID plan  : 0x1xx Node 1 (monitoring)   0x2xx Node 2 (controller)
 *             0x3xx Node 3 (gateway).     Lower ID = higher priority.
 *  E2E      : frames marked [E2E] carry an alive counter in byte 6 and a
 *             CRC-8 (SAE J1850) in byte 7, computed over the two ID bytes
 *             and data bytes 0..6. The receiver rejects a bad CRC and
 *             flags a frozen counter as stale data.
 ******************************************************************************
 */
#ifndef BMS_CAN_PROTOCOL_H
#define BMS_CAN_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMS_PROTOCOL_VERSION        1u
#define BMS_CAN_BITRATE             500000u
#define BMS_CELL_COUNT              16u
#define BMS_NTC_COUNT               4u

/* ==========================================================================
 * 1. MESSAGE IDs AND PERIODS
 * ========================================================================== */

/* --- Node 1 : Master Monitoring Unit ------------------------------------ */
#define CAN_ID_BMS_STATUS           0x100u  /* 100 ms + on change   [E2E] */
#define CAN_ID_BMS_PACK             0x101u  /* 100 ms               [E2E] */
#define CAN_ID_BMS_LIMITS           0x102u  /* 100 ms               [E2E] */
#define CAN_ID_BMS_CELL_STATS       0x103u  /* 100 ms                     */
#define CAN_ID_BMS_CELLS_1_4        0x110u  /* 100 ms                     */
#define CAN_ID_BMS_CELLS_5_8        0x111u  /* 100 ms                     */
#define CAN_ID_BMS_CELLS_9_12       0x112u  /* 100 ms                     */
#define CAN_ID_BMS_CELLS_13_16      0x113u  /* 100 ms                     */
#define CAN_ID_BMS_TEMPS            0x120u  /* 500 ms                     */
#define CAN_ID_BMS_BALANCE          0x121u  /* 500 ms                     */
#define CAN_ID_BMS_ENERGY           0x122u  /* 1000 ms                    */
#define CAN_ID_BMS_INFO             0x130u  /* 1000 ms                    */
#define CAN_ID_BMS_BQ_STATUS        0x131u  /* 1000 ms                    */

/* --- Node 2 : System Controller ----------------------------------------- */
#define CAN_ID_CTRL_STATUS          0x200u  /* 100 ms (also heartbeat) [E2E] */
#define CAN_ID_CTRL_CMD             0x201u  /* event, Node 2 -> Node 1 [E2E] */
#define CAN_ID_CTRL_INFO            0x210u  /* 1000 ms                    */

/* --- Node 3 : IoT Gateway ----------------------------------------------- */
#define CAN_ID_GW_HEARTBEAT         0x300u  /* 1000 ms              [E2E] */
#define CAN_ID_GW_CMD               0x301u  /* event, Node 3 -> Node 2 [E2E] */

#define CAN_PERIOD_FAST_MS          100u
#define CAN_PERIOD_SLOW_MS          500u
#define CAN_PERIOD_INFO_MS          1000u

/* Receiver time-outs (5 x period gives margin for jitter and one lost burst) */
#define CAN_TIMEOUT_BMS_MS          500u    /* Node 2 trips on this        */
#define CAN_TIMEOUT_CTRL_MS         1000u
#define CAN_TIMEOUT_GW_MS           5000u

/* ==========================================================================
 * 2. ENUMERATIONS
 * ========================================================================== */

/* Node 1 operating state (BMS_STATUS byte 0) */
typedef enum {
    BMS_STATE_INIT        = 0,
    BMS_STATE_IDLE        = 1,
    BMS_STATE_CHARGING    = 2,
    BMS_STATE_DISCHARGING = 3,
    BMS_STATE_FAULT       = 4
} BmsState_t;

/* Node 2 operating state (CTRL_STATUS byte 0) */
typedef enum {
    CTRL_STATE_INIT       = 0,
    CTRL_STATE_STANDBY    = 1,
    CTRL_STATE_PRECHARGE  = 2,
    CTRL_STATE_RUN        = 3,
    CTRL_STATE_STOPPING   = 4,
    CTRL_STATE_FAULT      = 5
} CtrlState_t;

/* CTRL_CMD (Node 2 -> Node 1) byte 0 */
#define CTRL_CMD_CLEAR_FAULTS       0x01u
#define CTRL_CMD_SET_BALANCING      0x02u   /* byte 1: 0 = block, 1 = allow */

/* GW_CMD (Node 3 -> Node 2) byte 0 */
#define GW_CMD_CLEAR_FAULTS         0x01u   /* forwarded to Node 1          */
#define GW_CMD_SET_BALANCING        0x02u   /* byte 1: 0/1, forwarded        */
#define GW_CMD_REQUEST_STOP         0x03u   /* open contactors (latched)     */
#define GW_CMD_FAN_OVERRIDE         0x04u   /* byte 1: 0..100 %, 0xFF = auto */

/* ==========================================================================
 * 3. BIT FIELDS
 * ========================================================================== */

/* Node 1 latched faults - BMS_STATUS bytes 1..3 (24 bit) */
#define BMS_FLT_BQ_COMM             (1UL << 0)   /* I2C link to BQ76952 lost      */
#define BMS_FLT_BQ_CONFIG           (1UL << 1)   /* BQ configuration not verified */
#define BMS_FLT_CELL_OV             (1UL << 2)   /* cell over-voltage             */
#define BMS_FLT_CELL_UV             (1UL << 3)   /* cell under-voltage            */
#define BMS_FLT_OVER_TEMP           (1UL << 4)   /* cell / die over-temperature   */
#define BMS_FLT_UNDER_TEMP          (1UL << 5)   /* cell under-temperature        */
#define BMS_FLT_OC_CHARGE           (1UL << 6)   /* over-current in charge        */
#define BMS_FLT_OC_DISCHARGE        (1UL << 7)   /* over-current in discharge     */
#define BMS_FLT_SHORT_CIRCUIT       (1UL << 8)   /* short circuit (BQ SCD)        */
#define BMS_FLT_IMBALANCE           (1UL << 9)   /* cell delta above hard limit   */
#define BMS_FLT_CELL_SENSE          (1UL << 10)  /* implausible cell / open wire  */
#define BMS_FLT_TEMP_SENSE          (1UL << 11)  /* NTC open or shorted           */
#define BMS_FLT_STACK_MISMATCH      (1UL << 12)  /* sum(cells) != stack voltage   */
#define BMS_FLT_BQ_PERMANENT_FAIL   (1UL << 13)  /* BQ76952 permanent fail        */

/* Node 1 live warnings - BMS_STATUS bytes 4..5 (16 bit) */
#define BMS_WRN_CELL_HIGH           (1u << 0)
#define BMS_WRN_CELL_LOW            (1u << 1)
#define BMS_WRN_TEMP_HIGH           (1u << 2)
#define BMS_WRN_TEMP_LOW            (1u << 3)
#define BMS_WRN_IMBALANCE           (1u << 4)
#define BMS_WRN_CURRENT_HIGH        (1u << 5)
#define BMS_WRN_CTRL_LOST           (1u << 6)   /* no CTRL_STATUS heartbeat      */
#define BMS_WRN_BALANCING           (1u << 7)   /* informative                   */
#define BMS_WRN_ALERT_PIN           (1u << 8)   /* ALERT line self-test failed   */
#define BMS_WRN_CAN_ERROR           (1u << 9)   /* error-passive / bus-off seen  */
#define BMS_WRN_WDG_RESET           (1u << 10)  /* last reset by watchdog        */
#define BMS_WRN_SOC_LOW             (1u << 11)
#define BMS_WRN_BQ_ALERT            (1u << 12)  /* BQ safety alert (pre-fault)   */
#define BMS_WRN_I2C_RETRY           (1u << 13)  /* I2C errors being recovered    */

/* BMS_LIMITS byte 4 */
#define BMS_LIM_CHARGE_ALLOWED      (1u << 0)
#define BMS_LIM_DISCHARGE_ALLOWED   (1u << 1)
#define BMS_LIM_BALANCING_ACTIVE    (1u << 2)
#define BMS_LIM_BALANCING_ENABLED   (1u << 3)
#define BMS_LIM_PACK_FULL           (1u << 4)
#define BMS_LIM_PACK_EMPTY          (1u << 5)

/* BMS_BALANCE byte 4 */
#define BMS_BAL_FLAG_ENABLED        (1u << 0)   /* permitted by controller/user  */
#define BMS_BAL_FLAG_CONDITIONS_OK  (1u << 1)   /* temp/current/voltage window   */
#define BMS_BAL_FLAG_ODD_GROUP      (1u << 2)   /* 1 = cells 1,3,5.. active      */

/* BMS_INFO byte 6: reset cause */
#define BMS_RST_POWER_ON            (1u << 0)
#define BMS_RST_PIN                 (1u << 1)
#define BMS_RST_WATCHDOG            (1u << 2)
#define BMS_RST_SOFTWARE            (1u << 3)

/* CTRL_STATUS byte 1: outputs */
#define CTRL_OUT_MAIN_NEG           (1u << 0)
#define CTRL_OUT_PRECHARGE          (1u << 1)
#define CTRL_OUT_MAIN_POS           (1u << 2)
#define CTRL_OUT_CHARGE_EN          (1u << 3)
#define CTRL_OUT_FAN                (1u << 4)

/* CTRL_STATUS byte 3: inputs / link health */
#define CTRL_IN_KEY_ON              (1u << 0)
#define CTRL_IN_ESTOP_OK            (1u << 1)
#define CTRL_IN_BMS_ONLINE          (1u << 2)
#define CTRL_IN_GW_ONLINE           (1u << 3)

/* CTRL_STATUS byte 4: controller faults (cause contactor opening) */
#define CTRL_FLT_BMS_TIMEOUT        (1u << 0)
#define CTRL_FLT_BMS_FAULT          (1u << 1)
#define CTRL_FLT_BMS_DATA           (1u << 2)   /* E2E errors / stale counter    */
#define CTRL_FLT_PRECHARGE          (1u << 3)
#define CTRL_FLT_ESTOP              (1u << 4)
#define CTRL_FLT_LIMIT_VIOLATION    (1u << 5)   /* current beyond BMS limits     */
#define CTRL_FLT_CAN_BUSOFF         (1u << 6)

/* CTRL_STATUS byte 5: controller warnings */
#define CTRL_WRN_FAN_FAIL           (1u << 0)
#define CTRL_WRN_GW_OFFLINE         (1u << 1)
#define CTRL_WRN_CHARGE_BLOCKED     (1u << 2)
#define CTRL_WRN_DERATED            (1u << 3)
#define CTRL_WRN_WDG_RESET          (1u << 4)
#define CTRL_WRN_REMOTE_STOP        (1u << 5)
#define CTRL_WRN_FAN_OVERRIDE       (1u << 6)

/* GW_HEARTBEAT byte 0 */
#define GW_FLAG_WIFI_OK             (1u << 0)
#define GW_FLAG_MQTT_OK             (1u << 1)
#define GW_FLAG_TIME_OK             (1u << 2)

/* ==========================================================================
 * 4. FRAME LAYOUTS  (byte offsets; LE = little endian)
 * ==========================================================================
 *
 *  0x100 BMS_STATUS    [0] state  [1..3] faults u24  [4..5] warnings u16
 *                      [6] counter [7] crc
 *  0x101 BMS_PACK      [0..1] pack voltage u16 10 mV  [2..3] current i16 10 mA
 *                      (+ charge / - discharge)  [4..5] SoC u16 0.1 %
 *                      [6] counter [7] crc
 *  0x102 BMS_LIMITS    [0..1] max charge current u16 0.1 A
 *                      [2..3] max discharge current u16 0.1 A (magnitude)
 *                      [4] flags  [5] max cell temp i8 degC
 *                      [6] counter [7] crc
 *  0x103 BMS_CELL_STATS[0..1] min mV [2..3] max mV [4..5] avg mV
 *                      [6] min cell no. (1..16) [7] max cell no. (1..16)
 *  0x110..0x113 CELLS  4 x u16 mV (cells 1-4, 5-8, 9-12, 13-16)
 *  0x120 BMS_TEMPS     4 x i16 0.1 degC  (NTC1..NTC4)
 *  0x121 BMS_BALANCE   [0..1] bleed mask (bit0 = cell 1) [2..3] delta mV
 *                      [4] flags [5] cells bleeding [6..7] balance time min
 *  0x122 BMS_ENERGY    [0..1] SoH u16 0.1 %  [2..3] remaining u16 10 mAh
 *                      [4..5] usable capacity u16 10 mAh
 *                      [6..7] discharged since boot u16 0.1 Ah
 *  0x130 BMS_INFO      [0..3] uptime s u32 [4] fw major [5] fw minor
 *                      [6] reset cause [7] BQ die temp i8 degC
 *  0x131 BMS_BQ_STATUS [0] Safety Status A [1] B [2] C [3..4] Battery Status
 *                      [5..6] Alarm Status (latched) [7] I2C error count
 *
 *  0x200 CTRL_STATUS   [0] state [1] outputs [2] fan duty % [3] inputs
 *                      [4] faults [5] warnings [6] counter [7] crc
 *  0x201 CTRL_CMD      [0] command [1] argument [2..5] 0 [6] counter [7] crc
 *  0x210 CTRL_INFO     [0..3] uptime s [4] fw major [5] fw minor
 *                      [6..7] fan speed rpm u16
 *
 *  0x300 GW_HEARTBEAT  [0] flags [1] Wi-Fi RSSI i8 dBm [2..5] uptime s
 *                      [6] counter [7] crc
 *  0x301 GW_CMD        [0] command [1] argument [2..5] 0 [6] counter [7] crc
 * ========================================================================== */

#define CAN_E2E_COUNTER_BYTE        6u
#define CAN_E2E_CRC_BYTE            7u

/* ==========================================================================
 * 5. HELPERS (header-only, no dependencies)
 * ========================================================================== */

static inline void can_put_u16(uint8_t *d, uint8_t off, uint16_t v)
{
    d[off]      = (uint8_t)(v & 0xFFu);
    d[off + 1u] = (uint8_t)(v >> 8);
}

static inline void can_put_i16(uint8_t *d, uint8_t off, int16_t v)
{
    can_put_u16(d, off, (uint16_t)v);
}

static inline void can_put_u24(uint8_t *d, uint8_t off, uint32_t v)
{
    d[off]      = (uint8_t)(v & 0xFFu);
    d[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    d[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
}

static inline void can_put_u32(uint8_t *d, uint8_t off, uint32_t v)
{
    can_put_u24(d, off, v);
    d[off + 3u] = (uint8_t)(v >> 24);
}

static inline uint16_t can_get_u16(const uint8_t *d, uint8_t off)
{
    return (uint16_t)((uint16_t)d[off] | ((uint16_t)d[off + 1u] << 8));
}

static inline int16_t can_get_i16(const uint8_t *d, uint8_t off)
{
    return (int16_t)can_get_u16(d, off);
}

static inline uint32_t can_get_u24(const uint8_t *d, uint8_t off)
{
    return (uint32_t)d[off] | ((uint32_t)d[off + 1u] << 8) |
           ((uint32_t)d[off + 2u] << 16);
}

static inline uint32_t can_get_u32(const uint8_t *d, uint8_t off)
{
    return can_get_u24(d, off) | ((uint32_t)d[off + 3u] << 24);
}

static inline int16_t can_clamp_i16(int32_t v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static inline uint16_t can_clamp_u16(int32_t v)
{
    if (v > 65535) return 65535u;
    if (v < 0)     return 0u;
    return (uint16_t)v;
}

/** CRC-8 SAE J1850: poly 0x1D, init 0xFF, final XOR 0xFF. */
static inline uint8_t can_crc8_j1850(uint16_t id, const uint8_t *d, uint8_t len)
{
    uint8_t crc = 0xFFu;
    uint8_t buf[2];
    buf[0] = (uint8_t)(id & 0xFFu);
    buf[1] = (uint8_t)(id >> 8);

    for (uint8_t i = 0; i < (uint8_t)(2u + len); i++) {
        crc ^= (i < 2u) ? buf[i] : d[i - 2u];
        for (uint8_t b = 0; b < 8u; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x1Du) : (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc ^ 0xFFu);
}

/** Write alive counter (byte 6) and CRC (byte 7) into an 8-byte payload. */
static inline void can_e2e_protect(uint16_t id, uint8_t *d, uint8_t counter)
{
    d[CAN_E2E_COUNTER_BYTE] = counter;
    d[CAN_E2E_CRC_BYTE]     = can_crc8_j1850(id, d, 7u);
}

/** True if the CRC in byte 7 matches. */
static inline bool can_e2e_check(uint16_t id, const uint8_t *d)
{
    return d[CAN_E2E_CRC_BYTE] == can_crc8_j1850(id, d, 7u);
}

#ifdef __cplusplus
}
#endif

#endif /* BMS_CAN_PROTOCOL_H */
