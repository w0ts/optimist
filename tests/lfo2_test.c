/* SPDX-License-Identifier: GPL-3.0-only */
/* LFO 2 (core.h P_L2RATE .. P_L2AB, voice.c lfo2_tick, project.c l2_pack). Run by tests/run_tests.sh:
 *   params    ranges, defaults, names; RATE turns as LFO's (a fast turn down stops at 0, below 0 a note value a detent)
 *   off       every slot OFF or at 0: nothing computed (the phase waits, the block's offsets none); a part renders as
 *             with LFO 2 at its defaults
 *   rate      free: LFO_INC[r] a block; synced: LFO's step at the same RATE (lfo_inc_of), every note value
 *   maths     PIT FLT SHP on LFO DEST's scale ((v AMT 3) >> 8, (v AMT) >> 8), AMP 32767 - u AMT 258, < 0 upside down,
 *             never below 0 nor above unity; two slots: their sum; POL UNI one way only (0..+1); the waves (SQR SAW
 *             exact); S&H once a cycle, no rng()
 *   SLEW      (issue 129) the shape: from one S&H value to the next on a smoothstep, at phase 0 exactly S&H's value; LFO
 *             and LFO 2 at their fastest free and synced rates: every block within the glide's steepest slope of the
 *             last (continuous across the wraps), each cycle starting at its S&H value, no extra rng() draw, LFO's S&H
 *             values the same draws as with S&H; SLEW kept by a project (LFO and LFO 2)
 *   trig      NOTE: a fresh phrase restarts it at 0, a note over a held one does not; FREE: never
 *   render    a part with LFO 2 on AMP / PIT sounds different from off; the render with it off is the render of before
 *   project   capture -> apply round trip (every value, both TRIGs and POLs, synced and free RATE, AMT -128 .. 127, two
 *             slots, AMT B in LFO DEST's words); the drum track's LFO slots hold it only when one is not at its
 *             default (a project without it: byte for byte as before); the macros' slot (P_LEG_FLT) untouched; a
 *             project from before (no tag) loads with LFO 2 at its defaults and the drum track's own slots; a song
 *             section (apply, not all) brings its LFO 2 too; LFO 2 with one DEST (L2_TAG_V1) loads, its AMT x 2
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/storage/project.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static dlrec_t tdl;
/* hostsim's host_tracks_init, the values after P_E7 too (track_defaults' tail); the same random state each time */
static void init(void)
{
    uint32_t k, i;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        for (i = P_ENG_END; i < P_COUNT; i++)
            trk[k].p[i] = TP[i].def;
    rng_state = 0x1234567u;
}
static project_t q, q2;
static int32_t o_[2 * CTL];

