/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio ISR against the main loop while recording live notes (2026-10-15: an FM-1 froze inside the audio render
 * with REC armed and the sequencer playing, B12 5a2c275a: prev_where 2, prev_in_audio 1, no exception, the watchdog).
 *
 * The whole Optimist build of tests/ui_optimist_test.c (UI, storage, sequencer, FX), with the user's sequencer switches
 * (run_tests.sh: mots.config's), plays with REC armed on the synth and drum tracks while random note-ons / offs (chords,
 * overlaps, the same note again, at the loop's wrap, a full pattern; REC QUANTIZE on / off, MONO / POLY), knob and
 * button use, step puts / clears, locks / nudges / chance / conditions, LEN / DIV / SWING changes, undo / redo,
 * pattern launches and track clears come from the "main loop".
 *
 *   det (default): one thread; the audio half (HALF_FRAMES / CTL mix_blocks, as audio.c) runs at random points between
 *        the main loop's actions and its stages (ui_input, ui_leds, ui_draw), from a fixed seed: reproducible.
 *   sig: the audio half runs from SIGALRM (setitimer, a random 40..400 us apart) at any instruction of the main loop,
 *        as the interrupt does; fm1_irq_off / fm1_irq_on block / unblock it (cli / sti). Not reproducible: the seed
 *        and the count are printed.
 * A watchdog thread: an audio half (or a main-loop frame) running longer than HANG_MS (default 2000) of wall time is a
 * hang: the main thread's stack is printed (SIGUSR1, backtrace) and the test exits 3. After every half (det) the
 * state the ISR walks is checked (counts, ring offsets: an inconsistency is a hang to come) and an sti inside the ISR
 * is counted (the device's cli / sti do not nest).
 * HOST_DUAL=1 (with FELUCCA_DUAL=2): the second core as a thread (tests/audio_hang_dual.h); with -fsanitize=thread.
 *   env: HANG_MODE=det|sig, HANG_SEED=n, HANG_FRAMES=n (det; default 6000), HANG_SECS=n (sig; default 10), HANG_MS=n */
#define HOST_IRQ_HOOKS 1
#ifndef FELUCCA_DUAL
#define FELUCCA_DUAL 0
#endif
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <execinfo.h>
#define UIO_NO_MAIN 1
#if HOST_DUAL
#include <stdint.h>
static _Thread_local uint32_t hd_core;                  /* 0 this thread ("CPU0"), 1 the worker */
static uint32_t fm1_cnum(void) { return hd_core; }      /* (hal/fm1_dual.h: eng_fm6.c's buses a core) */
static void mix_block(int32_t *out, uint32_t n);        /* (audio_hang_dual.h: the two-core render) */
#endif
#include "ui_optimist_test.c"
#if HOST_DUAL
#include "audio_hang_dual.h"
#endif

/* ---- the interrupt model */
static volatile sig_atomic_t in_isr, irq_masked;
static volatile uint32_t sti_in_isr, cli_in_isr;
static int sig_mode;
static void fm1_irq_off(void)
{
    if (in_isr) {
        cli_in_isr++;
        return;
    }
    if (sig_mode) {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGALRM);
        pthread_sigmask(SIG_BLOCK, &s, 0);
    }
    irq_masked = 1;
}
static void fm1_irq_on(void)
{
    if (in_isr) {
        sti_in_isr++;                                   /* (on the FM-1 this would let a higher interrupt in) */
        return;
    }
    irq_masked = 0;
    if (sig_mode) {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGALRM);
        pthread_sigmask(SIG_UNBLOCK, &s, 0);
    }
}

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
/* the process's CPU time (the watchdog's clock: a loaded machine stalls the wall clock, not this; the watchdog thread
 * itself sleeps) */
static uint64_t cpu_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static _Atomic uint64_t isr_t0, frame_t0;        /* (atomic: the watchdog thread reads them) */
static volatile uint64_t halves, frames_done;
static uint64_t isr_max_ns;
static uint32_t seed0, rs;
static const char *where_now = "setup";                /* the main loop's action (printed on a hang) */
static volatile const char *isr_where = "-";

static uint32_t R(uint32_t n) { rs = rs * 1664525u + 1013904223u; return n ? (rs >> 8) % n : 0u; }

