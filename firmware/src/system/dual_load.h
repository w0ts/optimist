/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU1's load (FELUCCA_DUAL 2, dual.c), shown beside CPU0's (song.cpu_q8) in SYSTEM's CPU row (the user,
 * 2026-10-12: "can we see the load of each cpu in the system menu when enabled").
 *
 * CPU1 adds the timer ticks of each job it runs to dual_c1_ticks (dual.c dual_job: two timer reads, its only
 * cost on CPU1). Once an audio half, CPU0 takes the ticks since the last half (dual.c dual_load_half, from
 * audio.c next to song.cpu_q8) and smooths them here the way song.cpu_q8 is: the share of the half's time
 * CPU1 spent rendering, 1/256, a 1/16 step a half. CPU0's own figure counts its wait for CPU1 (dual_join).
 * DUAL_LOAD 0 (every single-core build): nothing here, nothing in the image.
 * (DUAL_LOAD on its own: the host tests show the readout without the second core's render.) */
#ifndef DUAL_LOAD_H
#define DUAL_LOAD_H
#ifndef DUAL_LOAD
#if defined(FELUCCA_DUAL) && FELUCCA_DUAL >= 2   /* (0 / 1, not the expression: C code tests it too) */
#define DUAL_LOAD 1
#else
#define DUAL_LOAD 0
#endif
#endif
#if DUAL_LOAD
#define DUAL_C1_OFF 0xFFFFFFFFu                    /* dual_c1_q8: CPU1 is down (dual.c dual_down: for good) */
#define DUAL_HALF_US (HALF_FRAMES * 1000000u / FS) /* one audio half, us (= audio.c's) */
static uint32_t dual_c1_q8;                        /* CPU1's load, 1/256 (CPU0 writes it, the UI reads it) */

/* CPU0, once a half: us = the time CPU1 spent in its jobs since the last half; up = CPU1 runs */
static void dual_load_take(uint32_t us, uint32_t up)
{
    if (!up) {
        dual_c1_q8 = DUAL_C1_OFF;
        return;
    }
    if (us > DUAL_HALF_US)                         /* (a job that began before the half was counted: <= 100 %) */
        us = DUAL_HALF_US;
    dual_c1_q8 = (dual_c1_q8 * 15u + (us * 256u) / DUAL_HALF_US) / 16u;
}

/* the readout: CPU1's load in %, -1 when it is down */
static int32_t dual_c1_pct(void)
{
    return dual_c1_q8 == DUAL_C1_OFF ? -1 : (int32_t)(dual_c1_q8 * 100u / 256u);
}
#endif
#endif
