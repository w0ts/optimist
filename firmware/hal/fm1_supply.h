/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's own supply start-up (FELUCCA_STOCK_SUPPLY, the builder's STOCK_SUPPLY, system/supply.c, docs/DUAL-CORE.md
 * 3.5): what the stock M-VAVE application does to the rails as it starts (fm1_supply_stock, fm1_lvr_stock), applied at the
 * top of the boot, before the second core and the BLE radio; the second-core test image's other levels (fm1_supply_to,
 * the boost, the ladder: dualcfg). EXPERIMENTAL; on by default in builds with the second core.
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
static inline uint32_t fm1_supply_now(void)
{
    return fm1_rail_get(FM1_P3_VDC14, FM1_VDC14_MASK) << 4 | fm1_rail_get(FM1_P3_SYSVDD, FM1_SYSVDD_MASK);
}

/* VDC14 up to VDC, then SYSVDD up to SYS (Jangada's order; 0: that rail left as it is; never lowered), then 1 ms to
 * settle. -> VDC14 << 4 | SYSVDD as found (the console's) */
static inline uint32_t fm1_supply_to(uint32_t vdc, uint32_t sys)
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
static inline uint32_t fm1_supply_boost(void) { return fm1_supply_to(FM1_VDC14_BOOST, FM1_SYSVDD_BOOST); }

/* the analog registers the stock supply start-up touches, raw: P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24
 * (CON0 bit 2 LDO13_EN, bit 3 DCDC13_EN: which regulator makes the 1.4 V rail) */
static inline uint32_t fm1_supply_regs(void)
{
    return fm1_p33_read(0x00u) | (uint32_t)fm1_p33_read(0x05u) << 8 | (uint32_t)fm1_p33_read(FM1_P3_VDC14) << 16 |
           (uint32_t)fm1_p33_read(FM1_P3_SYSVDD) << 24;
}

static void fm1_p33_rmw(uint32_t reg, uint8_t clr, uint8_t set)
{
    fm1_p33_write(reg, (uint8_t)((fm1_p33_read(reg) & ~clr) | set));
}

/* The stock M-VAVE application's supply start-up (FM-1_015, app_core 0x02004F90..0x0200509A, as Melodee 1.3.6
 * hal/fm1_power.h ports it, keremimo/melodee 7a51700, GPL-3.0-only, from its BUILDING.md "The stock supply setup"; the
 * SDK p33.h names): the 1.4 V rail handed from the LDO to the DC-DC (LDO13 on, DCDC13 on, LDO13 off), CON1 = 0x20
 * (MAIN_VDD_LOAD_EN), CON5 = 0x74 (VDDIO_HD_SEL 1, VDDIOW 3 = 3.2 V, VDDIOM 4 = 3.2 V: stock's final value, written
 * once: stock goes through 0x40 = 2.8 V first), CON6 VD14_HD_SEL 1, CON7 = 0xA5 / CON8 = 0x96 (the BT DC-DC's bias,
 * duty, oscillator, V2I, current sense, comparator, zero), CON9 SYSVDD_VOL_HD_SEL 1, VDC14 to 7 (1.60 V), CON0
 * RC_250k_EN, P3_ANA_KEEP [5:0] cleared, CON2 VDDOK_DIS, P3_VLD_KEEP bit 5 cleared, SYSVDD to 15 (1.38 V) last. The
 * levels raised a step at a time (100 us), never lowered; stock's waits (a 1,000-pass loop) are 100 us here.
 * Never above stock: both fields end at their top code. IRQs off, CPU1 held (not started yet).
 * -> 0 when the read-back is right: DC-DC on, LDO off, VDC14 7, SYSVDD 15, VDDIOM 4, no P33 timeout; else bits:
 * FM1_SUP_TIMEOUT a transfer timed out (the sequence stopped there), FM1_SUP_MODE the regulator bits, FM1_SUP_LEVEL a level */
#define FM1_SUP_TIMEOUT 1u
#define FM1_SUP_MODE 2u
#define FM1_SUP_LEVEL 4u
#ifndef FM1_SUPPLY_HOST
#define FM1_P33_TIMEOUTS() fm1_p33_timeouts
#endif
static uint32_t fm1_supply_stock(void)
{
    static const struct { uint8_t reg, clr, set, wait; } STEP[] = {
        {0x00u, 0x00u, 0x04u, 1u}, {0x00u, 0x00u, 0x08u, 1u}, {0x00u, 0x04u, 0x00u, 1u},   /* LDO13 on, DCDC13 on, LDO13 off */
        {0x01u, 0xFFu, 0x20u, 0u}, {0x05u, 0xFFu, 0x74u, 0u}, {0x06u, 0x18u, 0x08u, 0u},
        {0x07u, 0xFFu, 0xA5u, 0u}, {0x08u, 0xFFu, 0x96u, 0u}, {0x09u, 0x00u, 0x10u, 0u},
    };
    static const struct { uint8_t reg, clr, set; } LATE[] = {
        {0x00u, 0x00u, 0x80u}, {0x16u, 0x3Fu, 0x00u}, {0x02u, 0x00u, 0x40u}, {0x17u, 0x20u, 0x00u},
    };
    uint32_t i, r, t0 = FM1_P33_TIMEOUTS(), err = 0;
    for (i = 0; i < sizeof STEP / sizeof STEP[0]; i++) {
        fm1_p33_rmw(STEP[i].reg, STEP[i].clr, STEP[i].set);
        if (STEP[i].wait)
            fm1_delay_us(100);
        if (FM1_P33_TIMEOUTS() != t0)
            return FM1_SUP_TIMEOUT;                       /* (stop: no field written from a failed read) */
    }
    fm1_rail_raise(FM1_P3_VDC14, FM1_VDC14_MASK, 7u);
    for (i = 0; i < sizeof LATE / sizeof LATE[0] && FM1_P33_TIMEOUTS() == t0; i++)
        fm1_p33_rmw(LATE[i].reg, LATE[i].clr, LATE[i].set);
    if (FM1_P33_TIMEOUTS() != t0)
        return FM1_SUP_TIMEOUT;
    fm1_rail_raise(FM1_P3_SYSVDD, FM1_SYSVDD_MASK, 15u);
    fm1_delay_us(1000);
    r = fm1_supply_regs();
    if ((r & 0x0Cu) != 0x08u)
        err |= FM1_SUP_MODE;
    if (((r >> 8) & 7u) != 4u || ((r >> 16) & 7u) != 7u || ((r >> 24) & 15u) != 15u)
        err |= FM1_SUP_LEVEL;
    if (FM1_P33_TIMEOUTS() != t0)
        err |= FM1_SUP_TIMEOUT;
    return err;
}

/* stock's low-voltage detector (app_core, stockre §7.2): P3_VLVD_CON = 0x78 (VLVD_SEL 7 = 2.6 V, the pending flag
 * cleared), then |= 1 (VLVD_EN). -> 0, or FM1_SUP_LVR when it does not read back */
#define FM1_P3_VLVD 0x11u
#define FM1_SUP_LVR 8u
static inline uint32_t fm1_lvr_stock(void)
{
    fm1_p33_write(FM1_P3_VLVD, 0x78u);
    fm1_p33_rmw(FM1_P3_VLVD, 0x00u, 0x01u);
    return (fm1_p33_read(FM1_P3_VLVD) & 0x39u) == 0x39u ? 0u : FM1_SUP_LVR;
}

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