/* ---- the state the ISR walks: consistent after every half (det mode) */
static int bad_state(char *why, size_t n)
{
    uint32_t i, j;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        if (t->rh_n > 4u || t->seq_n > 4u || t->rskip_n > 4u)
            return snprintf(why, n, "T%u rh_n %u seq_n %u rskip_n %u", i + 1u, t->rh_n, t->seq_n, t->rskip_n), 1;
        if (t->p[P_SLEN] < 1 || t->p[P_SLEN] > NSTEP)
            return snprintf(why, n, "T%u LEN %d", i + 1u, t->p[P_SLEN]), 1;
        if (!is_drum(t))
            for (j = 0; j < NSTEP; j++)
                if (t->step[j].n > 4u)
                    return snprintf(why, n, "T%u step %u n %u", i + 1u, j, t->step[j].n), 1;
#if FELUCCA_AUTO
        if (AUTO_L(i)->n > AUTO_MAX)
            return snprintf(why, n, "T%u auto n %u > %u", i + 1u, AUTO_L(i)->n, (unsigned)AUTO_MAX), 1;
#endif
    }
#if FELUCCA_UNDO_HISTORY
    if (undo_h.ready && (undo_h.all_b > undo_h.size || undo_h.done_b > undo_h.all_b || undo_h.n_done > undo_h.n_all ||
                         undo_h.head >= undo_h.size || undo_h.cur >= undo_h.size || undo_h.tail >= undo_h.size ||
                         (undo_h.n_all && !undo_h.all_b)))
        return snprintf(why, n, "undo ring: size %u all_b %u done_b %u n %u/%u head %u cur %u tail %u", undo_h.size,
                        undo_h.all_b, undo_h.done_b, undo_h.n_done, undo_h.n_all, undo_h.head, undo_h.cur, undo_h.tail), 1;
#endif
    return 0;
}

/* ---- one audio half, as audio.c fm1_alnk0_irq */
static void isr_half(void)
{
    static int32_t o[2u * HALF_FRAMES];
    uint32_t b;
    uint64_t t0 = cpu_ns(), d;
    in_isr = 1;
    isr_t0 = t0;
    for (b = 0; b < HALF_FRAMES; b += CTL) {
        isr_where = "mix_block";
        mix_block(o + 2u * b, CTL);
    }
    d = cpu_ns() - t0;
    if (d > isr_max_ns)
        isr_max_ns = d;
    halves++;
    isr_t0 = 0;
    in_isr = 0;
}
static void on_alarm(int sig)
{
    struct itimerval it = {{0, 0}, {0, 40 + (long)R(360)}};
    (void)sig;
    if (!irq_masked)
        isr_half();
    setitimer(ITIMER_REAL, &it, 0);
}

/* ---- the watchdog */
static pthread_t main_th;
static uint64_t hang_ns = 2000000000ull;
static volatile int dumping;
static void on_dump(int sig)
{
    void *bt[64];
    int n = backtrace(bt, 64);
    (void)sig;
    fprintf(stderr, "HANG: seed %u, half %llu, frame %llu, main loop at '%s', in_isr %d\n", seed0,
            (unsigned long long)halves, (unsigned long long)frames_done, where_now, (int)in_isr);
    backtrace_symbols_fd(bt, n, 2);
    _exit(3);
}
static void *watchdog(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t a = isr_t0, f = frame_t0, now = cpu_ns();
        if ((a && now > a && now - a > hang_ns) || (f && now > f && now - f > 4u * hang_ns)) {
            fprintf(stderr, "watchdog: %s for %.1f s\n", a && now - a > hang_ns ? "an audio half" : "a main-loop frame",
                    (double)(now - (a && now - a > hang_ns ? a : f)) / 1e9);
            dumping = 1;
            pthread_kill(main_th, SIGUSR1);
            sleep(5);
            _exit(3);
        }
        usleep(50000);
    }
    return 0;
}

