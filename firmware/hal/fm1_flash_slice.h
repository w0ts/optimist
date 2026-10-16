/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A 4 KiB sector erase in short interrupts-off windows: the erase is suspended (75h) after FLS_SLICE_US, the
 * interrupts run with the XIP back on, and the erase is resumed (7Ah) in the next window. Without it the whole erase
 * (P25Q80H tSE 8 ms typical, 12 ms max; 17.2 ms measured as SYSTEM > INTERNALS' FLASH IRQS OFF on a dual-core Synth
 * build, 2026-10-16) is one window, three audio halves.
 *
 * P25Q80H datasheet (Puya, §5.4 and §10.36 - 10.38): the suspend latency tESL is 30 us max; the memory reads while an
 * erase is suspended, except the sector being erased; WIP is 0 and SUS1 is 1 while suspended (the datasheet names bit
 * 10 SUS1 in §10.36 and bit 15 in its register table: either is taken); "in order for an Erase operation to make
 * progress, tERS >= 200us must be included in resume-to-suspend loop(s)"; the resume sets WIP to 1 at once.
 *
 * Safe on a part that ignores the suspend: when WIP is still 1 FLS_SUS_US after it, the window silences the audio
 * buffer and waits the erase out (as before this file: one window), and says so (FLS_HELD). After FLS_SLICES_MAX
 * windows the last one does the same (an erase that does not progress is never left suspended).
 *
 * Pure logic on the primitives below, so the host test (tests/flash_slice_test.c) runs it on a model of the part:
 *   FLS_NOW()          ticks, 24 per us, wrapping (TIMER4)
 *   FLS_SR1()          status register 1 (05h): bit 0 WIP, bit 1 WEL
 *   FLS_SR2()          status register 2 (35h): bits 2 / 7 SUS1 / SUS2 (S10 / S15)
 *   FLS_ERASE(off)     20h + address
 *   FLS_CMD(c)         a one-byte command (06h WREN, 75h suspend, 7Ah resume)
 *   FLS_INVAL(off)     drop the sector's cache lines
 * Everything here runs with the XIP off (inlined into a .ram_text function in fm1_flash.h): no libcalls, no
 * .rodata, no division by a variable.
 *
 * The XIP side, fls_erase(): the windows in a loop, the interrupts on between them (FLS_WINDOW_FN / FLS_GAP, the
 * caller's: in fm1_flash.h each window is a whole irq_save / irq_restore, so the dual-core park handshake runs for each
 * one, as for each page program). */
#pragma once
#include <stdint.h>

#ifndef FLS_INL
#define FLS_INL static inline __attribute__((always_inline))
#endif
#ifndef FLS_SLICE_US
#define FLS_SLICE_US 1000u          /* the erase's own time in one window (> tERS 200 us; an audio half is 5.8 ms) */
#endif
#define FLS_SUS_US 100u             /* the suspend's latency allowed (tESL 30 us max) */
#define FLS_HOLD_US 500000u         /* one erase at most (generic NOR 400 ms) */
#ifndef FLS_SLICES_MAX
#define FLS_SLICES_MAX 200u         /* windows before the rest is waited out in one (200 ms of erase time) */
#endif
#define FLS_TPU 24u                 /* ticks per us */

enum { FLS_START = 0, FLS_RESUME = 1 };
enum { FLS_DONE = 0, FLS_SUSPENDED = 1, FLS_HELD = 2 };   /* (< 0: errors, as fl_erase4k_ram's) */

typedef struct {
    uint32_t slice_us;              /* FLS_SLICE_US, or 0: one window (no suspend) */
    volatile int32_t *quiet;        /* the audio buffer, silenced when a window holds the rest of an erase */
    uint32_t quiet_n;               /* (its words) */
    uint32_t took_us;               /* the windows' time, summed (FLS_WINDOW_FN counts it, and windows: not RAM code) */
    uint32_t windows;               /* windows taken */
    uint8_t held;                   /* a window waited the erase out (no suspend, or FLS_SLICES_MAX) */
    uint8_t nosus;                  /* suspends whose WIP cleared without a SUS bit */
} fls_job_t;

