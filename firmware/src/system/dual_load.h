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

/* the dual render's times, for SYSTEM > CPU TIMES (FELUCCA_SYS_DEBUG; 2026-10-30: why CPU0 peaks over 100 % while
 * CPU1 sits at 25 %). Per 32-frame mix block, in timer ticks (24 a us), DTQ_N quantities: CPU0 before the fork (PRE:
 * the events, the macros, the duck, the LFOs' draws, the old sounds' fade, the split, the post), its own jobs (OWN),
 * waiting at the join (WAIT), after it (POST: the merge, the USB input, mix_finish with the buses and the master chain,
 * the macros' post), its whole block (BLK); CPU1 in its jobs (C1: 0 in a block it had none); each job's own time (J0 + p:
 * the parts, the drums, the reverb's kept block; only in a block it ran). Of each: the average (x 8, smoothed over ~8
 * blocks), the peak of each 512 ms window in a ring of four (the last 2 s: the main loop takes their largest), and its
 * value in the worst block (CPU0's longest) since the main loop last asked for a new one (wreset).
 * The render writes them (dual.c mix_block_dual, fm1-base's opt_dual.c mix_block and dual_run) with a few tick reads,
 * compares and stores; the core that runs a job stores its time (jt), CPU1 its own total (c1_last), CPU0 folds them in
 * after the join; the main loop only reads (and sets wreset) */
#if (defined(FELUCCA_SYS_DEBUG) && FELUCCA_SYS_DEBUG) || (defined(FELUCCA_SYS_TOP) && FELUCCA_SYS_TOP)   /* (SYS_TOP: SYSTEM > TOP
                                                    * reads the same times, op_top.c; none of the debug pages with it) */
#define DUAL_TIMES 1
#define DUAL_TM_NJ 8u                              /* jobs: the parts (NPART <= 4), the drums (half A), the reverb's kept
                                                    * block, the drums' half B, the Bluetooth stream's SBC frame (bt/
                                                    * bt_sbc_job.h); then the platform's frames at its feed (XJOB) */
#define DUAL_TM_WIN 4u                             /* the peaks' windows of 512 ms: the last 2 s */
enum { DTQ_PRE, DTQ_OWN, DTQ_WAIT, DTQ_POST, DTQ_BLK, DTQ_C1, DTQ_J0, DTQ_EVT = DTQ_J0 + DUAL_TM_NJ, DTQ_NS, DTQ_N };   /* (EVT, NS last: the older ones keep their places) */
typedef struct {
    uint32_t avg[DTQ_N];                           /* ticks x 8, smoothed */
    uint16_t pk[DUAL_TM_WIN][DTQ_N];               /* each window's peak, ticks (held to 65535) */
    uint16_t worst[DTQ_N];                         /* the worst block's, ticks */
    uint32_t jt[DUAL_TM_NJ];                       /* the last block's job times: the core that ran it writes it */
    volatile uint32_t c1_last;                     /* CPU1: its jobs' ticks in the last block it had some */
    uint8_t c1_mask;                               /* the last block's jobs CPU1 ran (bit p: job p) */
    uint8_t ran;                                   /* the jobs that ran in the last block, either core */
    uint8_t xran;                                  /* jobs timed outside the block since the last (DUAL_TM_XJOB): its ran */
    uint8_t w_c1, w_ran;                           /* the worst block's: the jobs CPU1 ran, the jobs that ran */
    uint8_t win;                                   /* the window written now (ms >> 9, of DUAL_TM_WIN) */
    volatile uint8_t wreset;                       /* the main loop: a new worst block from now (the page opened) */
    volatile uint8_t where;                        /* the main loop's span now: 0 none, 1 UI (ui_draw), 2 IDLE (fm1_idle) */
    volatile uint32_t isr;                         /* the render's ticks that fell inside the span (the render is an interrupt on CPU0) */
    uint8_t swin;                                  /* the spans' window written now (ms >> 9, of DUAL_TM_WIN) */
    uint32_t sp[2][DUAL_TM_WIN];                   /* UI, IDLE: each window's ticks, the render's taken out */
#if FELUCCA_BENCH
    uint32_t bsum[DTQ_N], bmax[DTQ_N], blocks;     /* (bench builds: each quantity's sum and largest since boot, the
                                                    * blocks counted: the emulator reads them, tools/cpubal_emu.py) */
#endif
    uint8_t in_evt, ns_depth;                      /* (CPU0: inside events_block; inside a timed trk_note_on) */
    uint32_t evt, ns;                              /* this block's ticks in events_block, in its note starts (PRE's parts) */
    uint8_t lastc[DUAL_TM_NJ];                     /* each job's core the last block it ran: 1 CPU0, 2 CPU1 (0: never) */
    volatile uint32_t sbc_n[3];                    /* the Bluetooth stream's SBC frames encoded as render jobs, counted since
                                                    * boot: [0] early on CPU1 (before the events), [1] at the split on CPU1,
                                                    * [2] at the split on CPU0 (fm1-base opt_dual.c; each core its own word) */
} dual_tm_t;
#ifndef DUAL_TM_SBC_FEED
#define DUAL_TM_SBC_FEED() 0u                      /* the frames the platform encoded at its feed, on CPU0 after the render
                                                    * (fm1-base: plat_bta_job_inline; none here) */
