/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core (CPU1). EXPERIMENTAL, emulator-validated only: docs/DUAL-CORE.md.
 *
 * FELUCCA_DUAL 1  stage 1: CPU1 starts and counts in RAM (shown on the screen); no audio work.
 * FELUCCA_DUAL 2  CPU1 renders some of the block's jobs (a synth part: mix_part, its engine, DIST, SLICER,
 *                 level, pan, sends into its own accumulators; the drum track: slicer_drums and dbus_run into
 *                 the buses) while CPU0 renders the others, inside the same audio block (fork / join in the
 *                 ALNK0 interrupt). The sums are integer: the same samples whichever core renders what.
 *                 Which jobs go to CPU1 is decided each block (dual_split): each job's render time, measured
 *                 on the core that ran it (TIMER4 ticks, smoothed), and the longest first onto the core with
 *                 less so far (perf/mots-hot, 2026-10-13: the X0X 808 alone was half of CPU0's work, while
 *                 parts 2-3 on CPU1 left it at 10 %). A build with DUAL_PARTS set (the emulator's
 *                 references, tests/emu_dual_check.py) keeps that fixed set of parts on CPU1 and the drums on
 *                 CPU0, as before.
 *
 * FELUCCA_DUAL_IDLE 1 (with 2, the default): CPU1 sleeps (idle) between jobs; CPU0 posts a job and raises
 *                 soft interrupt 124, whose handler on CPU1 runs it (fm1_cpu1_wake); 0: CPU1 spins in RAM.
 *
 * Never a hang: CPU1 that does not answer at boot stays in reset; a job that is not done within
 * DUAL_JOIN_US, or a fault on CPU1, puts CPU1 back into reset for good and CPU0 renders everything
 * (the parts CPU1 was rendering may click once).
 *
 * FM-1s whose CPU1 misreads RAM (docs/DUAL-CORE.md 3.5): CPU1 runs only the next posted job with a matching check
 * word (hal/fm1_dual_guard.h, after X0X), reads a boot canary before its hello, and is held at the first request it
 * refuses (MISREAD) or a wrong canary (CANARY). 2ND CORE OFF (dual_mode.h) or a boot after an early crash: one core.
 * FELUCCA_DUAL_BOOST: the core supply raised first (hal/fm1_supply.h, EXPERIMENTAL). */