static void t_params(void)
{
    static const char *const D[5] = {"OFF", "PIT", "FLT", "SHP", "AMP"};
    const param_desc_t *r = &TP[P_L2RATE];
    char v[8];
    const char *u;
    uint32_t i, k;
    int ok;
    ok = r->min == -LSYNC_N && r->max == 127 && r->def == 60 && r->fmt == F_LFOHZ;
    ok &= TP[P_L2WAVE].max == LFO_WAVES - 1 && TP[P_LWAVE].max == LFO_WAVES - 1 && TP[P_L2TRIG].max == 1 && TP[P_L2POL].max == 1;
    for (k = 0; k < 2u; k++)
        ok &= TP[P_L2DA + 2u * k].max == LT_N - 1 && TP[P_L2AA + 2u * k].min == -128 && TP[P_L2AA + 2u * k].max == 127 &&
              !TP[P_L2DA + 2u * k].def && !TP[P_L2AA + 2u * k].def;
    ok &= !TP[P_L2WAVE].def && !TP[P_L2TRIG].def && !TP[P_L2POL].def;
    check(ok, "params: RATE -22..127 (60), WAVE 6 (both LFOs), TRIG NOTE/FREE, POL BI/UNI, two slots (DEST LT_N, AMT -128..127); all off");
    for (i = 0, ok = 1; i < 5u; i++) {
        param_format(&TP[P_L2DA], (int32_t)i, v, &u);
        ok &= !strcmp(v, D[i]);
    }
    param_format(&TP[P_L2DB], LT_L1RT, v, &u);        /* (the LFOs' own, named so: params.c lt_text) */
    ok &= !strcmp(v, "L1 RT");
    param_format(&TP[P_L2DA], LT_L1AMT, v, &u);
    ok &= !strcmp(v, "L1 AMT");
    param_format(&TP[P_L1DB], LT_L2RT, v, &u);
    ok &= !strcmp(v, "L2 RT");
    param_format(&TP[P_L2DA], LT_REV, v, &u);         /* (phase 2: the sends, the inserts' amounts by their names) */
    ok &= !strcmp(v, "REV");
    param_format(&TP[P_L2DB], LT_INS1, v, &u);
    ok &= !strcmp(v, ins_amt(song.sel, 0) < P_COUNT ? TP[ins_amt(song.sel, 0)].label : "S1 --");   /* (the slot's type) */
    param_format(&TP[P_L2TRIG], 1, v, &u);
    ok &= !strcmp(v, "FREE");
    param_format(&TP[P_L2POL], 1, v, &u);
    ok &= !strcmp(v, "UNI");
    param_format(r, -11, v, &u);
    ok &= !strcmp(v, "1/4");
    param_format(&TP[P_L2AA], -128, v, &u);
    ok &= !strcmp(v, "-100");
    param_format(&TP[P_L2AA], 64, v, &u);
    ok &= !strcmp(v, "+50");
    check(ok, "names: DEST OFF PIT FLT SHP AMP .. L1 RT / L1 AMT on LFO 2 (L2 RT on LFO), TRIG FREE, POL UNI, RATE -11 = 1/4, AMT -128 = -100 %, 64 = +50 %");
    ok = param_step(r, 60, -8) == 52 && param_step(r, 5, -8) == 0 && param_step(r, 0, -8) == -1 && param_step(r, -22, -3) == -22;
    check(ok, "knob: RATE turns as LFO's (stops at 0 first, then a note value a detent)");
    check(P_L2RATE > P_E7 && P_E1PIT == P_L2AB + 1 && P_VOICES > P_E1PIT && P_COUNT == P_VOICES + 1 && P_COUNT < 128, "ids: after P_E7, before ENV 1 MOD's values, ENV2's slots and VOICES (no id before moves)");
}

/* one tick of part k's LFO 2 at phase ph (the step: LFO_INC[0] at RATE 0) -> its block's offsets */
static lfo_mod_t tick_at(uint32_t k, uint32_t ph)
{
    lfo_mod_t o = {{0, 0, 0}, -1, -1};
    lfo2[k].ph = ph - LFO_INC[0];
    lfo2_tick(&trk[k], &o);
    return o;
}
/* LFO 2 of t: RATE WAVE TRIG POL, slot A on dest at amt, slot B OFF */
static void l2_set(track_t *t, int32_t rate, int32_t wave, int32_t trig, int32_t pol, int32_t dest, int32_t amt)
{
    t->p[P_L2RATE] = (int16_t)rate, t->p[P_L2WAVE] = (int16_t)wave, t->p[P_L2TRIG] = (int16_t)trig;
    t->p[P_L2POL] = (int16_t)pol, t->p[P_L2DA] = (int16_t)dest, t->p[P_L2AA] = (int16_t)amt;
    t->p[P_L2DB] = t->p[P_L2AB] = 0;
}

