/* SPDX-License-Identifier: GPL-3.0-only */
/* The examples of CONTRIBUTING.md, each a builder item off by default (tests/run_tests.sh builds this with the three on:
 * -DFELUCCA_EX_GAIN=1 -DFELUCCA_EX_SQUARE=1 -DFELUCCA_NOTEGEN=1 -DFELUCCA_EX_NOTEGEN=1 -DFELUCCA_MOTION=1). A new piece
 * copies the shape of these checks.
 *   EXGN (fx/insfx/exgain.c), an insert type:
 *     - listed on the tracks' TYPE list (its amount's label), not on the master, the returns nor the USB input;
 *     - at 0 (OFF) from rest, in no slot, or the track's FX bypassed: the block bit for bit;
 *     - 112 (0 dB): bit for bit once settled; 100 (-6 dB) and 127 (+7.5 dB): the level they say (RMS, 1 %);
 *     - 127 then 0: glides back (no step bigger than the glide's), at rest (ifx_w 0) and bit for bit again;
 *     - on the drum bus: L and R both; its TLV round trip (none when every one is OFF); the motion's stored id
 *       (auto.c MOT_TAIL2: the second tail + 9) both ways.
 *   SQUARE (engines/square/eng_square.c), a synth engine:
 *     - engine UID 28 (29 UIDs), named SQUARE, its fallback ANALOG; every preset on the PRESETS list (model.c BANK);
 *     - every preset: a note held 1 s then let go: audible, bounded, every voice free at the end;
 *     - the pitch: the filter open, one pulse, A3 played: 220 Hz from the zero crossings (within 1 %);
 *     - each EDIT value changes the sound.
 *   WALK and UPDN (seq/gen_ex.c), note generators:
 *     - two more types on the GEN page (gen_ntypes), stored as 4 and 5 (GEN_TLV round trip);
 *     - WALK: a note every step, in the scale, within RANGE degrees of the ROOT, no step wider than LEAP degrees, the
 *       same walk twice (a pure function), another DRAW another walk;
 *     - UPDN: up HOP degrees a note to the top of RANGE and back down, a note every RATE steps (the others rests);
 *     - WRITE: the pattern then holds the notes that played; a drum lane gets the hits only.
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("examples: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ---------------------------------------------------------------- EXGN --- */
#define N 8192u
static int32_t x[N], y[N], z[N];
static void sine(int32_t amp, double f)
{
    uint32_t i;
    for (i = 0; i < N; i++)
        x[i] = (int32_t)lrint(amp * sin(2 * M_PI * f * i / FS));
}
static double rms(const int32_t *v, uint32_t from, uint32_t to)
{
    double s = 0;
    uint32_t i;
    for (i = from; i < to; i++)
        s += (double)v[i] * v[i];
    return sqrt(s / (to - from));
}
static int same(const int32_t *a, const int32_t *b, uint32_t from)
{
    uint32_t i;
    for (i = from; i < N; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static void gx_reset(void)
{
    static const uint8_t E[2] = {FXT_NONE, FXT_NONE};
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        ins_put(k, E);
        trk[k].p[P_FXOFF] = 0;
        trk[k].p[P_TEXGN] = 0;
    }
    memset(ifx_exgn, 0, sizeof ifx_exgn);
}
/* EXGN in part 1's S1 (fx.c ins_part) over x -> y: amount a0, from sample `at` a1 */
static void gx_part(int32_t a0, int32_t a1, uint32_t at)
{
    static const uint8_t S[2] = {FXT_EXGN, FXT_NONE};
    uint32_t i;
    ins_put(0, S);
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL) {
        trk[0].p[P_TEXGN] = (int16_t)(i < at ? a0 : a1);
        ins_part(&trk[0], y + i, CTL);
    }
}
static void t_exgn(void)
{
    uint8_t o[64];
    uint32_t i, k, n, sid, listed = 0, step_ok = 1;
    double r;
    int16_t *vp;
    (void)ins_desc(0, 0, &vp);                         /* (the TYPE list as INSERTS shows it) */
    for (k = 0; k < INS_NLIST; k++)
        listed |= ins_list[k] == FXT_EXGN && ins_names[k] && !strcmp(ins_names[k], "EXGN");
#ifdef UT_OK
    listed &= !(UT_OK >> FXT_EXGN & 1u);               /* (the USB input: with USB audio only) */
#endif
    check("EXGN: on the tracks' TYPE list as EXGN, not the master's, a return's nor the USB input's",
          listed && (FXT_BUILT & FXT_INSERT) >> FXT_EXGN & 1u && !(MST_OK >> FXT_EXGN & 1u) && !(RT_OK >> FXT_EXGN & 1u) &&
          FXT_AMT[FXT_EXGN] == P_TEXGN && FXT_EXGN == 29u);
    sine(20000, 220);                                  /* (+7.5 dB of it stays clear of the part's clamp) */
    gx_reset();
    gx_part(0, 0, 0);
    check("EXGN: at 0 (OFF) from rest, the block bit for bit", same(x, y, 0) && !ifx_w(FXT_EXGN, 0));
    gx_reset();
    trk[0].p[P_TEXGN] = 127;
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL)
        ins_part(&trk[0], y + i, CTL);                 /* (in no slot: its amount unheard) */
    check("EXGN: at 127 in no slot, bit for bit", same(x, y, 0));
    gx_reset();
    trk[0].p[P_FXOFF] = 1;
    gx_part(127, 127, 0);
    check("EXGN: at 127 with the track's FX bypassed, bit for bit", same(x, y, 0));
    gx_reset();
    gx_part(112, 112, 0);
    check("EXGN: 112 (0 dB): bit for bit (the gain's ramp starts there)", same(x, y, 0));
    gx_reset();
    gx_part(100, 100, 0);
    r = rms(y, N / 2u, N) / rms(x, N / 2u, N);
    printf("examples: EXGN 100: %.4f (-6 dB: 0.5012)\n", r);
    check("EXGN: 100 is -6 dB (RMS within 1 %)", fabs(r - 0.5012) < 0.005);
    gx_reset();
    gx_part(127, 127, 0);
    r = rms(y, N / 2u, N) / rms(x, N / 2u, N);
    printf("examples: EXGN 127: %.4f (+7.5 dB: 2.3714)\n", r);
    check("EXGN: 127 is +7.5 dB (RMS within 1 %)", fabs(r - 2.3714) < 0.024);
    gx_reset();
    gx_part(127, 0, N / 4u);
    for (i = N / 4u; i < N; i++)                       /* (the gain glides: y / x never jumps by more than a step) */
        if (abs(x[i]) > 10000 && i > N / 4u + CTL)
            step_ok &= fabs((double)y[i] / x[i] - (double)y[i - 1] / x[i - 1]) < 0.2 || abs(x[i - 1]) <= 10000;
    check("EXGN: 127, then 0: it glides back (no jump), rests (ifx_w 0) and is bit for bit after",
          step_ok && !ifx_w(FXT_EXGN, 0) && same(x, y, N - N / 8u));
    gx_reset();
    {
        static const uint8_t S[2] = {FXT_EXGN, FXT_NONE};
        ins_put(TRK_DRUM, S);
        memcpy(y, x, sizeof y), memcpy(z, x, sizeof z);
        for (i = 0; i + CTL <= N; i += CTL)
            ifx_bus(FXT_EXGN, 100, y + i, z + i, CTL);
        r = rms(y, N / 2u, N) / rms(x, N / 2u, N);
        check("EXGN: on the drum bus, L and R both at the gain", fabs(r - 0.5012) < 0.005 && same(y, z, 0));
    }
    gx_reset();
    check("EXGN: no TLV while every track's is OFF", fxs_tlv(FXT_EXGN, o) == 0u);
    trk[1].p[P_TEXGN] = 90, TDRUM->p[P_TEXGN] = 112;
    n = fxs_tlv(FXT_EXGN, o);
    trk[1].p[P_TEXGN] = TDRUM->p[P_TEXGN] = 0;
    fxs_untlv(FXT_EXGN, o, n, 1);
    check("EXGN: its TLV round trip (T1 T2 T3 DR)", n == NTRK && trk[1].p[P_TEXGN] == 90 && TDRUM->p[P_TEXGN] == 112 &&
          !trk[0].p[P_TEXGN]);
    sid = mot_sid(P_TEXGN);
    check("EXGN: the motion's stored id, the second tail + 9, both ways", sid == AUTO_TAIL2 + 9u && mot_id(sid) == P_TEXGN);
    gx_reset();
}

