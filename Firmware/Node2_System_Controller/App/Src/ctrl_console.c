/**
 ******************************************************************************
 * @file    ctrl_console.c
 * @brief   Node 2 debug console (see header).
 ******************************************************************************
 */
#include "ctrl_console.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TX_BUF_SIZE     1024u
#define LINE_MAX        32u

static UART_HandleTypeDef *s_uart;
static uint8_t           s_tx[TX_BUF_SIZE];
static volatile uint16_t s_tx_head, s_tx_tail, s_tx_len;
static uint8_t           s_rx_byte;
static char              s_line[LINE_MAX];
static volatile uint8_t  s_line_len;
static volatile bool     s_line_ready;
static bool              s_log_enabled = true;

static void tx_kick(void)
{
    uint16_t n;
    if (s_uart == NULL || s_tx_len != 0u || s_tx_head == s_tx_tail) return;
    n = (s_tx_head > s_tx_tail) ? (uint16_t)(s_tx_head - s_tx_tail)
                                : (uint16_t)(TX_BUF_SIZE - s_tx_tail);
    if (HAL_UART_Transmit_IT(s_uart, &s_tx[s_tx_tail], n) == HAL_OK) s_tx_len = n;
}

void Console_Printf(const char *fmt, ...)
{
#if (CTRL_USE_CONSOLE == 1)
    char buf[200];
    va_list ap;
    uint32_t primask;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;

    primask = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < n; i++) {
        uint16_t next = (uint16_t)((s_tx_head + 1u) % TX_BUF_SIZE);
        if (next == s_tx_tail) break;
        s_tx[s_tx_head] = (uint8_t)buf[i];
        s_tx_head = next;
    }
    tx_kick();
    __set_PRIMASK(primask);
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
    if (h == s_uart) (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

void Console_Init(UART_HandleTypeDef *huart)
{
    s_uart = huart;
#if (CTRL_USE_CONSOLE == 1)
    (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
#endif
}

static void print_status(const CtrlContext_t *c)
{
    const BmsView_t *b = &c->bms;
    long i10 = (long)(b->current_ma / 100);            /* 0.1 A */
    Console_Printf("[%lu s] %-9s key %s estop %s | BMS %s %lu.%02luV %s%ld.%ldA SoC %u.%u%% "
                   "Tmax %dC | out 0x%02X fan %u%% %urpm | flt 0x%02X wrn 0x%02X\r\n",
                   (unsigned long)c->uptime_s, Ctrl_App_StateName(c->state),
                   (c->inputs & CTRL_IN_KEY_ON) ? "ON" : "off",
                   (c->inputs & CTRL_IN_ESTOP_OK) ? "ok" : "OPEN",
                   b->online ? "online" : "OFFLINE",
                   (unsigned long)(b->pack_mv / 1000), (unsigned long)((b->pack_mv % 1000) / 10),
                   (i10 < 0) ? "-" : "", labs(i10) / 10, labs(i10) % 10,
                   b->soc_x10 / 10u, b->soc_x10 % 10u, b->tmax_c,
                   c->outputs, c->fan_duty, c->fan_rpm, c->faults, c->warnings);
}

static void run(const char *cmd)
{
    if (!strcmp(cmd, "help")) {
        Console_Printf("status clear stop bal on|off fan <0-100>|auto log on|off reset\r\n");
    } else if (!strcmp(cmd, "status")) {
        print_status(&g_ctrl);
    } else if (!strcmp(cmd, "clear")) {
        Ctrl_App_ClearFaults();
    } else if (!strcmp(cmd, "stop")) {
        Ctrl_App_RequestStop();
    } else if (!strcmp(cmd, "bal on")) {
        Ctrl_App_SetBalancing(true);
    } else if (!strcmp(cmd, "bal off")) {
        Ctrl_App_SetBalancing(false);
    } else if (!strcmp(cmd, "fan auto")) {
        Ctrl_App_FanOverride(-1);
    } else if (!strncmp(cmd, "fan ", 4)) {
        Ctrl_App_FanOverride((int16_t)atoi(&cmd[4]));
    } else if (!strcmp(cmd, "log on")) {
        s_log_enabled = true;
    } else if (!strcmp(cmd, "log off")) {
        s_log_enabled = false;
    } else if (!strcmp(cmd, "reset")) {
        HAL_Delay(20);
        NVIC_SystemReset();
    } else {
        Console_Printf("unknown '%s' - type help\r\n", cmd);
    }
}

void Console_Process(void)
{
    char cmd[LINE_MAX];
    if (!s_line_ready) return;
    strncpy(cmd, s_line, LINE_MAX);
    cmd[LINE_MAX - 1u] = '\0';
    s_line_len = 0;
    s_line_ready = false;
    Console_Printf("> %s\r\n", cmd);
    run(cmd);
}

void Console_Log(const CtrlContext_t *c)
{
    if (s_log_enabled) print_status(c);
}
