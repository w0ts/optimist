/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU1's load (firmware/src/system/dual_load.h, the dual-core build's SYSTEM CPU row). Built twice:
 *   -DFELUCCA_DUAL=0  nothing is defined (a single-core build carries none of it)
 *   -DFELUCCA_DUAL=2  dual_load_take smooths as song.cpu_q8 does (audio.c), stays in 0..256, reads OFF when down */
#include <stdint.h>
#include <stdio.h>
#define HALF_FRAMES 256
#define FS 44100
#include "system/dual_load.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

#if DUAL_LOAD
static uint32_t cpu0_q8(uint32_t q8, uint32_t us)   /* audio.c's song.cpu_q8 step, for the comparison */
{
    return (q8 * 15u + (us * 256u) / (HALF_FRAMES * 1000000u / FS)) / 16u;
}
#endif

int main(void)
{
#if !DUAL_LOAD
#ifdef DUAL_C1_OFF
    check(0, "DUAL off: dual_load.h defines nothing");
#else
    check(1, "DUAL off: dual_load.h defines nothing");
#endif
#else
    uint32_t i, q = 0, inr = 1, same = 1, half = DUAL_HALF_US;
    check(half == 5804u, "one audio half: 5804 us (audio.c's)");
    check(dual_c1_q8 == 0u && dual_c1_pct() == 0, "at boot: 0 %");
    for (i = 0; i < 400u; i++) {                     /* CPU1 busy 40 % of each half */
        dual_load_take(half * 2u / 5u, 1);
        q = cpu0_q8(q, half * 2u / 5u);
        same &= dual_c1_q8 == q;
        inr &= dual_c1_q8 <= 256u;
    }
    check(same, "the same smoothing as song.cpu_q8 (1/16 a half)");
    check(dual_c1_pct() >= 33 && dual_c1_pct() <= 40, "40 % of each half: it reads 33..40 % (the 1/16 step truncates, as song.cpu_q8)");
    for (i = 0; i < 400u; i++) {                     /* more than a half (a job straddling the boundary): capped */
        dual_load_take(half * 3u, 1);
        inr &= dual_c1_q8 <= 256u;
    }
    check(inr, "never over 256 (100 %)");
    check(dual_c1_pct() >= 90 && dual_c1_pct() <= 100, "busy the whole half: 90..100 %");
    for (i = 0; i < 400u; i++)
        dual_load_take(0, 1);
    check(dual_c1_pct() <= 1, "idle: back to 0..1 %");
    for (i = 0; i < 3u; i++)
        dual_load_take(half / 2u, 1);
    check(dual_c1_pct() > 0, "the value updates each half");
    dual_load_take(half / 2u, 0);
    check(dual_c1_q8 == DUAL_C1_OFF && dual_c1_pct() == -1, "CPU1 down: OFF (-1)");
#if DUAL_TIMES
    {   /* SYSTEM > DUAL TIMES (-DFELUCCA_SYS_DEBUG=1): a block of 1000 ticks, pre 100, CPU0's jobs 500, the wait 150, post
         * 200; CPU1's jobs 600 in the blocks it ran. Smoothed x 8 over ~8 blocks, as the jobs' dual_cost */
        uint32_t t;
        int ok = 1;
        for (i = 0; i < 200u; i++) {
            t = i * 1000u;
            dual_tm.c1_last = 600u;
            dual_tm_block(t, t + 100u, t + 600u, t + 750u, t + 950u, 0x6u, 0xFu);
        }
        ok &= dual_tm_pct(dual_tm.pre, 1000u) == 10u && dual_tm_pct(dual_tm.wait, 1000u) == 15u;
        ok &= dual_tm_pct(dual_tm.post, 1000u) == 20u && dual_tm_pct(dual_tm.blk, 1000u) == 95u;
        check(ok, "DUAL TIMES: PRE 10 %, WAIT 15 %, POST 20 %, CPU0's block 95 % (smoothed, settled)");
        check(dual_tm_pct(dual_tm.c1, 1000u) == 60u && dual_tm.c1_mask == 0x6u && dual_tm.ran == 0xFu,
              "DUAL TIMES: CPU1 60 % in its jobs; the last block's CPU1 jobs and the jobs that ran kept");
        for (i = 0; i < 200u; i++) {                 /* nothing posted: no wait, no CPU1 time; CPU0 over its block */
            t = i * 1000u;
            dual_tm_block(t, t + 100u, t + 1100u, t + 1100u, t + 1300u, 0u, 0xFu);
        }
        check(dual_tm_pct(dual_tm.wait, 1000u) == 0u && dual_tm_pct(dual_tm.c1, 1000u) == 0u && dual_tm.c1_mask == 0u,
              "DUAL TIMES: a block with nothing posted: no wait, CPU1 idle");
        check(dual_tm_pct(dual_tm.blk, 1000u) == 130u, "DUAL TIMES: CPU0 over its block reads over 100 % (130)");
        check(dual_tm_pct(123u, 0u) == 0u, "DUAL TIMES: no block time: 0, no division");
    }
#endif
#endif
    printf("%s\n", fails ? "dual_load: FAILED" : "dual_load: all ok");
    return fails != 0;
}
