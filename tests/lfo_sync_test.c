/* SPDX-License-Identifier: GPL-3.0-only */
/* The LFO's RATE below 0: one cycle per note value of the tempo (voice.c lfo_inc, params.c N_LSYNC), SLOOP 2.6's LFO 2
 * SYNC (its P_LSYNC n = our RATE -n: -1 4BAR .. -10 1/32). Run by tests/run_tests.sh (with and without the macros).
 *   range     RATE -10..127, default 60; 0..127 the free rates of LFO_INC as before (old projects: the same sound)
 *   format    -1..-10 "4BAR" .. "1/32" (SLOOP's names), 0..127 Hz as before
 *   sloop     SLOOP 2.6's increment (its voice.c lfo_sync_inc at 120 BPM) and ours agree; the mapping both ways
 *   clock     playing: the phase follows the transport clock's units (tempo change, nudge) to a few LSB a block;
 *             stopped: the tempo shown (n x BPM a block)
 *   retrig    a fresh phrase starts it at PHS (SLOOP's TRIG NOTE, Optimist's only mode), synced or not
 *   macros    (FELUCCA_MACROS) MOTION never moves a synced RATE, and never moves a free one below 0
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* SLOOP 2.6's voice.c lfo_sync_inc (sl25-safe-src/v2.6), its divides as written there */
static uint32_t sloop_inc(uint32_t lsync, uint32_t bpm)
{
    static const uint16_t P[11] = {0, 384, 192, 96, 48, 24, 12, 8, 6, 4, 3};
    uint32_t beat = (uint32_t)FS * 60u / bpm, cyc, q, r;
    cyc = beat * P[lsync] / 24u;
    q = 0xFFFFFFFFu / cyc;
    r = 0xFFFFFFFFu % cyc;
    return q * CTL + (r * CTL + CTL) / cyc;
}
static const double BEATS[11] = {0, 16, 8, 4, 2, 1, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};   /* a cycle, in beats */

