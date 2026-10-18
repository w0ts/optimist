/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's SPI NOR (firmware/hal/fm1_flash.h): the RAM image (sim_core.h sim_flash_img) at the FM-1's offsets,
 * so a backup made on an FM-1 restores here and back. The same entry points and the same range checks (FL_RANGE_OK,
 * FL_SDK_SYS, one page per program); an erase sets the sector to 0xFF, a program ANDs (NOR: only 1 -> 0). Every
 * erase or program bumps sim_flash_rev (the page saves the image when it moves). */
#pragma once
#include <stdint.h>
#include "fm1_xip.h"

#ifndef FL_ERASE_ONE_WINDOW
#define FL_ERASE_ONE_WINDOW 0
#endif
#define RAMFN  __attribute__((noinline, used))
#define RAMINL static inline __attribute__((always_inline))

static inline void *fl_far(void *p) { void *volatile q = p; return q; }
#define FL_FAR(fn) ((__typeof__(&fn))fl_far((void *)&fn))

#define FL_XIP(off)     FM1_XIP(off)
#include "fm1_flash_map.h"                        /* (the real one: plain macros) */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif

static uint32_t sim_flash_rev;                    /* erases and programs so far */

typedef struct { uint32_t sfc_con, spi_con; } fl_saved_t;

RAMINL void fl_inval(uint32_t off, uint32_t len) { (void)off; (void)len; }

static RAMFN uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* P25Q80H, as the FM-1's */
static RAMFN uint32_t fl_status_ram(void) { return 0; }

static void sim_fl_erase(uint32_t off)
{
    uint32_t i;
    for (i = 0; i < 0x1000u; i++)
        sim_flash_img[off + i] = 0xFFu;
    sim_flash_rev++;
}

static RAMFN int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    uint32_t i;
    if (!FL_RANGE_OK(off, n) || FL_SDK_SYS(off, n) ||
        n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u)
        return -1;
    for (i = 0; i < n; i++)
        sim_flash_img[off + i] &= src[i];
    sim_flash_rev++;
    *took_us = 0;
    return 0;
}

static RAMFN int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = sim_flash_img[(off + i) & (SIM_FLASH_SIZE - 1u)];
    return 0;
}

static void fl_plain_window_init(void) {}

enum { FM1_FLOP_OTHER = 0, FM1_FLOP_READ = 1, FM1_FLOP_WRITE = 2, FM1_FLOP_ERASE = 3 };
static volatile uint8_t fm1_flash_reading;
#ifndef FM1_FLASH_ENTER
#define FM1_FLASH_ENTER() ((void)0)
#endif
#ifndef FM1_FLASH_LEAVE
#define FM1_FLASH_LEAVE() ((void)0)
#endif
#if (defined(FELUCCA_USB_AUDIO) && FELUCCA_USB_AUDIO) || (defined(FELUCCA_CDC) && FELUCCA_CDC)
static uint32_t fm1_irqoff_max;
#define FM1_IRQOFF_TICKS(v) ((v) & 0x0FFFFFFFu)
#define FM1_IRQOFF_KIND(v)  ((v) >> 28)
#endif
/* (as the FM-1's: the interrupts off around each operation; they are back on as they were found) */
static inline uint32_t irq_save(void) { uint32_t ie = sim_ie; fm1_irq_off(); FM1_FLASH_ENTER(); return ie; }
static inline void irq_restore(uint32_t f) { FM1_FLASH_LEAVE(); if (f) fm1_irq_on(); }

#define FLS_SLICE_US 1000u
#if defined(FELUCCA_CDC) && FELUCCA_CDC
static uint16_t fl_erase_windows_max;
static uint8_t fl_erase_held, fl_erase_nosus, fl_erase_redo;
#endif

static int fl_erase4k(uint32_t off, uint32_t *took_us)
{
    if (!FL_RANGE_OK(off, 0x1000u) || FL_SDK_SYS(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    sim_fl_erase(off);
    *took_us = 0;
    return 0;
}

static int fl_erase4k_sliced(uint32_t off, uint32_t *took_us, int32_t *audio, uint32_t quiet_n)
{
    (void)audio;
    (void)quiet_n;
    return fl_erase4k(off, took_us);
}

static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n)
{
    uint32_t took;
    fm1_flash_reading = FM1_FLOP_WRITE;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu);
        int rc;
        uint32_t f;
        if (chunk > n) chunk = n;
        f = irq_save();
        rc = FL_FAR(fl_prog_ram)(off, src_ram, chunk, &took);
        irq_restore(f);
        if (rc) { fm1_flash_reading = 0; return rc; }
        off += chunk; src_ram += chunk; n -= chunk;
    }
    fm1_flash_reading = 0;
    return 0;
}

static int fl_blank_xip(uint32_t off)
{
    uint32_t i;
    for (i = 0; i < 4096u; i++)
        if (sim_flash_img[off + i] != 0xFFu)
            return 0;
    return 1;
}

static int fl_verify_xip(uint32_t off, const uint8_t *ref, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) if (sim_flash_img[off + i] != ref[i]) return -(int)(i + 1);
    return 0;
}