static void t_off_rate(void)
{
    track_t *t = &trk[0];
    lfo_mod_t o;
    uint32_t ph, i, n, ok = 1;
    init();
    mix_block(o_, CTL);                                 /* (the transport clock's step: clk_blk) */
    l2_set(t, 60, 0, 0, 0, LT_OFF, 40);
    t->p[P_L2DB] = LT_FLT;                              /* (B on FLT at 0: off too) */
    lfo2[0].ph = 12345u;
    o = (lfo_mod_t){{0, 0, 0}, -1, -1};
    lfo2_tick(t, &o);
    ok = lfo2[0].ph == 12345u && !o.m[0] && !o.m[1] && !o.m[2] && o.g2 < 0 && !LFO2_ON(t);
    t->p[P_L2DA] = LT_FLT, t->p[P_L2AA] = 0;
    lfo2_tick(t, &o);
    ok &= lfo2[0].ph == 12345u && !o.m[1] && !LFO2_ON(t);
    check(ok, "off: every slot OFF or at 0: the phase waits, no offset, no gain");
    t->p[P_L2AA] = 10;
    for (i = 0; i < 128u; i++) {
        t->p[P_L2RATE] = (int16_t)i;
        ph = lfo2[0].ph;
        lfo2_tick(t, &o);
        ok &= lfo2[0].ph - ph == LFO_INC[i];
    }
    check(ok, "rate: free 0..127 step LFO_INC[r] a block (LFO's)");
    for (n = 1, ok = 1; n <= LSYNC_N; n++) {
        t->p[P_L2RATE] = (int16_t)-(int32_t)n, t->p[P_LRATE] = (int16_t)-(int32_t)n;
        ph = lfo2[0].ph;
        lfo2_tick(t, &o);
        ok &= lfo2[0].ph - ph == lfo_inc(t) && lfo_inc(t) != 0u;
    }
    check(ok, "rate: synced -1..-22 step as LFO's at the same RATE (the transport clock)");
}

static void t_maths(void)
{
    track_t *t = &trk[0];
    lfo_mod_t o;
    int32_t a, ok = 1, lo = 0, hi = 0, mn = 32767, mx = 0;
    uint32_t ph, w, r0;
    init();
    for (a = -128; a <= 127; a++) {                    /* SQR: +32767 in the first half, -32767 in the second */
        l2_set(t, 0, 3, 0, 0, LT_PIT, a);
        o = tick_at(0, 0x20000000u);
        ok &= o.m[0] == pit_mul(32767, a) && !o.m[1] && !o.m[2] && o.g2 < 0;
        t->p[P_L2DA] = LT_FLT;
        o = tick_at(0, 0xA0000000u);
        ok &= o.m[1] == (-32767 * a) >> 8 && !o.m[0];
        t->p[P_L2DA] = LT_SHP;
        o = tick_at(0, 0x20000000u);
        ok &= o.m[2] == (32767 * a) >> 8;
        t->p[P_L2DB] = LT_SHP, t->p[P_L2AB] = 20;      /* (two slots on one: their AMTs added) */
        o = tick_at(0, 0x20000000u);
        ok &= o.m[2] == (32767 * (a + 20)) >> 8;
        t->p[P_L2DB] = LT_PIT, t->p[P_L2AB] = 7;       /* (one each) */
        o = tick_at(0, 0x20000000u);
        ok &= o.m[2] == (32767 * a) >> 8 && o.m[0] == pit_mul(32767, 7);
    }
    check(ok, "maths: PIT on the pitch curve (core.h pit_mul), FLT and SHP (v AMT) >> 8 (a depth d of before is AMT 2 d), two slots summed");
    l2_set(t, 0, 2, 0, 0, LT_FLT, 126);                /* SAW: (ph >> 16) - 32768 */
    for (w = 0, ok = 1; w < 224u; w++) {
        ph = w * 0x01230000u;
        o = tick_at(0, ph);
        ok &= o.m[1] == ((((int32_t)(ph >> 16)) - 32768) * 126) >> 8;
    }
    check(ok, "maths: SAW exact over the cycle");
    for (w = 0; w < LFO_WAVES; w++) {                  /* POL: BI both ways, UNI 0..+1 */
        int32_t blo = 0, bhi = 0, ulo = 0;
        for (ph = 0; ph < 0xFFF00000u; ph += 0x00F00000u) {
            l2_set(t, 0, (int32_t)w, 0, 0, LT_FLT, 126);
            lfo2[0].rnd = ph * 2654435761u;
            o = tick_at(0, ph);
            blo = o.m[1] < blo ? o.m[1] : blo, bhi = o.m[1] > bhi ? o.m[1] : bhi;
            t->p[P_L2POL] = 1;
            lfo2[0].rnd = ph * 2654435761u;
            o = tick_at(0, ph);
            ulo = o.m[1] < ulo ? o.m[1] : ulo;
            hi = o.m[1] > hi ? o.m[1] : hi;
        }
        lo |= blo >= 0 || bhi <= 0 || ulo < 0;
    }
    check(!lo && hi > 16000, "POL: BI goes both ways, UNI 0..+1 only (as far up), every wave");
    for (a = -128, ok = 1; a <= 127; a++)              /* AMP: within 0..unity, P_LD_AMP's of before at AMT */
        for (w = 0; w < 256u; w++) {
            ph = w << 24;
            int32_t u;
            l2_set(t, 0, 0, 0, (int32_t)(ph >> 28 & 1u), LT_AMP, a);
            o = tick_at(0, ph);
            u = (osc_sine(ph) + 32768) >> 1;
            ok &= !o.m[0] && !o.m[1] && !o.m[2];
            if (a > 0)
                ok &= o.g2 == 32767 - mulq15(u, a * 258);   /* (voice.c's LFO AMP of before at depth a) */
            else if (a < 0)
                ok &= o.g2 == 32767 - mulq15(32767 - u, a > -128 ? -a * 258 : 32767);
            else
                ok &= o.g2 < 0;
            if (a) {
                ok &= o.g2 >= 0 && o.g2 <= 32767;
                mn = o.g2 < mn ? o.g2 : mn, mx = o.g2 > mx ? o.g2 : mx;
            }
        }
    check(ok && mn < 600 && mx == 32767, "AMP: 32767 - u x AMT x 258 (the depth of before), < 0 upside down; 0..unity; 0: none");
    l2_set(t, 90, 4, 0, 0, LT_FLT, 126);               /* S&H: a new value at each wrap only, no rng() */
    r0 = rng_state;
    lfo2[0].ph = 0, lfo2[0].rnd = 0x80000000u;
    {
        int32_t last = 0x7FFFFFFF;
        uint32_t i, changes = 0, wraps = 0, old;
        ok = 1;
        for (i = 0; i < 4000u; i++) {
            old = lfo2[0].ph;
            o = (lfo_mod_t){{0, 0, 0}, -1, -1};
            lfo2_tick(t, &o);
            wraps += lfo2[0].ph < old;
            if (o.m[1] != last) {
                changes++;
                ok &= i == 0u || lfo2[0].ph < old;
            }
            last = o.m[1];
        }
        check(ok && changes >= wraps && wraps > 10u && rng_state == r0,
              "S&H: a new value at a wrap only, every wrap (a hash of the last: no rng(), either core)");
    }
}