#if FELUCCA_DUAL
#define DUAL_HELLO_US 50000u                       /* CPU1's hello at boot */
#define DUAL_JOIN_US ((HALF_FRAMES * 1000000u) / FS) /* one half buffer (5.8 ms): late anyway */
#define DUAL_KICK_US 100u                          /* a job not picked up after this long: CPU1 woken again (dual_join) */
#ifndef FELUCCA_DUAL_BOOST
#define FELUCCA_DUAL_BOOST 0                       /* EXPERIMENTAL, at the owner's risk: VDC14 5, SYSVDD 15 before CPU1
                                                    * starts (hal/fm1_supply.h; the builder's DUAL_BOOST) */
#endif
#define DUAL_SLEEPS (FELUCCA_DUAL >= 2 && FELUCCA_DUAL_IDLE)
#ifdef DUAL_PARTS
#define DUAL_BALANCE 0                             /* a fixed set of parts on CPU1, the drums on CPU0 */
#else
#define DUAL_BALANCE 1                             /* the jobs split each block by their measured cost */
#endif
#include "dual_mode.h"                            /* 2ND CORE AUTO / OFF, DUAL_*: why CPU1 is down (pure) */
_Static_assert(C1S_LOOP == 3u, "hal/fm1_dual.h fm1_dual_sleep_ram writes 3 as C1S_LOOP");
#if FELUCCA_DUAL_BOOST
#include "fm1_supply.h"                           /* EXPERIMENTAL: the core supply raised before CPU1 starts */
#endif
#define DUAL_JOB_DRUMS (1u << NPART)               /* a job's bit: the drum track (slicer_drums, dbus_run) */
#if REV_LATE
#define DUAL_JOB_REV (1u << (NPART + 1u))          /* the reverb's kept block (fx.c rev_job) */
#define DUAL_JOBS (NPART + 2u)
#else
#define DUAL_JOB_REV 0u
#define DUAL_JOBS (NPART + 1u)
#endif
#if DUAL_BALANCE
#define DUAL_ALLOWED ((1u << DUAL_JOBS) - 1u)      /* the job bits CPU1 may be given (fm1_dual_take): any */
#else
#define DUAL_ALLOWED (DUAL_PARTS | DUAL_JOB_REV)   /* (its fixed parts, and the reverb's kept block) */
#endif
static struct {
    uint8_t up;                                    /* CPU1 runs and takes jobs */
    uint8_t why;                                   /* DUAL_*: why it is down (dual_mode.h) */
    uint8_t boot_mode;                             /* the 2ND CORE setting this boot started with */
    uint8_t supply;                                /* FELUCCA_DUAL_BOOST: VDC14 << 4 | SYSVDD before the boost */
    uint32_t jobs, wait_max;                       /* jobs done; longest CPU0 wait (ticks) */
    uint32_t rewakes;                              /* wakes sent again for a job CPU1 had not picked up (dual_join) */
    /* when it went down (SYSTEM > INFO, console; every path: dual_down): fm1_ms then, the job's mask, the latch word
     * whether its wake was still pending (hal/fm1_dual.h fm1_dual_wake_pending), the beat it had counted;
     * stop: the INFO line's record (dual_mode.h) */
    uint32_t down_ms, down_mask, down_beat;
    uint8_t down_pend;
    uint32_t t_start;                              /* TIMER4 when CPU1 was started (dual_boot) */
    uint32_t posts;                                /* jobs posted (mix_block_dual) */
    dual_stop_t stop;
} dual;
static uint8_t dual_mode;                          /* the 2ND CORE setting (dual_mode.h; settings_word.c bits 27..28) */
static volatile uint8_t dual_c1_at;               /* CPU1: 1 + the job it renders now (0: none yet; dual_down's note) */

#if FELUCCA_DUAL >= 2
static mixacc_t dual_acc[2];                       /* [0] CPU0's parts, [1] CPU1's */
static volatile uint32_t dual_c1_ticks;            /* CPU1: timer ticks inside its jobs, a running total (it alone
                                                    * writes it; CPU0 reads it once a half: dual_load.h) */
#if FELUCCA_BENCH
static volatile uint32_t dual_bench[3];            /* (measurements, bench.c: CPU1's ticks in its jobs, CPU0's ticks
                                                    * waiting in dual_join, jobs) */
#define DUAL_BENCH(k, v) (dual_bench[k] += (v))
#else
#define DUAL_BENCH(k, v) ((void)0)
#endif

AINL void dual_clear(mixacc_t *A, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        A->send_c[i] = A->send_d[i] = A->send_r[i] = A->mix_l[i] = A->mix_r[i] = 0;
}

/* each job's render time (TIMER4 ticks x 8, smoothed over ~8 blocks): written by the core that ran it, read by CPU0
 * after the join (dual_split). Never an input of the audio: only of who renders what */
static uint32_t dual_cost[DUAL_JOBS];

/* the jobs of mask, on the core that calls it, into A (the parts) and the buses (the drums: CPU0 left them alone,
 * its parts go to its own accumulators). Each job's time measured as it runs */
static HOT void dual_run(uint32_t mask, uint32_t n, mixacc_t *A)
{
    uint32_t p, t0, t1;
    for (p = 0; p < DUAL_JOBS; p++)
        if ((mask >> p) & 1u) {
            if (A == &dual_acc[1])
                dual_c1_at = (uint8_t)(p + 1u);    /* (CPU1: where it is, if it never comes back) */
            t0 = fm1_ticks();
            if (p < NPART) {
                mix_part(&trk[p], n, A);
            } else if (p == NPART) {
                slicer_drums(mix_l, mix_r, send_r, n);
                dbus_run(n);                       /* the drum bus's inserts and sends (fx.c; the drums alone) */
            } else {
#if REV_LATE
                if (!rvj.done)                     /* (CPU1 down after it ran it: not twice) */
                    rev_job();                     /* the reverb's block kept from the last block (fx.c) */
#endif
            }
            t1 = fm1_ticks() - t0;
            dual_cost[p] += t1 - (dual_cost[p] >> 3);
        }
}

static HOT void dual_job(uint32_t mask)            /* CPU1: its jobs of the block (RAM: core.h HOT) */
{
    mixacc_t *A = &dual_acc[1];
    uint32_t t0 = fm1_ticks(), dt;                 /* (its load: the time in here, dual_load.h) */
    dual_clear(A, CTL);
    dual_run(mask & DUAL_ALLOWED, CTL, A);
    dt = fm1_ticks() - t0;
    dual_c1_ticks += dt;                           /* (before `done`: the caller's csync orders it) */
    DUAL_BENCH(0, dt);
    DUAL_BENCH(2, 1u);
}

/* CPU0, before the fork: the jobs for CPU1. The longest first, each onto the core with less so far (ties: CPU0, no
 * job when CPU1 would get nothing); CPU1 starts with its wake's delay. Integer compares only */
#define DUAL_WAKE_COST 64u                          /* (ticks x 8: ~0.3 us, CPU1's interrupt entry) */
static HOT uint32_t dual_split(uint32_t jobs)    /* jobs: the block's (the reverb's only when one is kept) */
{
#if DUAL_BALANCE
    uint32_t ord[DUAL_JOBS], i, j, k, c0 = 0, c1 = DUAL_WAKE_COST, mask = 0;
    for (i = 0; i < DUAL_JOBS; i++) {              /* (insertion sort, longest first: 4 or 5 jobs) */
        for (j = i; j && dual_cost[ord[j - 1u]] < dual_cost[i]; j--)
            ord[j] = ord[j - 1u];
        ord[j] = i;
    }
    for (i = 0; i < DUAL_JOBS; i++) {
        k = ord[i];
        if (!((jobs >> k) & 1u))
            continue;
        if (c1 < c0) {
            c1 += dual_cost[k];
            mask |= 1u << k;
        } else {
            c0 += dual_cost[k];
        }
    }
    return mask;
#else
    return (DUAL_PARTS | DUAL_JOB_REV) & jobs;
#endif
}
#endif

#ifndef DUAL_FAILTEST
#define DUAL_FAILTEST 0      /* emulator tests of the fallbacks: 1 no hello, 2 a job that never ends, 3 CPU1's
                              * fault handler in a job, 4 a request whose check word is wrong (a misread, as CPU1
                              * would see it), 5 a wrong boot canary, 6 a lost wake (CPU1 back to sleep with a job
                              * posted, once: dual_join wakes it again) (never in a release) */
#endif
/* CPU1: how far it got (dual_mode.h C1S_*), only ever forward */
#define C1_STAGE(s)                                                                                                    \
    do {                                                                                                               \
        if (fm1_dual_mb.stage < (s))                                                                                   \
            fm1_dual_mb.stage = (s);                                                                                   \
    } while (0)
#if !DUAL_SLEEPS
/* (stage 1, or CPU1 spinning: the wake interrupt is never set up, but hal/fm1_dual.h's entry isr_c1_wake
 * calls this body: without it FELUCCA_DUAL=1 did not link) */
void HOT fm1_cpu1_wake(void) { fm1_dual_wake_ack(); }
#endif
#if DUAL_SLEEPS
/* CPU1, soft interrupt 124 (isr_c1_wake): the jobs posted since it last looked, then back to its idle.
 * The latch is cleared before req is read, so a job posted after the last look wakes it again */
void HOT fm1_cpu1_wake(void)
{
    static uint32_t jobs, done;                    /* (done: CPU1's own, never read back from the mailbox) */
    uint32_t r, a;
    fm1_dual_wake_ack();
    fm1_dual_mb.wakes++;
    C1_STAGE(C1S_WOKEN);
#if DUAL_FAILTEST == 6
    {
        static uint8_t lost;
        if (jobs == 3000u && !lost) {
            lost = 1;
            return;                                /* (as if the wake had never been taken) */
        }
    }
#endif
    while ((r = fm1_dual_mb.req) != done) {
        fm1_dual_sync();                           /* the request's data after `req` */
        a = fm1_dual_mb.arg;                       /* (read once: the job gets what was checked) */
        if (fm1_dual_take(r, done, a, fm1_dual_mb.check, DUAL_ALLOWED) != FM1_DG_RUN) {
            fm1_dual_note_bad(r, done);            /* a misread: not run; CPU0 holds CPU1 (dual_join) */
            break;
        }
        fm1_dual_mb.took = r;                      /* (CPU0 stops waking it again) */
        C1_STAGE(C1S_TOOK);
#if DUAL_FAILTEST == 2
        if (jobs == 3000u)
            for (;;)
                ;
#elif DUAL_FAILTEST == 3
        if (jobs == 3000u)
            FAR(fm1_dual_cpu1_fault)(1);
#endif
        dual_job(a);
        jobs++;
        done = r;
        fm1_dual_sync();                           /* the job's stores before `done` */
        fm1_dual_mb.done = r;
        C1_STAGE(C1S_DONE);
        fm1_dual_sync();
    }
    if (fm1_dual_mb.ping != fm1_dual_mb.pong)
        fm1_dual_mb.pong = fm1_dual_mb.ping;       /* (answered at each wake: a job every audio block) */
}
#endif

void fm1_cpu1_main(void)                           /* CPU1, from fm1_cpu1_entry (hal/fm1_dual.h) */
{
    uint32_t seq = 0, arg = 0;
    fm1_dual_mb.stage = C1S_MAIN;
#if DUAL_FAILTEST == 1
    for (;;)
        ;
#endif
    fm1_dual_mb.canary = 1u + fm1_dual_canary_scan(fm1_dual_canary, FM1_DUAL_CANARY_PASSES);   /* (CPU0 checks it) */
#if DUAL_FAILTEST == 5
    fm1_dual_mb.canary++;                          /* (as if one word had been read wrong) */
#endif
    fm1_dual_sync();                               /* the canary before the hello */
    fm1_dual_mb.hello = FM1_DUAL_HELLO;
    fm1_dual_mb.stage = C1S_HELLO;
#if DUAL_SLEEPS
    (void)seq;
    (void)arg;
    FL_FAR(fm1_dual_sleep_ram)();                  /* never returns: the jobs come by interrupt */
    for (;;)
        ;
#else
    fm1_dual_mb.stage = C1S_LOOP;
    for (;;) {
        seq = FL_FAR(fm1_dual_idle_ram)(seq, 1u, DUAL_ALLOWED, &arg);   /* (a request it may run; its beat counted) */
#if FELUCCA_DUAL >= 2
        fm1_dual_mb.took = seq;                    /* (as the wake handler: CPU1 STOP's W / J) */
        C1_STAGE(C1S_TOOK);
#endif
#if DUAL_FAILTEST == 2
        if (seq == 3000u)
            for (;;)
                ;
#elif DUAL_FAILTEST == 3
        if (seq == 3000u)
            fm1_dual_cpu1_fault(1);                /* (what fm1_fault_c does on CPU1: an exception itself
                                                    * cannot be raised in the emulator) */
#endif
#if FELUCCA_DUAL >= 2
        FAR(dual_job)(arg);
        C1_STAGE(C1S_DONE);
#else
        (void)arg;
#endif
    }
#endif
}

/* CPU0: CPU1 into reset, for good; what it was doing kept for SYSTEM > INFO (CPU1 STOP, HELLO, BEAT) and the
 * console, on every path (a job not done, a fault, a misread, the boot's canary or no hello, a flash operation).
 * WAIT_US: how long CPU0 had waited for the job (0: not in a join) */
static void dual_down(uint32_t why, uint32_t wait_us)
{
    uint32_t req = fm1_dual_mb.req;
    dual.down_pend = (uint8_t)fm1_dual_wake_pending();   /* (before the halt: the wake as CPU1 left it) */
    fm1_dual_halt();
    dual.down_ms = fm1_ms;
    dual.down_mask = fm1_dual_mb.arg;
    dual.down_beat = fm1_dual_mb.count;
    dual.stop.stopped = 1;
    dual.stop.in_job = (uint8_t)(fm1_dual_mb.done != req);
    dual.stop.took = (uint8_t)(fm1_dual_mb.took == req);
    dual.stop.at = dual_c1_at;
    dual.stop.no_clock = (uint8_t)(fm1_ms == 0u);
    dual.stop.c1_ms = (fm1_ticks() - dual.t_start) / (1000u * FM1_TICKS_PER_US);   /* (TIMER4 wraps after 178 s) */
    if (dual.down_ms > 170000u)
        dual.stop.c1_ms = dual.down_ms;            /* (late: the ms since boot instead, as near) */
    dual.stop.job = dual.jobs;
    dual.stop.wait_us = wait_us;
    dual.up = 0;
    dual.why = (uint8_t)why;
}

/* boot (audio set up, its interrupt not yet on; the vectors still writable) */
static __attribute__((noinline)) void dual_boot(void)
{
    dual.boot_mode = dual_mode;
    dual.why = (uint8_t)dual_boot_why(dual_mode, bootguard.failed);   /* 2ND CORE OFF, or the last boot crashed early */
    if (dual.why != DUAL_UP)
        return;                                    /* (CPU1 stays in reset, as at power-on) */
#if FELUCCA_DUAL_BOOST
    dual.supply = (uint8_t)fm1_supply_boost();     /* EXPERIMENTAL: VDC14 5, SYSVDD 15 first (hal/fm1_supply.h) */
#endif
#if DUAL_SLEEPS
    fm1_dual_wake_setup(isr_c1_wake);
#endif
    dual.t_start = fm1_ticks();
    dual.up = (uint8_t)fm1_dual_start(DUAL_HELLO_US);
    if (!dual.up)
        dual_down(DUAL_NO_HELLO, 0);               /* (already in reset: the record only) */
    else if (fm1_dual_mb.canary != 1u)             /* it read the canary wrong (or never read it) */
        dual_down(DUAL_CANARY, 0);
}

/* every flash operation, IRQs off (hal/fm1_flash.h irq_save): CPU1 must be in its RAM loop.
 * The audio interrupt always ends its job (or halts it), so this only catches the impossible. */
static void dual_flash_enter(void)
{
    if (dual.up && (fm1_dual_mb.done != fm1_dual_mb.req || fm1_dual_mb.fault))
        dual_down(fm1_dual_mb.fault ? DUAL_FAULT : DUAL_FLASH, 0);
}

static void ui_say(const char *a, const char *b);   /* ui.c */
#define DUAL_WHY(w) dual_why_name(w)                /* (dual_mode.h: the reasons' names, the Optimist UI's CPU1 cell too) */

static void dual_dec(char *b, uint32_t v)          /* v in decimal, 10 digits at most */
{
    char t[10];
    uint32_t k = 0;
    do
        t[k++] = (char)('0' + v % 10u);
    while ((v /= 10u) && k < 10u);
    while (k)
        *b++ = t[--k];
    *b = 0;
}

/* main loop, every frame: the liveness ping (CPU1 answers from its idle loop); CPU1 going down is
 * told once (not when 2ND CORE is OFF: the user's choice), and on a misread AUTO turns itself OFF (dual_mode.h; saved by
 * settings_poll, once stopped). Stage 1: CPU1's counter and the ping answer at the bottom of the screen, twice a second. */
static void dual_frame(void)
{
    static uint8_t told;
    fm1_dual_mb.ping++;
    if (!dual.up && !told) {
        told = 1;
        if (dual.why != DUAL_SETOFF)
            ui_say("CPU1 OFF ", DUAL_WHY(dual.why));
        dual_mode = (uint8_t)dual_mode_after(dual_mode, dual.boot_mode, dual.why);
    }
#if FELUCCA_DUAL == 1
    {
        static uint32_t last;
        char b[32];
        if (fm1_ms - last >= 500u) {
            uint32_t lag = fm1_dual_mb.ping - fm1_dual_mb.pong;
            last = fm1_ms;
            str_cpy(b, "CPU1 ", sizeof b);
            if (dual.up) {
                dual_dec(b + 5, fm1_dual_mb.count);
                str_cpy(b + str_len(b), lag <= 2u ? " OK" : " LATE", 6);
            } else {
                str_cpy(b + 5, DUAL_WHY(dual.why), sizeof b - 5u);
            }
            draw_text_box(0, 222, 240, &FONT_S, b, dual.up ? RGB(80, 220, 120) : RGB(255, 80, 80), 1);
        }
    }
#endif
}

#if FELUCCA_DUAL >= 2
/* CPU0: wait for CPU1's job; 1 = its accumulators hold the parts */
static HOT int dual_join(uint32_t req)
{
    uint32_t t0 = fm1_ticks(), dt, why;
#if DUAL_SLEEPS
    uint32_t kick = DUAL_KICK_US * FM1_TICKS_PER_US;
#endif
    while (fm1_dual_mb.done != req || fm1_dual_mb.bad) {   /* (bad: a request nobody posted, now or while it waited;
                                                            * its parts here even if this block's job is done) */
        dt = fm1_ticks() - t0;
        why = fm1_dual_mb.fault ? DUAL_FAULT : fm1_dual_mb.bad ? DUAL_MISREAD
                                 : dt > DUAL_JOIN_US * FM1_TICKS_PER_US ? DUAL_TIMEOUT : DUAL_UP;
        if (why == DUAL_TIMEOUT && fm1_dual_mb.done == req)
            continue;                              /* (done while CPU0 was away, in a higher interrupt: not late) */
        if (why != DUAL_UP) {
            FAR(dual_down)(why, dt / FM1_TICKS_PER_US);
            return 0;
        }
#if DUAL_SLEEPS
        if (dt > kick && fm1_dual_mb.took != req) {   /* not picked up: its wake again (fm1_dual_rewake) */
            fm1_dual_rewake();
            dual.rewakes++;
            kick += DUAL_KICK_US * FM1_TICKS_PER_US;
        }
#endif
    }
    fm1_dual_sync();                               /* CPU1's sums after its done flag */
    dt = fm1_ticks() - t0;
    DUAL_BENCH(1, dt);
    if (dt > dual.wait_max)
        dual.wait_max = dt;
    dual.jobs++;
    return 1;
}

/* CPU0, once an audio half (audio.c, after song.cpu_q8; flash code): CPU1's load (dual_load.h). Its jobs of the
 * half are joined: their ticks are in */
static void dual_load_half(void)
{
    static uint32_t last;
    uint32_t b = dual_c1_ticks;
    dual_load_take((b - last) / FM1_TICKS_PER_US, dual.up);
    last = b;
}

/* CPU0, before the fork: the rng() draws of the parts' LFOs that wrap in this block (voice.c track_lfo_tick), in
 * part order as the single-core render draws them; the render takes them from dual_lfo_rnd */
static HOT void dual_lfo_draw(void)
{
    uint32_t p;
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        if ((uint32_t)(t->lfo_ph + lfo_inc(t)) < t->lfo_ph)   /* (voice.c: the same step) */
            dual_lfo_rnd[p] = rng();
    }
}

