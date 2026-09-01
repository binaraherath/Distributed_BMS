/**
  ******************************************************************************
  * @file    bms_can.c
  * @brief   CAN framing for the monitoring node.
  ******************************************************************************
  */
#include "bms_can.h"
#include "bms_config.h"
#include "bms_app.h"

#define BMS_FW_VERSION_MAJOR   1u
#define BMS_FW_VERSION_MINOR   0u

static CAN_HandleTypeDef *s_hcan = NULL;
static uint8_t s_rolling_counter = 0;

/* --------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/**
 * XOR checksum over the first 7 bytes plus the ID, placed in byte 7 together
 * with a 4-bit rolling counter. The receiver can then detect both corrupted
 * frames and a sender that has stopped updating (stale data with a frozen
 * counter). Cheap, and it catches the two failure modes that matter.
 */
static void StampFrame(uint8_t *d, uint16_t id)
{
    uint8_t crc = (uint8_t)(id & 0xFFu) ^ (uint8_t)(id >> 8);
    for (uint8_t i = 0; i < 7u; i++) crc ^= d[i];
    d[7] = (uint8_t)((s_rolling_counter & 0x0Fu) << 4) | (crc & 0x0Fu);
}

static void Send(uint16_t id, uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef hdr;
    uint32_t mailbox;

    if (s_hcan == NULL) return;
    if (HAL_CAN_GetTxMailboxesFreeLevel(s_hcan) == 0u) return;  /* drop, do not block */

    hdr.StdId = id;
    hdr.ExtId = 0;
    hdr.IDE   = CAN_ID_STD;
    hdr.RTR   = CAN_RTR_DATA;
    hdr.DLC   = dlc;
    hdr.TransmitGlobalTime = DISABLE;

    (void)HAL_CAN_AddTxMessage(s_hcan, &hdr, data, &mailbox);
}

static void PutU16(uint8_t *d, uint8_t off, uint16_t v)
{
    d[off]     = (uint8_t)(v & 0xFFu);
    d[off + 1] = (uint8_t)(v >> 8);
}

/* --------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------ */

