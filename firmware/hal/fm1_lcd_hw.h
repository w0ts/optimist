/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 LCD wiring: SPI1 on PC9 CLK, PC10 DO, PC7 CS,
 * PC8 D/C; backlight PA2, active low. The panel protocol is src/lcd.c.
 *
 *   fm1_lcd_hw_init()          pins, backlight on, SPI1 master at BAUD 4 (lcd_init then sets LCD_BAUD, 1)
 *   fm1_lcd_baud(b)            SPI1 clock = lsb / (b + 1)
 *   fm1_lcd_send_cmd(c)        D/C low, CS low, one byte (then fm1_lcd_wait)
 *   fm1_lcd_send_data(p, n)    D/C high, CS low, DMA n bytes from RAM p
 *   fm1_lcd_wait()             SPI done (or timeout, counted), pending cleared
 *   fm1_lcd_deselect()         CS high, D/C high (the panel ignores D/C while deselected)
 * With the SD card mod (FELUCCA_SDCARD, docs/SD-CARD-DESIGN.md: the card's CS is the LCD's inverted, its DO on D/C) the
 * screen idles SELECTED instead (CS low: the ST7789 allows CS tied low; D/C high, no clock), so the card is deselected and
 * lets go of PC8 between its sessions: an LCD command's D/C low never meets the card's DO (2026-10-25 review). The
 * card's sessions (hal/fm1_sd_bus.h) select it themselves. A firmware that idles CS high (the stock update loader)
 * selects the card: the card comes out before a stock update.
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_LCD_PC_OUT (*(volatile uint32_t *)0x50080u)
#define FM1_LCD_PC_DIR (*(volatile uint32_t *)0x50088u)
#define FM1_LCD_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
#define FM1_LCD_SPI_CON (*(volatile uint32_t *)0x11D00u)
#define FM1_LCD_SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define FM1_LCD_SPI_BUF (*(volatile uint32_t *)0x11D08u)
#define FM1_LCD_SPI_ADR (*(volatile uint32_t *)0x11D0Cu)
#define FM1_LCD_SPI_CNT (*(volatile uint32_t *)0x11D10u)
#define FM1_LCD_PA_OUT (*(volatile uint32_t *)0x50000u)
#define FM1_LCD_PA_DIR (*(volatile uint32_t *)0x50008u)
#define FM1_LCD_BL (1u << 2)
#define FM1_LCD_CS (1u << 7)
#define FM1_LCD_DC (1u << 8)
#define FM1_LCD_CLK (1u << 9)
#define FM1_LCD_DO (1u << 10)
#if defined(FELUCCA_SDCARD) && FELUCCA_SDCARD
#define FM1_LCD_IDLE_CS 0u                              /* idle selected: the SD card (CS inverted) off the bus */
#else
#define FM1_LCD_IDLE_CS FM1_LCD_CS
#endif

static uint32_t fm1_lcd_timeouts;

FM1_INLINE void fm1_lcd_hw_init(void)
{
    FM1_LCD_PA_OUT &= ~FM1_LCD_BL;
    FM1_LCD_PA_DIR &= ~FM1_LCD_BL;
    FM1_LCD_IOMAP_CON1 |= 0x10u;
#if FM1_LCD_IDLE_CS
    FM1_LCD_PC_OUT |= FM1_LCD_CS | FM1_LCD_DC;          /* deselected, D/C high (as fm1_lcd_deselect) */
#else
    FM1_LCD_PC_OUT |= FM1_LCD_DC;                       /* idle selected, D/C high (as fm1_lcd_deselect, the SD mod) */
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
#endif
    FM1_LCD_PC_OUT &= ~(FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PC_DIR &= ~(FM1_LCD_CS | FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_SPI_CON = 0x4021u;
    FM1_LCD_SPI_BAUD = 4u;
}

FM1_INLINE void fm1_lcd_baud(uint32_t b) { FM1_LCD_SPI_BAUD = b; }

/* backlight brightness by software PWM of PA2 (after X0X, charlesvestal/fm1-x0x 61654ba, Charles Vestal,
 * GPL-3.0-only), from the 10 kHz TIMER5 ISR (the key matrix drives port A from that ISR too, so nothing races
 * it there): on for duty of every 16 ticks, 625 Hz; duty 16 = always on (src/bright.c, FELUCCA_BRIGHT).
 * On X0X it froze a real FM-1 at duty 1 (issue #2).
 * Cause (Felucca 1.1.5.1, 213df5a: PA2 is also the /OE of the key matrix's 595s): with PA2 high no key, button or
 * knob reads. So the tick pulls PA2 low (fm1_lcd_bl_scan) before the matrix is scanned, and only afterwards does
 * fm1_lcd_bl_tick set it for the 100 us until the next tick. */
FM1_INLINE void fm1_lcd_bl_scan(void)
{
    FM1_LCD_PA_OUT &= ~FM1_LCD_BL;                      /* 595s enabled (and the backlight on) for the scan */
}

FM1_INLINE void fm1_lcd_bl_tick(uint32_t duty)
{
    static uint8_t ph;
    ph = (uint8_t)((ph + 1u) & 15u);
    if (ph < duty)
        FM1_LCD_PA_OUT &= ~FM1_LCD_BL;                  /* active low: on */
    else
        FM1_LCD_PA_OUT |= FM1_LCD_BL;
}

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_LCD_PC_OUT |= FM1_LCD_IDLE_CS | FM1_LCD_DC; }   /* (the SD mod: CS stays low) */

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_LCD_PC_OUT &= ~FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_LCD_PC_OUT |= FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}
