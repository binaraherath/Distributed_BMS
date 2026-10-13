/**
 ******************************************************************************
 * @file    bms_can.c
 * @brief   Node 1 CAN layer (see header and common/bms_can_protocol.h).
 ******************************************************************************
 */
#include "bms_can.h"
#include <string.h>

#define TXQ_SIZE    32u
#define RXQ_SIZE    8u

typedef struct {
    uint16_t id;
    uint8_t  d[8];
} Frame_t;

static CAN_HandleTypeDef *s_can;

static Frame_t          s_txq[TXQ_SIZE];
static volatile uint8_t s_tx_head, s_tx_tail;
static volatile uint32_t s_tx_dropped;

static Frame_t          s_rxq[RXQ_SIZE];
static volatile uint8_t s_rx_head, s_rx_tail;

static volatile bool    s_error_flag;

static uint8_t s_cnt_status, s_cnt_pack, s_cnt_limits;
static bool    s_cmd_seen;
static uint8_t s_cmd_last_counter;

/* ==========================================================================
 * TX queue
 * ========================================================================== */

/* Move queued frames into free mailboxes. Called with interrupts masked
 * from thread context, or from the TX-complete ISR. */
static void tx_pump(void)
{
    CAN_TxHeaderTypeDef hdr = {0};
    uint32_t mailbox;

    hdr.IDE = CAN_ID_STD;
    hdr.RTR = CAN_RTR_DATA;
    hdr.DLC = 8;
    hdr.TransmitGlobalTime = DISABLE;

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
    if (next == s_tx_tail) {
        s_tx_dropped++;                 /* bus down: keep the newest frames flowing later */
    } else {
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

    /* Drain the FIFO completely, otherwise the IRQ fires again at once. */
    while (HAL_CAN_GetRxFifoFillLevel(h, CAN_RX_FIFO0) > 0u) {
        if (HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &hdr, d) != HAL_OK) break;
        if (hdr.IDE != CAN_ID_STD || hdr.DLC != 8u) continue;

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
    uint32_t esr = h->Instance->ESR;
    if (esr & (CAN_ESR_BOFF | CAN_ESR_EPVF)) s_error_flag = true;
    HAL_CAN_ResetError(h);
}

static void handle_command(const uint8_t *d)
{
    /* Reject repeats: the controller increments the counter per command */
    if (s_cmd_seen && d[CAN_E2E_COUNTER_BYTE] == s_cmd_last_counter) return;
    s_cmd_seen = true;
    s_cmd_last_counter = d[CAN_E2E_COUNTER_BYTE];

    switch (d[0]) {
        case CTRL_CMD_CLEAR_FAULTS:
            (void)BMS_App_ClearFaults();
            BmsCan_SendStatusNow(&g_bms);
            break;
        case CTRL_CMD_SET_BALANCING:
            BMS_App_SetBalancing(d[1] != 0u);
            break;
        default:
            break;
    }
}

void BmsCan_Process(void)
{
    while (s_rx_tail != s_rx_head) {
        Frame_t f = s_rxq[s_rx_tail];
        s_rx_tail = (uint8_t)((s_rx_tail + 1u) % RXQ_SIZE);

        if (!can_e2e_check(f.id, f.d)) continue;

        switch (f.id) {
            case CAN_ID_CTRL_STATUS:
                BMS_App_NoteControllerHeartbeat();
                break;
            case CAN_ID_CTRL_CMD:
                BMS_App_NoteControllerHeartbeat();
                handle_command(f.d);
                break;
            default:
                break;
        }
    }
}

/* ==========================================================================
 * Init
 * ========================================================================== */

HAL_StatusTypeDef BmsCan_Init(CAN_HandleTypeDef *hcan)
{
    CAN_FilterTypeDef f = {0};

    s_can = hcan;

    /* Bank 0, 16-bit ID-list mode: only the two controller frames are
     * accepted in hardware. StdId sits in bits [15:5] of each entry. */
    f.FilterBank           = 0;
    f.FilterMode           = CAN_FILTERMODE_IDLIST;
    f.FilterScale          = CAN_FILTERSCALE_16BIT;
    f.FilterIdHigh         = (uint32_t)(CAN_ID_CTRL_STATUS << 5);
    f.FilterIdLow          = (uint32_t)(CAN_ID_CTRL_CMD << 5);
    f.FilterMaskIdHigh     = (uint32_t)(CAN_ID_CTRL_STATUS << 5);
    f.FilterMaskIdLow      = (uint32_t)(CAN_ID_CTRL_CMD << 5);
    f.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    f.FilterActivation     = CAN_FILTER_ENABLE;
    f.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(hcan, &f) != HAL_OK) return HAL_ERROR;
    if (HAL_CAN_Start(hcan) != HAL_OK)            return HAL_ERROR;

    /* No LAST_ERROR_CODE interrupt: alone on a bench bus every un-ACKed
     * retry would raise one. Warning / passive / bus-off are enough. */
    return HAL_CAN_ActivateNotification(hcan,
               CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY |
               CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF |
               CAN_IT_ERROR);
}

bool BmsCan_TakeErrorFlag(void)
{
    bool f = s_error_flag;
    s_error_flag = false;
    return f;
}

uint32_t BmsCan_DroppedFrames(void)
{
    return s_tx_dropped;
}

/* ==========================================================================
 * Frames
 * ========================================================================== */

