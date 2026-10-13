/**
 ******************************************************************************
 * @file    bms_display.h
 * @brief   0.96" OLED user interface: rotating summary / cells / temperature
 *          pages, with a fault page mixed in while a fault is latched.
 ******************************************************************************
 */
#ifndef BMS_DISPLAY_H
#define BMS_DISPLAY_H

#include "bms_app.h"

void Display_Init(SPI_HandleTypeDef *hspi);
void Display_Splash(const char *line1, const char *line2);
void Display_Update(const BmsContext_t *b);     /* every BMS_TASK_DISPLAY_MS */

/* Fixed-point text helpers, shared with the console (no float printf) */
void fmt_fixed(char *out, int32_t value, uint32_t divisor, uint8_t decimals);

#endif /* BMS_DISPLAY_H */