/* SLEW's steepest move over a phase step inc: smoothstep's slope 1.5 x the full range 65535, plus the rounding */
static int32_t slew_bound(uint32_t inc) { return (int32_t)((3ull * 65535u * inc) >> 33) + 3; }
static int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
/* LFO 2 of part 0 at RATE rate on SLEW for n blocks: each block's move within slew_bound, each wrap's value S&H's of the new
 * cycle (within a block's move), no rng() -> 1 when so; *span: its max - min */
static int slew_lfo2(int32_t rate, uint32_t n, int32_t *span)
{
    track_t *t = &trk[0];
    lfo_mod_t o;
    uint32_t i, old, inc = lfo_inc_of(rate), r0 = rng_state, wraps = 0;
    int32_t last, mn = 32767, mx = -32768, b = slew_bound(inc);
    int ok = 1;
    l2_set(t, rate, LFO_WAVE_SLEW, 0, 0, LT_FLT, 126);
    lfo2[0].ph = 0, lfo2[0].rnd = 0x80000000u;
    o = (lfo_mod_t){{0, 0, 0}, -1, -1};
    lfo2_tick(t, &o);
    last = lfo2[0].v;
    for (i = 0; i < n; i++) {
        old = lfo2[0].ph;
        lfo2_tick(t, &o);
        ok &= iabs(lfo2[0].v - last) <= b;
        if (lfo2[0].ph < old) {
            wraps++;
            ok &= iabs(lfo2[0].v - lfo_shape(LFO_WAVE_SH, 0, lfo2[0].rnd)) <= b;
        }
        last = lfo2[0].v;
        mn = last < mn ? last : mn, mx = last > mx ? last : mx;
    }
    *span = mx - mn;
    return ok && wraps > 20u && rng_state == r0;
}
/* LFO of part 0 at RATE rate, WAVE wave for n blocks (track_lfo_tick) -> the hash of its S&H values (lfo_rnd's top 16 bits at
 * each wrap); SLEW: *ok cleared on a move past slew_bound or a cycle not starting at the S&H value before it */
