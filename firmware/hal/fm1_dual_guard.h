/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core's guards against misread RAM (FELUCCA_DUAL; fm1_dual.h, src/system/dual.c). Pure: no register,
 * no call, always inline, so CPU1's RAM loops use it and the host tests include it (tests/dual_guard_test.c).
 *
 * Why: on some FM-1s core 1 reads words that are not in RAM (single bit flips, its instruction fetches too) at the
 * supply the boot loader leaves (charlesvestal/fm1-x0x#10, keremimo/melodee#17, measured by Jangada). A request
 * read wrong would run a job nobody posted, with an argument nobody wrote.
 *
 * After X0X (charlesvestal/fm1-x0x @ 47f8e4c, Charles Vestal, GPL-3.0-only, hal/fm1_cpu1.h): CPU1 runs only the
 * NEXT job (req == done + 1), and only when its check word (the argument and the job's number, written by CPU0
 * before the request) matches; its `done` is its own copy, never read back from the mailbox. Anything else is
 * counted (`bad`), never run, and CPU0 holds CPU1 at the first one. Optimist adds: the argument must be a part mask
 * within the parts CPU1 may render, and a boot canary (a table CPU0 writes, CPU1 reads back before its hello). */
#pragma once
#include <stdint.h>

#define FM1_DUAL_CHECK(req, arg) (((arg) ^ 0x5AC3E10Fu) ^ (req) * 0x9E3779B1u)   /* CPU0 writes it before req */

enum { FM1_DG_IDLE, FM1_DG_RUN, FM1_DG_BAD };   /* fm1_dual_take: nothing new, run the job, a request it must not run */

/* CPU1: what to do with the request REQ (as read), its own DONE, the argument and check word as read, ALLOWED the
 * parts it may render */
static inline __attribute__((always_inline)) uint32_t fm1_dual_take(uint32_t req, uint32_t done, uint32_t arg,
                                                                    uint32_t check, uint32_t allowed)
{
    if (req == done)
        return FM1_DG_IDLE;
    if (req == done + 1u && !(arg & ~allowed) && check == FM1_DUAL_CHECK(req, arg))
        return FM1_DG_RUN;
    return FM1_DG_BAD;
}

/* ---- the boot canary: CPU0 fills a table before CPU1 starts, CPU1 reads it FM1_DUAL_CANARY_PASSES times before its
 * hello and reports the words it read wrong. All ones, all zeros (the misread seen on X0X's unit: 0x00200000 where
 * core 0 reads 0) and mixed words. A cheap first look, not a proof: the misreads on the units measured came under
 * load, after seconds; the request guard above is what catches those */
#define FM1_DUAL_CANARY_N 32u
#define FM1_DUAL_CANARY_PASSES 64u
static inline __attribute__((always_inline)) uint32_t fm1_dual_canary_word(uint32_t i)
{
    switch (i & 3u) {
    case 1u:
        return 0u;
    case 3u:
        return 0xFFFFFFFFu;
    default:
        return (i * 0x9E3779B1u) ^ 0xA5C3F00Fu;
    }
}
static inline void fm1_dual_canary_fill(volatile uint32_t *t)
{
    uint32_t i;
    for (i = 0; i < FM1_DUAL_CANARY_N; i++)
        t[i] = fm1_dual_canary_word(i);
}
/* -> the reads that were wrong (0: all right) */
static inline uint32_t fm1_dual_canary_scan(const volatile uint32_t *t, uint32_t passes)
{
    uint32_t p, i, bad = 0;
    for (p = 0; p < passes; p++)
        for (i = 0; i < FM1_DUAL_CANARY_N; i++)
            bad += t[i] != fm1_dual_canary_word(i);
    return bad;
}
