/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 SPI NOR at runtime: erase / program / read. Used only when
 * FELUCCA_FLASH is 1.
 *
 * Everything that executes while the SFC (XIP) is switched off lives in
 * .ram_text and touches only SFRs, the stack and RAM buffers: no .rodata, no
 * libcalls, no XIP-resident helpers. Call the fl_*_ram() entry points with
 * interrupts disabled. Same sequence as the AC79 SDK flash driver
 * (enter_spi_code, norflash_erase, norflash_wait_ok, exit_spi_code).
 */
#pragma once
#include <stdint.h>
#include "fm1_xip.h"

#ifndef FL_ERASE_ONE_WINDOW
#define FL_ERASE_ONE_WINDOW 0                     /* 1: fl_erase4k_ram, the whole erase in one window (the loader) */
#endif
#define RAMFN  __attribute__((section(".ram_text"), noinline, used))
#define RAMINL static inline __attribute__((always_inline))

/* XIP (0x02xxxxxx) -> RAM (0x01C0xxxx) is beyond the 23-bit call range:
 * call RAM code through a pointer the compiler cannot fold back. */
static inline void *fl_far(void *p) { void *volatile q = p; return q; }
#define FL_FAR(fn) ((__typeof__(&fn))fl_far((void *)&fn))

#define REG32(a) (*(volatile uint32_t *)(a))
#define SFC_CON         REG32(0x40200u)            /* bit31 busy; [11:8] read mode */
#define SFCENC_CON      (*(volatile uint8_t *)0x40300u)  /* b0 decrypt, b1 plain window */
#define SFCENC_UNENC_H  REG32(0x40308u)            /* WO, XIP (CPU) address */
#define SFCENC_UNENC_L  REG32(0x4030Cu)            /* WO, XIP (CPU) address */
#define SPI0_CON        REG32(0x11C00u)            /* b15 PND, b14 PCLR, b12 DIR=rx */
#define SPI0_BUF        REG32(0x11C08u)
#define IOMAP_CON0      REG32(0x5101Cu)            /* b5: 1 = SFC owns the flash pins */
#define PORTD_OUT       REG32(0x500C0u)            /* PD0 = flash CS# */
#define PORTD_DIR       REG32(0x500C8u)
#define CACHE_CON       REG32(0x1EEE008u)          /* b14 = cache idle */
#define T4_CNT          REG32(0x10804u)            /* TIMER4, 24 MHz free-running */

#define FL_XIP(off)     FM1_XIP(off)              /* fm1_xip.h */
#include "fm1_flash_map.h"                 /* the ranges: FL_IN, FL_STORE_OK, FL_NEVER, FL_SDK_SYS */
/* Where the RAM driver may erase / program. The app build allows only its own
 * data regions; the update loader (firmware/loader) defines its own window. Either way the
 * SDK's own sectors (FL_SDK_SYS: the stock SDK VM, BTIF, key_mac) are refused below. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif

typedef struct { uint32_t sfc_con, spi_con; } fl_saved_t;

RAMINL void fl_csync(void) { __asm__ volatile("csync" ::: "memory"); }
RAMINL void fl_cache_idle(void) { while (!(CACHE_CON & 0x4000u)) ; }
RAMINL void fl_delay(uint32_t n) { volatile uint32_t i = n; while (i--) ; }

RAMINL void fl_cs(int high)
{
    PORTD_DIR &= ~1u;
    if (high) PORTD_OUT |= 1u; else PORTD_OUT &= ~1u;
    fl_delay(10);
}
RAMINL int fl_spi_wait(void)
{
    uint32_t n = 50000000u;
    fl_csync();
    while (!(SPI0_CON & 0x8000u))
        if (--n == 0) break;
    SPI0_CON |= 0x4000u;
    return n ? 0 : -1;
}
RAMINL void fl_tx(uint8_t b)
{
    SPI0_CON |= 8u; SPI0_CON &= ~0x1000u;
    SPI0_BUF = b; fl_spi_wait();
    SPI0_CON &= ~8u;
}
RAMINL uint8_t fl_rx(void)
{
    uint8_t v;
    SPI0_CON |= 8u; SPI0_CON |= 0x1000u;
    SPI0_BUF = 0xFFu; fl_spi_wait();
    v = (uint8_t)SPI0_BUF;
    SPI0_CON &= ~8u;
    return v;
}
RAMINL void fl_addr(uint32_t a) { fl_tx((uint8_t)(a >> 16)); fl_tx((uint8_t)(a >> 8)); fl_tx((uint8_t)a); }
RAMINL uint8_t fl_rdsr(uint8_t cmd) { uint8_t v; fl_cs(0); fl_tx(cmd); v = fl_rx(); fl_cs(1); return v; }
RAMINL void fl_wren(void) { fl_cs(0); fl_tx(0x06); fl_cs(1); }

RAMINL int fl_enter(fl_saved_t *s)
{
    uint32_t mode;
    fl_cache_idle();
    s->sfc_con = SFC_CON;
    s->spi_con = SPI0_CON;
    mode = (s->sfc_con >> 8) & 0xFu;
    if (mode == 6u || mode == 7u)                 /* continuous-read XIP: not handled here */
        return -1;
    fl_csync();
    while ((int32_t)SFC_CON < 0) ;                /* SFC idle */
    SFC_CON = 0;                                  /* XIP OFF from here on */
    IOMAP_CON0 &= ~0x20u;                         /* pins -> SPI0 / GPIO */
    SPI0_CON = 0;
    SPI0_CON = 0x1029u;                           /* SDK default */
    fl_csync();
    return 0;
}
RAMINL void fl_exit(const fl_saved_t *s)
{
    SPI0_CON = s->spi_con;
    IOMAP_CON0 |= 0x20u;
    fl_csync();
    while ((int32_t)SFC_CON < 0) ;
    SFC_CON = s->sfc_con;                         /* XIP back on */
    fl_csync();
}
RAMINL void fl_inval(uint32_t off, uint32_t len)
{
    uint32_t a = FL_XIP(off) & ~31u, e = FL_XIP(off) + len;
    fl_cache_idle();
    for (; a < e; a += 32u) {
        fl_csync();
        __asm__ volatile("flushinv [%0]" :: "r"(a) : "memory");
        fl_csync();
        fl_cache_idle();
    }
}
RAMINL int fl_wait_ready(uint32_t max_us, uint32_t *took_us)
{
    uint32_t t0 = T4_CNT, dt;
    for (;;) {
        dt = (T4_CNT - t0) / 24u;
        if (!(fl_rdsr(0x05) & 1u)) { *took_us = dt; return 0; }
        if (dt > max_us) { *took_us = dt; return -4; }
    }
}