/* -------------------------------------------------------------- SQUARE --- */
static void sq_reset(void)
{
    uint32_t i;
    seq_stop();
    transport_req = 0;
    for (i = 0; i < NTRK; i++)
        memset(trk[i].v, 0, sizeof trk[i].v);
    host_tracks_init();
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
}
static uint32_t sq_active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}
typedef struct { double rms; int32_t peak; uint32_t busy, xings; uint64_t hash; } sq_res_t;
/* preset pi on T1, dry, EDIT k (8: none) at v: note held 1 s, then 1 s more; the zero crossings of the held second */
static sq_res_t ex_sq_render(uint32_t pi, uint32_t k, int32_t v, uint32_t note)
{
    sq_res_t r = {0, 0, 0, 0, 1469598103934665603ull};
    int32_t out[CTL * 2], last = 0;
    double acc = 0;
    uint32_t b, i, nb = 2u * FS / CTL;
    sq_reset();
    host_preset(&trk[0], ENG_IX_SQUARE, pi);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_ED_FLT] = 0;                            /* (no filter envelope: the pitch test's filter stays open) */
    if (k < 8u)
        trk[0].p[P_E0 + k] = (int16_t)v;
    trk_note_on(&trk[0], (uint8_t)note, 100);
    for (b = 0; b < nb; b++) {
        if (b == nb / 2u)
            trk_note_off(&trk[0], (uint8_t)note);
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = abs(out[i]);
            acc += (double)out[i] * out[i];
            r.peak = a > r.peak ? a : r.peak;
            r.hash = (r.hash ^ (uint32_t)out[i]) * 1099511628211ull;
        }
        for (i = 0; b >= nb / 4u && b < nb / 2u && i < CTL; i++) {   /* (the held note's last half second, left) */
            r.xings += (last < 0) != (out[2 * i] < 0) && b > nb / 4u;
            last = out[2 * i];
        }
    }
    r.rms = sqrt(acc / (2.0 * nb * CTL));
    for (b = 0; b < FS / CTL; b++)                     /* (a second more: the release) */
        mix_block(out, CTL);
    r.busy = sq_active(&trk[0]);
    return r;
}
static void t_square(void)
{
    const engine_t *e = ENGINES[ENG_IX_SQUARE];
    uint32_t pi, k, ok = 1;
    sq_res_t r, base;
    double hz;
    check("SQUARE: engine UID 28 of 29, named SQUARE, ANALOG its fallback, 3 presets (model.c BANK: tests/preset_cover_test.c)",
          ENG_IX_SQUARE < NENGINES && !strcmp(e->name, "SQUARE") && eng_uid(ENG_IX_SQUARE) == ENG_UID_SQUARE &&
          ENG_UID_SQUARE == 28u && ENG_UID_N == 29u && ENG_FALLBACK[ENG_UID_SQUARE] == 0u && eng_built(28u) &&
          e->npresets == 3u);
    for (pi = 0; pi < e->npresets; pi++) {
        r = ex_sq_render(pi, 8, 0, 57);
        printf("examples: SQUARE %-9s rms %6.0f peak %7d\n", e->presets[pi].name, r.rms, r.peak);
        ok &= r.rms > 300 && r.peak < 884000 && !r.busy;
    }
    check("SQUARE: every preset audible, bounded, every voice free after its release", ok);
    ex_sq_render(0, 8, 0, 57);                            /* (warm the voice table, then the pitch: one open pulse) */
    sq_reset();
    host_preset(&trk[0], ENG_IX_SQUARE, 0);
    {
        static const int8_t OPEN[8] = {0, 0, 0, 0, 127, 0, 0, 0};   /* PW SUB DTN NOIS CUT RES TRK DRV */
        int32_t out[CTL * 2], last = 0;
        uint32_t b, i, xings = 0, nb = FS / CTL;
        trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        trk[0].p[P_ED_FLT] = 0, trk[0].p[P_VOICE] = V_POLY;
        for (k = 0; k < 8u; k++)
            trk[0].p[P_E0 + k] = OPEN[k];
        trk_note_on(&trk[0], 57, 100);                 /* A3 */
        for (b = 0; b < nb; b++) {
            mix_block(out, CTL);
            for (i = 0; b >= nb / 4u && i < CTL; i++) {
                xings += (last < 0) != (out[2 * i] < 0) && (b > nb / 4u || i);
                last = out[2 * i];
            }
        }
        hz = xings / 2.0 / (0.75 * nb * CTL / FS);
        printf("examples: SQUARE A3: %.1f Hz from the zero crossings\n", hz);
        check("SQUARE: A3 (MIDI 57) plays 220 Hz (within 1 %)", fabs(hz - 220.0) < 2.2);
    }
    base = ex_sq_render(1, 8, 0, 45);
    for (k = 0, ok = 1; k < 8u; k++) {
        int32_t v = e->presets[1].e[k] < 64 ? 120 : 10;
        r = ex_sq_render(1, k, v, 45);
        if (r.hash == base.hash)
            printf("examples: SQUARE EDIT %u (%s) changed nothing\n", k + 1u, e->edit[k].label);
        ok &= r.hash != base.hash;
    }
    check("SQUARE: each EDIT value changes the sound", ok);
    sq_reset();
}

