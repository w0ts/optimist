/* SPDX-License-Identifier: GPL-3.0-only */
/* A render of the step extras and motion (docs/UI-OPTIMIST-DESIGN.md 6, phase 3): the four tracks for eight bars at
 * 120 BPM with nudges on the drum and synth steps (micro timing), fill conditions (a fill held in bar 3), locks of
 * PAN and ENV DEST FLT, motion on CHORUS (hold events), a synth step's chance bits; the mix hashed (FNV-1a over every
 * sample). Built with the SLOOP 2.4 sequencer switches and MOTION on. It writes the extras through whichever store
 * the sources have: the automation store (seq/auto.h, AUTO_MAX defined) or the stores before it (stepx_t through
 * TX(), motion_set_event), so the same program built against 2907501's sources gives the render the automation
 * store has to equal: the hash it printed there is AUTO_RENDER_HASH. Exit status: 1 when the hash differs (argv[1]
 * "print": print it, exit 0). Run by tests/run_tests.sh.
 * argv[1] "cpu": the sequencer's cost of a step's events (the instructions the kernel counts, as tests/regress.c): a
 * step with 24 locks and 30 hold events on track 1 entered 20000 times (with the step after it, which lets the locks
 * go), the same calls with no event; built against 2907501 it calls motion_step and lock_step, here auto_step. Here
 * also a full list (128 events, all on the one step). Budget: AUTO_CPU_STEP instructions a step at most. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
#define AUTO_CPU_STEP 6000u                    /* (a 1/32 step at 300 BPM on four tracks: 160 steps a second, ~1 M
                                                * instructions a second at most: 0.4 % of 240 MHz) */

#define AUTO_RENDER_HASH 0x60AF78F6u           /* (2907501's render of this program, measured 2026-10-08) */

static void reset(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].seq_abs = SEQ_NONE;
        trk[i].seq_idx = 0;
        trk[i].p[P_SLEN] = 16;
    }
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    clk_beat = clk_pos = 0;
    song.playing = 0;
}
static void nudge(track_t *t, uint32_t i, int32_t m)
{
#ifdef AUTO_MAX
    step_micro_set(t, i, m);
#else
    TX(t)->micro[i] = (int8_t)m;
#endif
}
static void fill(track_t *t, uint32_t i, uint32_t c) { step_fill_set(t, i, c); }
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static void enter(track_t *t, uint32_t idx)            /* the sequencer enters step idx: its events */
{
#ifdef AUTO_MAX
    (void)auto_step(t, idx);
#else
    motion_step(t, idx);
    lock_step(t, idx);
#endif
}
/* the instructions a step costs: steps 0 and 1 of track t entered n times each, per step */
static uint32_t step_cost(track_t *t, uint32_t n)
{
    uint64_t a = instr_now(), b;
    uint32_t i;
    for (i = 0; i < n; i++)
        enter(t, 0), enter(t, 1);
    b = instr_now();
    return (uint32_t)((b - a) / (2u * n));
}
static int cpu(void)
{
    static const uint8_t LK[24] = {P_LEVEL, P_ATK, P_DEC, P_SUS, P_REL, P_ED_FLT, P_ED_PIT, P_ED_SHP, P_LRATE, P_LPHASE,
                                   P_LFADE, P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP, P_SGATE, P_DIST, P_CHOR, P_DLY, P_REV,
                                   P_GLIDE, P_PAN, P_DETUNE, P_E0};
    track_t *t = &trk[0];
    uint32_t i, none, full, ok = 1;
    reset();
    (void)step_cost(t, 1000);                          /* (warm) */
    none = step_cost(t, 20000);
    for (i = 0; i < 24u; i++)
        (void)lock_set(t, 0, LK[i], 10 + (int32_t)i);
    for (i = 0; i < 30u; i++)
        (void)motion_set_event(t, 0, LK[i % 24u] == P_SGATE ? P_E1 : LK[(i + 3u) % 24u], 20 + (int32_t)i);
    motion_begin();
    full = step_cost(t, 20000);
    printf("auto cpu: a step, no event: %u instructions; 24 locks + 30 hold events on it: %u\n", none, full);
    ok = !instr_now() || full <= AUTO_CPU_STEP;
#ifdef AUTO_MAX
    {
        uint32_t f2;
        for (i = 0; i < 120u && AL(t)->n < AUTO_MAX; i++)
            (void)auto_put(AL(t), 0u | AUTO_ONLY, 200u - i, 1);   /* (ids this build does not play: scanned, skipped) */
        f2 = step_cost(t, 20000);
        printf("auto cpu: a full list (%u events) on one step: %u instructions a step\n", AL(t)->n, f2);
        ok &= !instr_now() || f2 <= AUTO_CPU_STEP;
    }
#endif
    motion_end();
    printf("auto cpu: budget %u instructions a step: %s\n", AUTO_CPU_STEP, ok ? "ok" : "OVER");
    return !ok;
}