/* ---- entry points: IRQs OFF, args/buffers in RAM ---- */

static RAMFN uint32_t fl_jedec_ram(void)                 /* expect 0x856014 */
{
    fl_saved_t s; uint32_t id;
    if (fl_enter(&s)) return 0;
    fl_cs(0); fl_tx(0x9F);
    id = (uint32_t)fl_rx() << 16; id |= (uint32_t)fl_rx() << 8; id |= fl_rx();
    fl_cs(1);
    fl_exit(&s);
    return id;
}

static RAMFN uint32_t fl_status_ram(void)                /* SR1 | SR2 << 8 */
{
    fl_saved_t s; uint32_t v;
    if (fl_enter(&s)) return 0xFFFFFFFFu;
    v = fl_rdsr(0x05) | ((uint32_t)fl_rdsr(0x35) << 8);
    fl_exit(&s);
    return v;
}

#if FL_ERASE_ONE_WINDOW                          /* the update loader (firmware/loader): the whole erase in one window */
static RAMFN int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    fl_saved_t s; int rc;
    if (!FL_RANGE_OK(off, 0x1000u) || FL_SDK_SYS(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    if (fl_enter(&s)) return -2;
    fl_wren();
    if (!(fl_rdsr(0x05) & 2u)) rc = -3;           /* WEL did not set */
    else {
        fl_cs(0); fl_tx(0x20); fl_addr(off); fl_cs(1);
        fl_inval(off, 4096u);
        rc = fl_wait_ready(500000u, took_us);     /* generic NOR max 400 ms */
    }
    fl_exit(&s);
    return rc;
}
#else
/* the app: a sector erase in short windows (fm1_flash_slice.h, fl_erase4k_sliced below) */
#define FLS_INL     RAMINL
#define FLS_NOW()   T4_CNT
#define FLS_SR1()   fl_rdsr(0x05)
#define FLS_SR2()   fl_rdsr(0x35)
#define FLS_ERASE(off) do { fl_cs(0); fl_tx(0x20); fl_addr(off); fl_cs(1); } while (0)
#define FLS_CMD(c)  do { fl_cs(0); fl_tx((uint8_t)(c)); fl_cs(1); } while (0)
#define FLS_INVAL(off) fl_inval(off, 4096u)
#define FLS_GAP()   do { uint32_t g0 = T4_CNT; while (T4_CNT - g0 < 20u * 24u) ; } while (0)   /* 20 us, IRQs on */
#define FLS_WINDOW_FN fl_erase_window
#include "fm1_flash_slice.h"

static RAMFN int fl_erase_window_ram(uint32_t off, uint32_t op, fls_job_t *j)
{
    fl_saved_t s; int rc;
    if (FL_SDK_SYS(off, 0x1000u) || (off & 0xFFFu))   /* (the SDK's sectors: never; FL_RANGE_OK: fl_erase4k_sliced, XIP side) */
        return -1;
    if (fl_enter(&s)) return -2;
    rc = fls_window(off, op, j);
    fl_exit(&s);
    return rc;
}
#endif

static RAMFN int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    fl_saved_t s; int rc; uint32_t i;
    if (!FL_RANGE_OK(off, n) || FL_SDK_SYS(off, n) ||
        n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u) return -1;    /* one page, no wrap */
    if (fl_enter(&s)) return -2;
    fl_wren();
    if (!(fl_rdsr(0x05) & 2u)) rc = -3;
    else {
        fl_cs(0); fl_tx(0x02); fl_addr(off);
        for (i = 0; i < n; i++) fl_tx(src[i]);
        fl_cs(1);
        fl_inval(off, n);
        rc = fl_wait_ready(10000u, took_us);      /* generic NOR max 3 ms */
    }
    fl_exit(&s);
    return rc;
}