/* ---- the main loop's actions */
static uint32_t ui_held;
static uint64_t play_frames, rec_frames, key_frames, ft_frames;
static uint32_t ft_hold;                                /* buttons held by the random use */
static void maybe_isr(void)
{
    if (!sig_mode && R(3) == 0) {
        char why[160];
        isr_half();
        if (bad_state(why, sizeof why)) {
            fprintf(stderr, "STATE: %s (seed %u, half %llu, after '%s')\n", why, seed0, (unsigned long long)halves, where_now);
            exit(4);
        }
    }
}
static void act_notes(void)
{
    uint32_t r = R(16), k = R(27);
    where_now = "notes";
    if (r < 5)
        fm1_in.notes |= 1u << k;                        /* a key down */
    else if (r < 9)
        fm1_in.notes &= ~(1u << k);                     /* a key up (or none) */
    else if (r < 11) {                                  /* a chord at once */
        uint32_t i, m = 0;
        for (i = 0; i < 2u + R(4); i++)
            m |= 1u << R(27);
        fm1_in.notes = R(2) ? m : (fm1_in.notes | m);
    } else if (r < 12)
        fm1_in.notes = 0;                               /* every key up */
    else if (r < 14) {                                  /* the same note again, an audio block apart or not */
        uint32_t m = fm1_in.notes;
        fm1_in.notes &= ~(1u << k);
        maybe_isr();
        fm1_in.notes = m | 1u << k;
    } else if (fm1_in.notes) {                          /* one held key up, another down */
        uint32_t i;
        for (i = 0; i < 27u && !((fm1_in.notes >> i) & 1u); i++)
            ;
        fm1_in.notes = (fm1_in.notes & ~(1u << i)) | 1u << k;
    }
}
static void act_buttons(void)
{
    uint32_t bt = R(NB);
    where_now = "buttons";
    if (bt == B_PLAY || bt == B_REC)
        return;
    edges_btn |= 1u << panel.btn[bt];
    if (R(3) == 0)
        ui_held ^= 1u << panel.btn[bt];
    if (R(20) == 0)
        ui_held = 0;
    if (R(2))
        encs[R(7)] += (int32_t)R(9) - 4;
}
static const uint8_t SEQ_IDS[] = {P_SLEN, P_SLEN, P_SLEN, P_SDIV, P_SDIV, P_SSWING, P_SGATE, P_VOICE, P_VOICE,
                                   P_AMODE, P_CHORD, P_ROOT, P_SCALE, P_QUANT, P_TRANS, P_LEVEL, P_REV, P_DLY, P_CHOR};
