/**
 ******************************************************************************
 * @file    ctrl_can.c
 * @brief   Node 2 CAN layer (see header and common/bms_can_protocol.h).
 ******************************************************************************
 */
#include "ctrl_can.h"
#include <string.h>

#define TXQ_SIZE    16u
#define RXQ_SIZE    32u

typedef struct {
    uint16_t id;
    uint8_t  d[8];
} Frame_t;

static CAN_HandleTypeDef *s_can;

static Frame_t           s_txq[TXQ_SIZE];
static volatile uint8_t  s_tx_head, s_tx_tail;
static Frame_t           s_rxq[RXQ_SIZE];
static volatile uint8_t  s_rx_head, s_rx_tail;
static volatile bool     s_error_flag;

static uint8_t s_cnt_status, s_cnt_cmd;

/* E2E tracking for the three safety frames from Node 1 */
typedef struct { bool seen; uint8_t last; uint8_t errors; } Alive_t;
static Alive_t s_alive_status, s_alive_pack, s_alive_limits;
static bool    s_gw_cmd_seen;
static uint8_t s_gw_cmd_last;

/* ==========================================================================
 * TX queue (same scheme as Node 1: refilled from the mailbox-empty IRQ)
 * ========================================================================== */

static void tx_pump(void)
{
    CAN_TxHeaderTypeDef hdr = {0};
    uint32_t mailbox;

    hdr.IDE = CAN_ID_STD;
    hdr.RTR = CAN_RTR_DATA;
    hdr.DLC = 8;

    while (s_tx_tail != s_tx_head && HAL_CAN_GetTxMailboxesFreeLevel(s_can) > 0u) {
        hdr.StdId = s_txq[s_tx_tail].id;
        if (HAL_CAN_AddTxMessage(s_can, &hdr, s_txq[s_tx_tail].d, &mailbox) != HAL_OK) break;
        s_tx_tail = (uint8_t)((s_tx_tail + 1u) % TXQ_SIZE);
    }
}

static void send(uint16_t id, const uint8_t *d)
{
    uint32_t primask;
    uint8_t  next;

    if (s_can == NULL) return;
    primask = __get_PRIMASK();
    __disable_irq();
    next = (uint8_t)((s_tx_head + 1u) % TXQ_SIZE);
    if (next != s_tx_tail) {
        s_txq[s_tx_head].id = id;
        memcpy(s_txq[s_tx_head].d, d, 8);
        s_tx_head = next;
    }
    tx_pump();
    __set_PRIMASK(primask);
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *h) { (void)h; tx_pump(); }
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *h) { (void)h; tx_pump(); }
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *h) { (void)h; tx_pump(); }

/* ==========================================================================
 * RX
 * ========================================================================== */

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h)
{
    CAN_RxHeaderTypeDef hdr;
    uint8_t d[8];

    while (HAL_CAN_GetRxFifoFillLevel(h, CAN_RX_FIFO0) > 0u) {
        if (HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &hdr, d) != HAL_OK) break;
        if (hdr.IDE != CAN_ID_STD || hdr.RTR != CAN_RTR_DATA || hdr.DLC != 8u) continue;

        uint8_t next = (uint8_t)((s_rx_head + 1u) % RXQ_SIZE);
        if (next != s_rx_tail) {
            s_rxq[s_rx_head].id = (uint16_t)hdr.StdId;
            memcpy(s_rxq[s_rx_head].d, d, 8);
            s_rx_head = next;
        }
    }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *h)
{
    if (h->Instance->ESR & (CAN_ESR_BOFF | CAN_ESR_EPVF)) s_error_flag = true;
    HAL_CAN_ResetError(h);
}

bool CtrlCan_IsBusOff(void)
{
    return (s_can != NULL) && ((s_can->Instance->ESR & CAN_ESR_BOFF) != 0u);
}

bool CtrlCan_TakeErrorFlag(void)
{
    bool f = s_error_flag;
    s_error_flag = false;
    return f;
}

static void e2e_update_worst(void)
{
    uint8_t w = s_alive_status.errors;
    if (s_alive_pack.errors   > w) w = s_alive_pack.errors;
    if (s_alive_limits.errors > w) w = s_alive_limits.errors;
    g_ctrl.bms.e2e_errors = w;
}

/** CRC must match and the alive counter must move. Returns true if the
 *  frame may be used. Consecutive problems are counted per message and the
 *  worst count is published in bms.e2e_errors. */
static bool e2e_accept(uint16_t id, const uint8_t *d, Alive_t *a)
{
    bool ok = can_e2e_check(id, d);

    if (ok) {
        ok = !a->seen || d[CAN_E2E_COUNTER_BYTE] != a->last;   /* frozen sender? */
        a->seen = true;
        a->last = d[CAN_E2E_COUNTER_BYTE];
    }
    if (ok) a->errors = 0;
    else if (a->errors < 0xFFu) a->errors++;
    e2e_update_worst();
    return ok;
}

