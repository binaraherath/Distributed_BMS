/**
 ******************************************************************************
 * @file    bms_display.c
 * @brief   OLED pages (see header). 6x8 font -> 21 columns x 8 rows.
 ******************************************************************************
 */
#include "bms_display.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

enum { PAGE_SUMMARY = 0, PAGE_CELLS, PAGE_TEMPS, PAGE_COUNT, PAGE_FAULT };

static bool     s_ready;
static uint8_t  s_slot;
static uint32_t s_slot_start_ms;

/* "value / divisor" with a fixed number of decimals, e.g.
 * fmt_fixed(buf, 59843, 1000, 2) -> "59.84", fmt_fixed(buf, -123, 10, 1) -> "-12.3" */
void fmt_fixed(char *out, int32_t value, uint32_t divisor, uint8_t decimals)
{
    uint32_t scale = 1, mag, ip, fp;
    bool neg = value < 0;

    for (uint8_t i = 0; i < decimals; i++) scale *= 10u;
    mag = (uint32_t)(neg ? -value : value);
    /* round to the shown resolution */
    mag = (uint32_t)(((uint64_t)mag * scale + divisor / 2u) / divisor);
    ip = mag / scale;
    fp = mag % scale;

    if (decimals == 0u) sprintf(out, "%s%lu", neg ? "-" : "", (unsigned long)ip);
    else sprintf(out, "%s%lu.%0*lu", neg ? "-" : "", (unsigned long)ip, (int)decimals, (unsigned long)fp);
}

static void text(uint8_t col, uint8_t y, const char *s)
{
    (void)SSD1306_Text((uint8_t)(col * 6u), y, s, 1, false);
}

static void header(const char *title, const BmsContext_t *b)
{
    const char *st = BMS_App_StateName(b->state);
    uint8_t w = (uint8_t)(strlen(st) * 6u + 2u);

    text(0, 0, title);
    SSD1306_FillRect((uint8_t)(128u - w), 0, w, 8, true);
    (void)SSD1306_Text((uint8_t)(128u - w + 1u), 0, st, 1, true);
    SSD1306_HLine(0, 9, 128);
}

static void page_summary(const BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    char v[14], s[40], a[14], c[14];

    header("MASTER BMU", b);

    fmt_fixed(v, m->stack_mv, 1000, 2);
    strcat(v, "V");
    (void)SSD1306_Text(0, 13, v, 2, false);

    fmt_fixed(a, m->current_ma, 1000, 1);
    snprintf(s, sizeof(s), "%sA", a);
    text(14, 13, s);
    fmt_fixed(a, (int32_t)(b->soc_pct * 10.0f), 10, 1);
    snprintf(s, sizeof(s), "%s%%", a);
    text(14, 22, s);

    fmt_fixed(a, m->cell_min_mv, 1000, 3);
    fmt_fixed(c, m->cell_max_mv, 1000, 3);
    snprintf(s, sizeof(s), "Cell %s-%sV", a, c);
    text(0, 33, s);

    fmt_fixed(a, m->temp_max_dc, 10, 1);
    snprintf(s, sizeof(s), "dV %umV Tmax %sC", m->cell_delta_mv, a);
    text(0, 42, s);

    if (b->faults != 0u) {
        snprintf(s, sizeof(s), "FAULT %s", BMS_App_FaultName(b->faults));
        (void)SSD1306_Text(0, 55, s, 1, true);
    } else {
        uint32_t up = b->uptime_s;
        snprintf(s, sizeof(s), "%s  %02lu:%02lu:%02lu",
                 (b->warnings & BMS_WRN_CTRL_LOST) ? "CAN --" : "CAN OK",
                 (unsigned long)(up / 3600u), (unsigned long)((up / 60u) % 60u),
                 (unsigned long)(up % 60u));
        text(0, 55, s);
    }
}