/* mix_block (fx.c) with the jobs dual_split gives CPU1 (the parts of DUAL_PARTS when set) */
static HOT void mix_block_dual(int32_t *out, uint32_t n)
{
    mixacc_t *A = &dual_acc[0], *B = &dual_acc[1];
    uint32_t i, mask = 0, req = 0, got = 0, post = 0, jobs, sw;
#if FELUCCA_USB_AUDIO
    capture_clear(n);                              /* the stems (each part writes its own; fx.c) */
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    dual_clear(A, n);
    FAR(events_block)(n);                          /* (the sequencer stays in XIP) */
    BENCH_BLOCK();
#if FELUCCA_MACROS
    FAR(mac_pre)();                                /* the macros' values in (macro.c), for both cores */
#endif
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    dual_vbusy = voices_busy();                    /* the swarm's copies: the same on both cores */
    dual_lfo_draw();                               /* the LFOs' random values (the render draws none) */
    sw = pold_any() ? FAR(pold_in)() : 0u;         /* (SLOOP 2.6) the parts fading after a sound load: their old
                                                    * sound for this block, on both cores (voice.c sound_change) */
    drums.a0 = TDRUM->att;                         /* the drum track's mute / solo fade (before the fork: either */
    drums.a1 = 32767 - gain_next(TDRUM);           /* core may render the drums) */
    jobs = (1u << (NPART + 1u)) - 1u;              /* the parts and the drums */
#if REV_LATE
    rev_late = (uint8_t)(dual.up && n == CTL);     /* CPU1 up: the reverb one block late (fx.c fx_buses) */
    jobs |= rvj.pend ? DUAL_JOB_REV : 0u;          /* (its kept block, on either core) */
#endif
    if (dual.up && n == CTL) {
        mask = dual_split(jobs);
        post = mask || !DUAL_BALANCE;              /* (a fixed DUAL_PARTS=0: CPU1 up, an empty job each block) */
    }
    if (post) {
        req = fm1_dual_mb.req + 1u;
        dual.posts++;
        fm1_dual_mb.arg = mask;
        fm1_dual_mb.check = FM1_DUAL_CHECK(req, mask) ^ (uint32_t)(DUAL_FAILTEST == 4 && req == 3000u);
        dual_c1_at = 0;                            /* (CPU1 sets it as it starts each of these jobs) */
        fm1_dual_sync();                           /* the block's state, arg and check before req */
        fm1_dual_mb.req = req;
#if DUAL_SLEEPS
        fm1_dual_sync();
        fm1_dual_wake();                           /* CPU1 out of its idle (soft interrupt 124) */
#endif
    }
    dual_run(jobs & ~mask, n, A);                  /* CPU0's jobs: those CPU1 was not given */
    if (post) {
        got = dual_join(req);
        if (!got) {                                /* CPU1 is down: its jobs here (a click, once) */
            if (mask & DUAL_JOB_DRUMS)             /* (whatever CPU1 left on the buses: the drums again) */
                for (i = 0; i < n; i++)
                    send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
            dual_run(mask, n, A);
        }
    }
    for (i = 0; i < n; i++) {
        send_c[i] += A->send_c[i];
        send_d[i] += A->send_d[i];
        send_r[i] += A->send_r[i];
        mix_l[i] += A->mix_l[i];
        mix_r[i] += A->mix_r[i];
    }
    if (got)
        for (i = 0; i < n; i++) {
            send_c[i] += B->send_c[i];
            send_d[i] += B->send_d[i];
            send_r[i] += B->send_r[i];
            mix_l[i] += B->mix_l[i];
            mix_r[i] += B->mix_r[i];
        }
    if (sw)
        FAR(pold_out)(sw);                         /* (and the new one back) */
    mix_finish(out, n);
#if FELUCCA_MACROS
    FAR(mac_post)();
#endif
}
#endif
#endif