void BmsCan_SendStatusNow(const BmsContext_t *b)
{
    uint8_t d[8] = {0};
    d[0] = (uint8_t)b->state;
    can_put_u24(d, 1, b->faults);
    can_put_u16(d, 4, b->warnings);
    can_e2e_protect(CAN_ID_BMS_STATUS, d, s_cnt_status++);
    send(CAN_ID_BMS_STATUS, d);
}

static void send_pack(const BmsContext_t *b)
{
    uint8_t d[8] = {0};
    can_put_u16(d, 0, can_clamp_u16(b->m.stack_mv / 10));
    can_put_i16(d, 2, can_clamp_i16(b->m.current_ma / 10));
    can_put_u16(d, 4, (uint16_t)(b->soc_pct * 10.0f + 0.5f));
    can_e2e_protect(CAN_ID_BMS_PACK, d, s_cnt_pack++);
    send(CAN_ID_BMS_PACK, d);
}

static void send_limits(const BmsContext_t *b)
{
    uint8_t d[8] = {0};
    int16_t tmax = (int16_t)(b->m.temp_max_dc / 10);
    can_put_u16(d, 0, can_clamp_u16(b->chg_limit_ma / 100));
    can_put_u16(d, 2, can_clamp_u16(b->dsg_limit_ma / 100));
    d[4] = b->limit_flags;
    d[5] = (uint8_t)(int8_t)(tmax > 127 ? 127 : (tmax < -128 ? -128 : tmax));
    can_e2e_protect(CAN_ID_BMS_LIMITS, d, s_cnt_limits++);
    send(CAN_ID_BMS_LIMITS, d);
}

static void send_cell_stats(const BmsContext_t *b)
{
    uint8_t d[8];
    can_put_u16(d, 0, b->m.cell_min_mv);
    can_put_u16(d, 2, b->m.cell_max_mv);
    can_put_u16(d, 4, b->m.cell_avg_mv);
    d[6] = (uint8_t)(b->m.cell_min_idx + 1u);
    d[7] = (uint8_t)(b->m.cell_max_idx + 1u);
    send(CAN_ID_BMS_CELL_STATS, d);
}

static void send_cells(const BmsContext_t *b)
{
    uint8_t d[8];
    for (uint8_t g = 0; g < 4u; g++) {
        for (uint8_t c = 0; c < 4u; c++) can_put_u16(d, (uint8_t)(2u * c), b->m.cell_mv[4u * g + c]);
        send((uint16_t)(CAN_ID_BMS_CELLS_1_4 + g), d);
    }
}

void BmsCan_SendFast(const BmsContext_t *b)
{
    BmsCan_SendStatusNow(b);
    send_pack(b);
    send_limits(b);
    send_cell_stats(b);
    send_cells(b);
}

void BmsCan_SendSlow(const BmsContext_t *b)
{
    uint8_t d[8] = {0};

    for (uint8_t i = 0; i < BMS_NTC_COUNT; i++) can_put_i16(d, (uint8_t)(2u * i), b->m.ntc_dc[i]);
    send(CAN_ID_BMS_TEMPS, d);

    memset(d, 0, sizeof(d));
    can_put_u16(d, 0, b->bal_mask);
    can_put_u16(d, 2, b->m.cell_delta_mv);
    d[4] = (uint8_t)((b->bal_enabled ? BMS_BAL_FLAG_ENABLED : 0u) |
                     (b->bal_conditions_ok ? BMS_BAL_FLAG_CONDITIONS_OK : 0u) |
                     (b->bal_odd_group ? BMS_BAL_FLAG_ODD_GROUP : 0u));
    {
        uint8_t n = 0;
        for (uint16_t m = b->bal_mask; m; m &= (uint16_t)(m - 1u)) n++;
        d[5] = n;
    }
    can_put_u16(d, 6, can_clamp_u16((int32_t)(b->bal_seconds / 60u)));
    send(CAN_ID_BMS_BALANCE, d);
}

void BmsCan_SendInfo(const BmsContext_t *b)
{
    uint8_t d[8] = {0};
    int16_t die = (int16_t)(b->m.die_dc / 10);

    can_put_u16(d, 0, (uint16_t)(b->soh_pct * 10.0f + 0.5f));
    can_put_u16(d, 2, can_clamp_u16((int32_t)(b->remaining_ah * 100.0f)));
    can_put_u16(d, 4, can_clamp_u16((int32_t)(b->usable_ah * 100.0f)));
    can_put_u16(d, 6, can_clamp_u16((int32_t)(b->discharged_ah * 10.0f)));
    send(CAN_ID_BMS_ENERGY, d);

    memset(d, 0, sizeof(d));
    can_put_u32(d, 0, b->uptime_s);
    d[4] = BMS_FW_VERSION_MAJOR;
    d[5] = BMS_FW_VERSION_MINOR;
    d[6] = b->reset_cause;
    d[7] = (uint8_t)(int8_t)(die > 127 ? 127 : (die < -128 ? -128 : die));
    send(CAN_ID_BMS_INFO, d);

    memset(d, 0, sizeof(d));
    d[0] = b->bq.status_a;
    d[1] = b->bq.status_b;
    d[2] = b->bq.status_c;
    can_put_u16(d, 3, b->bq.battery_status);
    can_put_u16(d, 5, b->bq_alarm_latched);
    d[7] = (uint8_t)(BQ_ErrorCount() > 255u ? 255u : BQ_ErrorCount());
    send(CAN_ID_BMS_BQ_STATUS, d);
}