static void t_range_format(void)
{
    static const char *const W[10] = {"4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
    const param_desc_t *d = &TP[P_LRATE];
    char v[8];
    const char *u;
    int ok = 1;
    int32_t i;
    track_t *t = &trk[0];
    check(d->min == -10 && d->max == 127 && d->def == 60, "range: RATE -10..127, default 60");
    for (i = 0; i < 128; i++) {
        t->p[P_LRATE] = (int16_t)i;
        ok &= lfo_inc(t) == LFO_INC[i];
    }
    check(ok, "free: 0..127 step LFO_INC as before (no sound change for old projects)");
    for (i = 1, ok = 1; i <= 10; i++) {
        param_format(d, -i, v, &u);
        ok &= !strcmp(v, W[i - 1]) && !strcmp(u, "");
    }
    check(ok, "format: -1..-10 = 4BAR 2BAR 1BAR 1/2 1/4 1/8 8T 1/16 16T 1/32 (SLOOP 2.6's names)");
    param_format(d, 60, v, &u);
    check(!strcmp(u, "Hz"), "format: 60 in Hz as before");
    param_format(&GP[G_CRATE], 40, v, &u);
    check(!strcmp(u, "Hz"), "format: the chorus RATE (same format) in Hz");
}

static void t_sloop(void)
{
    uint32_t n, ok = 1;
    track_t *t = &trk[0];
    clk_blk = CTL * 120u;
    for (n = 1; n <= 10u; n++) {                       /* (SLOOP truncates its cycle to whole samples: 1/16 at 120
                                                        * BPM is 5512 for 5512.5, 1e-4 fast; ours is exact) */
        int64_t a, b;
        double x = 4294967296.0 * CTL * 120.0 / ((double)BEAT_U * BEATS[n]);
        t->p[P_LRATE] = (int16_t)-(int32_t)n;
        a = lfo_inc(t), b = sloop_inc(n, 120u);
        ok &= fabs((double)a - x) <= 2.0 && fabs((double)(a - b)) <= (double)b / 5000.0;
        if (fabs((double)a - x) > 2.0 || fabs((double)(a - b)) > (double)b / 5000.0)
            printf("  LSYNC %u: ours %lld exact %.1f SLOOP %lld\n", n, (long long)a, x, (long long)b);
    }
    check(ok, "sloop: each SYNC value's step exact (+-2 of 2^32 a block) and as SLOOP 2.6's at 120 BPM (2e-4)");
    for (n = 0, ok = 1; n <= 10u; n++)
        ok &= LRATE_OF_SLOOP(n, 77) == (n ? -(int32_t)n : 77) && SLOOP_LSYNC_OF(LRATE_OF_SLOOP(n, 77)) == (int32_t)n;
    ok &= SLOOP_LSYNC_OF(0) == 0 && SLOOP_LSYNC_OF(127) == 0 && LRATE_OF_SLOOP(11, 50) == 50;
    check(ok, "sloop: 2.6's SYNC n (RATE r) -> our RATE -n (OFF: r), and back; out of range: OFF");
}

static int32_t o_[2 * CTL];
static double units_now(void) { return (double)clk_beat * BEAT_U + clk_pos; }
/* n blocks; returns how far the phase of track 0 moved against what the clock units say (in LSB of 2^32, signed) */
static double run_blocks(uint32_t n, uint32_t div, int playing_ref)
{
    track_t *t = &trk[0];
    double u0 = units_now(), want, got, d;
    uint32_t ph0 = t->lfo_ph, i, bpm = (uint32_t)song.g[G_BPM];
    for (i = 0; i < n; i++)
        mix_block(o_, CTL);
    want = playing_ref ? units_now() - u0 : (double)n * CTL * bpm;
    want = fmod(want / ((double)BEAT_U * BEATS[div]), 1.0) * 4294967296.0;
    got = (double)(uint32_t)(t->lfo_ph - ph0);
    d = got - want;
    if (d > 2147483648.0)
        d -= 4294967296.0;
    if (d < -2147483648.0)
        d += 4294967296.0;
    return d;
}

static void t_clock(void)
{
    track_t *t = &trk[0];
    uint32_t div, ok = 1;
    double d;
    host_tracks_init();
    song.playing = 0;
    for (div = 1; div <= 10u; div++) {
        t->p[P_LRATE] = (int16_t)-(int32_t)div;
        d = run_blocks(3000, div, 0);
        ok &= fabs(d) <= 3000.0;
        if (fabs(d) > 3000.0)
            printf("  stopped div %u: %.0f LSB off\n", div, d);
    }
    check(ok, "stopped: one cycle per note value at the tempo shown (120 BPM), 3000 blocks, <= 1 LSB a block");
    transport_req = 1;
    mix_block(o_, CTL);
    check(song.playing, "PLAY");
    for (div = 1, ok = 1; div <= 10u; div++) {
        t->p[P_LRATE] = (int16_t)-(int32_t)div;
        d = run_blocks(2000, div, 1);
        ok &= fabs(d) <= 2000.0;
        if (fabs(d) > 2000.0)
            printf("  playing div %u: %.0f LSB off\n", div, d);
    }
    check(ok, "playing at 120 BPM: the phase moves as the transport clock, every note value");
    t->p[P_LRATE] = -8;                                 /* 1/16 */
    song.g[G_BPM] = 97;
    d = run_blocks(2000, 8, 1);
    check(fabs(d) <= 2000.0, "playing, the tempo changed to 97: still with the clock");
    clk_nudge = 10;
    d = run_blocks(500, 8, 1);
    clk_nudge = -10;
    ok = fabs(d) <= 500.0 && fabs(run_blocks(500, 8, 1)) <= 500.0;
    clk_nudge = 0;
    check(ok, "playing, nudged faster then slower (TEMPO page): still with the clock");
    t->p[P_LRATE] = 60;
    {
        uint32_t ph0 = t->lfo_ph, i;
        for (i = 0; i < 100u; i++)
            mix_block(o_, CTL);
        check(t->lfo_ph - ph0 == 100u * LFO_INC[60], "playing, a free RATE (60): LFO_INC a block, as before");
    }
    transport_req = 2;
    mix_block(o_, CTL);
    song.g[G_BPM] = 120;
}

static void t_retrig(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    host_tracks_init();
    host_preset(t, 0, 0);
    t->p[P_LRATE] = -5;                                 /* 1/4 */
    t->p[P_LPHASE] = 32;
    for (i = 0; i < 50u; i++)
        mix_block(o_, CTL);
    trk_note_on(t, 60, 100);
    check(t->lfo_ph == 32u << 25, "retrig: a fresh phrase, synced: the phase at PHS (SLOOP's TRIG NOTE)");
    mix_block(o_, CTL);
    trk_note_on(t, 64, 100);
    check(t->lfo_ph != 32u << 25, "retrig: a second note over the first: no restart (legato phrase)");
    trk_all_off(t);
    for (i = 0; i < 400u; i++)
        mix_block(o_, CTL);
}

static void t_macros(void)
{
#if FELUCCA_MACROS
    track_t *t = &trk[0];
    host_tracks_init();
    TDRUM->p[MAC_ID[1]] = -64;                          /* MOTION all the way down: RATE -20 */
    check(mac_effective(0, P_LRATE, 5) == 0, "macros: MOTION down takes a free RATE 5 to 0, not into the synced values");
    check(mac_effective(0, P_LRATE, -3) == -3, "macros: MOTION down leaves a synced RATE (1BAR) as it is");
    TDRUM->p[MAC_ID[1]] = 63;
    check(mac_effective(0, P_LRATE, -3) == -3, "macros: MOTION up leaves a synced RATE as it is");
    check(mac_effective(0, P_LRATE, 30) == 50, "macros: MOTION up moves a free RATE as before (+20)");
    t->p[P_LRATE] = -3;
    mac_pre();
    check(t->p[P_LRATE] == -3, "macros: the audio ISR's mac_pre leaves a synced RATE as it is");
    mac_post();
    t->p[P_LRATE] = 4;
    TDRUM->p[MAC_ID[1]] = -64;
    mac_pre();
    check(t->p[P_LRATE] == 0, "macros: mac_pre takes a free RATE 4 down to 0 at most");
    mac_post();
    check(t->p[P_LRATE] == 4, "macros: mac_post puts it back");
    TDRUM->p[MAC_ID[1]] = 0;
#endif
}

int main(void)
{
    host_tracks_init();
    t_range_format();
    t_sloop();
    t_clock();
    t_retrig();
    t_macros();
    printf("%d failed\n", fails);
    return fails;
}