static uint32_t slew_lfo1(int32_t rate, uint32_t wave, uint32_t n, int *ok, int32_t *span)
{
    track_t *t = &trk[0];
    uint32_t i, old, h = 2166136261u, prev = 0, inc;
    int32_t last, mn = 32767, mx = -32768, b;
    rng_state = 0x1234567u;
    t->p[P_LRATE] = (int16_t)rate, t->p[P_LWAVE] = (int16_t)wave, t->p[P_LFADE] = 0;
    t->lfo_ph = 0, t->lfo_rnd = 0, t->lfo_fade = 32767;
    inc = lfo_inc(t), b = slew_bound(inc);
    track_lfo_tick(t);
    last = t->lfo_val;
    for (i = 0; i < n; i++) {
        old = t->lfo_ph;
        prev = t->lfo_rnd;
        track_lfo_tick(t);
        if (t->lfo_ph < old) {
            h = (h ^ (t->lfo_rnd >> 16)) * 16777619u;
            if (wave == LFO_WAVE_SLEW)                 /* (LFO's draw is rng(): SLEW starts at the value drawn before) */
                *ok &= iabs(t->lfo_val - lfo_shape(LFO_WAVE_SH, 0, prev)) <= b;
        }
        if (wave == LFO_WAVE_SLEW)
            *ok &= iabs(t->lfo_val - last) <= b;
        last = t->lfo_val;
        mn = last < mn ? last : mn, mx = last > mx ? last : mx;
    }
    *span = mx - mn;
    return h ^ rng_state;
}
static void t_slew(void)
{
    uint32_t i, j, ph;
    int ok = 1;
    int32_t span, span2, span3;
    uint32_t keep_blk = clk_blk;
    init();
    for (i = 0; i < 4000u; i++) {                       /* the shape alone */
        uint32_t a = lfo_rnd_of(i, 7u), b = lfo_rnd_of(i + 1u, 7u), r = lfo_slew_pair(a, b);
        int32_t last = lfo_shape(LFO_WAVE_SLEW, 0, r), v, lo = lfo_shape(LFO_WAVE_SH, 0, a), hi = lfo_shape(LFO_WAVE_SH, 0, b);
        ok &= last == lo && iabs(lfo_shape(LFO_WAVE_SLEW, 0xFFFFFFFFu, r) - hi) <= 2;
        for (j = 1; j <= 256u; j++) {
            ph = j == 256u ? 0xFFFFFFFFu : j << 24;
            v = lfo_shape(LFO_WAVE_SLEW, ph, r);
            ok &= iabs(v - last) <= slew_bound(1u << 24) &&   /* (one way: within the Q15 rounding, 8) */
                  (hi >= lo ? v >= last - 8 && v <= hi + 2 : v <= last + 8 && v >= hi - 2);
            last = v;
        }
    }
    check(ok, "SLEW: the shape goes from one S&H value (exactly, at phase 0) to the next (at the wrap, within 2), one way, never "
              "steeper than 1.5 x the range a cycle");
    ok = slew_lfo2(127, 20000u, &span);                 /* (the fastest free rate) */
    clk_blk = CTL * 240u;                               /* (the fastest synced: 1/64 at 240 BPM) */
    ok &= slew_lfo2(-LSYNC_N, 20000u, &span2);
    clk_blk = CTL * 120u;
    ok &= slew_lfo2(-11, 20000u, &span3);               /* (1/4 at 120) */
    clk_blk = keep_blk;
    printf("SLEW LFO 2: spans %d %d %d, block steps <= %d (free 127) %d (1/64 at 240 BPM)\n", (int)span, (int)span2, (int)span3,
           (int)slew_bound(LFO_INC[127]), (int)slew_bound((uint32_t)(((uint64_t)CTL * 240u * LSYNC_KT[LSYNC_N - 1]) >> 16)));
    check(ok && span > 40000 && span2 > 40000 && span3 > 40000,
          "SLEW LFO 2: at free 127, 1/64 at 240 BPM and 1/4: each block within the steepest glide of the last (continuous at "
          "the wraps), each cycle starting at its S&H value, the whole range covered, no rng()");
    {
        int ok1 = 1, ok0 = 1;
        uint32_t h4, h5;
        h4 = slew_lfo1(127, LFO_WAVE_SH, 20000u, &ok0, &span);
        h5 = slew_lfo1(127, LFO_WAVE_SLEW, 20000u, &ok1, &span2);
        clk_blk = CTL * 240u;
        ok1 &= slew_lfo1(-LSYNC_N, LFO_WAVE_SH, 20000u, &ok0, &span) == slew_lfo1(-LSYNC_N, LFO_WAVE_SLEW, 20000u, &ok1, &span3);
        clk_blk = keep_blk;
        check(ok1 && h4 == h5 && span2 > 40000 && span3 > 40000,
              "SLEW LFO: at free 127 and 1/64 at 240 BPM each block within the steepest glide (continuous at the wraps), each "
              "cycle starting at the S&H value drawn before; the same rng() draws and S&H values as S&H");
    }
    {                                                   /* a project keeps SLEW on both LFOs */
        uint32_t k;
        init();
        for (k = 0; k < NPART; k++) {
            trk[k].p[P_LWAVE] = LFO_WAVE_SLEW;
            l2_set(&trk[k], -11, LFO_WAVE_SLEW, 1, 1, LT_FLT, 50);
        }
        proj_capture(&q, &tdl);
        init();
        proj_apply(&q, &tdl, 1);
        for (k = 0, ok = 1; k < NPART; k++)
            ok &= trk[k].p[P_LWAVE] == LFO_WAVE_SLEW && trk[k].p[P_L2WAVE] == LFO_WAVE_SLEW && trk[k].p[P_L2DA] == LT_FLT;
        check(ok, "SLEW: a project keeps it (LFO and LFO 2 WAVE 5 back after capture -> apply)");
    }
}