static void handle_gw_command(const uint8_t *d)
{
    if (!can_e2e_check(CAN_ID_GW_CMD, d)) return;
    if (s_gw_cmd_seen && d[CAN_E2E_COUNTER_BYTE] == s_gw_cmd_last) return;   /* duplicate */
    s_gw_cmd_seen = true;
    s_gw_cmd_last = d[CAN_E2E_COUNTER_BYTE];

    switch (d[0]) {
        case GW_CMD_CLEAR_FAULTS:  Ctrl_App_ClearFaults();              break;
        case GW_CMD_SET_BALANCING: Ctrl_App_SetBalancing(d[1] != 0u);   break;
        case GW_CMD_REQUEST_STOP:  Ctrl_App_RequestStop();              break;
        case GW_CMD_FAN_OVERRIDE:  Ctrl_App_FanOverride(d[1] == 0xFFu ? -1 : (int16_t)d[1]); break;
        default: break;
    }
}

void CtrlCan_Process(void)
{
    BmsView_t *v = &g_ctrl.bms;
    uint32_t now = HAL_GetTick();

    while (s_rx_tail != s_rx_head) {
        Frame_t f = s_rxq[s_rx_tail];
        s_rx_tail = (uint8_t)((s_rx_tail + 1u) % RXQ_SIZE);
        const uint8_t *d = f.d;

        switch (f.id) {
            case CAN_ID_BMS_STATUS:
                if (!e2e_accept(f.id, d, &s_alive_status)) break;
                v->state    = (BmsState_t)d[0];
                v->faults   = can_get_u24(d, 1);
                v->warnings = can_get_u16(d, 4);
                v->last_status_ms = now;
                v->online   = true;
                break;

            case CAN_ID_BMS_PACK:
                if (!e2e_accept(f.id, d, &s_alive_pack)) break;
                v->pack_mv    = (int32_t)can_get_u16(d, 0) * 10;
                v->current_ma = (int32_t)can_get_i16(d, 2) * 10;
                v->soc_x10    = can_get_u16(d, 4);
                v->pack_rx_count++;
                break;

            case CAN_ID_BMS_LIMITS:
                if (!e2e_accept(f.id, d, &s_alive_limits)) break;
                v->chg_limit_ma = (int32_t)can_get_u16(d, 0) * 100;
                v->dsg_limit_ma = (int32_t)can_get_u16(d, 2) * 100;
                v->limit_flags  = d[4];
                v->tmax_c       = (int8_t)d[5];
                break;

            case CAN_ID_GW_HEARTBEAT:
                if (!can_e2e_check(f.id, d)) break;
                g_ctrl.gw.flags      = d[0];
                g_ctrl.gw.rssi_dbm   = (int8_t)d[1];
                g_ctrl.gw.last_rx_ms = now;
                g_ctrl.gw.online     = true;
                break;

            case CAN_ID_GW_CMD:
                handle_gw_command(d);
                break;

            default:
                break;
        }
    }

    if (v->online && (now - v->last_status_ms) > CTRL_BMS_TIMEOUT_MS) v->online = false;
    if (g_ctrl.gw.online && (now - g_ctrl.gw.last_rx_ms) > CTRL_GW_TIMEOUT_MS) g_ctrl.gw.online = false;
}

/* ==========================================================================
 * Init and TX frames
 * ========================================================================== */

HAL_StatusTypeDef CtrlCan_Init(CAN_HandleTypeDef *hcan)
{
    CAN_FilterTypeDef f = {0};

    s_can = hcan;

    /* Bank 0, two 16-bit mask filters, standard data frames only
     * (IDE and RTR bits included in the mask):
     *   high: 0x100-0x13F  (all Node 1 frames)
     *   low : 0x300-0x301  (gateway heartbeat and commands)            */
    f.FilterBank           = 0;
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_16BIT;
    f.FilterIdHigh         = (uint32_t)(0x100u << 5);
    f.FilterMaskIdHigh     = (uint32_t)((0x7C0u << 5) | 0x18u);
    f.FilterIdLow          = (uint32_t)(0x300u << 5);
    f.FilterMaskIdLow      = (uint32_t)((0x7FEu << 5) | 0x18u);
    f.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    f.FilterActivation     = CAN_FILTER_ENABLE;
    f.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(hcan, &f) != HAL_OK) return HAL_ERROR;
    if (HAL_CAN_Start(hcan) != HAL_OK)            return HAL_ERROR;
    return HAL_CAN_ActivateNotification(hcan,
               CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY |
               CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF |
               CAN_IT_ERROR);
}

void CtrlCan_SendStatus(const CtrlContext_t *c)
{
    uint8_t d[8] = {0};
    d[0] = (uint8_t)c->state;
    d[1] = c->outputs;
    d[2] = c->fan_duty;
    d[3] = c->inputs;
    d[4] = c->faults;
    d[5] = c->warnings;
    can_e2e_protect(CAN_ID_CTRL_STATUS, d, s_cnt_status++);
    send(CAN_ID_CTRL_STATUS, d);
}

void CtrlCan_SendInfo(const CtrlContext_t *c)
{
    uint8_t d[8] = {0};
    can_put_u32(d, 0, c->uptime_s);
    d[4] = CTRL_FW_VERSION_MAJOR;
    d[5] = CTRL_FW_VERSION_MINOR;
    can_put_u16(d, 6, c->fan_rpm);
    send(CAN_ID_CTRL_INFO, d);
}

void CtrlCan_SendBmsCommand(uint8_t cmd, uint8_t arg)
{
    uint8_t d[8] = {0};
    d[0] = cmd;
    d[1] = arg;
    can_e2e_protect(CAN_ID_CTRL_CMD, d, ++s_cnt_cmd);
    send(CAN_ID_CTRL_CMD, d);
}
