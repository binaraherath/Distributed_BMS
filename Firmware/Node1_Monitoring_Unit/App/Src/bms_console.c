/**
 ******************************************************************************
 * @file    bms_console.c
 * @brief   Debug console (see header).
 ******************************************************************************
 */
#include "bms_console.h"
#include "bms_display.h"
#include "bms_can.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TX_BUF_SIZE     1024u
#define LINE_MAX        32u

static UART_HandleTypeDef *s_uart;

static uint8_t           s_tx[TX_BUF_SIZE];
static volatile uint16_t s_tx_head, s_tx_tail;
static volatile uint16_t s_tx_len;          /* bytes in flight */

static uint8_t           s_rx_byte;
static char              s_line[LINE_MAX];
static volatile uint8_t  s_line_len;
static volatile bool     s_line_ready;
static bool              s_log_enabled;

/* ==========================================================================
 * Output
 * ========================================================================== */

/* Start the next contiguous chunk. Called with interrupts masked. */
static void tx_kick(void)
{
    uint16_t n;

    if (s_uart == NULL || s_tx_len != 0u || s_tx_head == s_tx_tail) return;
    n = (s_tx_head > s_tx_tail) ? (uint16_t)(s_tx_head - s_tx_tail)
                                : (uint16_t)(TX_BUF_SIZE - s_tx_tail);
    if (HAL_UART_Transmit_IT(s_uart, &s_tx[s_tx_tail], n) == HAL_OK) s_tx_len = n;
}

static void tx_write(const char *s, uint16_t n)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (uint16_t i = 0; i < n; i++) {
        uint16_t next = (uint16_t)((s_tx_head + 1u) % TX_BUF_SIZE);
        if (next == s_tx_tail) break;           /* full: drop the rest */
        s_tx[s_tx_head] = (uint8_t)s[i];
        s_tx_head = next;
    }
    tx_kick();
    __set_PRIMASK(primask);
}

void Console_Printf(const char *fmt, ...)
{
#if (BMS_USE_CONSOLE == 1)
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) tx_write(buf, (uint16_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1));
#else
    (void)fmt;
#endif
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h)
{
    if (h != s_uart) return;
    s_tx_tail = (uint16_t)((s_tx_tail + s_tx_len) % TX_BUF_SIZE);
    s_tx_len = 0;
    tx_kick();
}