static void t_trig(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    init();
    host_preset(t, 0, 0);
    l2_set(t, 100, 0, 0, 0, LT_FLT, 60);
    for (i = 0; i < 50u; i++)
        mix_block(o_, CTL);
    check(lfo2[0].ph != 0u, "trig: it runs while stopped, nothing held");
    trk_note_on(t, 60, 100);
    check(lfo2[0].ph == 0u, "trig NOTE: a fresh phrase restarts it at phase 0");
    mix_block(o_, CTL);
    trk_note_on(t, 64, 100);
    check(lfo2[0].ph != 0u, "trig NOTE: a note over a held one: no restart");
    trk_all_off(t);
    for (i = 0; i < 400u; i++)
        mix_block(o_, CTL);
    t->p[P_L2TRIG] = 1;
    lfo2[0].ph = 0x12345678u;
    trk_note_on(t, 60, 100);
    check(lfo2[0].ph == 0x12345678u, "trig FREE: a fresh phrase leaves the phase where it is");
    trk_all_off(t);
    for (i = 0; i < 400u; i++)
        mix_block(o_, CTL);
}

/* part 0 (DIGITAL's first preset, a chord) for n blocks with LFO 2 as set by f -> a hash of its own render (track_render:
 * the part alone, from the same state each time: no voice, LFO at 0) */