/* ------------------------------------------------------------ WALK UPDN --- */
static uint32_t in_scale(const track_t *t, uint32_t n)
{
    uint32_t m = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[1];
    return (m >> ((n + 120u - (uint32_t)t->p[P_ROOT]) % 12u)) & 1u;
}
static int32_t degree(const track_t *t, uint32_t n)   /* scale degrees from the ROOT an octave under C4 */
{
    uint32_t r = 48u + (uint32_t)t->p[P_ROOT] % 12u, d = 0;
    if (n < r)
        return -1;
    while (r < n)
        d += in_scale(t, ++r);
    return (int32_t)d;
}
/* the notes slot 0 (T1) plays at steps 0..len-1 (0: none) -> o */
static void played(uint8_t *o, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        step_t tmp;
        const step_t *s = gen_eff(&trk[0], i, &tmp);
        o[i] = s->time == ST_NOTE && s->n ? s->note[0] : 0u;
    }
}
static void g_setup(uint32_t type, uint8_t p0, uint8_t p1, uint8_t p2)
{
    sq_reset();
    gen_reset_all();
    trk[0].p[P_SLEN] = 32;
    trk[0].p[P_ROOT] = 2, trk[0].p[P_SCALE] = 2;       /* (D minor) */
    gen_defaults(&gen[0], type);
    gen[0].p[0] = p0, gen[0].p[1] = p1, gen[0].p[2] = p2;
    gen[0].on = 1, gen[0].mode = GM_REPLACE;
    gen_sync();
}
static void t_gen(void)
{
    uint8_t a[32], b[32], w[32 * 4];
    uint32_t i, ok, n;
    int32_t d0, d1;
    check("WALK UPDN: two more TYPEs, stored as 4 and 5", gen_ntypes() == 3u + FELUCCA_GEN_GRIDS + 2u && GT_EXWALK == 4 &&
          GT_EXUPDN == 5 && gen_has(GT_EXWALK) && gen_has(GT_EXUPDN) && !strcmp(GEN_TYPES[GT_EXWALK].name, "WALK"));
    g_setup(GT_EXWALK, 7, 2, 1);
    played(a, 32);
    played(b, 32);
    for (i = 0, ok = 1; i < 32u; i++) {
        d1 = degree(&trk[0], a[i]);
        ok &= a[i] && in_scale(&trk[0], a[i]) && d1 >= 0 && d1 < 7;
        if (i) {
            d0 = degree(&trk[0], a[i - 1]);
            ok &= abs(d1 - d0) <= 2;
        }
    }
    check("WALK: a note every step, in the scale, within RANGE 7, steps of at most LEAP 2 degrees", ok);
    check("WALK: the same walk twice (a pure function of the values and the step)", !memcmp(a, b, sizeof a));
    gen[0].p[2] = 2;
    played(b, 32);
    check("WALK: another DRAW, another walk", memcmp(a, b, sizeof a) != 0);
    g_setup(GT_EXUPDN, 4, 1, 1);
    played(a, 8);
    for (i = 0, ok = 1; i < 8u; i++) {
        static const int32_t D[8] = {0, 1, 2, 3, 2, 1, 0, 1};
        ok &= degree(&trk[0], a[i]) == D[i];
    }
    check("UPDN: RANGE 4 HOP 1: degrees 0 1 2 3 2 1 0 1", ok);
    g_setup(GT_EXUPDN, 8, 2, 2);
    played(a, 8);
    for (i = 0, ok = 1; i < 8u; i++) {
        static const int32_t D[8] = {0, -1, 2, -1, 4, -1, 6, -1};
        ok &= D[i] < 0 ? !a[i] : degree(&trk[0], a[i]) == D[i];
    }
    check("UPDN: HOP 2 RATE 2: a note every second step (rests between), 0 2 4 6 degrees", ok);
    g_setup(GT_EXWALK, 9, 3, 7);
    played(a, 32);
    check("WRITE: the generator's notes into the pattern", gen_write(&trk[0], 0) != 0);
    for (i = 0, ok = 1; i < 32u; i++)
        ok &= trk[0].step[i].time == ST_NOTE && trk[0].step[i].n == 1u && trk[0].step[i].note[0] == a[i];
    gen[0].on = 0, gen_sync();
    played(b, 32);
    check("WRITE: the pattern holds what played, the generator off plays the same", ok && !memcmp(a, b, sizeof a));
    n = gen_tlv(w);
    gen_reset_all();
    gen_untlv(w, n);
    check("WALK: through GEN_TLV (type 4, its values)", n == 4u && gen[0].type == GT_EXWALK && gen[0].p[0] == 9 &&
          gen[0].p[1] == 3 && gen[0].p[2] == 7);
    {                                                  /* a drum lane: the hits, no note */
        track_t *d = TDRUM;
        dstep_t tmp;
        const dstep_t *s;
        steps_clear(d);
        d->p[P_SLEN] = 16;
        gen_defaults(&gen[NPART + 1], GT_EXUPDN);
        gen[NPART + 1].p[2] = 2, gen[NPART + 1].on = 1;
        gen_sync();
        for (i = 0, ok = 1; i < 16u; i++) {
            s = gen_deff(d, i, &tmp);
            ok &= (uint32_t)dstep_has(s, 1) == !(i & 1u);
        }
        check("UPDN on a drum lane: its hits (RATE 2: every second step), no note", ok);
    }
    gen_reset_all();
}

int main(void)
{
    host_tracks_init();
    t_exgn();
    t_square();
    t_gen();
    printf("examples: %d failed\n", fails);
    return fails;
}
