/* SPDX-License-Identifier: GPL-3.0-only */
/* LFO 2 (FELUCCA_LFO2: core.h P_L2RATE .. P_L2AMT, voice.c lfo2_tick, project.c l2_pack). Run by tests/run_tests.sh with
 * LFO2=1 (everything below) and LFO2=0 (a project that has LFO 2 loads in a build without it):
 *   params    ranges, defaults, names; RATE turns as LFO's (a fast turn down stops at 0, below 0 a note value a detent)
 *   off       DEST OFF or AMT 0: nothing computed (the phase waits, the block's offsets none); a part renders as with
 *             LFO 2 at its defaults
 *   rate      free: LFO_INC[r] a block; synced: LFO's step at the same RATE (lfo_inc_of), every note value
 *   maths     PIT FLT SHP on LFO DEST's scale (as P_LD_*), AMP as P_LD_AMP at 2 x AMT, < 0 upside down, never below 0
 *             nor above unity; POL UNI one way only (0..+1); the waves (SQR SAW exact); S&H once a cycle, no rng()
 *   trig      NOTE: a fresh phrase restarts it at 0, a note over a held one does not; FREE: never
 *   render    a part with LFO 2 on AMP / PIT sounds different from off; the render with it off is the render of before
 *   project   capture -> apply round trip (every value, both TRIGs and POLs, synced and free RATE, AMT < 0); the
 *             drum track's LFO slots hold it only when one is not at its default (a project without it: byte for
 *             byte as before); the macros' slot (P_LD_FLT) untouched; a project from before (no tag) loads with LFO 2
 *             at its defaults and the drum track's own slots; a song section (apply, not all) brings its LFO 2 too
 *   LFO2=0    a project with LFO 2: the drum track's slots back to their defaults, l2_miss names the parts it acts on
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

#if FELUCCA_LFO2
static void t_params(void)
{
    static const char *const D[L2D_N] = {"OFF", "PIT", "FLT", "SHP", "AMP"};
    const param_desc_t *r = &TP[P_L2RATE];
    char v[8];
    const char *u;
    uint32_t i;
    int ok;
    ok = r->min == -LSYNC_N && r->max == 127 && r->def == 60 && r->fmt == F_LFOHZ;
    ok &= TP[P_L2WAVE].max == 4 && TP[P_L2TRIG].max == 1 && TP[P_L2POL].max == 1 && TP[P_L2DEST].max == L2D_N - 1;
    ok &= TP[P_L2AMT].min == -64 && TP[P_L2AMT].max == 63 && TP[P_L2AMT].def == 0;
    ok &= !TP[P_L2WAVE].def && !TP[P_L2TRIG].def && !TP[P_L2POL].def && !TP[P_L2DEST].def;
    check(ok, "params: RATE -10..127 (60), WAVE 5, TRIG NOTE/FREE, POL BI/UNI, DEST 5, AMT -64..63; all off by default");
    for (i = 0, ok = 1; i < L2D_N; i++) {
        param_format(&TP[P_L2DEST], (int32_t)i, v, &u);
        ok &= !strcmp(v, D[i]);
    }
    param_format(&TP[P_L2TRIG], 1, v, &u);
    ok &= !strcmp(v, "FREE");
    param_format(&TP[P_L2POL], 1, v, &u);
    ok &= !strcmp(v, "UNI");
    param_format(r, -5, v, &u);
    ok &= !strcmp(v, "1/4");
    check(ok, "names: DEST OFF PIT FLT SHP AMP, TRIG FREE, POL UNI, RATE -5 = 1/4");
    ok = param_step(r, 60, -8) == 52 && param_step(r, 5, -8) == 0 && param_step(r, 0, -8) == -1 && param_step(r, -10, -3) == -10;
    check(ok, "knob: RATE turns as LFO's (stops at 0 first, then a note value a detent)");
    check(P_L2RATE > P_E7 && P_COUNT == P_L2AMT + 1 && P_COUNT < 128, "ids: after P_E7, the last ones (no id before moves)");
}

/* one tick of part k's LFO 2 at phase ph (the step: LFO_INC[0] at RATE 0) -> its block's offsets */
static lfo2_mod_t tick_at(uint32_t k, uint32_t ph)
{
    lfo2_mod_t o = {{0, 0, 0}, 32767};
    lfo2[k].ph = ph - LFO_INC[0];
    lfo2_tick(&trk[k], &o);
    return o;
}
static void l2_set(track_t *t, int32_t rate, int32_t wave, int32_t trig, int32_t pol, int32_t dest, int32_t amt)
{
    t->p[P_L2RATE] = (int16_t)rate, t->p[P_L2WAVE] = (int16_t)wave, t->p[P_L2TRIG] = (int16_t)trig;
    t->p[P_L2POL] = (int16_t)pol, t->p[P_L2DEST] = (int16_t)dest, t->p[P_L2AMT] = (int16_t)amt;
}

