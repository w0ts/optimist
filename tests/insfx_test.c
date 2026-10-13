/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the further insert types (firmware/src/fx/insfx/insfx.c: FOLD FUZZ PHSR FLNG FRMT WIDE), the firmware's
 * own sources through hostsim.c, built with every one of them (run_tests.sh: -DFELUCCA_INS_*=1). For each type, on a
 * part (fx.c ins_part: S1) and on the drum bus (ifx_bus: L, R):
 *   1. bypass: in a slot at 0 (from rest), in no slot, or its FX bypassed: the block bit for bit; after a fade-out
 *      (127, then 0) bit for bit again within 2 blocks
 *   2. silence in, silence out: 127 from rest on zeros, every sample 0 (and after a loud passage, back to 0)
 *   3. bounded: a full-scale square and white noise (the mix's clamp, +-884000) at 127: every sample within 2^21
 *   4. heard: at 64 / 127 a sine comes out changed (its RMS difference above 3 % of the input's)
 *   5. per type: FOLD and FUZZ add harmonics, FRMT's A and U differ where their first formants are (800 / 350 Hz),
 *      WIDE gives a part a side (L != R after the mix, L + R the mono of the block)
 *   6. the amounts: their TLV (fx_slots.c fxs_tlv) round trips, none when all are 0; the motion's stored ids
 *      (auto.c mot_sid, mot_id) P_ENG_END + 4 .. 9 both ways; the slots' type list holds the six
 *   7. cost: host instructions a sample of each at 127 on a part (printed) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define N 8192u
static int fails;
static void check(const char *what, int ok)
{
    printf("insfx: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static const char *const NAME[FXT_N] = {"", "", "", "", "", "", "", "FOLD", "FUZZ", "PHSR", "FLNG", "FRMT", "WIDE"};
static int32_t x[N], y[N], z[N];

static void sine(double amp, double f)
{
    uint32_t i;
    for (i = 0; i < N; i++)
        x[i] = (int32_t)lrint(amp * sin(2 * M_PI * f * i / FS));
}
static double rms(const int32_t *v, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)v[i] * v[i];
    return sqrt(s / n);
}
static double goertzel(const int32_t *v, uint32_t n, double f)
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double s = v[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1, s1 = s;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

/* every state at rest, every track's slots empty, the amounts 0 */
static void reset(void)
{
    static const uint8_t E[2] = {FXT_NONE, FXT_NONE};
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        ins_put(k, E);
        trk[k].p[P_FXOFF] = 0;
    }
    for (k = P_TFOLD; k <= P_TWIDE; k++)
        trk[0].p[k] = trk[1].p[k] = trk[2].p[k] = TDRUM->p[k] = 0;
    memset(ifx_fold, 0, sizeof ifx_fold), memset(ifx_fuzz, 0, sizeof ifx_fuzz), memset(ifx_phsr, 0, sizeof ifx_phsr);
    memset(ifx_flng, 0, sizeof ifx_flng), memset(ifx_frmt, 0, sizeof ifx_frmt), memset(ifx_wide, 0, sizeof ifx_wide);
    memset(ifx_sd, 0, sizeof ifx_sd);
}
/* type ty on part 1 (S1, through fx.c ins_part) at amount a over x -> y; amt(i): the amount of block i */
static void run_part(uint32_t ty, int32_t a0, int32_t a1, uint32_t switch_at)
{
    uint8_t s[2] = {(uint8_t)ty, FXT_NONE};
    uint32_t i;
    ins_put(0, s);
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL) {
        trk[0].p[FXT_AMT[ty]] = (int16_t)(i < switch_at ? a0 : a1);
        ins_part(&trk[0], y + i, CTL);
    }
}
/* the same on the drum bus: x on L, z (a copy) on R -> y (L), z (R) */
static void run_bus(uint32_t ty, int32_t a)
{
    uint8_t s[2] = {(uint8_t)ty, FXT_NONE};
    uint32_t i;
    ins_put(TRK_DRUM, s);
    TDRUM->p[FXT_AMT[ty]] = (int16_t)a;
    memcpy(y, x, sizeof y), memcpy(z, x, sizeof z);
    for (i = 0; i + CTL <= N; i += CTL)
        ifx_bus(ty, a, y + i, z + i, CTL);
}
static int same(const int32_t *a, const int32_t *b, uint32_t from)
{
    uint32_t i;
    for (i = from; i < N; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int32_t peak(const int32_t *v)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < N; i++)
        m = abs(v[i]) > m ? abs(v[i]) : m;
    return m;
}
static void noise(void)
{
    uint32_t i, st = 12345;
    for (i = 0; i < N; i++) {
        st = st * 1664525u + 1013904223u;
        x[i] = (int32_t)(st >> 8) % 884000;
    }
}

static void t_type(uint32_t ty)
{
    char w[120];
    double d;
    int ok;
    uint32_t i;
    /* 1. bypass */
    reset();
    sine(60000, 220);
    run_part(ty, 0, 0, 0);
    snprintf(w, sizeof w, "%s: in S1 at 0 from rest: the block bit for bit", NAME[ty]);
    check(w, same(x, y, 0));
    reset();
    trk[0].p[P_FXOFF] = 1;
    run_part(ty, 127, 127, 0);
    snprintf(w, sizeof w, "%s: at 127 with the track's FX bypassed: bit for bit", NAME[ty]);
    check(w, same(x, y, 0));
    reset();
    {
        uint32_t k;
        memcpy(y, x, sizeof y);
        trk[0].p[FXT_AMT[ty]] = 127;                    /* (in no slot: its amount unheard) */
        for (k = 0; k + CTL <= N; k += CTL)
            ins_part(&trk[0], y + k, CTL);
        snprintf(w, sizeof w, "%s: at 127 in no slot: bit for bit", NAME[ty]);
        check(w, same(x, y, 0));
    }
    reset();
    run_part(ty, 127, 0, N / 2u);
    snprintf(w, sizeof w, "%s: 127, then 0: faded out (2.9 ms) and bit for bit again within 5 blocks", NAME[ty]);
    check(w, !same(x, y, 0) && same(x, y, N / 2u + 5u * CTL));
    reset();
    run_bus(ty, 0);
    snprintf(w, sizeof w, "%s: on the drum bus at 0: L and R bit for bit", NAME[ty]);
    check(w, same(x, y, 0) && same(x, z, 0));
    reset();
    run_part(ty, 127, 127, 0);                          /* (on, then the part goes silent mid-effect: frozen) */
    trk[0].p[P_FXOFF] = 1;
    ifx_quiet_part(&trk[0]);                            /* (fx.c mix_part's silent branch) */
    ok = !ifx_w(ty, 0);
    trk[0].p[P_FXOFF] = 0;
    run_part(ty, 127, 127, 0);
    ifx_quiet_part(&trk[0]);
    ok &= ifx_w(ty, 0) != 0;                            /* (still on: kept) */
    snprintf(w, sizeof w, "%s: a silent part: switched off (bypassed) at rest at once, still on kept", NAME[ty]);
    check(w, ok);
    /* 2. silence */
    reset();
    memset(x, 0, sizeof x);
    run_part(ty, 127, 127, 0);
    ok = !peak(y);
    run_bus(ty, 127);
    ok &= !peak(y) && !peak(z);
    snprintf(w, sizeof w, "%s: silence in at 127 (a part, the drum bus): silence out", NAME[ty]);
    check(w, ok);
    reset();
    sine(200000, 330);
    for (i = N / 2u; i < N; i++)
        x[i] = 0;
    run_part(ty, 127, 127, 0);
    for (i = N - 64u, ok = 1; i < N; i++)
        ok &= y[i] == 0;
    if (!ok)
        printf("  tail: %d %d %d\n", y[N - 64u], y[N - 2u], y[N - 1u]);
    snprintf(w, sizeof w, "%s: a loud sine then silence at 127: back to exact 0 (the tail ends)", NAME[ty]);
    check(w, ok);
    /* 3. bounded */
    reset();
    for (i = 0; i < N; i++)
        x[i] = (i / 50u) & 1u ? 884000 : -884000;
    run_part(ty, 127, 127, 0);
    ok = peak(y) < (1 << 21);
    run_bus(ty, 127);
    ok &= peak(y) < (1 << 21) && peak(z) < (1 << 21);
    reset();
    noise();
    run_part(ty, 127, 127, 0);
    ok &= peak(y) < (1 << 21);
    snprintf(w, sizeof w, "%s: a full-scale square and noise at 127: within 2^21 (peak %d)", NAME[ty], peak(y));
    check(w, ok);
    /* 4. heard */
    reset();
    sine(60000, 220);
    run_part(ty, 64, 64, 0);
    for (i = 0; i < N; i++)
        z[i] = y[i] - x[i];
    d = rms(z + N / 2u, N / 2u) / rms(x + N / 2u, N / 2u);
    snprintf(w, sizeof w, "%s: heard at 64 (the difference %.0f %% of the sine)", NAME[ty], d * 100);
    check(w, d > 0.03);
}

/* the harmonics (2..15) over the fundamental, % */
static double thd(const int32_t *v, double f0)
{
    double h1 = goertzel(v + N / 2u, N / 2u, f0), hs = 0;
    uint32_t k;
    for (k = 2; k <= 15u; k++)
        hs += pow(goertzel(v + N / 2u, N / 2u, f0 * k), 2);
    return 100 * sqrt(hs) / (h1 + 1e-9);
}

static void t_specific(void)
{
    double t0, t1, a8, a3, u8, u3;
    uint32_t i;
    char w[120];
    /* FOLD, FUZZ: harmonics rise with the amount */
    for (i = 0; i < 2u; i++) {
        uint32_t ty = i ? FXT_FUZZ : FXT_FOLD;
        reset();
        sine(60000, 215.33);                            /* (a bin of N / 2) */
        run_part(ty, 30, 30, 0);
        t0 = thd(y, 215.33);
        reset();
        run_part(ty, 127, 127, 0);
        t1 = thd(y, 215.33);
        snprintf(w, sizeof w, "%s: the harmonics rise with the amount (%.1f %% at 30, %.1f %% at 127)", NAME[ty], t0, t1);
        check(w, t1 > t0 && t1 > 20);
    }
    /* FRMT: A's first formant (800 Hz) against U's (350 Hz) on noise */
    reset();
    noise();
    for (i = 0; i < N; i++)
        x[i] >>= 3;
    run_part(FXT_FRMT, 1, 1, 0);
    a8 = goertzel(y, N, 800) + goertzel(y, N, 810), a3 = goertzel(y, N, 350) + goertzel(y, N, 340);
    reset();
    run_part(FXT_FRMT, 127, 127, 0);
    u8 = goertzel(y, N, 800) + goertzel(y, N, 810), u3 = goertzel(y, N, 350) + goertzel(y, N, 340);
    snprintf(w, sizeof w, "FRMT: A (1) louder at 800 Hz than U (127), U louder at 350 Hz (%.2f, %.2f)", a8 / u8, u3 / a3);
    check(w, a8 > 1.3 * u8 && u3 > 1.3 * a3);
    /* WIDE: a part's side, through the mix (mix_part: L and R), against the same part without it */
    {
        double r[2];
        uint32_t pass;
        for (pass = 0; pass < 2u; pass++) {
            int32_t o[2 * CTL];
            uint32_t b;
            double sd = 0, md = 0;
            uint8_t s[2] = {FXT_WIDE, FXT_NONE};
            host_tracks_init();
            reset();
            host_preset(&trk[0], 0, 0);
            trk[0].p[P_PAN] = 0;
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            trk[0].p[P_SUS] = 127;
            trk_note_on(&trk[0], 57, 100);
            ins_put(0, s);
            trk[0].p[P_TWIDE] = pass ? 127 : 0;
            for (b = 0; b < N / CTL; b++) {
                mix_block(o, CTL);
                for (i = 0; b >= N / CTL / 2u && i < CTL; i++) {
                    sd += (double)(o[2 * i] - o[2 * i + 1]) * (o[2 * i] - o[2 * i + 1]);
                    md += (double)(o[2 * i] + o[2 * i + 1]) * (o[2 * i] + o[2 * i + 1]);
                }
            }
            r[pass] = sqrt(sd / (md + 1));
            if (pass) {
                trk[0].p[P_TWIDE] = 0;
                for (b = 0; b < 8u; b++)
                    mix_block(o, CTL);
                check("WIDE: at 0 again: faded out, no side left", !ifx_sd[0] && !ifx_wide[0].w);
            }
            trk_note_off(&trk[0], 57);
        }
        snprintf(w, sizeof w, "WIDE: a centred part comes out wide (side / mid %.3f, without it %.3f)", r[1], r[0]);
        check(w, r[1] > 0.2 && r[1] > 10 * r[0]);
        reset();
    }
}

static void t_store(void)
{
    uint8_t o[NTRK], s[INS_N];
    uint32_t t, k, ok = 1, n;
    int16_t *vp;
    const param_desc_t *d;
    reset();
    for (t = FXT_NEW0; t < FXT_N; t++)
        ok &= fxs_tlv(t, o) == 0u;
    check("the further inserts' TLVs: none while every amount is 0", ok);
    for (t = FXT_NEW0, ok = 1; t < FXT_N; t++) {
        uint8_t a[NTRK] = {(uint8_t)(t * 3u), 0, 127, (uint8_t)t};
        for (k = 0; k < NTRK; k++)
            trk[k].p[FXT_AMT[t]] = a[k];
        n = fxs_tlv(t, o);
        ok &= n == NTRK && !memcmp(o, a, NTRK);
        for (k = 0; k < NTRK; k++)
            trk[k].p[FXT_AMT[t]] = 5;
        fxs_untlv(t, o, n, 1);
        for (k = 0; k < NTRK; k++)
            ok &= trk[k].p[FXT_AMT[t]] == a[k];
        fxs_untlv(t, o, 2, 1);                          /* (a cut TLV: the rest 0) */
        ok &= trk[2].p[FXT_AMT[t]] == 0 && trk[0].p[FXT_AMT[t]] == a[0];
    }
    check("each type's TLV: T1 T2 T3 DR's amounts round trip; a cut one: the rest 0", ok);
    fxs_untlv_none(1);
    for (t = FXT_NEW0, ok = 1; t < FXT_N; t++)
        for (k = 0; k < NTRK; k++)
            ok &= trk[k].p[FXT_AMT[t]] == 0;
    check("a record without them: every amount 0", ok);
#if FELUCCA_AUTO
    for (t = FXT_NEW0, ok = 1; t < FXT_N; t++)
        ok &= mot_sid(FXT_AMT[t]) == P_ENG_END + 4u + (t - FXT_NEW0) && mot_id(P_ENG_END + 4u + (t - FXT_NEW0)) == FXT_AMT[t] &&
              motion_param(&trk[0], FXT_AMT[t]) && motion_param(TDRUM, FXT_AMT[t]);
    check("motion: FOLD .. WIDE stored as P_ENG_END + 4 .. 9 in every build, both ways; recordable on every track", ok);
#endif
    d = ins_desc(0, 0, &vp);
    for (t = FXT_NEW0, ok = d->max + 1 == INS_NLIST; t < FXT_N; t++) {
        uint32_t i, f = 0;
        for (i = 0; i < INS_NLIST; i++)
            f |= ins_list[i] == t && !strcmp(ins_names[i], NAME[t]);
        ok &= f;
    }
    check("INSERTS' type list: the six, each named after its amount (FOLD FUZZ PHSR FLNG FRMT WIDE)", ok);
    s[0] = FXT_PHSR, s[1] = FXT_FLNG;
    ins_put(1, s);
    ins_load(1, 0, FXT_FLNG);
    check("one type once a track: FLNG into S1 swaps PHSR to S2", ins_slot[1][0] == FXT_FLNG && ins_slot[1][1] == FXT_PHSR);
    reset();
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
static void t_cost(void)
{
    uint32_t t, r;
    for (t = FXT_NEW0; t < FXT_N; t++) {
        uint64_t i0;
        reset();
        sine(60000, 220);
        run_part(t, 127, 127, 0);                       /* (warm) */
        i0 = instr_now();
        for (r = 0; r < 20u; r++)
            run_part(t, 127, 127, 0);
        printf("insfx: cost %s on a part at 127: %.1f host instructions a sample (with the copy)\n", NAME[t],
               (double)(instr_now() - i0) / (20.0 * N));
    }
}

int main(void)
{
    uint32_t t;
    host_tracks_init();
    for (t = FXT_NEW0; t < FXT_N; t++)
        if (FXT_AMT[t] != 0xFFu)
            t_type(t);
    t_specific();
    t_store();
    t_cost();
    printf("insfx: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