#endif
static dual_tm_t dual_tm;
#define DUAL_TM_JOB(p, t) ((p) < DUAL_TM_NJ ? (void)(dual_tm.jt[p] = (t)) : (void)0)   /* (dual_run: a job's time) */
/* PRE's two parts (2026-11-01, PRE at 37..60 % of a block on the user's FM-1 while the host counts 9 k instructions at
 * most: which of PRE): EVT the sequencer's events (seq.c events_block, from its XIP code), NOTE the note starts inside
 * them (voice.c trk_note_on: the parts' notes and the drum hits, whatever started them in events_block; a note start
 * the main loop makes is not counted). CPU0 only, in XIP code: fm1_ticks there. A block's sums, folded in by
 * dual_tm_block (no events_block in a block: 0) */
#define DUAL_TM_EVT_IN() (dual_tm.in_evt = 1, FAST_CALL(seqev, fm1_ticks)())
#define DUAL_TM_EVT_OUT(t0) (dual_tm.evt += FAST_CALL(seqev, fm1_ticks)() - (t0), dual_tm.in_evt = 0)
#define DUAL_TM_NS_IN() (dual_tm.in_evt && !dual_tm.ns_depth++ ? FAST_CALL(seqev, fm1_ticks)() : 0u)
#define DUAL_TM_NS_OUT(t0) (dual_tm.in_evt && !--dual_tm.ns_depth ? (void)(dual_tm.ns += FAST_CALL(seqev, fm1_ticks)() - (t0)) : (void)0)
static inline __attribute__((always_inline)) void dual_tm_q(uint32_t q, uint32_t t, uint16_t *pk, uint16_t *v)
{
    uint16_t c = (uint16_t)(t < 0xFFFFu ? t : 0xFFFFu);
    dual_tm.avg[q] += t - (dual_tm.avg[q] >> 3);
#if FELUCCA_BENCH
    dual_tm.bsum[q] += t;
    if (t > dual_tm.bmax[q])
        dual_tm.bmax[q] = t;
#endif
    if (c > pk[q])
        pk[q] = c;
    v[q] = c;
}
/* CPU0, a block's end: t0 its start, tf the fork (its own jobs begin), tj0 its jobs done, tj1 the join done (tj0 when
 * nothing was posted), te now; c1 = the jobs CPU1 ran (0: none joined), ran = the block's jobs; ms = fm1_ms */
