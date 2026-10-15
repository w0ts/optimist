/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader (device side). Runs from RAM, started by the SPL from
 * an update record, with interrupts off: USB is polled from the main loop.
 * It is a USB-MIDI device "Felucca Update" (1209:0002) that answers the
 * M-UPGRADE update protocol as "ota-FM-1_700" (hosts look for "ota-"), and
 * writes the app area only (ldr_core.c). The UBOOT soft key works here too.
 * Single compilation unit. */
#include <stdint.h>
#define FELUCCA_LOADER 1
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_USB_PID 0x0002
#define FELUCCA_ID "ota-FM-1_700"      /* Optimist's loader; installers resume ota-FM-1_7XX and 9XX */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
/* the loader may write the app area and erase update records; never the head, nor the SDK's own sectors
 * (fm1_flash.h refuses FL_SDK_SYS: 0xE7000..0xE9FFF, the stock SDK VM and BTIF among them, and key_mac) */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x93000u) || FL_IN(off, n, 0x93000u, 0xFC000u))
#include "fm1_flash.h"
#include "../src/system/libc.c"
#include "../src/io/usb/usb.c"

static uint32_t ldr_ms(void)   /* monotonic ms; survives the TIMER4 wrap (178.9 s) */
{
    static uint32_t last, acc, ms;
    uint32_t now = fm1_ticks();
    acc += now - last;
    last = now;
    ms += acc / (1000u * FM1_TICKS_PER_US);
    acc %= 1000u * FM1_TICKS_PER_US;
    return ms;
}

static void ldr_poll(void)                          /* USB at ~2 kHz, watchdog */
{
    static uint32_t last;
    uint32_t now = fm1_ticks();
    fm1_wdt_feed();
    if (now - last >= 500u * FM1_TICKS_PER_US) {
        last = now;
        usb_poll();
    }
}

/* ---- ota.c hooks (the SysEx frame hooks are in usb.c) ---- */
static uint32_t ota_now_ms(void) { return ldr_ms(); }
static void ota_idle(void) { ldr_poll(); }
/* step-1 hooks of ota.c: not used by the loader */
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../src/system/ota.c"

/* ---- ldr_core.c hooks: interrupts are off for the whole loader ---- */
static int ldr_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static int ldr_erase(uint32_t off)
{
    uint32_t took;
    return fl_erase4k_ram(off, &took);
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t took;
    return fl_prog_ram(off, p, n, &took);
}
static void ldr_record_clear(void)
{
    fm1_updata_parm_clear();                        /* CRC 0: the SPL ignores it on a warm reset */
}
static int ldr_flash_known(void) { return fl_jedec_ram() == 0x856014u; }   /* as the app (project.c) */

/* A progress bar so the panel is not dark while the app area is rewritten. No panel init or reset: the ST7789
 * keeps its picture (the app's UPDATE screen) over the core reset, only the pins and the backlight (PA2) fall
 * back to their defaults, so fm1_lcd_hw_init alone brings them back. It runs after a sector is verified, never
 * inside a flash operation, and every SPI wait is bounded (fm1_lcd_wait); the watchdog is still fed here.
 * LDR_LCD 0 = the old dark loader. */
#ifndef LDR_LCD
#define LDR_LCD 1
#endif
#if LDR_LCD
#include "fm1_lcd_hw.h"
#define LDR_BAR_X 4u                                /* the bar: columns 4..235, rows 160..167 */
#define LDR_BAR_W 232u
#define LDR_BAR_Y 160u
#define LDR_BAR_H 8u
static uint16_t ldr_row[LDR_BAR_W];                 /* one row of the colour, DMA source (RAM) */
static uint8_t ldr_win[4];

static void ldr_lcd_cmd(uint8_t c, uint32_t v0, uint32_t v1)   /* c, then two big-endian 16-bit values */
{
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    if (c == 0x2Cu)
        return;
    ldr_win[0] = (uint8_t)(v0 >> 8);
    ldr_win[1] = (uint8_t)v0;
    ldr_win[2] = (uint8_t)(v1 >> 8);
    ldr_win[3] = (uint8_t)v1;
    fm1_lcd_send_data(ldr_win, 4);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void ldr_bar(uint32_t from, uint32_t to, uint16_t c)    /* bar columns from..to-1 (c is byte-swapped RGB565) */
{
    uint32_t i, y;
    if (to > LDR_BAR_W)
        to = LDR_BAR_W;
    if (from >= to)
        return;
    for (i = 0; i < to - from; i++)
        ldr_row[i] = c;
    ldr_lcd_cmd(0x2Au, LDR_BAR_X + from, LDR_BAR_X + to - 1u);
    ldr_lcd_cmd(0x2Bu, LDR_BAR_Y, LDR_BAR_Y + LDR_BAR_H - 1u);
    ldr_lcd_cmd(0x2Cu, 0, 0);
    for (y = 0; y < LDR_BAR_H; y++) {
        fm1_lcd_send_data(ldr_row, (to - from) * 2u);
        fm1_lcd_wait();
        fm1_lcd_deselect();
    }
}

static uint32_t ldr_bar_x;                          /* columns of the bar already drawn */

static void ldr_lcd_start(void)                     /* the first thing the loader does: backlight and track */
{
    fm1_lcd_hw_init();
    fm1_lcd_baud(1u);
    ldr_bar(0, LDR_BAR_W, 0x0842u);                  /* the track */
}

/* One bar that fills once and never goes back: the loader reads the package twice (the check, then the write),
 * so the check is the first half and the write the second. A repeated pass draws nothing until it passes the
 * columns already filled. done < 0x100000: no overflow. */
static void ldr_fill(uint32_t done, uint32_t total, uint32_t base)
{
    uint32_t x = base + (done > total ? LDR_BAR_W / 2u : done * (LDR_BAR_W / 2u) / total);
    if (x > ldr_bar_x) {
        ldr_bar(ldr_bar_x, x, 0xFFFFu);
        ldr_bar_x = x;
    }
    ldr_poll();
}
static void ldr_checking(uint32_t done, uint32_t total) { ldr_fill(done, total, 0); }
static void ldr_progress(uint32_t done, uint32_t total) { ldr_fill(done, total, LDR_BAR_W / 2u); }
#else
static void ldr_lcd_start(void) {}
static void ldr_checking(uint32_t done, uint32_t total) { (void)done; (void)total; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; ldr_poll(); }
#endif
#include "ldr_core.c"

extern uint32_t _bss_start[], _bss_end[];

void ldr_main(void)
{
    fm1_wdt_arm(0x0D);                              /* ~8 s, fed by ldr_poll */
    fm1_time_init();
    usb_start();
    ldr_lcd_start();
    for (;;) {
        ldr_poll();
        usb_retry(ldr_ms());
        if (usb.uboot_req) {                        /* soft key: mask-ROM UBOOT, as in Felucca */
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();                              /* handshake */
        if (usb.ota_req) {
            int rc;
            usb.ota_req = 0;
            rc = ldr_session();
            if (rc == 0 || rc == LDR_EFLASH) {      /* done, or another flash chip: the old firmware again */
                fm1_delay_ms(200);                  /* the "success" reply leaves */
                usb_detach();
                fm1_delay_ms(30);
                fm1_core_reset();                   /* the SPL boots the new app */
            }
        }
    }
}

void ldr_cstart(void)
{
    uint32_t *p;
    ldr_record_clear();                             /* first thing: a crash below cannot loop through a
                                                     * warm reset; resume after power loss uses the flash record */
    for (p = _bss_start; p < _bss_end; p++)
        *p = 0;
    ldr_main();
}
