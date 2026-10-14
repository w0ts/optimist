/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of GATER (firmware/src/fx/insfx/gater.c), the firmware's own sources through hostsim.c, built with it
 * (run_tests.sh: -DFELUCCA_INS_GATER=1). A part's S1 (fx.c ins_part) and the drum bus (insfx.c ifx_bus), the transport
 * clock moved as seq.c events_block moves it (clk_blk, then clk_pos: the render sees the block's end):
 *   1. bypass: at 0 from rest, in no slot, FX bypassed: bit for bit; on, an open step: the sample as it is
 *   2. silence in, silence out (a part, the drum bus)
 *   3. in phase: every pattern (written here as 'x' / '.' strings, read apart from gater.c's bits) at 1/16 and 120 BPM
 *      from PLAY: open in an 'x' step, closed in a '.' and in the gap before an 'x' that follows an 'x'; every RATE;
 *      a second tempo; the swing on the track's own grid; PLAY again restarts at step 0
 *   4. no click: on a constant, a sample's step at most 1/48 of full scale (+1), through DEPTH 0 -> 127, a pattern and
 *      a rate change mid-play, a jump of the transport; SHAPE 127 ramps over about half a step
 *   5. pattern and rate changed mid-play: the new one heard from the next step on
 *   6. the drum bus: one gain on L and R; stopped: it runs on at the tempo
 *   7. its TLV (fx_slots.c fxs_tlv): PATTERN RATE SHAPE round trip, only when off their defaults; a TLV of 4: defaults
 *   8. cost: host instructions a sample at 127 on a part (printed) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"           /* (the FX record: fx_rec.c) */
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define N (1u << 18)                                    /* 5.9 s: three cycles of 16 sixteenths at 120 BPM */
#define DC 400000                                       /* a constant in (the mix's scale: +-884000) */
static int fails;
static void check(const char *what, int ok)
{
    printf("gater: %-104s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int32_t x[N], y[N], z[N];
static const char *const PAT[16] = {
    "xxxxxxxxxxxxxxxx", "x.x.x.x.x.x.x.x.", "x...x...x...x...", "x.......x.......", "..x...x...x...x.", ".x.x.x.x.x.x.x.x",
    "x..x..x..x..x..x", "x.....x.....x...", "x.xx.xx.x.xx.xx.", "xx.xx.xx.xx.xx.x", "xxx.xxx.xxx.xx.x", "x.x.xx.x.x.xx.xx",
    "x.xxx.xxx.xxx.xx", "xx.xxx.xxx.xxx.x", "x..x..x...x.x...", "x..x..x.x..x..x."};
static const double DEN[6] = {2, 4, 8, 3, 6, 12};      /* RATE: steps a beat (1/8 1/16 1/32 8T 16T 32T) */

static void reset(void)
{
    static const uint8_t E[2] = {FXT_NONE, FXT_NONE};
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        ins_put(k, E);
        trk[k].p[P_FXOFF] = 0;
        trk[k].p[P_TGATE] = 0;
        fxs_gx_def(k);
        trk[k].p[P_SSWING] = 0;
        trk[k].p[P_SDIV] = 2;                           /* (1/16, the default) */
    }
    song.g[G_SWING] = 0;
    song.g[G_BPM] = 120;
    memset(ifx_gate, 0, sizeof ifx_gate);
    memset(ifx_awake, 0, sizeof ifx_awake);
    song.playing = 0;
    clk_beat = 0, clk_pos = 0, clk_blk = 0;
}
static void play(void)                                  /* PLAY (seq.c seq_start): step 0 now */
{
    song.playing = 1;
    clk_beat = 0, clk_pos = 0;
}
static void clock_block(void)                           /* as events_block: the block's units, then the clock moved */
{
    clk_blk = CTL * (uint32_t)song.g[G_BPM];
    if (!song.playing)
        return;
    clk_pos += clk_blk;
    while (clk_pos >= BEAT_U)
        clk_pos -= BEAT_U, clk_beat++;
}
static void fill(int32_t v)
{
    uint32_t i;
    for (i = 0; i < N; i++)
        x[i] = v;
}
/* GATER in part 1's S1 over x -> y, from sample `from` to `to`; set: called at each block start (mid-play changes) */
static void run_part(uint32_t from, uint32_t to, void (*set)(uint32_t))
{
    uint8_t s[2] = {FXT_GATE, FXT_NONE};
    uint32_t i;
    ins_put(0, s);
    if (!from)
        memcpy(y, x, sizeof y);
    for (i = from; i + CTL <= to; i += CTL) {
        if (set)
            set(i);
        clock_block();
        ins_part(&trk[0], y + i, CTL);
    }
}
static void run_bus(uint32_t to)
{
    uint8_t s[2] = {FXT_GATE, FXT_NONE};
    uint32_t i;
    ins_put(TRK_DRUM, s);
    memcpy(y, x, sizeof y), memcpy(z, x, sizeof z);
    for (i = 0; i + CTL <= to; i += CTL) {
        clock_block();
        ifx_bus(FXT_GATE, TDRUM->p[P_TGATE], y + i, z + i, CTL);
    }
}
static int same(const int32_t *a, const int32_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int32_t max_step(const int32_t *v, uint32_t from, uint32_t to)
{
    int32_t m = 0;
    uint32_t i;
    for (i = from + 1u; i < to; i++)
        m = abs(v[i] - v[i - 1u]) > m ? abs(v[i] - v[i - 1u]) : m;
    return m;
}
#define STEP_MAX (DC / 48 + 2)                          /* (IGT_SMAX: 1/48 of the gain a sample, rounding) */

/* the gate expected at fraction f (0..1) of step k of pattern p: 1 open, 0 closed, -1 near an edge (not checked) */
static int expect(const char *p, uint32_t k, double f)
{
    int on = p[k % 16u] == 'x', next = p[(k + 1u) % 16u] == 'x';
    double end = next ? 0.5 : 1.0;
    if (!on)
        return f > 0.1 && f < 0.95 ? 0 : -1;           /* (the ramps around its edges: a few ms) */
    return f > 0.05 && f < end - 0.05 ? 1 : f > end + 0.05 && f < 0.95 ? 0 : -1;
}
/* y against pattern p at RATE r, the tempo, from PLAY at sample 0 (swing: the odd steps sw of a step late). Returns
 * the mismatches; *n: the samples checked */
static uint32_t verify(const char *p, uint32_t r, double sw, uint32_t to, uint32_t *n)
{
    double step = FS * 60.0 / song.g[G_BPM] / DEN[r];
    uint32_t i, bad = 0;
    *n = 0;
    for (i = 0; i < to; i += 7u) {
        double pos = i / step, pp = fmod(pos, 2.0), f;
        uint32_t k = (uint32_t)(pos / 2.0) * 2u;
        int e;
        if (pp < 1.0 + sw)
            f = pp / (1.0 + sw);
        else
            k++, f = (pp - 1.0 - sw) / (1.0 - sw);
        e = expect(p, k, f);
        if (e < 0)
            continue;
        (*n)++;
        bad += e ? y[i] != x[i] : y[i] != 0;
    }
    return bad;
}

static void t_bypass(void)
{
    uint32_t i;
    reset();
    for (i = 0; i < N; i++)
        x[i] = (int32_t)lrint(300000 * sin(2 * M_PI * 220.0 * i / FS));
    play();
    run_part(0, N, 0);
    check("in S1 at 0 from rest, playing: the block bit for bit", same(x, y, N));
    reset();
    trk[0].p[P_TGATE] = 127;
    trk[0].p[P_FXOFF] = 1;
    play();
    run_part(0, N, 0);
    check("at 127 with the track's FX bypassed: bit for bit", same(x, y, N));
    reset();
    trk[0].p[P_TGATE] = 127;                            /* (in no slot: its amount unheard) */
    play();
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL) {
        clock_block();
        ins_part(&trk[0], y + i, CTL);
    }
    check("at 127 in no slot: bit for bit, never called (ifx_awake clear)", same(x, y, N) && !(ifx_awake[0] & FXT_BIT(FXT_GATE)));
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = 127;
    trk[0].p[P_TGPAT] = 0;                              /* (16TH: open in the first half of every step) */
    play();
    run_part(0, N, 0);
    {
        uint32_t n, bad = verify(PAT[0], 1, 0, N, &n);
        check("on, in an open step: the sample as it is (bit for bit)", !bad && n > 1000u);
    }
}

static void t_silence(void)
{
    reset();
    fill(0);
    trk[0].p[P_TGATE] = 127, TDRUM->p[P_TGATE] = 127;
    play();
    run_part(0, N, 0);
    check("silence in at 127 (a part): silence out", same(x, y, N));
    reset();
    fill(0);
    TDRUM->p[P_TGATE] = 127;
    play();
    run_bus(N);
    check("silence in at 127 (the drum bus): silence out on L and R", same(x, y, N) && same(x, z, N));
}

static void t_phase(void)
{
    char w[160];
    uint32_t p, r, n, bad, tot = 0, all = 0;
    for (p = 0; p < 16u; p++) {
        reset();
        fill(DC);
        trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = (int16_t)p;
        play();
        run_part(0, N, 0);
        bad = verify(PAT[p], 1, 0, N, &n);
        tot += n, all += bad;
        if (bad) {
            snprintf(w, sizeof w, "pattern %u (%s) at 1/16, 120 BPM: %u of %u samples off", p, PAT[p], bad, n);
            check(w, 0);
        }
    }
    snprintf(w, sizeof w, "every pattern at 1/16, 120 BPM from PLAY: open / closed as written (%u samples)", tot);
    check(w, !all && tot > 100000u);
    for (r = 0, all = 0, tot = 0; r < 6u; r++) {
        reset();
        fill(DC);
        trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = 8, trk[0].p[P_TGRATE] = (int16_t)r;
        song.g[G_BPM] = r == 4u ? 97 : 120;            /* (16T at another tempo) */
        play();
        run_part(0, N, 0);
        all += verify(PAT[8], r, 0, N, &n), tot += n;
    }
    snprintf(w, sizeof w, "TRNC1 at every RATE (1/8 1/16 1/32 8T 16T 32T; 16T at 97 BPM): in phase (%u samples)", tot);
    check(w, !all && tot > 100000u);
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = 1, trk[0].p[P_SSWING] = 50;   /* (8TH: swing 50 -> 62.5 %) */
    play();
    run_part(0, N, 0);
    bad = verify(PAT[1], 1, 0.25, N, &n);
    snprintf(w, sizeof w, "swing 50 on the track's 1/16 grid: the odd steps a quarter step late (%u of %u off)", bad, n);
    check(w, !bad && n > 10000u);
    trk[0].p[P_SDIV] = 1;                               /* (the track on 1/8: GATER's 1/16 straight) */
    memset(ifx_gate, 0, sizeof ifx_gate);
    play();
    run_part(0, N, 0);
    bad = verify(PAT[1], 1, 0, N, &n);
    check("the track's grid 1/8, GATER at 1/16: straight (swing only on the track's own grid)", !bad);
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = 8;
    play();
    run_part(0, 100000u, 0);
    play();                                             /* (PLAY again mid-pattern, at sample 100000) */
    {
        uint32_t i, i0 = 100000u - 100000u % CTL;
        run_part(i0, N, 0);
        for (i = 0; i0 + i < N; i++)                    /* (verify from the restart: shift) */
            z[i] = y[i0 + i];
        memcpy(y, z, (N - i0) * sizeof y[0]);
        bad = verify(PAT[8], 1, 0, N - i0 - CTL, &n);
        check("PLAY again mid-pattern: step 0 from there (TRNC1 in phase after the restart)", bad <= 2u && n > 10000u);
    }
}

static void t_ramps(void)
{
    char w[160];
    int32_t m;
    reset();
    fill(DC);
    trk[0].p[P_TGPAT] = 8;
    play();
    run_part(0, N / 4u, 0);                             /* (at 0: nothing) */
    trk[0].p[P_TGATE] = 127;
    run_part(N / 4u, N / 2u, 0);                        /* (DEPTH 0 -> 127 mid-play) */
    trk[0].p[P_TGPAT] = 5, trk[0].p[P_TGRATE] = 4;      /* (a pattern and a rate change) */
    run_part(N / 2u, 3u * N / 4u, 0);
    clk_beat += 7, clk_pos = BEAT_U / 3u;               /* (the transport jumps: a song position) */
    run_part(3u * N / 4u, N, 0);
    m = max_step(y, 0, N);
    snprintf(w, sizeof w, "no click: a sample's step at most %d (1/48 of the gain) through depth, pattern, rate, jump: %d",
             STEP_MAX, m);
    check(w, m <= STEP_MAX);
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = 1, trk[0].p[P_TGSHP] = 127;   /* (8TH, smooth) */
    play();
    run_part(0, N, 0);
    {
        uint32_t i, st = 0, ramp = 0;                   /* (the longest run of samples strictly between open and closed) */
        for (i = 1; i < N; i++) {
            st = y[i] != 0 && y[i] != DC ? st + 1u : 0u;
            ramp = st > ramp ? st : ramp;
        }
        snprintf(w, sizeof w, "SHAPE 127: a ramp of about half a 1/16 step (%u samples, the step 5512)", ramp);
        check(w, ramp > 2000u && ramp < 3500u && max_step(y, 0, N) <= STEP_MAX);
    }
}

static uint32_t set_at;
static void set_mid(uint32_t i)                         /* TRNC1 1/16 -> 8TH 1/32 at sample set_at */
{
    if (i == set_at)
        trk[0].p[P_TGPAT] = 1, trk[0].p[P_TGRATE] = 2;
}
static void t_change(void)
{
    char w[160];
    uint32_t i, n, bad, i0;
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = 8;
    set_at = 70016u;                                    /* (a block start, mid-step) */
    play();
    run_part(0, N, set_mid);
    bad = verify(PAT[8], 1, 0, set_at, &n);
    i0 = 75000u;                                        /* (from the next 1/32 step on: 8TH at 1/32, the same clock) */
    for (i = 0; i < N; i++)
        z[i] = i < i0 ? x[i] : y[i];
    memcpy(y, z, sizeof y);
    {
        uint32_t n2, b2 = verify(PAT[1], 2, 0, N, &n2);   /* (y before i0 is x: every 'x' check passes, a '.' fails) */
        uint32_t i2, bb = 0, nn = 0;
        double step = FS * 60.0 / 120.0 / 8.0;
        for (i2 = i0; i2 < N; i2 += 7u) {               /* (only after i0) */
            double pos = i2 / step, f = pos - floor(pos);
            int e = expect(PAT[1], (uint32_t)pos, f);
            if (e < 0)
                continue;
            nn++;
            bb += e ? y[i2] != x[i2] : y[i2] != 0;
        }
        (void)b2, (void)n2;
        snprintf(w, sizeof w, "a pattern and rate change mid-play: TRNC1 1/16 before (%u off), 8TH 1/32 after (%u of %u off)",
                 bad, bb, nn);
        check(w, !bad && !bb && nn > 10000u);
    }
}

static void t_bus_stop(void)
{
    uint32_t i, open = 0, shut = 0;
    reset();
    fill(DC);
    TDRUM->p[P_TGATE] = 127, TDRUM->p[P_TGPAT] = 1;
    play();
    run_bus(N);
    check("the drum bus: one gain on L and R (identical), and gated", same(y, z, N) && !same(x, y, N));
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = 127, trk[0].p[P_TGPAT] = 1;   /* (stopped: the phase runs on at the tempo shown) */
    run_part(0, N, 0);
    for (i = 0; i < N; i++)
        open += y[i] == DC, shut += y[i] == 0;
    check("stopped: it runs on at the tempo (8TH: about half open, half closed)", open > N / 3u && shut > N / 3u);
}

static void t_store(void)
{
    uint8_t o[NTRK + FXT_TLV_GX];
    uint32_t n, k, ok = 1;
    reset();
    check("its TLV: none while every DEPTH is 0 and PATTERN RATE SHAPE are the defaults", fxs_tlv(FXT_GATE, o) == 0u);
    trk[1].p[P_TGPAT] = 3, trk[2].p[P_TGRATE] = 5, TDRUM->p[P_TGSHP] = 99, trk[0].p[P_TGATE] = 64;
    n = fxs_tlv(FXT_GATE, o);
    for (k = 0; k < NTRK; k++)
        fxs_gx_def(k), trk[k].p[P_TGATE] = 0;
    fxs_untlv(FXT_GATE, o, n, 1);
    ok = n == NTRK + FXT_TLV_GX && trk[1].p[P_TGPAT] == 3 && trk[2].p[P_TGRATE] == 5 && TDRUM->p[P_TGSHP] == 99 &&
         trk[0].p[P_TGATE] == 64 && trk[0].p[P_TGPAT] == TP[P_TGPAT].def;
    check("its TLV: the amounts, then PATTERN RATE SHAPE a track each, round trip", ok);
    fxs_untlv(FXT_GATE, o, NTRK, 1);                   /* (a reader of before's length: the defaults) */
    check("a TLV of 4: PATTERN RATE SHAPE the defaults", trk[1].p[P_TGPAT] == TP[P_TGPAT].def && TDRUM->p[P_TGSHP] == TP[P_TGSHP].def);
    trk[1].p[P_TGPAT] = 3;
    fxs_untlv_none(1);
    check("a record without it: DEPTH 0, PATTERN RATE SHAPE the defaults", trk[1].p[P_TGPAT] == TP[P_TGPAT].def && !trk[0].p[P_TGATE]);
    {   /* the FX record (fx_rec.c): every track's slots and GATER's values back, the longest record within FXR_MAX */
        uint8_t rec[FXR_MAX], s[2] = {FXT_GATE, FXT_NONE};
        reset();
        for (k = 0; k < NTRK; k++)
            ins_put(k, s), trk[k].p[P_TGATE] = (int16_t)(20 + k), trk[k].p[P_TGPAT] = (int16_t)(15 - k),
                trk[k].p[P_TGRATE] = (int16_t)(5 - k), trk[k].p[P_TGSHP] = (int16_t)(127 - k);
        n = fxr_encode(rec);
        reset();
        ok = n > 0u && n <= FXR_MAX && fxr_decode(rec, n, 1) >= 0;
        for (k = 0; k < NTRK; k++)
            ok &= ins_slot[k][0] == FXT_GATE && trk[k].p[P_TGATE] == (int16_t)(20 + k) && trk[k].p[P_TGPAT] == (int16_t)(15 - k) &&
                  trk[k].p[P_TGRATE] == (int16_t)(5 - k) && trk[k].p[P_TGSHP] == (int16_t)(127 - k);
        check("the FX record: GATER in every track's S1, its DEPTH PATTERN RATE SHAPE back after a load", ok);
        reset();
    }
#if FELUCCA_AUTO
    check("motion: DEPTH stored as P_ENG_END + 12, both ways; PATTERN not recordable",
          mot_sid(P_TGATE) == P_ENG_END + 12u && mot_id(P_ENG_END + 12u) == P_TGATE && motion_param(&trk[0], P_TGATE) &&
              !motion_param(&trk[0], P_TGPAT));
#endif
}

#ifdef __APPLE__
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
#else
static uint64_t instr_now(void) { return 0; }
#endif
static double cost_at(int32_t a)                       /* host instructions a sample of a part's run at depth a */
{
    uint64_t i0;
    reset();
    fill(DC);
    trk[0].p[P_TGATE] = (int16_t)a;
    play();
    run_part(0, N, 0);
    i0 = instr_now();
    run_part(0, N, 0);
    return (double)(instr_now() - i0) / N;
}
static void t_cost(void)
{
    double on = cost_at(127), off = cost_at(0);
    printf("gater: cost on a part at 127 (TRNC1 1/16): %.1f host instructions a sample (%.1f with the copy and the "
           "clock, %.1f of it at 0)\n", on - off, on, off);
}

int main(void)
{
    host_tracks_init();
    t_bypass();
    t_silence();
    t_phase();
    t_ramps();
    t_change();
    t_bus_stop();
    t_store();
    t_cost();
    printf("gater: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
