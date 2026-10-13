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
#endif
    printf("%s\n", fails ? "dual_load: FAILED" : "dual_load: all ok");
    return fails != 0;
}