int main(int argc, char **argv)
{
    static const int8_t M[16] = {0, 3, -5, 0, 16, 0, -9, 0, -16, 7, 31, -32, 0, 12, -32, 31};
    uint32_t i, h = 0x811C9DC5u, blk;
    int32_t out[CTL * 2];
    if (argc > 1 && !strcmp(argv[1], "cpu"))
        return cpu();
    reset();
    for (i = 0; i < 16u; i++) {                       /* the drum track: a hit a step, its own lane, nudged */
        dstep_set(&TDRUM->dstep[i], i % 8u, LV_NORM, (uint32_t)(i % 3u == 2u));
        nudge(TDRUM, i, M[i]);
    }
    fill(TDRUM, 4, FC_FILL);
    fill(TDRUM, 12, FC_NOFILL);
    for (i = 0; i < 16u; i += 2u) {                   /* track 1: notes on the even steps, nudged, two locked */
        trk[0].step[i].note[0] = (uint8_t)(48u + i);
        trk[0].step[i].n = 1;
        trk[0].step[i].time = ST_NOTE;
        trk[0].step[i].vel = 100;
        nudge(&trk[0], i, M[(i + 5u) % 16u]);
    }
    step_set_chance(&trk[0].step[6], 50);             /* (the chance bits: the same random draws) */
    fill(&trk[0], 8, FC_FILL);
    (void)lock_set(&trk[0], 2, P_PAN, -40);
    (void)lock_set(&trk[0], 4, P_LD_FLT, 50);
    (void)lock_set(&trk[0], 10, P_PAN, 30);
    for (i = 0; i < 16u; i += 4u) {                   /* track 2: a chord every 4 steps, motion on CHORUS */
        trk[1].step[i].note[0] = 60, trk[1].step[i].note[1] = 64, trk[1].step[i].n = 2;
        trk[1].step[i].time = ST_NOTE;
        trk[1].step[i].vel = 90;
    }
    (void)motion_set_event(&trk[1], 1, P_CHOR, 90);
    (void)motion_set_event(&trk[1], 9, P_CHOR, 10);
    (void)motion_set_event(&trk[0], 3, P_DLY, 60);    /* (motion and a lock on one track) */
    transport_req = 1;
    for (blk = 0; clk_beat < 32u; blk++) {
        fill_held = (uint8_t)(clk_beat >= 8u && clk_beat < 12u);
        mix_block(out, CTL);
        for (i = 0; i < CTL * 2u; i++)
            h = (h ^ (uint32_t)out[i]) * 16777619u;
    }
    if (argc > 1 && !strcmp(argv[1], "print")) {
        printf("0x%08Xu\n", h);
        return 0;
    }
    printf("auto render: 8 bars, nudges, fills, locks, motion: 0x%08X (want 0x%08X) %s\n", h, AUTO_RENDER_HASH,
           h == AUTO_RENDER_HASH ? "ok" : "FAIL");
    return h != AUTO_RENDER_HASH;
}