static inline __attribute__((always_inline)) void dual_tm_block(uint32_t t0, uint32_t tf, uint32_t tj0, uint32_t tj1,
                                                                uint32_t te, uint32_t c1, uint32_t ran, uint32_t ms)
{
    uint32_t w = (ms >> 9) % DUAL_TM_WIN, p;
    uint16_t v[DTQ_N], *pk = dual_tm.pk[w];
    ran |= dual_tm.xran, dual_tm.xran = 0;         /* (a job timed since, outside the block: DUAL_TM_XJOB) */
    if (w != dual_tm.win) {                        /* (a new window: its peaks from 0) */
        dual_tm.win = (uint8_t)w;
        for (p = 0; p < DTQ_N; p++)
            pk[p] = 0;
    }
    dual_tm_q(DTQ_PRE, tf - t0, pk, v);
    dual_tm_q(DTQ_OWN, tj0 - tf, pk, v);
    dual_tm_q(DTQ_WAIT, tj1 - tj0, pk, v);
    dual_tm_q(DTQ_POST, te - tj1, pk, v);
    dual_tm_q(DTQ_BLK, te - t0, pk, v);
    if (dual_tm.where)                             /* (this block ran inside the main loop's UI or idle span: not theirs) */
        dual_tm.isr += te - t0;
    dual_tm_q(DTQ_C1, c1 ? dual_tm.c1_last : 0u, pk, v);
    dual_tm_q(DTQ_EVT, dual_tm.evt, pk, v);
    dual_tm_q(DTQ_NS, dual_tm.ns, pk, v);
    dual_tm.evt = dual_tm.ns = 0;
    for (p = 0; p < DUAL_TM_NJ; p++)
        if ((ran >> p) & 1u)
            dual_tm_q(DTQ_J0 + p, dual_tm.jt[p], pk, v), dual_tm.lastc[p] = (uint8_t)(1u + ((c1 >> p) & 1u));
        else
            v[DTQ_J0 + p] = 0;
    dual_tm.c1_mask = (uint8_t)c1;
    dual_tm.ran = (uint8_t)ran;
#if FELUCCA_BENCH
    dual_tm.blocks++;
#endif
    if (dual_tm.wreset) {                          /* (the page opened: the worst block from now) */
        dual_tm.wreset = 0;
        dual_tm.worst[DTQ_BLK] = 0;
    }
    if (v[DTQ_BLK] > dual_tm.worst[DTQ_BLK]) {     /* CPU0's longest block: every quantity as it was in it */
        for (p = 0; p < DTQ_N; p++)
            dual_tm.worst[p] = v[p];
        dual_tm.w_c1 = (uint8_t)c1;
        dual_tm.w_ran = (uint8_t)ran;
    }
}
/* a mix block's time in timer ticks (32 frames, core.h CTL; 24 ticks a us): a constant, no divide by a variable */
#define DUAL_TM_BLK (32u * 1000000u * 24u / FS)
/* ticks as a share of a block, in %, rounded (it may pass 100: a block over its time) */
static inline uint32_t dual_tm_pct_t(uint32_t t)
{
    return (t * 100u + DUAL_TM_BLK / 2u) / DUAL_TM_BLK;
}
static inline uint32_t dual_tm_pct(uint32_t x8) { return dual_tm_pct_t(x8 / 8u); }   /* (an average, x 8) */
/* the main loop: quantity q's peak over the last 2 s (the largest of the windows), ticks */
static inline uint32_t dual_tm_peak(uint32_t q)
{
    uint32_t w, m = 0;
    for (w = 0; w < DUAL_TM_WIN; w++)
        m = dual_tm.pk[w][q] > m ? dual_tm.pk[w][q] : m;
    return m;
}
/* a job CPU0 runs after the block, outside mix_block_dual (Optimist's own firmware: Bluetooth's SBC frames, audio.c
 * bt_audio_feed): its time, folded into the next block as a job of its own (p: its slot, the first after the render's) */
#define DUAL_TM_XJOB(p, t) ((p) < DUAL_TM_NJ ? (void)(dual_tm.jt[p] = (t), dual_tm.xran |= (uint8_t)(1u << (p))) : (void)0)
/* The main loop's own time on CPU0 (SYSTEM > TOP's UI and IDLE rows, 2026-11-02; a coarse figure: the order of magnitude).
 * DUAL_TM_SPAN_IN(k) before, DUAL_TM_SPAN_OUT(k, t0) after a stretch of the main loop (k 1: ui_draw, the screen's strips
 * with their waits; 2: the sleep). Its wall time, less the render blocks that ran inside it (dual_tm_block), summed in
 * 512 ms windows; the timer, the USB and the UART interrupts stay in it. k 1 / 2 is the row, 0 / 1 in sp */
#define DUAL_TM_WINT (512u * 1000u * 24u)           /* a window, ticks */
#define DUAL_TM_SPAN_IN(k) (dual_tm.isr = 0, dual_tm.where = (k), fm1_ticks())
#define DUAL_TM_SPAN_OUT(k, t0) dual_tm_span((k), fm1_ticks() - (t0), fm1_ms)
static inline void dual_tm_span(uint32_t k, uint32_t t, uint32_t ms)
{
    uint32_t w = (ms >> 9) % DUAL_TM_WIN, i = dual_tm.isr;
    dual_tm.where = 0;
    if (w != dual_tm.swin) {                       /* (a new window: from 0) */
        dual_tm.swin = (uint8_t)w;
        dual_tm.sp[0][w] = dual_tm.sp[1][w] = 0;
    }
    dual_tm.sp[k - 1u][w] += t > i ? t - i : 0u;
}
/* span k's share of CPU0's time, %: the average over the windows (the last 2 s), or the largest window */
static inline uint32_t dual_tm_span_pct(uint32_t k, uint32_t avg)
{
    uint32_t w, s = 0, m = 0;
    for (w = 0; w < DUAL_TM_WIN; w++) {
        uint32_t x = dual_tm.sp[k - 1u][w];
        s += x, m = x > m ? x : m;
    }
    return avg ? (s / DUAL_TM_WIN * 100u + DUAL_TM_WINT / 2u) / DUAL_TM_WINT : (m * 100u + DUAL_TM_WINT / 2u) / DUAL_TM_WINT;
}
#else
#define DUAL_TIMES 0
#endif
#endif
#endif
