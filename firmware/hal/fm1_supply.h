/* SPDX-License-Identifier: GPL-3.0-only */
/* The core supply raised before the second core starts: FELUCCA_DUAL_BOOST, EXPERIMENTAL, at the owner's risk
 * (docs/DUAL-CORE.md section 8). Never built by default, never on a build without the second core.
 *
 * Registers (JieLi AC79 SDK, include_lib/driver/cpu/wl82/asm/p33.h), both P33 analog registers:
 *   P3_ANA_CON6 (0x06) bits [2:0] VDC14_VOL_SEL: 1.25 V + 50 mV a step (VDC14_VOL_SEL_125V = 0 .. _160V = 7);
 *                      bits [4:3] VD14_HD_SEL, kept as they are
 *   P3_ANA_CON9 (0x09) bits [3:0] SYSVDD_VOL_SEL: 0.93 V + 30 mV a step (SYSVDD_VOL_SEL_093V = 0 .. _138V = 15),
 *                      the core and SRAM rail
 * The boot loader leaves VDC14 at 3 (1.40 V) and SYSVDD at 11 (1.26 V). Raised to VDC14 5 (1.50 V), then SYSVDD 15
 * (1.38 V), one step at a time, 100 us apart, never lowered: the levels and the order of Jangada's fm1_core_supply
 * (zednaked/jangada @ 07cce38, GPL-3.0-only, hal/fm1_sys.h, after Melodee's hal/fm1_power.h, keremimo/melodee, GPL-3.0-only),
 * which ran an FM-1 whose core 1 misread the RAM for 15 min and 1.23 M blocks with no misread at 15 / 5 (VDC14 is what
 * mattered; SYSVDD alone changed nothing). Measured on that one unit only. SYSVDD 15 is above JieLi's own profile
 * (1.35 V above 320 MHz) and the chip's absolute limits are not public.
 *
 * Call with interrupts off (boot: before the audio and the timers), as every P33 access (fm1_sys.h). */
#pragma once
#include <stdint.h>
#ifndef FM1_SUPPLY_HOST                 /* (the host test gives its own fm1_p33_read / write and fm1_delay_us) */
#include "fm1_sys.h"
#include "fm1_time.h"
#endif

#define FM1_P3_VDC14 0x06u              /* P3_ANA_CON6 */
#define FM1_P3_SYSVDD 0x09u             /* P3_ANA_CON9 */
#define FM1_VDC14_MASK 0x07u            /* VDC14_VOL_SEL [2:0] */
#define FM1_SYSVDD_MASK 0x0Fu           /* SYSVDD_VOL_SEL [3:0] */
#define FM1_VDC14_BOOST 5u              /* VDC14_VOL_SEL_150V */
#define FM1_SYSVDD_BOOST 15u            /* SYSVDD_VOL_SEL_138V */

static uint32_t fm1_rail_get(uint32_t reg, uint32_t mask) { return fm1_p33_read(reg) & mask; }

/* the field up to LEVEL, a step at a time (the register's other bits kept); never down. -> the level reached */
static uint32_t fm1_rail_raise(uint32_t reg, uint32_t mask, uint32_t level)
{
    uint32_t n, v, now = fm1_rail_get(reg, mask);
    for (n = 0; now < level && n <= mask; n++) {          /* (bounded: a write P33 does not take ends it) */
        v = fm1_p33_read(reg);
        fm1_p33_write(reg, (uint8_t)((v & ~mask) | ((v & mask) + 1u)));
        fm1_delay_us(100);
        now = fm1_rail_get(reg, mask);
    }
    return now;
}

/* both rails as they are now: VDC14 << 4 | SYSVDD */
static uint32_t fm1_supply_now(void)
{
    return fm1_rail_get(FM1_P3_VDC14, FM1_VDC14_MASK) << 4 | fm1_rail_get(FM1_P3_SYSVDD, FM1_SYSVDD_MASK);
}

/* VDC14 up to VDC, then SYSVDD up to SYS (Jangada's order; 0: that rail left as it is; never lowered), then 1 ms to
 * settle. -> VDC14 << 4 | SYSVDD as found (the console's) */
static uint32_t fm1_supply_to(uint32_t vdc, uint32_t sys)
{
    uint32_t was = fm1_supply_now();
    if (vdc)
        fm1_rail_raise(FM1_P3_VDC14, FM1_VDC14_MASK, vdc > FM1_VDC14_MASK ? FM1_VDC14_MASK : vdc);
    if (sys)
        fm1_rail_raise(FM1_P3_SYSVDD, FM1_SYSVDD_MASK, sys > FM1_SYSVDD_MASK ? FM1_SYSVDD_MASK : sys);
    fm1_delay_us(1000);
    return was;
}

/* the boost: VDC14 5 (1.50 V), SYSVDD 15 (1.38 V) */
static uint32_t fm1_supply_boost(void) { return fm1_supply_to(FM1_VDC14_BOOST, FM1_SYSVDD_BOOST); }

/* B24 test image, `dualcfg boostlvl N`: a ladder up to the boost, VDC14 first (what mattered on Jangada's unit), a
 * step of the smallest unit each: 0 nothing; 1 VDC14 4 (1.45 V); 2 VDC14 5 (1.50 V); 3..6 VDC14 5 and SYSVDD 12..15
 * (1.29 .. 1.38 V); 6 = the boost. -> VDC14 << 4 | SYSVDD targets (0: leave) */
#define FM1_BOOST_LEVELS 7u
static inline uint32_t fm1_boost_level(uint32_t n)
{
    if (!n || n >= FM1_BOOST_LEVELS)
        return n ? (FM1_VDC14_BOOST << 4 | FM1_SYSVDD_BOOST) : 0u;
    if (n <= 2u)
        return (3u + n) << 4;
    return FM1_VDC14_BOOST << 4 | (9u + n);
}