static void page_cells(const BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    char s[24];

    header("CELLS mV", b);
    for (uint8_t i = 0; i < BMS_CELLS; i++) {
        bool bleeding = ((b->bal_mask >> i) & 1u) != 0u;
        snprintf(s, sizeof(s), "%4u", m->cell_mv[i]);
        (void)SSD1306_Text((uint8_t)((i % 4u) * 32u), (uint8_t)(12u + (i / 4u) * 10u), s, 1, bleeding);
    }
    snprintf(s, sizeof(s), "min C%02u  max C%02u", m->cell_min_idx + 1u, m->cell_max_idx + 1u);
    text(0, 55, s);
}

static void page_temps(const BmsContext_t *b)
{
    const BmsMeasurement_t *m = &b->m;
    char s[24], t0[8], t1[8];

    header("TEMP degC", b);
    fmt_fixed(t0, m->ntc_dc[0], 10, 1); fmt_fixed(t1, m->ntc_dc[1], 10, 1);
    snprintf(s, sizeof(s), "T1 %-6s T2 %s", t0, t1);
    text(0, 13, s);
    fmt_fixed(t0, m->ntc_dc[2], 10, 1); fmt_fixed(t1, m->ntc_dc[3], 10, 1);
    snprintf(s, sizeof(s), "T3 %-6s T4 %s", t0, t1);
    text(0, 22, s);
    fmt_fixed(t0, m->die_dc, 10, 1);
    snprintf(s, sizeof(s), "BQ die %s", t0);
    text(0, 31, s);

    snprintf(s, sizeof(s), "SoH %u%%  Bal %s", (unsigned)(b->soh_pct + 0.5f),
             b->bal_enabled ? "ON" : "OFF");
    text(0, 42, s);
    snprintf(s, sizeof(s), "FW %u.%u  I2C err %lu", BMS_FW_VERSION_MAJOR, BMS_FW_VERSION_MINOR,
             (unsigned long)BQ_ErrorCount());
    text(0, 55, s);
}

static void page_fault(const BmsContext_t *b)
{
    uint8_t row = 0;

    SSD1306_FillRect(0, 0, 128, 11, true);
    (void)SSD1306_Text(34, 2, "! FAULT !", 1, true);
    for (uint8_t bit = 0; bit < 24u && row < 5u; bit++) {
        if (b->faults & (1UL << bit)) {
            text(0, (uint8_t)(15u + row * 9u), BMS_App_FaultName(1UL << bit));
            row++;
        }
    }
}

void Display_Init(SPI_HandleTypeDef *hspi)
{
    SSD1306_Init(hspi);
    s_ready = true;
    s_slot = 0;
    s_slot_start_ms = HAL_GetTick();
}

void Display_Splash(const char *line1, const char *line2)
{
    if (!s_ready) return;
    SSD1306_Clear();
    (void)SSD1306_Text(22, 4, "16S BMS", 2, false);          /* 7 x 12 px = 84 px */
    (void)SSD1306_Text(31, 24, "DISTRIBUTED", 1, false);
    SSD1306_HLine(0, 35, 128);
    if (line1) text(0, 42, line1);
    if (line2) text(0, 53, line2);
    SSD1306_Update();
}

void Display_Update(const BmsContext_t *b)
{
    uint8_t page;

    if (!s_ready) return;

    if ((HAL_GetTick() - s_slot_start_ms) >= BMS_DISPLAY_PAGE_MS) {
        s_slot_start_ms = HAL_GetTick();
        s_slot = (uint8_t)((s_slot + 1u) % (2u * PAGE_COUNT));
    }

    /* Normal: summary, cells, temps. With a fault, every second slot is the
     * fault page so the cause is always on screen within 3 s. */
    if (b->faults != 0u && (s_slot & 1u)) page = PAGE_FAULT;
    else page = (uint8_t)((s_slot / (b->faults != 0u ? 2u : 1u)) % PAGE_COUNT);

    SSD1306_Clear();
    switch (page) {
        case PAGE_SUMMARY: page_summary(b); break;
        case PAGE_CELLS:   page_cells(b);   break;
        case PAGE_TEMPS:   page_temps(b);   break;
        default:           page_fault(b);   break;
    }
    SSD1306_Update();
}
