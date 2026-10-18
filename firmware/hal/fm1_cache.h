/* SPDX-License-Identifier: GPL-3.0-only */
/* Cache RAM (FELUCCA_CACHE_RAM, the builder's CACHE_RAM, EXPERIMENTAL, default 0): 7 of the 8 data-cache ways used as
 * 28 KiB of RAM at 0x01F28000..0x01F2F000, the configuration M-VAVE's FM-1 firmware (V15) runs with and the AC79 SDK's
 * default for a flash-only build (cpu/wl82/sdk_ld_sfc.c FREE_DACHE_WAY = 7 unless SDRAM can be switched on). All 8
 * instruction-cache ways (the flash's code and constants) stay caches. docs/MEMORY-MAP.md, "Cache RAM".
 *
 * The registers (corex2, the SDK's asm/csfr.h): CACHE_CON 0x01EEE008 (bit 14 = idle, read only; bit 8 = the cache on,
 * bit 9 = on for SDRAM / PSRAM: the SDK sets it only when those controllers are on), DCACHE_WAY 0x01EEE00C, ICACHE_WAY
 * 0x01EEE010. A set bit is a way kept as a cache: DCACHE_WAY has three 8-bit masks (bits 0..7, 8..15, 16..23; which
 * requester each serves is not known), ICACHE_WAY two (0..7, 8..15). The sequence (the SDK's cache_way_config, as V15
 * runs it from RAM at 0x01C00E8C): wait for idle; CACHE_CON bits 8-9 off; DCACHE_WAY low 24 bits = 0x808080 (way 7
 * kept); ICACHE_WAY low 16 bits = 0xFFFF; the tag RAM zeroed (0x01F00000..0x01F04000, 0x01F08000..0x01F0A200,
 * 0x01F0B000..0x01F0B200: every line invalid); CACHE_CON back on. With the cache off nothing may be fetched from the
 * flash: fm1_cache_set runs from .ram_text (copied by main.c before it is called), interrupts off, CPU1 not started,
 * and calls nothing (build.py checks .ram_text for calls); the tag loops are volatile so no memset is generated.
 * Here CACHE_CON's bits 8-9 are put back as the boot loader left them (not re-derived), so only the ways change.
 *
 * Then the 28 KiB are tested (address patterns and their complements, two passes) before anything uses them; on a
 * mismatch the old way masks are written back and nothing is placed there (status FM1_CR_BAD). Users check
 * fm1_cache_ram_ok(): the undo history's second segment (seq/undo.c) is the only one. Measured in the emulator only
 * (it maps the 64 KiB at 0x01F20000 as plain RAM and has no cache timing); on an FM-1 unverified: the console's
 * `boot` prints what was found and `tabbench` the read speed. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#ifndef FELUCCA_CACHE_RAM
#define FELUCCA_CACHE_RAM 0
#endif

#define FM1_CR_LO 0x01F28000u                         /* data ways 0..6 as RAM (7 x 4 KiB); way 7 stays a cache */
#define FM1_CR_HI 0x01F2F000u
enum { FM1_CR_OFF, FM1_CR_OK, FM1_CR_BUSY, FM1_CR_REG, FM1_CR_BAD };   /* not built / ready / never idle / masks not
                                                                        * taken / memory test failed (both restored) */
static struct {
    uint32_t status, con, dway0, iway0;               /* the result; CACHE_CON and the way masks as the boot loader left them */
    uint32_t dway, iway, bad_at;                      /* the masks read back after; the first failing address */
} fm1_cache;

FM1_INLINE int fm1_cache_ram_ok(void) { return fm1_cache.status == FM1_CR_OK; }

#if FELUCCA_CACHE_RAM && defined(__PI32V2__)
#define FM1_CACHE_CON  (*(volatile uint32_t *)0x01EEE008u)
#define FM1_DCACHE_WAY (*(volatile uint32_t *)0x01EEE00Cu)
#define FM1_ICACHE_WAY (*(volatile uint32_t *)0x01EEE010u)