static uint32_t render(void (*f)(track_t *), uint32_t n)
{
    uint32_t i, j, h = 2166136261u;
    track_t *t = &trk[0];
    init();
    host_preset(t, 1, 0);
    memset(t->v, 0, sizeof t->v);
    t->lfo_ph = 0, t->lfo_val = 0, t->lfo_fade = 32767, t->lfo_rnd = 0;
    vage = 0;
    lfo2[0].ph = 0, lfo2[0].rnd = 0x80000000u;
    if (f)
        f(t);
    trk_note_on(t, 60, 100);
    trk_note_on(t, 64, 100);
    for (i = 0; i < n; i++) {
        track_render(t, o_, CTL);
        for (j = 0; j < CTL; j++)
            h = (h ^ (uint32_t)o_[j]) * 16777619u;
    }
    memset(t->v, 0, sizeof t->v);
    return h;
}
static void f_amp(track_t *t) { l2_set(t, 100, 0, 0, 0, LT_AMP, 100); }
static void f_pit(track_t *t) { l2_set(t, 100, 1, 0, 0, LT_PIT, 40); }
static void f_off(track_t *t) { l2_set(t, 100, 1, 1, 1, LT_OFF, 40); }
static void f_zero(track_t *t) { l2_set(t, -3, 4, 1, 1, LT_AMP, 0); }
static void t_render(void)
{
    uint32_t base = render(0, 300);
    check(render(f_off, 300) == base && render(f_zero, 300) == base,
          "render: DEST OFF, or AMT 0 (any RATE WAVE TRIG POL): the part as without LFO 2, sample for sample");
    check(render(f_amp, 300) != base && render(f_pit, 300) != base && render(f_amp, 300) == render(f_amp, 300),
          "render: on AMP, on PIT: heard (and the same twice: no rng())");
}

