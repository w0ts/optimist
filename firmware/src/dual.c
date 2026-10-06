/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core (CPU1). EXPERIMENTAL, emulator-validated only: docs/DUAL-CORE.md.
 *
 * FELUCCA_DUAL 1  stage 1: CPU1 starts and counts in RAM (shown on the screen); no audio work.
 * FELUCCA_DUAL 2  CPU1 renders the synth parts of DUAL_PARTS (mix_part: engine, DIST, SLICER, level,
 *                 pan, sends) into its own accumulators while CPU0 renders the other parts and the
 *                 drums, inside the same audio block (fork / join in the ALNK0 interrupt). The sums
 *                 are integer: the same samples as one core rendering all parts.
 *
 * FELUCCA_DUAL_IDLE 1 (with 2, the default): CPU1 sleeps (idle) between jobs; CPU0 posts a job and raises
 *                 soft interrupt 124, whose handler on CPU1 runs it (fm1_cpu1_wake); 0: CPU1 spins in RAM.
 *
 * Never a hang: CPU1 that does not answer at boot stays in reset; a job that is not done within
 * DUAL_JOIN_US, or a fault on CPU1, puts CPU1 back into reset for good and CPU0 renders everything
 * (the parts CPU1 was rendering may click once). */
#if FELUCCA_DUAL
#define DUAL_HELLO_US 50000u                       /* CPU1's hello at boot */
#define DUAL_JOIN_US ((HALF_FRAMES * 1000000u) / FS) /* one half buffer (5.8 ms): late anyway */
#ifndef FELUCCA_DUAL_IDLE
#define FELUCCA_DUAL_IDLE 1                        /* CPU1 asleep between jobs (2 only) */
#endif
#define DUAL_SLEEPS (FELUCCA_DUAL >= 2 && FELUCCA_DUAL_IDLE)
#ifndef DUAL_PARTS
#define DUAL_PARTS 0x6u                            /* parts 2 and 3 on CPU1 */
#endif
enum { DUAL_OFF, DUAL_NO_HELLO, DUAL_TIMEOUT, DUAL_FAULT, DUAL_FLASH };
static struct {
    uint8_t up;                                    /* CPU1 runs and takes jobs */
    uint8_t why;                                   /* DUAL_*: why it is down */
    uint32_t jobs, wait_max;                       /* jobs done; longest CPU0 wait (ticks) */
} dual;

#if FELUCCA_DUAL >= 2
static mixacc_t dual_acc[2];                       /* [0] CPU0's parts, [1] CPU1's */

AINL void dual_clear(mixacc_t *A, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        A->send_c[i] = A->send_d[i] = A->send_r[i] = A->mix_l[i] = A->mix_r[i] = 0;
}

static HOT void dual_job(uint32_t mask)            /* CPU1: its parts of the block (RAM: core.h HOT) */
{
    mixacc_t *A = &dual_acc[1];
    uint32_t p;
    dual_clear(A, CTL);
    for (p = 0; p < NPART; p++)
        if ((mask >> p) & 1u)
            mix_part(&trk[p], CTL, A);
}
#endif

#ifndef DUAL_FAILTEST
#define DUAL_FAILTEST 0      /* emulator tests of the fallbacks: 1 no hello, 2 a job that never ends, 3 CPU1's
                              * fault handler in a job (never in a release) */
#endif
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
    static uint32_t jobs;
    uint32_t r;
    fm1_dual_wake_ack();
    while ((r = fm1_dual_mb.req) != fm1_dual_mb.done) {
        fm1_dual_sync();                           /* the request's data after `req` */
#if DUAL_FAILTEST == 2
        if (jobs == 3000u)
            for (;;)
                ;
#elif DUAL_FAILTEST == 3
        if (jobs == 3000u)
            FAR(fm1_dual_cpu1_fault)(1);
#endif
        dual_job(fm1_dual_mb.arg);
        jobs++;
        fm1_dual_sync();                           /* the job's stores before `done` */
        fm1_dual_mb.done = r;
        fm1_dual_sync();
    }
    if (fm1_dual_mb.ping != fm1_dual_mb.pong)
        fm1_dual_mb.pong = fm1_dual_mb.ping;       /* (answered at each wake: a job every audio block) */
}
#endif