static void act_param(void)
{
    uint32_t i = R(NTRK), id = R(3) ? SEQ_IDS[R(sizeof SEQ_IDS)] : R(P_E0);
    const param_desc_t *d = &TP[id];
    int32_t v = d->min + (int32_t)R((uint32_t)(d->max - d->min + 1));
    where_now = "param";
    if (id == P_SLEN && R(4) == 0)
        v = R(2) ? 1 : NSTEP;                           /* (the extremes: one step, a full pattern) */
    trk[i].p[id] = (int16_t)v;                          /* (a knob: a 16-bit store, no IRQ off, as the UI does) */
}
static void act_global(void)
{
    static const uint8_t G_IDS[] = {G_RECQ, G_RECQ, G_BPM, G_BPM, G_SWING};
    uint32_t id = R(4) ? G_IDS[R(sizeof G_IDS)] : R(G_COUNT);
    const param_desc_t *d = &GP[id];
    where_now = "global";
    if (id == G_SYNC || id == G_CLOCK)
        return;                                         /* (no external clock here) */
    song.g[id] = (int16_t)(d->min + (int32_t)R((uint32_t)(d->max - d->min + 1)));
}
static void act_step(void)                              /* a step put / cleared, its extras (the UI: IRQ off) */
{
    track_t *t = &trk[R(NTRK)];
    uint32_t idx = R(NSTEP), r = R(10);
    where_now = "step";
    undo_mark(t, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    if (is_drum(t)) {
        if (r < 5)
            dstep_set(&t->dstep[idx], R(DRUM_LANES), R(4), R(4));
        else if (r < 7)
            memset(&t->dstep[idx], 0, sizeof t->dstep[idx]);
    } else if (r < 4) {
        step_t *s = &t->step[idx];
        memset(s, 0, sizeof *s);
        s->time = ST_NOTE;
        s->n = (uint8_t)(1u + R(4));
        for (r = 0; r < s->n; r++)
            s->note[r] = (uint8_t)(48u + R(24));
        s->vel = (uint8_t)(1u + R(127));
        s->rat = (uint8_t)(R(3) ? 0 : R(256));
    } else if (r < 6) {
        memset(&t->step[idx], 0, sizeof t->step[idx]);
        t->step[idx].time = (uint8_t)(R(2) ? ST_REST : ST_TIE);
    }
#if FELUCCA_PLOCK
    if (R(3) == 0)
        (void)lock_set(t, idx, R(2) ? P_SGATE : P_LEVEL, (int32_t)R(128));
#endif
#if FELUCCA_MICRO
    if (R(3) == 0)
        (void)step_micro_set(t, idx, (int32_t)R(MICRO_MAX - MICRO_MIN + 1) + MICRO_MIN);
#endif
#if FELUCCA_CHANCE
    if (R(4) == 0)
        (void)step_chance_set(t, idx, R(101));
#endif
#if FELUCCA_COND
    if (R(4) == 0)
        (void)step_cond_set(t, idx, R(AUTO_NCOND + 1u));
#endif
#if FELUCCA_FILLS
    if (R(5) == 0)
        (void)step_fill_set(t, idx, R(3));
#endif
    fm1_irq_on();
}
static void act_misc(void)
{
    uint32_t r = R(12);
    if (r < 3) {
        where_now = "undo";
        if (R(3) == 0)
            (void)undo_apply((int)R(2));
    } else if (r < 5) {
        where_now = "select";
        track_select(R(NTRK));
    } else if (r < 6) {
        where_now = "clear";
        if (R(8) == 0)
            op_clear_track(R(NTRK));
#if FELUCCA_PATTERNS
    } else if (r < 8) {
        uint32_t s = R(PAT_N + 1u);
        where_now = "pat_launch";
        pat_launch(R(NTRK), s == PAT_N ? PAT_NONE : s, R(3));
#endif
    } else if (r < 10) {
        where_now = "rec";
        if (R(4) == 0 || (!song.rec && !rec_wait))
            (void)op_rec();
    } else if (r < 11) {
        where_now = "play";
        if (!song.playing || R(8) == 0)
            op_play();
    } else if (R(3)) {
        where_now = "undo_close";
        undo_close();
    } else {                                            /* a sound picked (PRESETS: an engine, its preset) */
        track_t *t = &trk[R(NPART)];
        where_now = "sound";
        set_engine_of(t, R(NENGINES));
        apply_preset_to(t, R(4));
    }
}

static void stress_frame(void)
{
    uint32_t n = 1u + R(4), i;
    frame_t0 = cpu_ns();
    for (i = 0; i < n; i++) {
        uint32_t r = R(100);
        if (r < 40)
            act_notes();
        else if (r < 55)
            act_buttons();
        else if (r < 68)
            act_param();
        else if (r < 73)
            act_global();
        else if (r < 88)
            act_step();
        else
            act_misc();
        maybe_isr();
    }
#if FELUCCA_REC_MODES
    if (R(600) == 0) {                                  /* an empty project, stopped, armed: a free take or a count-in */
        where_now = "new_take";
        if (song.playing)
            transport_req = 2;
        maybe_isr();
        op_project_new();
        rec_tempo = (uint8_t)R(2);
        rec_count = (uint8_t)R(2);
        song.rec = 0;
        rec_wait = 0;
        (void)op_rec();
        ft_hold = 40u + R(400);                         /* (frames: no PLAY meanwhile, the take runs) */
    }
#endif
    if (ft_hold)
        ft_hold--;
    else if (!song.playing && R(20) == 0)
        op_play();                                      /* (mostly playing) */
    if (!song.rec && !rec_wait && R(10) == 0)
        (void)op_rec();                                 /* (mostly armed) */
    fm1_in.buttons = ui_held;
    where_now = "ui_input";
    ui_input();
    maybe_isr();
#if INS_FX
    where_now = "fxi_poll";
    fxi_poll();
#endif
#if FELUCCA_PATTERNS
    where_now = "pat_service";
    pat_service();
#endif
    maybe_isr();
    where_now = "ui_leds";
    ui_leds();
    maybe_isr();
    where_now = "ui_draw";
    ui_draw();
    maybe_isr();
    if (!sig_mode) {                                    /* ~16 ms of audio a frame: about three halves */
        while (R(4))
            maybe_isr();
    } else {                                            /* (main.c: the rest of the frame in ui_input / fxi_poll) */
        uint64_t h0 = halves;
        where_now = "idle";
        while (halves - h0 < 3u) {
            ui_input();
#if INS_FX
            fxi_poll();
#endif
        }
    }
    fm1_ms += 16;
    play_frames += song.playing != 0;
    rec_frames += song.playing && song.rec;
    key_frames += song.playing && song.rec && fm1_in.notes;
    ft_frames += ft_on != 0;
    frames_done++;
    frame_t0 = 0;
}

int main(int argc, char **argv)
{
    const char *m = getenv("HANG_MODE"), *e;
    uint64_t nf = 6000, secs = 10, f;
    (void)argc;
    (void)argv;
    sig_mode = m && !strcmp(m, "sig");
    seed0 = (e = getenv("HANG_SEED")) ? (uint32_t)strtoul(e, 0, 0) : 20261015u;
    if ((e = getenv("HANG_FRAMES")))
        nf = strtoull(e, 0, 0);
    if ((e = getenv("HANG_SECS")))
        secs = strtoull(e, 0, 0);
    if ((e = getenv("HANG_MS")))
        hang_ns = strtoull(e, 0, 0) * 1000000ull;
    rs = seed0;
    outdir = "build/host";
#if HOST_DUAL
    hd_start();
#endif
    {
        uint32_t i;
        for (i = 0; i < 5u * 16u; i++)
            FM1_KEYMAP[i / 16u][i % 16u] = -1;
        for (i = 0; i < 41u; i++)
            FM1_KEYMAP[1u + i / 16u][i % 16u] = (int8_t)i;
    }
    host_tracks_init();
#if FELUCCA_PATTERNS
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
#endif
    tracks_default();
    band_y = (uint32_t)OP_FOOT_Y;
    panel = PANEL_DEFAULT;
    settings_init();
    layers_init();
    go_home();
    frames(3);
    song.g[G_RECQ] = 0;                                 /* (REC QUANTIZE OFF to start: the own ends, nudges, locks) */
    op_play();
    frames(2);
    (void)op_rec();
    main_th = pthread_self();
    {
        struct sigaction sa;
        pthread_t w;
        sigset_t s;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_dump;
        sigaction(SIGUSR1, &sa, 0);
        sigemptyset(&s);
        sigaddset(&s, SIGALRM);
        pthread_sigmask(SIG_BLOCK, &s, 0);              /* (the watchdog thread never takes the "interrupt") */
        pthread_create(&w, 0, watchdog, 0);
        pthread_sigmask(SIG_UNBLOCK, &s, 0);
        if (sig_mode) {
            struct itimerval it = {{0, 0}, {0, 200}};
            memset(&sa, 0, sizeof sa);
            sa.sa_handler = on_alarm;
            sigaddset(&sa.sa_mask, SIGALRM);
            sigaction(SIGALRM, &sa, 0);
            setitimer(ITIMER_REAL, &it, 0);
        }
    }
    if (sig_mode) {
        uint64_t end = mono_ns() + secs * 1000000000ull;
        while (mono_ns() < end)
            stress_frame();
        {
            struct itimerval it = {{0, 0}, {0, 0}};
            setitimer(ITIMER_REAL, &it, 0);
        }
    } else {
        for (f = 0; f < nf; f++)
            stress_frame();
    }
    {
        char why[160];
        uint32_t i, notes = 0, ev = 0;
        if (bad_state(why, sizeof why)) {
            fprintf(stderr, "STATE: %s (seed %u, end)\n", why, seed0);
            return 4;
        }
        for (i = 0; i < NPART; i++) {
            uint32_t j;
            for (j = 0; j < NSTEP; j++)
                notes += trk[i].step[j].time == ST_NOTE ? trk[i].step[j].n : 0u;
        }
#if FELUCCA_AUTO
        for (i = 0; i < NTRK; i++)
            ev += AUTO_L(i)->n;
#endif
        printf("audio_hang_stress %s seed %u: %llu frames, %llu halves, longest half %.2f ms CPU, notes in the patterns %u, "
               "automation events %u, cli / sti inside the ISR %u / %u, frames playing %llu recording %llu with keys %llu, in a free take %llu: no hang\n",
               sig_mode ? "sig" : "det", seed0, (unsigned long long)frames_done, (unsigned long long)halves,
               (double)isr_max_ns / 1e6, notes, ev, cli_in_isr, sti_in_isr, (unsigned long long)play_frames,
               (unsigned long long)rec_frames, (unsigned long long)key_frames, (unsigned long long)ft_frames);
    }
    return 0;
}