/* ==========================================================================
 * Input
 * ========================================================================== */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h != s_uart) return;
    char c = (char)s_rx_byte;

    if (!s_line_ready) {
        if (c == '\r' || c == '\n') {
            if (s_line_len > 0u) { s_line[s_line_len] = '\0'; s_line_ready = true; }
        } else if (s_line_len < LINE_MAX - 1u && c >= ' ') {
            s_line[s_line_len++] = c;
        }
    }
    (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h != s_uart) return;
    (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

void Console_Init(UART_HandleTypeDef *huart)
{
    s_uart = huart;
#if (BMS_USE_CONSOLE == 1)
    (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
#endif
}

/* ==========================================================================
 * Commands
 * ========================================================================== */

static void print_status(const BmsContext_t *b)
{
    char v[12], i[12], soc[12], tmax[12];
    fmt_fixed(v, b->m.stack_mv, 1000, 2);
    fmt_fixed(i, b->m.current_ma, 1000, 2);
    fmt_fixed(soc, (int32_t)(b->soc_pct * 10.0f), 10, 1);
    fmt_fixed(tmax, b->m.temp_max_dc, 10, 1);
    Console_Printf("[%lu s] %s  %sV %sA  SoC %s%%  cell %u..%u mV (d%u)  Tmax %sC  "
                   "flt 0x%06lX wrn 0x%04X bal 0x%04X\r\n",
                   (unsigned long)b->uptime_s, BMS_App_StateName(b->state), v, i, soc,
                   b->m.cell_min_mv, b->m.cell_max_mv, b->m.cell_delta_mv, tmax,
                   (unsigned long)b->faults, b->warnings, b->bal_mask);
}

static void print_cells(const BmsContext_t *b)
{
    for (uint8_t c = 0; c < BMS_CELLS; c++) {
        Console_Printf("C%02u %4u mV%s%s", c + 1u, b->m.cell_mv[c],
                       ((b->bal_mask >> c) & 1u) ? "*" : " ",
                       ((c % 4u) == 3u) ? "\r\n" : "   ");
    }
    Console_Printf("stack %ld mV  sum %lu mV  (* = bleeding)\r\n",
                   (long)b->m.stack_mv, (unsigned long)b->m.cell_sum_mv);
}

static void print_temps(const BmsContext_t *b)
{
    char t[12];
    for (uint8_t n = 0; n < BMS_NTC_COUNT; n++) {
        fmt_fixed(t, b->m.ntc_dc[n], 10, 1);
        Console_Printf("NTC%u %sC%s  ", n + 1u, t, (BMS_NTC_FITTED_MASK & (1u << n)) ? "" : "(off)");
    }
    fmt_fixed(t, b->m.die_dc, 10, 1);
    Console_Printf("BQ die %sC\r\n", t);
}

static void print_bq(const BmsContext_t *b)
{
    Console_Printf("BQ76952 dev 0x%04X fw 0x%04X ready %u  I2C errors %lu\r\n",
                   b->bq_device, b->bq_fw, b->bq_ready, (unsigned long)BQ_ErrorCount());
    Console_Printf("SafetyA %02X/%02X  B %02X/%02X  C %02X/%02X (alert/status)\r\n",
                   b->bq.alert_a, b->bq.status_a, b->bq.alert_b, b->bq.status_b,
                   b->bq.alert_c, b->bq.status_c);
    Console_Printf("BatStatus %04X  Alarm %04X (latched %04X)  FET %02X  ALERT pin %s\r\n",
                   b->bq.battery_status, b->bq.alarm_status, b->bq_alarm_latched,
                   b->bq.fet_status, b->alert_pin_ok ? "OK" : "NOT VERIFIED");
    Console_Printf("CAN frames dropped %lu\r\n", (unsigned long)BmsCan_DroppedFrames());
}

static void print_faults(const BmsContext_t *b)
{
    if (b->faults == 0u) { Console_Printf("No latched faults\r\n"); return; }
    Console_Printf("Latched faults:");
    for (uint8_t bit = 0; bit < 24u; bit++) {
        if (b->faults & (1UL << bit)) Console_Printf(" %s", BMS_App_FaultName(1UL << bit));
    }
    Console_Printf("\r\n");
}

static void run(const char *cmd)
{
    const BmsContext_t *b = &g_bms;

    if      (!strcmp(cmd, "help"))    Console_Printf("status cells temps bq faults clear bal on|off "
                                                     "log on|off reconf reset\r\n");
    else if (!strcmp(cmd, "status"))  print_status(b);
    else if (!strcmp(cmd, "cells"))   print_cells(b);
    else if (!strcmp(cmd, "temps"))   print_temps(b);
    else if (!strcmp(cmd, "bq"))      print_bq(b);
    else if (!strcmp(cmd, "faults"))  print_faults(b);
    else if (!strcmp(cmd, "clear"))   Console_Printf("clear faults: %s\r\n",
                                                     BMS_App_ClearFaults() ? "done" : "refused (condition active)");
    else if (!strcmp(cmd, "bal on"))  { BMS_App_SetBalancing(true);  Console_Printf("balancing allowed\r\n"); }
    else if (!strcmp(cmd, "bal off")) { BMS_App_SetBalancing(false); Console_Printf("balancing blocked\r\n"); }
    else if (!strcmp(cmd, "log on"))  s_log_enabled = true;
    else if (!strcmp(cmd, "log off")) s_log_enabled = false;
    else if (!strcmp(cmd, "reconf"))  { BMS_App_RequestReconfigure(); Console_Printf("BQ re-configure queued\r\n"); }
    else if (!strcmp(cmd, "reset"))   { HAL_Delay(20); NVIC_SystemReset(); }
    else Console_Printf("unknown '%s' - type help\r\n", cmd);
}

void Console_Process(void)
{
    if (!s_line_ready) return;
    char cmd[LINE_MAX];
    strncpy(cmd, s_line, LINE_MAX);
    cmd[LINE_MAX - 1u] = '\0';
    s_line_len = 0;
    s_line_ready = false;
    Console_Printf("> %s\r\n", cmd);
    run(cmd);
}

void Console_Log(const BmsContext_t *b)
{
    if (s_log_enabled) print_status(b);
}