void fm1_cpu1_main(void)                           /* CPU1, from fm1_cpu1_entry (hal/fm1_dual.h) */
{
    uint32_t seq = 0;
#if DUAL_FAILTEST == 1
    for (;;)
        ;
#endif
    fm1_dual_mb.hello = FM1_DUAL_HELLO;
#if DUAL_SLEEPS
    (void)seq;
    FL_FAR(fm1_dual_sleep_ram)();                  /* never returns: the jobs come by interrupt */
#endif
    for (;;) {
        seq = FL_FAR(fm1_dual_idle_ram)(seq, FELUCCA_DUAL == 1);
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
        FAR(dual_job)(fm1_dual_mb.arg);
#endif
    }
}

static void dual_down(uint32_t why)                /* CPU0: CPU1 into reset, for good */
{
    fm1_dual_halt();
    dual.up = 0;
    dual.why = (uint8_t)why;
}

/* boot (audio set up, its interrupt not yet on; the vectors still writable) */
static void dual_boot(void)
{
#if DUAL_SLEEPS
    fm1_dual_wake_setup(isr_c1_wake);
#endif
    dual.up = (uint8_t)fm1_dual_start(DUAL_HELLO_US);
    dual.why = dual.up ? DUAL_OFF : DUAL_NO_HELLO;
}

/* every flash operation, IRQs off (hal/fm1_flash.h irq_save): CPU1 must be in its RAM loop.
 * The audio interrupt always ends its job (or halts it), so this only catches the impossible. */
static void dual_flash_enter(void)
{
    if (dual.up && (fm1_dual_mb.done != fm1_dual_mb.req || fm1_dual_mb.fault))
        dual_down(fm1_dual_mb.fault ? DUAL_FAULT : DUAL_FLASH);
}

static void ui_say(const char *a, const char *b);   /* ui.c */
static const char *const DUAL_WHY[] = {"", "NO START", "TIMEOUT", "FAULT", "FLASH"};

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
 * told once. Stage 1: CPU1's counter and the ping answer at the bottom of the screen, twice a second. */
static void dual_frame(void)
{
    static uint8_t told;
    fm1_dual_mb.ping++;
    if (!dual.up && !told) {
        told = 1;
        ui_say("CPU1 OFF ", DUAL_WHY[dual.why % 5u]);
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
                str_cpy(b + 5, DUAL_WHY[dual.why % 5u], sizeof b - 5u);
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
    uint32_t t0 = fm1_ticks(), dt;
    while (fm1_dual_mb.done != req) {
        dt = fm1_ticks() - t0;
        if (fm1_dual_mb.fault) {
            FAR(dual_down)(DUAL_FAULT);
            return 0;
        }
        if (dt > DUAL_JOIN_US * FM1_TICKS_PER_US) {
            FAR(dual_down)(DUAL_TIMEOUT);
            return 0;
        }
    }
    fm1_dual_sync();                               /* CPU1's sums after its done flag */
    dt = fm1_ticks() - t0;
    if (dt > dual.wait_max)
        dual.wait_max = dt;
    dual.jobs++;
    return 1;
}

/* mix_block (fx.c) with the parts of DUAL_PARTS on CPU1 */
static HOT void mix_block_dual(int32_t *out, uint32_t n)
{
    mixacc_t *A = &dual_acc[0], *B = &dual_acc[1];
    uint32_t i, p, mask = 0, req = 0, got = 0;
#if FELUCCA_USB_AUDIO
    for (i = 0; i < n * NTRK; i++)                 /* the stems (each part writes its own) */
        track_capture[i] = 0;
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    dual_clear(A, n);
    FAR(events_block)(n);                          /* (the sequencer stays in XIP) */
    BENCH_BLOCK();
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    dual_vbusy = voices_busy();                    /* the swarm's copies: the same on both cores */
    if (dual.up && n == CTL) {
        mask = DUAL_PARTS;
        req = fm1_dual_mb.req + 1u;
        fm1_dual_mb.arg = mask;
        fm1_dual_sync();                           /* the block's state and arg before req */
        fm1_dual_mb.req = req;
#if DUAL_SLEEPS
        fm1_dual_sync();
        fm1_dual_wake();                           /* CPU1 out of its idle (soft interrupt 124) */
#endif
    }
    for (p = 0; p < NPART; p++)
        if (!((mask >> p) & 1u))
            mix_part(&trk[p], n, A);
    drums.a0 = TDRUM->att;                         /* the drum track's mute / solo fade */
    drums.a1 = 32767 - gain_next(TDRUM);
    slicer_drums(mix_l, mix_r, send_r, n);
    if (mask) {
        got = dual_join(req);
        if (!got)                                  /* CPU1 is down: its parts here (a click, once) */
            for (p = 0; p < NPART; p++)
                if ((mask >> p) & 1u)
                    mix_part(&trk[p], n, A);
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
    mix_finish(out, n);
}
#endif
#endif