FM1_INLINE void fm1_cr_zero(uint32_t lo, uint32_t hi)
{
    volatile uint32_t *p;
    for (p = (volatile uint32_t *)lo; p < (volatile uint32_t *)hi; p++)
        *p = 0;
}

/* the way masks set, every tag invalidated; RAM only, interrupts off. 0: the cache never went idle (nothing changed) */
static __attribute__((section(".ram_text"), noinline, used)) uint32_t fm1_cache_set(uint32_t dway, uint32_t iway)
{
    uint32_t con, n = 2400000u;                       /* (~10 ms at 240 MHz: a fill never takes that long) */
    while (!(FM1_CACHE_CON & 0x4000u))
        if (!--n)
            return 0;
    con = FM1_CACHE_CON;
    FM1_CACHE_CON = con & ~0x300u;
    __asm__ volatile("csync" ::: "memory");
    FM1_DCACHE_WAY = dway;
    FM1_ICACHE_WAY = iway;
    fm1_cr_zero(0x01F00000u, 0x01F04000u);
    fm1_cr_zero(0x01F08000u, 0x01F0A200u);
    fm1_cr_zero(0x01F0B000u, 0x01F0B200u);
    __asm__ volatile("csync\n\tssync" ::: "memory");
    FM1_CACHE_CON = con;                              /* bits 8-9 as they were */
    __asm__ volatile("csync\n\tssync" ::: "memory");
    return 1;
}

/* the 28 KiB: each word its address pattern, then the complement; read back after the whole pass (aliased ways show).
 * 0 = good, else the first failing address. Ends zeroed. */
static uint32_t fm1_cache_test(void)
{
    volatile uint32_t *p;
    uint32_t pass, x;
    for (pass = 0; pass < 2u; pass++) {
        x = pass ? 0xFFFFFFFFu : 0u;
        for (p = (volatile uint32_t *)FM1_CR_LO; p < (volatile uint32_t *)FM1_CR_HI; p++)
            *p = ((uint32_t)(uintptr_t)p ^ 0x5AC3A53Cu) ^ x;
        __asm__ volatile("csync" ::: "memory");
        for (p = (volatile uint32_t *)FM1_CR_LO; p < (volatile uint32_t *)FM1_CR_HI; p++)
            if (*p != (((uint32_t)(uintptr_t)p ^ 0x5AC3A53Cu) ^ x))
                return (uint32_t)(uintptr_t)p;
    }
    fm1_cr_zero(FM1_CR_LO, FM1_CR_HI);
    return 0;
}

/* once, from fm1_cstart: after .ram_text is copied, before the guards, CPU1 and the interrupts */
static void fm1_cache_ram_init(void)
{
    uint32_t (*volatile set)(uint32_t, uint32_t) = fm1_cache_set;   /* (XIP -> RAM: past the 23-bit call range) */
    uint32_t dw, iw;
    fm1_cache.con = FM1_CACHE_CON;
    fm1_cache.dway0 = FM1_DCACHE_WAY;
    fm1_cache.iway0 = FM1_ICACHE_WAY;
    dw = (fm1_cache.dway0 & 0xFF000000u) | 0x00808080u;
    iw = (fm1_cache.iway0 & 0xFFFF0000u) | 0x0000FFFFu;
    if (!set(dw, iw)) {
        fm1_cache.status = FM1_CR_BUSY;
        return;
    }
    fm1_cache.dway = FM1_DCACHE_WAY;
    fm1_cache.iway = FM1_ICACHE_WAY;
    if (fm1_cache.dway != dw || fm1_cache.iway != iw)
        fm1_cache.status = FM1_CR_REG;
    else if ((fm1_cache.bad_at = fm1_cache_test()) != 0)
        fm1_cache.status = FM1_CR_BAD;
    else {
        fm1_cache.status = FM1_CR_OK;
        return;
    }
    set(fm1_cache.dway0, fm1_cache.iway0);            /* back as the boot loader left them; nothing is placed there */
}
#else
static inline void fm1_cache_ram_init(void) {}
#endif