/* one window, the XIP off: START issues the erase, RESUME resumes it; then the erase runs for slice_us at most and
 * is suspended, or ends. FLS_DONE / FLS_HELD: erased (the sector's cache lines dropped); FLS_SUSPENDED: call again
 * with RESUME after the interrupts ran; < 0: an error (-3 WEL not set, -4 the part never got ready).
 * One status poll for the three waits (the erase's slice, the suspend's latency, the hold), each primitive inlined
 * once: .ram_text code may not call anything (tools/build.py) and its room is tight. */
enum { FLS_W_SLICE, FLS_W_SUSPEND, FLS_W_HOLD };
FLS_INL int fls_window(uint32_t off, uint32_t op, fls_job_t *j)
{
    uint32_t t, lim, i, phase, sr, start = op == FLS_START, cmd = start ? 0x06u : 0x7Au;   /* (WREN, or resume) */
    int rc;
    t = FLS_NOW();
    phase = j->slice_us ? FLS_W_SLICE : FLS_W_HOLD;
    lim = (j->slice_us ? j->slice_us : FLS_HOLD_US) * FLS_TPU;
    for (;;) {
        if (cmd) {
            FLS_CMD(cmd);
            cmd = 0;
        }
        sr = FLS_SR1();
        if (start) {                            /* WREN sent: WEL set, then the erase */
            if (!(sr & 2u))
                return -3;
            FLS_ERASE(off);
            start = 0;
            t = FLS_NOW();
            continue;
        }
        if (phase == FLS_W_HOLD && !j->held) {
            j->held = 1;                        /* the window holds the rest: the DMA must loop silence, not a stale half */
            for (i = 0; i < j->quiet_n; i++)    /* (volatile: never turned into a memset call) */
                j->quiet[i] = 0;
        }
        if (!(sr & 1u)) {                       /* WIP 0 */
            uint32_t sus;
            if (phase == FLS_W_SLICE) {
                rc = FLS_DONE;
                break;
            }
            sus = FLS_SR2() & 0x84u;            /* SUS1 / SUS2 */
            if (phase == FLS_W_SUSPEND) {
                j->nosus += !sus;               /* (no SUS bit: it ended as the suspend came, or the part does not show
                                                 * it. Either way the next window resumes it: a resume of a part not
                                                 * suspended is ignored, while a suspended erase taken as done would
                                                 * leave the sector half erased) */
                return FLS_SUSPENDED;           /* (the cache is not touched: the sector is never run from) */
            }
            if (!sus) {
                rc = FLS_HELD;
                break;
            }
            cmd = 0x7Au;                        /* (suspended after all, past the resume: resumed again) */
            continue;
        }
        if ((uint32_t)(FLS_NOW() - t) <= lim)
            continue;
        if (phase == FLS_W_SLICE) {             /* the slice is over: suspend */
            cmd = 0x75u;
            phase = FLS_W_SUSPEND;
            lim = FLS_SUS_US * FLS_TPU;
        } else if (phase == FLS_W_SUSPEND) {    /* not suspended: hold the rest (a suspend taken later than FLS_SUS_US
                                                 * must not pass for the end: resumed first) */
            cmd = 0x7Au;
            phase = FLS_W_HOLD;
            lim = FLS_HOLD_US * FLS_TPU;
        } else {
            rc = -4;
            break;
        }
        t = FLS_NOW();
    }
    FLS_INVAL(off);                             /* (an interrupt between windows may have cached the sector mid-erase) */
    return rc;
}

#ifdef FLS_WINDOW_FN
static int FLS_WINDOW_FN(uint32_t off, uint32_t op, fls_job_t *j);   /* one window, IRQs off around it (the caller's) */
/* the whole erase: windows until it is done (0) or failed (< 0). j: slice_us, quiet, quiet_n set, the rest 0 */
static int fls_erase(uint32_t off, fls_job_t *j)
{
    int rc = FLS_WINDOW_FN(off, FLS_START, j);
    while (rc == FLS_SUSPENDED) {
        FLS_GAP();                              /* the interrupts run (the XIP on, the erase suspended) */
        if (j->windows >= FLS_SLICES_MAX)
            j->slice_us = 0;                    /* (no progress: the rest in one window) */
        rc = FLS_WINDOW_FN(off, FLS_RESUME, j);
    }
    return rc < 0 ? rc : 0;
}
#endif
