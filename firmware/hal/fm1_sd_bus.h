/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 SD card on the LCD's SPI1 (builder item SDCARD; a hardware mod, docs/SD-CARD-DESIGN.md):
 *   PC9  CLK -> card CLK        PC10 DO -> card CMD (DI)
 *   PC8  D/C <- card DAT0 (DO) through 1 kOhm (the card drives it only while selected)
 *   PC7  CS  -> 74LVC1G04 -> card DAT3 (CS): the card is selected whenever the LCD is not.
 * So "select the card" is the LCD's CS high, and "deselect" is the LCD's CS low: the LCD then sees the bytes clocked
 * while the card is deselected, with D/C high (data). The caller sends the LCD a NOP first (src/storage/sd/sdcard.c
 * sd_begin), which ends any memory write: data with no command waiting is ignored [ST7789V datasheet, NOP / RAMWR].
 * Main loop only, the LCD idle (lcd_sync) between fm1_sd_bus_begin and fm1_sd_bus_end.
 *
 *   fm1_sd_bus_begin(baud)  the bus to the card: PC8 readable, SPI1 at lsb / (baud + 1)
 *   fm1_sd_baud(baud)       the clock, as above
 *   fm1_sd_sel() / _desel() the card selected (LCD CS high, PC8 an input) / deselected (PC8 driven high, LCD CS low)
 *   fm1_sd_tx(b), _rx()     one byte out / in (0xFF out while reading)
 *   fm1_sd_bus_end()        the card selected and idle, PC8 driven (D/C), SPI1 as the LCD set it; the caller puts the
 *                           LCD's clock back (fm1_lcd_baud)
 * Unverified on an FM-1 (no SD mod built yet): which way SPI1 reads. fm1_sd_mode picks it at run time (console "sd mode"):
 *   FM1_SD_DUPLEX  each byte sent in transmit mode and the shifted-in byte read back from BUF
 *   FM1_SD_DIRRX   as the flash driver reads SPI0 (hal/fm1_flash.h fl_rx: CON b3 + b12 DIR = receive, 0xFF written)
 *   FM1_SD_BITBANG PC9 / PC10 / PC8 as plain GPIO, SPI1 off its pins (IOMAP_CON1 b4 cleared) for the session
 * Blocks (512 B of a CMD17 / CMD18 read; storage/sd/sd_mmc.c sd_rd_*):
 *   fm1_sd_rx_block(p, n)   n bytes in by the CPU, the byte mode's way with CON set once (the fallback, any mode)
 *   fm1_sd_rx_dma(p, n)     n bytes in by SPI1's DMA, returns at once: CON b3 + b12 (DIR = receive, as DIRRX and the
 *                           SDK's slave receive, ac79-sdk spi_recv.c: b12, then ADR, then CNT starts it), the master
 *                           clocking n bytes into RAM p (4-byte aligned) while the CPU runs; not in BITBANG.
 *                           Unverified on an FM-1: that SPI1 clocks a DMA receive as master, what DO holds meanwhile
 *                           (a card ignores DI during a read unless it looks like a command: high, low or steady is
 *                           fine; PC10 is pulled up for the session in case it floats), and whether the DMA writes
 *                           RAM past a data cache (none known on this chip's RAM)
 *   fm1_sd_rx_dma_done()    1 when it has finished (CON b15)
 *   fm1_sd_rx_dma_end()     the end waited for (bounded), pending cleared, SPI1 back to transmit; 0 on a timeout
 *   fm1_sd_tx_dma(p, n)     n bytes out by DMA (the LCD's own path: lcd.c lcd_data), returns at once; _dma_end as above
 * fm1_sd_dma (console "sd dma on|off", sd_mount's probe): 1 = the block data by DMA when the mode is not BITBANG. */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"
#include "fm1_lcd_hw.h"

enum { FM1_SD_DUPLEX = 0, FM1_SD_DIRRX = 1, FM1_SD_BITBANG = 2 };
#define FM1_SD_BAUD_SLOW 149u          /* 60 MHz / 150 = 400 kHz: the card's start-up (at most 400 kHz) */
#define FM1_SD_BAUD_FAST 2u            /* 20 MHz (SD default speed allows 25) */
#define FM1_SD_BAUD_OVER 1u            /* 30 MHz: past the default speed's 25; many cards take it (console sd bench) */
#define FM1_SD_CON_LCD 0x4021u         /* SPI1 as fm1_lcd_hw_init sets it */

static uint8_t fm1_sd_mode = FM1_SD_DUPLEX;
static uint8_t fm1_sd_dma;             /* 1: block data by DMA (not in BITBANG); 0 until sd_mount's probe proves it */
static uint8_t fm1_sd_bb_half;         /* bit-bang: spin loops per half clock (0: as fast as the GPIO writes go) */

static void fm1_sd_spi_done(void)
{
    uint32_t n;
    for (n = 0; n < 100000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_sd_baud(uint32_t b) { FM1_LCD_SPI_BAUD = b; fm1_sd_bb_half = (uint8_t)(b > 20u ? 40u : 0u); }

FM1_INLINE void fm1_sd_sel(void)
{
    FM1_LCD_PC_OUT |= FM1_LCD_CS;                    /* the LCD off the bus: the card on it */
    FM1_LCD_PC_DIR |= FM1_LCD_DC;                    /* PC8 the card's DO */
}

FM1_INLINE void fm1_sd_desel(void)
{
    FM1_LCD_PC_OUT |= FM1_LCD_DC;                    /* D/C high (data, ignored after the NOP), driven before the */
    FM1_LCD_PC_DIR &= ~FM1_LCD_DC;                   /* card lets go: the 1 kOhm takes any overlap */
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;                   /* the card off the bus */
}

static void fm1_sd_bb_wait(void)
{
    for (volatile uint32_t i = 0; i < fm1_sd_bb_half; i++)
        ;
}

static uint8_t fm1_sd_bb(uint8_t b)                  /* SPI mode 0, MSB first */
{
    uint32_t i, r = 0;
    for (i = 0; i < 8u; i++, b = (uint8_t)(b << 1)) {
        if (b & 0x80u)
            FM1_LCD_PC_OUT |= FM1_LCD_DO;
        else
            FM1_LCD_PC_OUT &= ~FM1_LCD_DO;
        fm1_sd_bb_wait();
        FM1_LCD_PC_OUT |= FM1_LCD_CLK;
        r = (r << 1) | ((FM1_PR(FM1_PC, FM1_IN) >> 8) & 1u);
        fm1_sd_bb_wait();
        FM1_LCD_PC_OUT &= ~FM1_LCD_CLK;
    }
    return (uint8_t)r;
}

static void fm1_sd_tx(uint8_t b)
{
    if (fm1_sd_mode == FM1_SD_BITBANG) {
        (void)fm1_sd_bb(b);
        return;
    }
    FM1_LCD_SPI_CON |= 8u;
    FM1_LCD_SPI_CON &= ~0x1000u;
    FM1_LCD_SPI_BUF = b;
    fm1_sd_spi_done();
    FM1_LCD_SPI_CON &= ~8u;
}

static uint8_t fm1_sd_rx(void)
{
    uint8_t v;
    if (fm1_sd_mode == FM1_SD_BITBANG)
        return fm1_sd_bb(0xFFu);
    FM1_LCD_SPI_CON |= 8u;
    if (fm1_sd_mode == FM1_SD_DIRRX)
        FM1_LCD_SPI_CON |= 0x1000u;
    else
        FM1_LCD_SPI_CON &= ~0x1000u;
    FM1_LCD_SPI_BUF = 0xFFu;
    fm1_sd_spi_done();
    v = (uint8_t)FM1_LCD_SPI_BUF;
    FM1_LCD_SPI_CON &= ~(8u | 0x1000u);
    return v;
}

static void fm1_sd_rx_block(uint8_t *p, uint32_t n)   /* n > 0 */
{
    uint32_t c, k;
    if (fm1_sd_mode == FM1_SD_BITBANG) {
        do
            *p++ = fm1_sd_bb(0xFFu);
        while (--n);
        return;
    }
    c = FM1_LCD_SPI_CON | 8u;
    c = fm1_sd_mode == FM1_SD_DIRRX ? c | 0x1000u : c & ~0x1000u;
    FM1_LCD_SPI_CON = c;
    do {
        FM1_LCD_SPI_BUF = 0xFFu;
        for (k = 0; k < 100000u && !(FM1_LCD_SPI_CON & 0x8000u); k++)
            ;
        FM1_LCD_SPI_CON = c | 0x4000u;               /* (b14 clears the pending; b15 reads back as it is) */
        *p++ = (uint8_t)FM1_LCD_SPI_BUF;
    } while (--n);
    FM1_LCD_SPI_CON = c & ~(8u | 0x1000u);
}

FM1_INLINE void fm1_sd_rx_dma(void *p, uint32_t n)      /* n > 0 (SPI_CNT 0 never completes) */
{
    FM1_LCD_SPI_CON |= 8u | 0x1000u;                 /* both data pins, receive */
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;                             /* starts it */
}

FM1_INLINE void fm1_sd_tx_dma(const void *p, uint32_t n)   /* n > 0 */
{
    FM1_LCD_SPI_CON |= 8u;
    FM1_LCD_SPI_CON &= ~0x1000u;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

FM1_INLINE uint32_t fm1_sd_rx_dma_done(void) { return (FM1_LCD_SPI_CON & 0x8000u) != 0; }

static uint32_t fm1_sd_rx_dma_end(void)
{
    uint32_t n;
    for (n = 0; n < 1000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_CON &= ~(8u | 0x1000u);
    return n < 1000000u;
}

static void fm1_sd_bus_begin(uint32_t baud)
{
    FM1_PR(FM1_PC, FM1_DIE) |= FM1_LCD_DC;           /* PC8 readable */
    FM1_PR(FM1_PC, FM1_PU) |= FM1_LCD_DC | FM1_LCD_DO;   /* (no card: reads 0xFF, as an absent card answers; DO high
                                                          * if SPI1 lets it float while it receives) */
    if (fm1_sd_mode == FM1_SD_BITBANG) {
        FM1_LCD_SPI_CON = 0;
        FM1_LCD_IOMAP_CON1 &= ~0x10u;                /* SPI1 off PC9 / PC10: plain GPIO */
        FM1_LCD_PC_OUT &= ~FM1_LCD_CLK;
    }
    fm1_sd_baud(baud);
}

static void fm1_sd_bus_end(void)
{
    fm1_sd_sel();                                    /* the LCD deselected, as lcd.c leaves it */
    FM1_PR(FM1_PC, FM1_PU) &= ~(FM1_LCD_DC | FM1_LCD_DO);
    FM1_LCD_PC_DIR &= ~FM1_LCD_DC;                   /* D/C driven again */
    if (fm1_sd_mode == FM1_SD_BITBANG)
        FM1_LCD_IOMAP_CON1 |= 0x10u;
    FM1_LCD_SPI_CON = FM1_SD_CON_LCD;
}