HAL_StatusTypeDef BMS_CAN_Init(CAN_HandleTypeDef *hcan)
{
    CAN_FilterTypeDef f;

    s_hcan = hcan;

    /* Filter bank 0: accept 0x200-0x2FF (everything from the controller node).
     * Mask mode, 32-bit: StdId sits in bits 31:21 of the ID register. */
    f.FilterBank           = 0;
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterIdHigh         = (uint16_t)(0x200u << 5);
    f.FilterIdLow          = 0x0000;
    f.FilterMaskIdHigh     = (uint16_t)(0x700u << 5);   /* match top 3 bits */
    f.FilterMaskIdLow      = 0x0000;
    f.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    f.FilterActivation     = CAN_FILTER_ENABLE;
    f.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(hcan, &f) != HAL_OK) return HAL_ERROR;
    if (HAL_CAN_Start(hcan) != HAL_OK)            return HAL_ERROR;

    if (HAL_CAN_ActivateNotification(hcan,
            CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR |
            CAN_IT_BUSOFF) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

bool BMS_CAN_IsBusOff(void)
{
    if (s_hcan == NULL) return true;
    return (READ_BIT(s_hcan->Instance->ESR, CAN_ESR_BOFF) != 0u);
}

/* --------------------------------------------------------------------------
 * Transmit
 * ------------------------------------------------------------------------ */

/* 0x100 Pack summary
 *   [0:1] pack voltage, 0.1 V, u16
 *   [2:3] pack current, 0.1 A signed offset by 3200 (u16, 0 = -320.0 A)
 *   [4]   SoC, 0.5 %/bit
 *   [5]   state (0=INIT 1=IDLE 2=ACTIVE 3=FAULT)
 *   [6]   warnings low byte
 *   [7]   counter/checksum
 */
static void SendPackSummary(void)
{
    uint8_t d[8];
    uint16_t v_dv = (uint16_t)(g_bms.meas.stack_mv / 100u);
    int32_t  i_da = g_bms.meas.current_ma / 100;
    uint16_t i_enc;

    if (i_da < -3200) i_da = -3200;
    if (i_da >  3200) i_da =  3200;
    i_enc = (uint16_t)(i_da + 3200);

    PutU16(d, 0, v_dv);
    PutU16(d, 2, i_enc);
    d[4] = (uint8_t)(g_bms.soc_pct * 2.0f);
    d[5] = (uint8_t)g_bms.state;
    d[6] = (uint8_t)(g_bms.warnings & 0xFFu);
    StampFrame(d, CAN_ID_PACK_SUMMARY);
    Send(CAN_ID_PACK_SUMMARY, d, 8);
}

/* 0x101-0x104: four cell voltages per frame, raw mV, u16 each. No checksum
 * byte here -- all eight bytes carry data, and a corrupt cell voltage is
 * caught by the plausibility check on the receiving side. */
static void SendCellVoltages(void)
{
    static const uint16_t ids[4] = {
        CAN_ID_CELLS_1_4, CAN_ID_CELLS_5_8,
        CAN_ID_CELLS_9_12, CAN_ID_CELLS_13_16
    };
    uint8_t d[8];

    for (uint8_t g = 0; g < 4u; g++) {
        for (uint8_t c = 0; c < 4u; c++) {
            PutU16(d, (uint8_t)(c * 2u), g_bms.meas.cell_mv[g * 4u + c]);
        }
        Send(ids[g], d, 8);
    }
}

/* 0x106 Cell statistics
 *   [0:1] min mV, [2:3] max mV, [4] min index, [5] max index,
 *   [6:7] balancing bitmap
 */
static void SendCellStats(void)
{
    uint8_t d[8];
    PutU16(d, 0, g_bms.meas.cell_min_mv);
    PutU16(d, 2, g_bms.meas.cell_max_mv);
    d[4] = g_bms.meas.cell_min_idx;
    d[5] = g_bms.meas.cell_max_idx;
    PutU16(d, 6, g_bms.balance_mask);
    Send(CAN_ID_CELL_STATS, d, 8);
}

/* 0x107 Fault status
 *   [0:3] fault bitfield (u32), [4:5] warning bitfield (u16),
 *   [6]   BQ safety status A, [7] counter/checksum
 */
static void SendFaultStatus(void)
{
    uint8_t d[8];
    d[0] = (uint8_t)(g_bms.faults & 0xFFu);
    d[1] = (uint8_t)((g_bms.faults >> 8) & 0xFFu);
    d[2] = (uint8_t)((g_bms.faults >> 16) & 0xFFu);
    d[3] = (uint8_t)((g_bms.faults >> 24) & 0xFFu);
    PutU16(d, 4, g_bms.warnings);
    d[6] = g_bms.meas.safety_status_a;
    StampFrame(d, CAN_ID_FAULT_STATUS);
    Send(CAN_ID_FAULT_STATUS, d, 8);
}

/* 0x105 Temperatures: five NTCs plus the BQ die, all int8 degC, [6] spare */
static void SendTemperatures(void)
{
    uint8_t d[8] = {0};
    for (uint8_t i = 0; i < BMS_THERMISTOR_COUNT; i++)
        d[i] = (uint8_t)(int8_t)(g_bms.meas.temp_dc[i] / 10);
    d[5] = (uint8_t)(int8_t)(g_bms.meas.internal_temp_dc / 10);
    StampFrame(d, CAN_ID_TEMPERATURES);
    Send(CAN_ID_TEMPERATURES, d, 8);
}

/* 0x108 Limits: [0:1] charge limit 0.1 A, [2:3] discharge limit 0.1 A,
 *               [4:5] SoH 0.1 %, [6] spare */
static void SendLimits(void)
{
    uint8_t d[8] = {0};
    int32_t chg = g_bms.charge_limit_ma / 100;
    int32_t dsg = -g_bms.discharge_limit_ma / 100;   /* transmit as magnitude */

    if (chg < 0) chg = 0;
    if (dsg < 0) dsg = 0;
    PutU16(d, 0, (uint16_t)chg);
    PutU16(d, 2, (uint16_t)dsg);
    PutU16(d, 4, (uint16_t)(g_bms.soh_pct * 10.0f));
    StampFrame(d, CAN_ID_LIMITS);
    Send(CAN_ID_LIMITS, d, 8);
}

/* 0x109 Heartbeat: [0:3] uptime s, [4] fw major, [5] fw minor,
 *                  [6] rolling counter, [7] checksum */
static void SendHeartbeatFrame(void)
{
    uint8_t d[8] = {0};
    d[0] = (uint8_t)(g_bms.uptime_s & 0xFFu);
    d[1] = (uint8_t)((g_bms.uptime_s >> 8) & 0xFFu);
    d[2] = (uint8_t)((g_bms.uptime_s >> 16) & 0xFFu);
    d[3] = (uint8_t)((g_bms.uptime_s >> 24) & 0xFFu);
    d[4] = BMS_FW_VERSION_MAJOR;
    d[5] = BMS_FW_VERSION_MINOR;
    d[6] = s_rolling_counter;
    StampFrame(d, CAN_ID_HEARTBEAT);
    Send(CAN_ID_HEARTBEAT, d, 8);
}

void BMS_CAN_SendFast(void)
{
    s_rolling_counter++;
    SendPackSummary();
    SendCellVoltages();
    SendCellStats();
    SendFaultStatus();
}

void BMS_CAN_SendSlow(void)
{
    SendTemperatures();
    SendLimits();
}

void BMS_CAN_SendHeartbeat(void)
{
    SendHeartbeatFrame();
}

void BMS_CAN_SendFaultsNow(void)
{
    SendFaultStatus();
}

/* --------------------------------------------------------------------------
 * Receive
 * ------------------------------------------------------------------------ */

/* 0x201 Command frame from the system controller
 *   [0] command: 1 = clear faults, 2 = balancing enable/disable
 *   [1] argument
 */
static void HandleCommand(const uint8_t *d)
{
    switch (d[0]) {
        case 1u:
            (void)BMS_App_ClearFaults();
            BMS_CAN_SendFaultsNow();
            break;
        case 2u:
            BMS_App_SetBalancingPermission(d[1] != 0u);
            break;
        default:
            break;
    }
}

void BMS_CAN_Poll(void)
{
    CAN_RxHeaderTypeDef hdr;
    uint8_t d[8];

    if (s_hcan == NULL) return;

    while (HAL_CAN_GetRxFifoFillLevel(s_hcan, CAN_RX_FIFO0) > 0u) {
        if (HAL_CAN_GetRxMessage(s_hcan, CAN_RX_FIFO0, &hdr, d) != HAL_OK) break;

        switch (hdr.StdId) {
            case CAN_ID_CTRL_HEARTBEAT:
                BMS_App_NoteControllerHeartbeat();
                break;
            case CAN_ID_CTRL_COMMAND:
                BMS_App_NoteControllerHeartbeat();
                HandleCommand(d);
                break;
            default:
                break;
        }
    }
}
