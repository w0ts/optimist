/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 CPU clock: read-only. The firmware never programs the PLL; it runs at whatever the SPL left.
 *
 * Registers (AC79 SDK asm/WL82.h; lsfr base 0x10000):
 *   JL_CLOCK  SYS_DIV 0x10008, CLK_CON0..3 0x1000C..0x10018
 *   JL_ANA    PLL_CON0 0x119A0, PLL_CON1 0x119A4, PLL2_CON0 0x119A8, PLL2_CON1 0x119AC
 * Their bit fields are not documented in the SDK (the clock driver is in the closed cpu.a).
 *
 * fm1_cpu_khz(): a loop of FM1_CLK_CHAIN dependent adds plus a decrement and a branch, run from
 * .ram_text (no flash or I-cache effect), timed with TIMER4 (24 MHz crystal, independent of the
 * PLL). It assumes one cycle per instruction: dependent adds cannot pair, and the decrement and
 * the branch are 2 of FM1_CLK_CHAIN + 2 instructions, so a branch costing 3 cycles instead of 1
 * reads ~6 % low. Each try runs with interrupts off (~0.1 ms at 320 MHz, ~1.4 ms at 24 MHz) and
 * the shortest of FM1_CLK_TRIES counts. Unverified on hardware: compare with the PLL registers.
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"

#define FM1_SYS_DIV (*(volatile uint32_t *)0x10008u)
#define FM1_CLK_CON(n) (*(volatile uint32_t *)(0x1000Cu + 4u * (n)))   /* CLK_CON0..3 */
#define FM1_PLL_REG(n) (*(volatile uint32_t *)(0x119A0u + 4u * (n)))   /* PLL_CON0/1, PLL2_CON0/1 */

/* i = 0: SYS_DIV, 1..4: CLK_CON0..3, 5..8: PLL_CON0, PLL_CON1, PLL2_CON0, PLL2_CON1 */
static inline uint32_t fm1_clk_reg(uint32_t i)
{
    return i == 0u ? FM1_SYS_DIV : i < 5u ? FM1_CLK_CON(i - 1u) : FM1_PLL_REG((i - 5u) & 3u);
}

#define FM1_CLK_CHAIN 32u
#define FM1_CLK_PASSES 1000u
#define FM1_CLK_TRIES 4u

/* FM1_CLK_PASSES passes of the loop; returns x so the adds are not dead */
__attribute__((section(".ram_text"), noinline, used)) static uint32_t fm1_clk_spin(uint32_t n, uint32_t x)
{
#define FM1_ADD4 "%1 = %1 + %2\n\t%1 = %1 + %2\n\t%1 = %1 + %2\n\t%1 = %1 + %2\n\t"
    __asm__ volatile("1:\n\t" FM1_ADD4 FM1_ADD4 FM1_ADD4 FM1_ADD4 FM1_ADD4 FM1_ADD4 FM1_ADD4 FM1_ADD4
                     "%0 += -1\n\t"
                     "if (%0 != 0) goto 1b"
                     : "+r"(n), "+r"(x)
                     : "r"(x | 1u));
#undef FM1_ADD4
    return x;
}

static inline void *fm1_clk_far(void *p) { void *volatile q = p; return q; }

/* the CPU clock in kHz (0: the timer did not move) */
static uint32_t fm1_cpu_khz(void)
{
    uint32_t (*spin)(uint32_t, uint32_t) = (uint32_t (*)(uint32_t, uint32_t))fm1_clk_far((void *)&fm1_clk_spin);
    uint32_t k, best = 0xFFFFFFFFu, x = 0;
    for (k = 0; k < FM1_CLK_TRIES; k++) {
        uint32_t t0, t;
        __asm__ volatile("cli" ::: "memory");
        t0 = fm1_ticks();
        x += spin(FM1_CLK_PASSES, k);
        t = fm1_ticks() - t0;
        __asm__ volatile("csync\n\tsti" ::: "memory");
        if (t < best)
            best = t;
    }
    (void)x;
    if (!best || best == 0xFFFFFFFFu)
        return 0;
    /* cycles = PASSES * (CHAIN + 2); kHz = cycles * 24000 / ticks (34000 * 24000 < 2^32) */
    return FM1_CLK_PASSES * (FM1_CLK_CHAIN + 2u) * 24000u / best;
}
