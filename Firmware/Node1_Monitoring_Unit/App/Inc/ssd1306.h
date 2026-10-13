/**
 ******************************************************************************
 * @file    ssd1306.h
 * @brief   Minimal SSD1306 128x64 OLED driver, 4-wire SPI, 1 KB frame buffer.
 *          Pins on the PCB DISPLAY header: D0=SCK(PA5) D1=MOSI(PA7)
 *          RES=PA2 DC=PA3 CS=PA4.
 ******************************************************************************
 */
#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"

#define SSD1306_WIDTH   128u
#define SSD1306_HEIGHT  64u

void SSD1306_Init(SPI_HandleTypeDef *hspi);
void SSD1306_Clear(void);
void SSD1306_Update(void);                  /* push the frame buffer    */

void SSD1306_Pixel(uint8_t x, uint8_t y, bool on);
void SSD1306_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on);
void SSD1306_HLine(uint8_t x, uint8_t y, uint8_t w);

/** Draw text in a 6x8 cell per character, scaled by 'scale' (1 or 2).
 *  'invert' draws light text on a filled background. Returns next x. */
uint8_t SSD1306_Text(uint8_t x, uint8_t y, const char *s, uint8_t scale, bool invert);

#endif /* SSD1306_H */