static RAMFN int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n)   /* 0x0B fast read */
{
    fl_saved_t s; uint32_t i;
    if (fl_enter(&s)) return -2;
    fl_cs(0); fl_tx(0x0B); fl_addr(off); fl_tx(0xFF);
    for (i = 0; i < n; i++) dst[i] = fl_rx();
    fl_cs(1);
    fl_exit(&s);
    return 0;
}

/* ---- XIP side (normal .text) ---- */

/* Once at boot, before any XIP read of the data region: map the data area
 * as an unencrypted XIP window (as the SDK's IOCTL_SET_VM_INFO does). */
static void fl_plain_window_init(void)
{
    SFCENC_CON &= (uint8_t)~2u;
    SFCENC_UNENC_L = FL_XIP(0x93000u);            /* 0x0208F000 */
    SFCENC_UNENC_H = 0x07FFFFFFu;
    SFCENC_CON |= 2u;
    __asm__ volatile("cli" ::: "memory");
    fl_inval(0x93000u, 0x100000u - 0x93000u);     /* drop any decrypted lines */
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

enum { FM1_FLOP_OTHER = 0, FM1_FLOP_READ = 1, FM1_FLOP_WRITE = 2, FM1_FLOP_ERASE = 3 };
/* the operation running, FM1_FLOP_*: READ while a read runs (st_read: the hooks below tell it from a write), WRITE
 * (fl_write), ERASE (fl_erase_window), OTHER (0: the boot's JEDEC read) */
static volatile uint8_t fm1_flash_reading;
#ifndef FM1_FLASH_ENTER                         /* a check with IRQs off before every flash operation */
#define FM1_FLASH_ENTER() ((void)0)               /* (the dual-core build: CPU1 off the flash, src/dual.c) */
#endif
#ifndef FM1_FLASH_LEAVE                         /* after it, before the interrupts are on again (dual.c: CPU1 resumed) */
#define FM1_FLASH_LEAVE() ((void)0)
#endif
#if (defined(FELUCCA_USB_AUDIO) && FELUCCA_USB_AUDIO) || (defined(FELUCCA_CDC) && FELUCCA_CDC)   /* (CDC: the console test image) */
/* USB audio builds: the longest interrupts-off window a flash operation held (TIMER4 ticks, SYSTEM > INFO). The
 * window's start is the token irq_save returns (the callers pass it back unchanged). */
#include "fm1_time.h"
static uint32_t fm1_irqoff_max;                   /* bits 0-27 the ticks, 28-29 the operation (FM1_FLOP_*) that held it */
#define FM1_IRQOFF_TICKS(v) ((v) & 0x0FFFFFFFu)
#define FM1_IRQOFF_KIND(v)  ((v) >> 28)
static inline uint32_t irq_save(void) { __asm__ volatile("cli" ::: "memory"); FM1_FLASH_ENTER(); return fm1_ticks(); }
static inline void irq_restore(uint32_t f)
{
    uint32_t d = fm1_ticks() - f;
    if (d > FM1_IRQOFF_TICKS(fm1_irqoff_max))     /* (no byte of its own for the kind: the RAM is full) */
        fm1_irqoff_max = (d > 0x0FFFFFFFu ? 0x0FFFFFFFu : d) | (uint32_t)fm1_flash_reading << 28;
    FM1_FLASH_LEAVE();
    __asm__ volatile("csync\n\tsti" ::: "memory");
}
#else
static inline uint32_t irq_save(void) { __asm__ volatile("cli" ::: "memory"); FM1_FLASH_ENTER(); return 0; }
static inline void irq_restore(uint32_t f) { (void)f; FM1_FLASH_LEAVE(); __asm__ volatile("csync\n\tsti" ::: "memory"); }
#endif

#if FL_ERASE_ONE_WINDOW
static int fl_erase4k(uint32_t off, uint32_t *took_us)
{
    uint32_t f = irq_save();
    int rc = FL_FAR(fl_erase4k_ram)(off, took_us);
    irq_restore(f);
    return rc;
}
#else
/* ---- the app's sector erase in short windows (fm1_flash_slice.h): suspended every FLS_SLICE_US, the interrupts on
 * between ---- */
static int fl_erase_window(uint32_t off, uint32_t op, fls_job_t *j)
{
    uint32_t f, t;
    int rc;
    fm1_flash_reading = FM1_FLOP_ERASE;
    f = irq_save();                               /* (each window: CPU1 parked and resumed, as for each page program) */
    t = T4_CNT;
    j->windows++;
    rc = FL_FAR(fl_erase_window_ram)(off, op, j);
    j->took_us += (T4_CNT - t) / 24u;
    irq_restore(f);
    fm1_flash_reading = 0;
    return rc;
}
/* through the XIP plain window (fl_plain_window_init): the sector reads all 0xFF */
static int fl_blank_xip(uint32_t off)
{
    const volatile uint32_t *p = (const volatile uint32_t *)FL_XIP(off);
    uint32_t i;
    for (i = 0; i < 1024u; i++)
        if (p[i] != 0xFFFFFFFFu)
            return 0;
    return 1;
}
/* the erase, in windows of FLS_SLICE_US. quiet / quiet_n: the audio buffer, silenced only if a window must hold the
 * rest of the erase (a part that does not suspend). An erase that went through a suspend is read back blank when the
 * plain window is on (else, or not blank: erased again in one window, as before) */
#if defined(FELUCCA_CDC) && FELUCCA_CDC              /* the console test image's counters (console.c flash_erase_*) */
static uint16_t fl_erase_windows_max;
static uint8_t fl_erase_held, fl_erase_nosus, fl_erase_redo;   /* (saturating) */
#define FL_ESTAT(x) (x)
#else
#define FL_ESTAT(x) ((void)0)                     /* (a product build: no RAM for them) */
#endif
static int fl_erase4k_sliced(uint32_t off, uint32_t *took_us, int32_t *audio, uint32_t quiet_n)
{
    volatile int32_t *quiet = audio;              /* (the DMA reads it: stores never dropped or merged) */
    fls_job_t j = { FLS_SLICE_US, quiet, quiet_n, 0, 0, 0, 0 };
    int rc;
    if (!FL_RANGE_OK(off, 0x1000u) || FL_SDK_SYS(off, 0x1000u) || (off & 0xFFFu))
        return -1;                                /* (checked here: the .ram_text room is tight) */
    rc = fls_erase(off, &j);
    FL_ESTAT(fl_erase_windows_max = j.windows > fl_erase_windows_max && j.windows <= 0xFFFFu ? (uint16_t)j.windows
                                                                                             : fl_erase_windows_max);
    FL_ESTAT(fl_erase_held += fl_erase_held < 0xFFu && j.held);
    FL_ESTAT(fl_erase_nosus += fl_erase_nosus < 0xFFu && j.nosus);
    if (!rc && j.windows > 1u && !((SFCENC_CON & 2u) && off >= 0x93000u && fl_blank_xip(off))) {
        fls_job_t k = { 0, quiet, quiet_n, 0, 0, 0, 0 };
        FL_ESTAT(fl_erase_redo += fl_erase_redo < 0xFFu);
        rc = fl_erase_window(off, FLS_RESUME, &k);   /* (an erase still suspended: ended first; a part idle ignores it) */
        if (rc >= 0)
            rc = fls_erase(off, &k);              /* (then once more in one window, as before) */
        j.took_us += k.took_us;
    }
    *took_us = j.took_us;
    return rc;
}
#endif

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

/* Read back through XIP (needs fl_plain_window_init) and compare. */
static int fl_verify_xip(uint32_t off, const uint8_t *ref, uint32_t n)
{
    const volatile uint8_t *p = (const volatile uint8_t *)FL_XIP(off);
    uint32_t i;
    for (i = 0; i < n; i++) if (p[i] != ref[i]) return -(int)(i + 1);
    return 0;
}