static void t_off_rate(void)
{
    track_t *t = &trk[0];
    lfo2_mod_t o;
    uint32_t ph, i, n, ok = 1;
    init();
    mix_block(o_, CTL);                                 /* (the transport clock's step: clk_blk) */
    l2_set(t, 60, 0, 0, 0, L2D_OFF, 40);
    lfo2[0].ph = 12345u;
    o = (lfo2_mod_t){{0, 0, 0}, 32767};
    lfo2_tick(t, &o);
    ok = lfo2[0].ph == 12345u && !o.m[0] && !o.m[1] && !o.m[2] && o.amp == 32767;
    t->p[P_L2DEST] = L2D_FLT, t->p[P_L2AMT] = 0;
    lfo2_tick(t, &o);
    ok &= lfo2[0].ph == 12345u && !o.m[1];
    check(ok, "off: DEST OFF or AMT 0: the phase waits, no offset, no gain");
    t->p[P_L2AMT] = 10;
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
    check(ok, "rate: synced -1..-10 step as LFO's at the same RATE (the transport clock)");
}

static void t_maths(void)
{
    track_t *t = &trk[0];
    lfo2_mod_t o;
    int32_t a, ok = 1, lo = 0, hi = 0, mn = 32767, mx = 0;
    uint32_t ph, w, r0;
    init();
    for (a = -64; a <= 63; a++) {                      /* SQR: +32767 in the first half, -32767 in the second */
        l2_set(t, 0, 3, 0, 0, L2D_PIT, a);
        o = tick_at(0, 0x20000000u);
        ok &= o.m[0] == (32767 * a * 3) >> 7 && !o.m[1] && !o.m[2] && o.amp == 32767;
        t->p[P_L2DEST] = L2D_FLT;
        o = tick_at(0, 0xA0000000u);
        ok &= o.m[1] == (-32767 * a) >> 7 && !o.m[0];
        t->p[P_L2DEST] = L2D_SHP;
        o = tick_at(0, 0x20000000u);
        ok &= o.m[2] == (32767 * a) >> 7;
    }
    check(ok, "maths: PIT (v AMT 3) >> 7, FLT and SHP (v AMT) >> 7: LFO DEST's scale, each its own offset");
    l2_set(t, 0, 2, 0, 0, L2D_FLT, 63);                /* SAW: (ph >> 16) - 32768 */
    for (w = 0, ok = 1; w < 224u; w++) {
        ph = w * 0x01230000u;
        o = tick_at(0, ph);
        ok &= o.m[1] == ((((int32_t)(ph >> 16)) - 32768) * 63) >> 7;
    }
    check(ok, "maths: SAW exact over the cycle");
    for (w = 0; w < 5u; w++) {                         /* POL: BI both ways, UNI 0..+1 */
        int32_t blo = 0, bhi = 0, ulo = 0;
        for (ph = 0; ph < 0xFFF00000u; ph += 0x00F00000u) {
            l2_set(t, 0, (int32_t)w, 0, 0, L2D_FLT, 63);
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
    for (a = -64, ok = 1; a <= 63; a++)                /* AMP: within 0..unity, P_LD_AMP's at 2 x AMT */
        for (w = 0; w < 256u; w++) {
            ph = w << 24;
            int32_t u;
            l2_set(t, 0, 0, 0, (int32_t)(ph >> 28 & 1u), L2D_AMP, a);
            o = tick_at(0, ph);
            u = (osc_sine(ph) + 32768) >> 1;
            ok &= o.amp >= 0 && o.amp <= 32767 && !o.m[0] && !o.m[1] && !o.m[2];
            if (a > 0)
                ok &= o.amp == 32767 - mulq15(u, a * 2 * 258);   /* (P_LD_AMP = 2 a: voice.c's LFO AMP) */
            else if (a < 0)
                ok &= o.amp == 32767 - mulq15(32767 - u, a > -64 ? -a * 516 : 32767);
            else
                ok &= o.amp == 32767;
            mn = o.amp < mn ? o.amp : mn, mx = o.amp > mx ? o.amp : mx;
        }
    check(ok && mn < 600 && mx == 32767, "AMP: 32767 - u x 2 AMT x 258 (P_LD_AMP's at 2 x AMT), < 0 upside down; 0..unity");
    l2_set(t, 90, 4, 0, 0, L2D_FLT, 63);               /* S&H: a new value at each wrap only, no rng() */
    r0 = rng_state;
    lfo2[0].ph = 0, lfo2[0].rnd = 0x80000000u;
    {
        int32_t last = 0x7FFFFFFF;
        uint32_t i, changes = 0, wraps = 0, old;
        ok = 1;
        for (i = 0; i < 4000u; i++) {
            old = lfo2[0].ph;
            o = (lfo2_mod_t){{0, 0, 0}, 32767};
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

static void t_trig(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    init();
    host_preset(t, 0, 0);
    l2_set(t, 100, 0, 0, 0, L2D_FLT, 30);
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
static void f_amp(track_t *t) { l2_set(t, 100, 0, 0, 0, L2D_AMP, 50); }
static void f_pit(track_t *t) { l2_set(t, 100, 1, 0, 0, L2D_PIT, 20); }
static void f_off(track_t *t) { l2_set(t, 100, 1, 1, 1, L2D_OFF, 20); }
static void f_zero(track_t *t) { l2_set(t, -3, 4, 1, 1, L2D_AMP, 0); }
static void t_render(void)
{
    uint32_t base = render(0, 300);
    check(render(f_off, 300) == base && render(f_zero, 300) == base,
          "render: DEST OFF, or AMT 0 (any RATE WAVE TRIG POL): the part as without LFO 2, sample for sample");
    check(render(f_amp, 300) != base && render(f_pit, 300) != base && render(f_amp, 300) == render(f_amp, 300),
          "render: on AMP, on PIT: heard (and the same twice: no rng())");
}
#endif

/* the parts' LFO 2 values -> their packed form in q (project.c), the round trip */
static void t_project(void)
{
    int ok;
    uint32_t k;
    init();
    TDRUM->p[P_LD_FLT] = 33;                           /* (the macros' ENERGY: MAC_ID) */
    TDRUM->p[P_LRATE] = 77, TDRUM->p[P_LWAVE] = 2, TDRUM->p[P_LD_PIT] = 5;   /* (the drum track's own, never read) */
    proj_capture(&q, &tdl);
    ok = !l2_tagged(&q) && q.t[TRK_DRUM].p[P_LRATE] == 77 && q.t[TRK_DRUM].p[P_LWAVE] == 2 && q.t[TRK_DRUM].p[P_LD_PIT] == 5;
    check(ok, "project: every LFO 2 at its defaults: no tag, the drum track's slots its own (a project of before)");
    init();
    proj_apply(&q, &tdl, 1);
    ok = TDRUM->p[P_LRATE] == 77 && TDRUM->p[P_LWAVE] == 2 && TDRUM->p[P_LD_PIT] == 5 && TDRUM->p[P_LD_FLT] == 33;
#if FELUCCA_LFO2
    for (k = 0; k < NPART; k++)
        ok &= trk[k].p[P_L2RATE] == 60 && !trk[k].p[P_L2DEST] && !trk[k].p[P_L2AMT];
#endif
    check(ok, "project: a project without LFO 2 (or from before) loads with LFO 2 at its defaults, the drum's slots kept");
#if FELUCCA_LFO2
    {
        static const int16_t V[NPART][L2_NV] = {{-10, 4, 1, 1, L2D_AMP, -64}, {127, 0, 0, 1, L2D_PIT, 63},
                                                 {0, 3, 1, 0, L2D_SHP, -1}};
        uint32_t j;
        init();
        TDRUM->p[P_LD_FLT] = -20;
        for (k = 0; k < NPART; k++)
            memcpy(&trk[k].p[P_L2RATE], V[k], sizeof V[k]);
        proj_capture(&q, &tdl);
        ok = l2_tagged(&q) && q.t[TRK_DRUM].p[P_LD_FLT] == -20 && proj_ok(&q);
        check(ok, "project: LFO 2 on: tagged in the drum track's LFO slots, the macros' slot (P_LD_FLT) untouched");
        init();
        for (k = 0; k < NPART; k++)
            l2_set(&trk[k], 1, 1, 0, 0, 1, 1);
        proj_apply(&q, &tdl, 1);
        for (k = 0, ok = 1; k < NPART; k++)
            for (j = 0; j < L2_NV; j++)
                ok &= trk[k].p[P_L2RATE + j] == V[k][j];
        ok &= TDRUM->p[P_LRATE] == TP[P_LRATE].def && TDRUM->p[P_LWAVE] == 0 && TDRUM->p[P_LD_PIT] == 0 &&
              TDRUM->p[P_LD_AMP] == 0 && TDRUM->p[P_LD_FLT] == -20;
        check(ok, "project: capture -> apply: every LFO 2 value back (synced / free RATE, TRIG, POL, AMT -64 .. 63)");
        proj_capture(&q2, &tdl);
        check(!memcmp(&q, &q2, sizeof q), "project: captured again, byte for byte the same");
        for (k = 0; k < NPART; k++)
            l2_set(&trk[k], 60, 0, 0, 0, 0, 0);
        proj_apply(&q, &tdl, 0);
        check(trk[1].p[P_L2AMT] == 63 && trk[0].p[P_L2RATE] == -10, "project: a song section (apply, not all) brings its LFO 2");
        q2 = q;
        q2.t[TRK_DRUM].p[L2_W0[0]] = (int16_t)(0x7Fu | 0x40u << 8);   /* (RATE 127, AMT -64) */
        q2.t[TRK_DRUM].p[L2_W1[0]] = (int16_t)0xFF;    /* (WAVE 7, DEST 7, TRIG, POL: out of range) */
        q2.sum = proj_sum(&q2);
        proj_apply(&q2, &tdl, 1);
        check(trk[0].p[P_L2WAVE] == 4 && trk[0].p[P_L2DEST] == L2D_AMP && trk[0].p[P_L2AMT] == -64,
              "project: a damaged packed value: clamped into its range");
    }
#else
    {   /* a project saved with LFO 2 on parts 1 and 3 (the packed form: project.c l2_pack), in a build without it */
        init();
        proj_capture(&q, &tdl);
        q.t[TRK_DRUM].p[P_LRATE] = (int16_t)L2_TAG;
        q.t[TRK_DRUM].p[L2_W0[0]] = (int16_t)(60u | 20u << 8), q.t[TRK_DRUM].p[L2_W1[0]] = (int16_t)(L2D_FLT << 3);
        q.t[TRK_DRUM].p[L2_W0[1]] = (int16_t)60, q.t[TRK_DRUM].p[L2_W1[1]] = (int16_t)(L2D_PIT << 3);   /* (AMT 0) */
        q.t[TRK_DRUM].p[L2_W0[2]] = (int16_t)(60u | 0x7Fu << 8), q.t[TRK_DRUM].p[L2_W1[2]] = (int16_t)(L2D_AMP << 3 | 1u << 6);
        q.sum = proj_sum(&q);
        proj_apply(&q, &tdl, 1);
        ok = TDRUM->p[P_LRATE] == TP[P_LRATE].def && TDRUM->p[P_LWAVE] == 0 && TDRUM->p[P_LPHASE] == 0 &&
             TDRUM->p[P_LFADE] == 0 && TDRUM->p[P_LD_PIT] == 0 && TDRUM->p[P_LD_SHP] == 0 && TDRUM->p[P_LD_AMP] == 0;
        check(ok, "LFO2=0: a project with LFO 2: the drum track's LFO slots back to their defaults");
        check(l2_miss == 5u, "LFO2=0: l2_miss: parts 1 and 3 (AMT not 0), not part 2 (AMT 0): TOOLS > MISS says LFO 2");
        q.t[TRK_DRUM].p[P_LRATE] = 60;
        q.sum = proj_sum(&q);
        proj_apply(&q, &tdl, 1);
        check(l2_miss == 0u, "LFO2=0: a project without the tag: nothing missing");
    }
#endif
}

int main(void)
{
#if FELUCCA_LFO2
    t_params();
    t_off_rate();
    t_maths();
    t_trig();
    t_render();
#endif
    t_project();
    printf("lfo2_test (LFO2=%d): %d failed\n", FELUCCA_LFO2, fails);
    return fails;
}
