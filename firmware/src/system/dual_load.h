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

/* the dual render's times, for SYSTEM > INTERNALS' DUAL lines (FELUCCA_SYS_DEBUG; 2026-10-30: why CPU0 peaks over
 * 100 % while CPU1 sits at 25 %). Per 32-frame mix block, in timer ticks (24 a us) x 8, smoothed over ~8 blocks as the
 * jobs' own dual_cost: CPU0 before the fork (pre: the events, the macros, the duck, the LFOs' draws, the old sounds'
 * fade, the split, the post), waiting at the join (wait), after it (post: the merge, the USB input, mix_finish with the
 * buses and the master chain, the macros' post), its whole block (blk); CPU1 in its jobs (c1: 0 in a block it had none).
 * The render writes them with a few tick reads (dual.c mix_block_dual, fm1-base's opt_dual.c mix_block); CPU1 stores
 * only its own last time (c1_last), CPU0 folds it in after the join; the main loop only reads */
#if defined(FELUCCA_SYS_DEBUG) && FELUCCA_SYS_DEBUG
#define DUAL_TIMES 1
typedef struct {
    uint32_t pre, wait, post, blk, c1;             /* ticks x 8, smoothed */
    volatile uint32_t c1_last;                     /* CPU1: its jobs' ticks in the last block it had some */
    uint8_t c1_mask;                               /* the last block's jobs CPU1 ran (bit p: job p) */
    uint8_t ran;                                   /* the jobs that ran in the last block, either core */
} dual_tm_t;
static dual_tm_t dual_tm;
#define DUAL_TM(f, t) (dual_tm.f += (uint32_t)(t) - (dual_tm.f >> 3))
/* CPU0, a block's end: t0 its start, tf the fork (its own jobs begin), tj0 its jobs done, tj1 the join done (tj0 when
 * nothing was posted), te now;
 * c1 = the jobs CPU1 ran (0: none joined), ran = the block's jobs */
static inline __attribute__((always_inline)) void dual_tm_block(uint32_t t0, uint32_t tf, uint32_t tj0, uint32_t tj1, uint32_t te, uint32_t c1, uint32_t ran)
{
    DUAL_TM(pre, tf - t0);
    DUAL_TM(wait, tj1 - tj0);
    DUAL_TM(post, te - tj1);
    DUAL_TM(blk, te - t0);
    DUAL_TM(c1, c1 ? dual_tm.c1_last : 0u);
    dual_tm.c1_mask = (uint8_t)c1;
    dual_tm.ran = (uint8_t)ran;
}
/* a mix block's time in timer ticks (32 frames, core.h CTL; 24 ticks a us): a constant, no divide by a variable */
#define DUAL_TM_BLK (32u * 1000000u * 24u / FS)
/* a smoothed time as a share of a block, in %, rounded (it may pass 100: a block over its time) */
static inline uint32_t dual_tm_pct(uint32_t x8)
{
    return (x8 / 8u * 100u + DUAL_TM_BLK / 2u) / DUAL_TM_BLK;
}
#else
#define DUAL_TIMES 0
#endif
#endif
#endif
