/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU1's load (firmware/src/system/dual_load.h, the dual-core build's SYSTEM CPU row). Built twice:
 *   -DFELUCCA_DUAL=0  nothing is defined (a single-core build carries none of it)
 *   -DFELUCCA_DUAL=2  dual_load_take smooths as song.cpu_q8 does (audio.c), stays in 0..256, reads OFF when down */
#include <stdint.h>
#include <stdio.h>
#define HALF_FRAMES 256
#define FS 44100
static uint32_t fake_ticks;                         /* (the macros of PRE's parts read the timer: fm1_ticks) */
static uint32_t fm1_ticks(void) { return fake_ticks; }
#define __attribute__(x)
#include "core/core.h"
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
    {   /* SYSTEM > CPU TIMES (-DFELUCCA_SYS_DEBUG=1): a block (DUAL_TM_BLK ticks, P(k) = k % of it): pre 10 %, CPU0's jobs 50 %,
         * the wait 15 %, post 20 %; CPU1's jobs 60 % in the blocks it ran; job 0 30 %, job 1 (CPU1) 60 %. The averages
         * (x 8, ~8 blocks), the peaks of the 512 ms windows (the last 2 s), the worst block's values */
        uint32_t t, ms = 0;
        int ok = 1;
#define P(k) (DUAL_TM_BLK * (k) / 100u)
        for (i = 0; i < 200u; i++, ms += 2u) {
            t = i * DUAL_TM_BLK;
            dual_tm.c1_last = P(60u);
            DUAL_TM_JOB(0u, P(30u));
            DUAL_TM_JOB(1u, P(60u));
            dual_tm_block(t, t + P(10u), t + P(60u), t + P(75u), t + P(95u), 0x2u, 0x3u, ms);
        }
        ok &= dual_tm_pct(dual_tm.avg[DTQ_PRE]) == 10u && dual_tm_pct(dual_tm.avg[DTQ_OWN]) == 50u;
        ok &= dual_tm_pct(dual_tm.avg[DTQ_WAIT]) == 15u && dual_tm_pct(dual_tm.avg[DTQ_POST]) == 20u;
        ok &= dual_tm_pct(dual_tm.avg[DTQ_BLK]) == 95u && dual_tm_pct(dual_tm.avg[DTQ_C1]) == 60u;
        ok &= dual_tm_pct(dual_tm.avg[DTQ_J0]) == 30u && dual_tm_pct(dual_tm.avg[DTQ_J0 + 1u]) == 60u;
        check(ok, "CPU TIMES: the averages: PRE 10, JOBS 50, WAIT 15, POST 20, CPU0 95, CPU1 60, the jobs 30 / 60 %");
        check(dual_tm.c1_mask == 0x2u && dual_tm.ran == 0x3u, "CPU TIMES: the last block's CPU1 jobs and the jobs that ran");
        check(dual_tm.lastc[0] == 1u && dual_tm.lastc[1] == 2u && !dual_tm.lastc[2],
              "CPU TIMES: each job's core the last block it ran (job 0 CPU0, job 1 CPU1, job 2 never: C '-')");
        t = 200u * DUAL_TM_BLK;                          /* one block of 180 %: CPU0's own jobs 135 % */
        dual_tm_block(t, t + P(10u), t + P(145u), t + P(160u), t + P(180u), 0x2u, 0x3u, ms);
        ok = dual_tm_pct_t(dual_tm_peak(DTQ_BLK)) == 180u && dual_tm_pct_t(dual_tm_peak(DTQ_OWN)) == 135u;
        ok &= dual_tm_pct_t(dual_tm.worst[DTQ_BLK]) == 180u && dual_tm_pct_t(dual_tm.worst[DTQ_OWN]) == 135u;
        ok &= dual_tm_pct_t(dual_tm.worst[DTQ_PRE]) == 10u && dual_tm.w_c1 == 0x2u && dual_tm.w_ran == 0x3u;
        check(ok, "CPU TIMES: a block of 180 %: its peak (MAX) and the worst block's values (CPU0 180, its own jobs 135, PRE 10)");
        for (i = 0; i < 1100u; i++, ms += 2u) {         /* 2.2 s of the steady blocks: the peak ages out, the worst stays */
            t = (201u + i) * DUAL_TM_BLK;
            dual_tm_block(t, t + P(10u), t + P(60u), t + P(75u), t + P(95u), 0x2u, 0x3u, ms);
        }
        check(dual_tm_pct_t(dual_tm_peak(DTQ_BLK)) == 95u && dual_tm_pct_t(dual_tm.worst[DTQ_BLK]) == 180u,
              "CPU TIMES: 2 s on, the peak back to 95 %, the worst block still 180 %");
        dual_tm.wreset = 1;                             /* (the page opened again: the worst from now) */
        dual_tm_block(t, t + P(10u), t + P(60u), t + P(75u), t + P(95u), 0x2u, 0x3u, ms);
        check(dual_tm_pct_t(dual_tm.worst[DTQ_BLK]) == 95u && !dual_tm.wreset, "CPU TIMES: the page opened again: a new worst block");
        dual_tm_block(t, t + P(10u), t + P(1110u), t + P(1110u), t + P(1130u), 0u, 0x1u, ms);   /* (no post; a block past 65535 ticks) */
        check(dual_tm.worst[DTQ_BLK] == 0xFFFFu && dual_tm.worst[DTQ_WAIT] == 0u && dual_tm.worst[DTQ_C1] == 0u &&
                  dual_tm.worst[DTQ_J0 + 1u] == 0u,
              "CPU TIMES: nothing posted: no wait, no CPU1 time; a block past 65535 ticks held there; a job not run: 0");
        check(DUAL_TM_BLK == 17414u, "CPU TIMES: a block of 32 frames at 44.1 kHz: 17414 ticks (726 us)");
        {   /* PRE's parts: EVT the events (events_block's wrapper), NOTE the note starts in them (trk_note_on's, nested
             * ones counted once); a note start outside the events (the main loop's) not counted; both folded into the
             * block and cleared */
            uint32_t e0, n0, n1, m0;
            fake_ticks = 1000u;
            m0 = DUAL_TM_NS_IN();                       /* (the main loop's note start: not in the events) */
            fake_ticks += 500u;
            DUAL_TM_NS_OUT(m0);
            e0 = DUAL_TM_EVT_IN();
            fake_ticks += P(4u);
            n0 = DUAL_TM_NS_IN();
            fake_ticks += P(3u);
            n1 = DUAL_TM_NS_IN();                       /* (a note start inside one: a chord's) */
            fake_ticks += P(2u);
            DUAL_TM_NS_OUT(n1);
            fake_ticks += P(1u);
            DUAL_TM_NS_OUT(n0);
            n0 = DUAL_TM_NS_IN();
            fake_ticks += P(5u);
            DUAL_TM_NS_OUT(n0);
            fake_ticks += P(5u);
            DUAL_TM_EVT_OUT(e0);
            check(dual_tm.evt == P(4u) + P(3u) + P(2u) + P(1u) + P(5u) + P(5u) && dual_tm.ns == P(3u) + P(2u) + P(1u) + P(5u) && !dual_tm.in_evt && !dual_tm.ns_depth,
                  "CPU TIMES: EVT the events' 20 %, NOTE their note starts' 11 % (a nested one once, the main loop's not)");
            dual_tm.wreset = 1;
            t = 3000u * DUAL_TM_BLK;
            dual_tm_block(t, t + P(25u), t + P(60u), t + P(75u), t + P(95u), 0x2u, 0x3u, ms);
            check(dual_tm_pct_t(dual_tm.worst[DTQ_EVT]) == 20u && dual_tm_pct_t(dual_tm.worst[DTQ_NS]) == 11u && dual_tm_pct_t(dual_tm_peak(DTQ_EVT)) == 20u &&
                      !dual_tm.evt && !dual_tm.ns,
                  "CPU TIMES: EVT and NOTE folded into the block (MAX, WST), cleared for the next");
            dual_tm_block(t, t + P(10u), t + P(60u), t + P(75u), t + P(95u), 0x2u, 0x3u, ms);
            check(dual_tm_pct_t(dual_tm.worst[DTQ_EVT]) == 20u, "CPU TIMES: a block without events: EVT 0 in it, the worst block's kept");
        }
#undef P
    }
#endif
#endif
    printf("%s\n", fails ? "dual_load: FAILED" : "dual_load: all ok");
    return fails != 0;
}
