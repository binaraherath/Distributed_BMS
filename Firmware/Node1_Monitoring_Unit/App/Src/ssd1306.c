/**
 ******************************************************************************
 * @file    ssd1306.c
 * @brief   SSD1306 OLED driver (see header).
 ******************************************************************************
 */
#include "ssd1306.h"
#include "font5x7.h"
#include "main.h"
#include <string.h>

#define SPI_TIMEOUT_MS  20u

static SPI_HandleTypeDef *s_spi;
static uint8_t s_fb[SSD1306_WIDTH * SSD1306_HEIGHT / 8u];

static void write_cmds(const uint8_t *c, uint16_t n)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
    (void)HAL_SPI_Transmit(s_spi, (uint8_t *)c, n, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

void SSD1306_Init(SPI_HandleTypeDef *hspi)
{
    /* 128x64, internal charge pump, horizontal addressing. A1/C8 is the
     * normal orientation of 0.96" modules; use A0/C0 to flip it 180 deg. */
    static const uint8_t init[] = {
        0xAE,             /* display off                  */
        0xD5, 0x80,       /* clock divide                 */
        0xA8, 0x3F,       /* multiplex 64                 */
        0xD3, 0x00,       /* display offset 0             */
        0x40,             /* start line 0                 */
        0x8D, 0x14,       /* charge pump on               */
        0x20, 0x00,       /* horizontal addressing        */
        0xA1,             /* segment remap                */
        0xC8,             /* COM scan descending          */
        0xDA, 0x12,       /* COM pins                     */
        0x81, 0xCF,       /* contrast                     */
        0xD9, 0xF1,       /* pre-charge                   */
        0xDB, 0x40,       /* VCOMH                        */
        0xA4,             /* follow RAM                   */
        0xA6,             /* normal (not inverted)        */
        0x2E,             /* scroll off                   */
        0xAF              /* display on                   */
    };

    s_spi = hspi;
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(5);

    write_cmds(init, sizeof(init));
    SSD1306_Clear();
    SSD1306_Update();
}

void SSD1306_Clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void SSD1306_Update(void)
{
    static const uint8_t window[] = { 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07 };

    if (s_spi == NULL) return;
    write_cmds(window, sizeof(window));
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
    (void)HAL_SPI_Transmit(s_spi, s_fb, sizeof(s_fb), SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

void SSD1306_Pixel(uint8_t x, uint8_t y, bool on)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    uint16_t i = (uint16_t)(x + (y / 8u) * SSD1306_WIDTH);
    uint8_t  b = (uint8_t)(1u << (y & 7u));
    if (on) s_fb[i] |= b; else s_fb[i] &= (uint8_t)~b;
}

void SSD1306_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on)
{
    for (uint8_t j = 0; j < h; j++)
        for (uint8_t i = 0; i < w; i++)
            SSD1306_Pixel((uint8_t)(x + i), (uint8_t)(y + j), on);
}

void SSD1306_HLine(uint8_t x, uint8_t y, uint8_t w)
{
    SSD1306_FillRect(x, y, w, 1, true);
}

uint8_t SSD1306_Text(uint8_t x, uint8_t y, const char *s, uint8_t scale, bool invert)
{
    if (scale == 0u) scale = 1u;

    while (*s != '\0' && x < SSD1306_WIDTH) {
        char c = *s++;
        if (c < FONT5X7_FIRST || c > FONT5X7_LAST) c = '?';
        const uint8_t *g = k_font5x7[c - FONT5X7_FIRST];

        /* 6x8 cell: 5 glyph columns + 1 spacing column, 7 rows + 1 spacing row */
        for (uint8_t col = 0; col < 6u; col++) {
            uint8_t bits = (col < 5u) ? g[col] : 0u;
            for (uint8_t row = 0; row < 8u; row++) {
                bool on = ((bits >> row) & 1u) != 0u;
                if (invert) on = !on;
                if (scale == 1u) {
                    SSD1306_Pixel((uint8_t)(x + col), (uint8_t)(y + row), on);
                } else {
                    SSD1306_FillRect((uint8_t)(x + col * scale), (uint8_t)(y + row * scale),
                                     scale, scale, on);
                }
            }
        }
        x = (uint8_t)(x + 6u * scale);
    }
    return x;
}