/* the parts' LFO 2 values -> their packed form in q (project.c), the round trip */
static void t_project(void)
{
    int ok;
    uint32_t k;
    init();
    TDRUM->p[P_LEG_FLT] = 33;                          /* (the macros' ENERGY: MAC_ID) */
    TDRUM->p[P_LRATE] = 77, TDRUM->p[P_LWAVE] = 2, TDRUM->p[P_LEG_PIT] = 5;   /* (the drum track's own, never read) */
    proj_capture(&q, &tdl);
    ok = !l2_tagged(&q) && q.t[TRK_DRUM].p[P_LRATE] == 77 && q.t[TRK_DRUM].p[P_LWAVE] == 2 && q.t[TRK_DRUM].p[P_LEG_PIT] == 5;
    check(ok, "project: every LFO 2 at its defaults: no tag, the drum track's slots its own (a project of before)");
    init();
    proj_apply(&q, &tdl, 1);
    ok = TDRUM->p[P_LRATE] == 77 && TDRUM->p[P_LWAVE] == 2 && TDRUM->p[P_LEG_PIT] == 5 && TDRUM->p[P_LEG_FLT] == 33;
    for (k = 0; k < NPART; k++)
        ok &= trk[k].p[P_L2RATE] == 60 && !trk[k].p[P_L2DA] && !trk[k].p[P_L2AA] && !trk[k].p[P_L2DB] && !trk[k].p[P_L2AB];
    check(ok, "project: a project without LFO 2 (or from before) loads with LFO 2 at its defaults, the drum's slots kept");
    {
        static const int16_t V[NPART][L2_NV] = {{-10, 4, 1, 1, LT_AMP, -128, LT_PAN, 127}, {127, 0, 0, 1, LT_PIT, 126, 0, 0},
                                                 {0, 3, 1, 0, LT_SHP, -1, LT_L2AMT, -77}};   /* (a DEST past 31) */
        uint32_t j;
        init();
        TDRUM->p[P_LEG_FLT] = -20;
        for (k = 0; k < NPART; k++)
            memcpy(&trk[k].p[P_L2RATE], V[k], sizeof V[k]);
        trk[1].p[P_L1DA] = LT_FLT, trk[1].p[P_L1AA] = 40;   /* (LFO's slots: the depths of before, part 2) */
        proj_capture(&q, &tdl);
        ok = !l2_tagged(&q) && q.t[TRK_DRUM].p[P_LEG_FLT] == -20 && proj_ok(&q);
        for (k = 0; k < NPART; k++)
            ok &= !q.t[k].p[P_LEG_PIT] && !q.t[k].p[P_LEG_FLT] && !q.t[k].p[P_LEG_SHP] && !q.t[k].p[P_LEG_AMP];
        check(ok, "project: LFO 2 on: none of it in the drum track's slots (macros' untouched), the parts' LFO DEST words 0: "
                  "the slots in the modulation record (2026-10-14)");
        init();
        for (k = 0; k < NPART; k++)
            l2_set(&trk[k], 1, 1, 0, 0, 1, 1);
        proj_apply(&q, &tdl, 1);
        for (k = 0, ok = 1; k < NPART; k++)
            for (j = 0; j < L2_NV; j++)
                ok &= trk[k].p[P_L2RATE + j] == V[k][j];
        ok &= trk[1].p[P_L1DA] == LT_FLT && trk[1].p[P_L1AA] == 40 && !trk[0].p[P_L1DA] && !trk[0].p[P_L1AA];
        ok &= TDRUM->p[P_LRATE] == TP[P_LRATE].def && TDRUM->p[P_LWAVE] == 0 && TDRUM->p[P_LEG_PIT] == 0 &&
              TDRUM->p[P_LEG_AMP] == 0 && TDRUM->p[P_LEG_FLT] == -20;
        check(ok, "project: capture -> apply: every LFO 2 value back (synced / free RATE, TRIG, POL, AMT -128 .. 127, both slots)");
        proj_capture(&q2, &tdl);
        check(!memcmp(&q, &q2, sizeof q), "project: captured again, byte for byte the same");
        for (k = 0; k < NPART; k++)
            l2_set(&trk[k], 60, 0, 0, 0, 0, 0);
        proj_apply(&q, &tdl, 0);
        check(trk[1].p[P_L2AA] == 126 && trk[0].p[P_L2RATE] == -10 && trk[0].p[P_L2AB] == 127,
              "project: a song section (apply, not all) brings its LFO 2");
        q2 = q;
        q2.t[TRK_DRUM].p[L2_W0[0]] = (int16_t)(0x7Fu | 0x80u << 8);   /* (RATE 127, AMT A -128) */
        q2.t[TRK_DRUM].p[L2_W1[0]] = (int16_t)0x7FFF;  /* (a project of before: WAVE 7, TRIG, POL, DEST A 31, DEST B 31: */
        q2.t[TRK_DRUM].p[P_LRATE] = (int16_t)L2_TAG;   /* past its list) */
        q2.sum = proj_sum(&q2);
        proj_apply(&q2, &tdl, 1);
        check(trk[0].p[P_L2WAVE] == 4 && trk[0].p[P_L2DA] == LT_OFF && trk[0].p[P_L2DB] == LT_OFF && trk[0].p[P_L2RATE] == 127,
              "project: a damaged packed value: clamped into its range (a DEST past them: OFF)");
        q2 = q;                                        /* LFO 2 with one DEST (feat/lfo2's L2_TAG_V1) */
        q2.t[TRK_DRUM].p[P_LRATE] = (int16_t)L2_TAG_V1;
        q2.t[TRK_DRUM].p[L2_W0[0]] = (int16_t)(70u | (uint32_t)((-40) & 0x7F) << 8);
        q2.t[TRK_DRUM].p[L2_W1[0]] = (int16_t)(2u | 2u << 3 | 1u << 6);   /* (TRI, FLT, FREE, BI) */
        q2.t[0].p[P_LEG_PIT] = q2.t[0].p[P_LEG_FLT] = q2.t[0].p[P_LEG_SHP] = q2.t[0].p[P_LEG_AMP] = 0;
        q2.sum = proj_sum(&q2);
        proj_apply(&q2, &tdl, 1);
        check(trk[0].p[P_L2RATE] == 70 && trk[0].p[P_L2WAVE] == 2 && trk[0].p[P_L2TRIG] == 1 && !trk[0].p[P_L2POL] &&
                  trk[0].p[P_L2DA] == LT_FLT && trk[0].p[P_L2AA] == -80 && !trk[0].p[P_L2DB],
              "project: LFO 2 of one DEST (L2_TAG_V1) loads: slot A, its AMT x 2 (the same sound)");
    }
    (void)k;
}

int main(void)
{
    t_params();
    t_off_rate();
    t_maths();
    t_slew();
    t_trig();
    t_render();
    t_project();
    printf("lfo2_test: %d failed\n", fails);
    return fails;
}
