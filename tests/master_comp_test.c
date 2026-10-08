/* SPDX-License-Identifier: GPL-3.0-only */
/* The master COMP and LIMIT (firmware/src/fx/master_comp/master_comp.c, FELUCCA_MASTER_COMP):
 *   tables     MC_LOG2 / MC_EXP2 against double precision; mc_log2 within 1/256 octave over 16 .. 2^20
 *   curve      the static curve (THRS, RATIO, the 6 dB soft knee) within +-0.5 dB of Giannoulis et al. eq. 4, -40..+12 dB
 *   timing     attack and release reach 63 % of the step in their time constant (+-25 %); AUTO: a short burst lets go
 *              fast, a long squeeze slowly
 *   unity      on but below the knee, and off: every sample as it came, bit for bit
 *   limiter    full-scale bursts, noise, impulses, a sine at up to +18 dB: no sample past each CEIL; unlimited, the
 *              input exactly, 64 samples late
 *   project    the six values through a project and back; a project from before (reserved bytes 0): THRS / CEIL OFF,
 *              the others at their defaults; song sections leave them alone
 *   cost       instructions per sample (proc_pid_rusage): the compressor, the limiter, a busy mix off / on
 * RENDER=dir: a busy mix (16 s) with the comp off, gentle glue, heavy, the limiter only (GAIN 0, 1, 2, 6) and glue + limiter, as WAVs and
 * a GR log (CSV, 10 ms) each, and the numbers (peak, RMS, crest factor, GR). Exit status: the failed checks. */
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
#include <libproc.h>

static int fails;
static void check(int ok, const char *what)
{
    printf("master_comp: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static double db(double x) { return 20.0 * log10(x); }
static double gr_db16(int32_t gr16) { return gr16 * 6.0206 / 65536.0; }

/* ---- tables */
static void t_tables(void)
{
    uint32_t i, a;
    int ok = 1;
    double worst = 0;
    for (i = 0; i <= 64u; i++)
        ok &= MC_LOG2[i] == (uint16_t)lround(4096.0 * log2(1.0 + i / 64.0)) && MC_EXP2[i] == (uint32_t)lround(32768.0 * pow(2.0, i / 64.0));
    ok &= MC_SLOPE[1] == 8192 && MC_SLOPE[5] == 14336 && MC_SLOPE[7] == 16384;
    for (i = 0; i < 6u; i++) {                           /* one-poles: 1 - exp(-1 / (tau 11025)), Q20 */
        static const double A[6] = {0.1, 0.3, 1, 3, 10, 30}, R[6] = {50, 100, 200, 300, 600, 1200};
        ok &= labs(MC_KATK[i] - lround((1 - exp(-1 / (A[i] * 11.025))) * 1048576.0)) <= 1;
        ok &= labs(MC_KREL[i] - lround((1 - exp(-1 / (R[i] * 11.025))) * 1048576.0)) <= 1;
    }
    for (i = 0; i < 7u; i++) {                           /* the ceilings: floor(256 log2 C) - 2 */
        static const double C[7] = {0.1, 0.3, 0.5, 1, 2, 3, 6};
        double c = 32767.0 * pow(10.0, -C[i] / 20.0);
        ok &= MLIM_C8[i] == (int)floor(256.0 * log2(c)) - 2 && MLIM_CLIN[i] == (int32_t)floor(pow(2.0, MLIM_C8[i] / 256.0));
    }
    check(ok, "tables: MC_LOG2, MC_EXP2, the slopes, the time constants, the ceilings as their formulas");
    for (a = 16; a < (1u << 20); a += 1u + a / 97u) {
        double e = fabs(mc_log2(a) - 256.0 * log2((double)a));
        worst = e > worst ? e : worst;
    }
    printf("master_comp: mc_log2 worst error %.3f / 256 octave\n", worst);
    check(worst < 0.6, "mc_log2: within 0.6 / 256 octave (0.014 dB) from 16 to 2^20");
    worst = 0;
    for (i = 0; i < 65536u * 5u; i += 37u) {
        int32_t x = (int32_t)i - 4 * 65536;
        double e = fabs(mc_exp2(x) - 8192.0 * pow(2.0, x / 65536.0));
        worst = e > worst ? e : worst;
    }
    printf("master_comp: mc_exp2 worst error %.2f Q13 steps (-4 .. +1 octave)\n", worst);
    check(worst <= 1.5 && mc_exp2(0) == 8192, "mc_exp2: within 1.5 / 8192 from -4 to +1 octave; 2^0 is exactly 8192 (Q13)");
}

/* ---- a compressor instance of its own on a square wave of amplitude A (the peak detector sees A every sub-block) */
static double theory_gr(double x, double t, double r)   /* Giannoulis et al. eq. 4, W = 6.0206 dB */
{
    double w = 6.0206, s = r > 1e6 ? 1.0 : 1.0 - 1.0 / r, d = x - t;
    if (2 * d < -w)
        return 0;
    if (2 * fabs(d) <= w)
        return s * (d + w / 2) * (d + w / 2) / (2 * w);
    return s * d;
}
static int32_t run_sq(mc_t *c, const mc_set_t *s, int32_t amp, uint32_t n, int32_t *last)
{
    int32_t l[CTL], r[CTL];
    uint32_t i, k;
    for (k = 0; k < n; k += CTL) {
        for (i = 0; i < CTL; i++)
            l[i] = r[i] = (i & 1u) ? -amp : amp;
        mc_run(c, s, l, r, l, r, CTL);
    }
    *last = l[CTL - 2];
    return c->gr16;
}
static void t_curve(void)
{
    static const double RAT[8] = {1.5, 2, 3, 4, 6, 8, 20, 1e9};
    static const int THR[3] = {-6, -15, -30};
    static const int RI[4] = {1, 3, 5, 7};
    double worst = 0;
    int ti, ri, xd;
    for (ti = 0; ti < 3; ti++)
        for (ri = 0; ri < 4; ri++)
            for (xd = -40; xd <= 12; xd++) {
                mc_t c = {0, 0, 8192, 0};
                mc_set_t s;
                int32_t amp = (int32_t)lround(32768.0 * pow(10.0, xd / 20.0)), out;
                double meas, th;
                mc_settings(&s, THR[ti], RI[ri], 0, 0, 0);   /* 0.1 ms, 50 ms */
                run_sq(&c, &s, amp, FS / 2u, &out);
                meas = db((double)amp / out);
                th = theory_gr(db(amp / 32768.0), THR[ti], RAT[RI[ri]]);
                if (fabs(meas - th) > worst)
                    worst = fabs(meas - th);
                if (fabs(meas - th) > 0.5)
                    printf("master_comp: curve THRS %d RATIO %g at %d dB: %.2f dB, theory %.2f\n", THR[ti], RAT[RI[ri]], xd, meas, th);
            }
    printf("master_comp: static curve worst deviation %.3f dB (THRS -6/-15/-30, RATIO 2/4/8/INF, -40..+12 dB)\n", worst);
    check(worst <= 0.5, "static curve within +-0.5 dB of the soft-knee theory");
}

/* time (ms) for the reduction to cross frac of the way from a to b after the step */
static double step_time(int atk, int rel, int up, double frac, double *final)
{
    mc_t c = {0, 0, 8192, 0};
    mc_set_t s;
    int32_t out, lo = 33, hi = 32768;                    /* -60 dB, 0 dB: THRS -20, INF: 20 dB of reduction */
    uint32_t n;
    double g0, g1;
    mc_settings(&s, -20, 7, atk, rel, 0);
    run_sq(&c, &s, up ? lo : hi, 4u * FS, &out);
    g0 = gr_db16(c.slow16 > c.gr16 ? c.slow16 : c.gr16);
    g1 = up ? 20.0 - 6.0206 / 4 * 0 : 0.0;              /* (above the knee: d * 1) */
    *final = g1;
    for (n = 0; n < 6u * FS; n += CTL) {
        double g;
        run_sq(&c, &s, up ? hi : lo, CTL, &out);
        g = gr_db16(c.slow16 > c.gr16 ? c.slow16 : c.gr16);
        if (up ? g >= g0 + (g1 - g0) * frac : g <= g0 + (g1 - g0) * frac)
            return (n + CTL) * 1000.0 / FS;
    }
    return -1;
}
static void t_timing(void)
{
    static const double ATK[6] = {0.1, 0.3, 1, 3, 10, 30}, REL[6] = {50, 100, 200, 300, 600, 1200};
    int k, ok = 1;
    double f;
    for (k = 2; k < 6; k++) {                            /* (0.1, 0.3 ms: shorter than a block of the test) */
        double t = step_time(k, 0, 1, 0.632, &f);
        printf("master_comp: ATK %.1f ms: 63 %% at %.2f ms\n", ATK[k], t);
        ok &= t > 0 && fabs(t - ATK[k]) <= ATK[k] * 0.25 + 0.73;   /* (+ a block: the test's resolution) */
    }
    check(ok, "attack: 63 % of a 20 dB step within its time constant +-25 % (1, 3, 10, 30 ms)");
    ok = 1;
    for (k = 0; k < 6; k++) {
        double t = step_time(0, k, 0, 0.632, &f);
        printf("master_comp: REL %.0f ms: 63 %% recovered at %.1f ms\n", REL[k], t);
        ok &= t > 0 && fabs(t - REL[k]) <= REL[k] * 0.25;
    }
    check(ok, "release: 63 % recovered within its time constant +-25 % (50 .. 1200 ms)");
    {   /* AUTO: 30 ms and 3 s at 0 dB, then quiet: how long until 63 % recovered */
        double ts[2];
        uint32_t b;
        for (b = 0; b < 2u; b++) {
            mc_t c = {0, 0, 8192, 0};
            mc_set_t s;
            int32_t out;
            uint32_t n;
            double g0 = 0;
            mc_settings(&s, -20, 7, 0, 6, 0);
            run_sq(&c, &s, 33, FS, &out);
            run_sq(&c, &s, 32768, b ? 3u * FS : (FS * 30u / 1000u) & ~(CTL - 1u), &out);
            g0 = gr_db16(c.slow16 > c.gr16 ? c.slow16 : c.gr16);
            ts[b] = -1;
            for (n = 0; n < 6u * FS; n += CTL) {
                run_sq(&c, &s, 33, CTL, &out);
                if (gr_db16(c.slow16 > c.gr16 ? c.slow16 : c.gr16) <= g0 * 0.368) {
                    ts[b] = (n + CTL) * 1000.0 / FS;
                    break;
                }
            }
        }
        printf("master_comp: REL AUTO: after 30 ms of squeeze 63 %% recovered at %.0f ms, after 3 s at %.0f ms\n", ts[0], ts[1]);
        check(ts[0] > 0 && ts[0] < 120 && ts[1] > 600, "release AUTO: a short squeeze lets go fast (< 120 ms), a long one slowly (> 600 ms)");
    }
}

/* ---- unity: on below the knee, and off */
static void t_unity(void)
{
    mc_t c = {0, 0, 8192, 0};
    mc_set_t s;
    int32_t l[CTL], r[CTL], l0[CTL], r0[CTL];
    uint32_t k, i;
    int same = 1;
    uint32_t rnd = 1;
    mc_settings(&s, -10, 3, 4, 6, 0);                    /* -10 dB, 4:1: the knee starts at -13 dB (7336) */
    for (k = 0; k < 2000u; k++) {
        for (i = 0; i < CTL; i++) {
            rnd = rnd * 1664525u + 1013904223u;
            l0[i] = l[i] = (int32_t)(rnd >> 16) % 7000;
            r0[i] = r[i] = -(int32_t)(rnd >> 17) % 7000;
        }
        mc_run(&c, &s, l, r, l, r, CTL);
        same &= !memcmp(l, l0, sizeof l) && !memcmp(r, r0, sizeof r);
    }
    check(same, "comp on, the mix below the knee: every sample as it came (bit for bit)");
    song.g[G_CTHR] = 0, song.g[G_CGAIN] = 0, song.g[G_CCEIL] = 0;
    same = 1;
    for (k = 0; k < 100u; k++) {
        for (i = 0; i < CTL; i++)
            l0[i] = l[i] = (int32_t)(k * 977u + i * 131u) - 50000, r0[i] = r[i] = 300000 - (int32_t)(i * 9000u);
        mc_master(l, r, CTL);
        same &= !memcmp(l, l0, sizeof l) && !memcmp(r, r0, sizeof r) && !mlim.on;
    }
    check(same, "THRS OFF, GAIN 0, CEIL OFF: mc_master touches nothing, the limiter is off");
}

/* ---- the limiter */
static void lim_set(int ceil)
{
    int32_t l[CTL] = {0}, r[CTL] = {0};
    song.g[G_CTHR] = 0, song.g[G_CGAIN] = 0, song.g[G_CCEIL] = (int16_t)ceil;
    mlim.on = 0;
    mc_master(l, r, CTL);
}
static void t_limiter(void)
{
    static const double CEIL_DB[7] = {0.1, 0.3, 0.5, 1, 2, 3, 6};
    int ce, sig, ok = 1, exact = 1;
    for (ce = 1; ce <= 7; ce++) {
        double cmax = 32767.0 * pow(10.0, -CEIL_DB[ce - 1] / 20.0), worst = 0;
        for (sig = 0; sig < 6; sig++) {
            uint32_t n, rnd = 12345u + (uint32_t)sig;
            lim_set(ce);
            for (n = 0; n < 3u * FS; n++) {
                int32_t x, y;
                double ph = n * 2.0 * M_PI * 1000.0 / FS;
                rnd = rnd * 1664525u + 1013904223u;
                switch (sig) {
                case 0: x = (n / 441u) & 1u ? ((n & 1u) ? 131071 : -131071) : 0; break;   /* +12 dB square bursts, 10 ms */
                case 1: x = (int32_t)(rnd >> 14) - 131072; break;                       /* noise, +12 dB peaks */
                case 2: x = (rnd >> 24) < 3u ? ((rnd & 256u) ? 262143 : -262143) : 0; break;   /* impulses, +18 dB */
                case 3: x = (int32_t)(65534.0 * sin(ph)); break;                         /* a sine, +6 dB */
                case 4: x = (int32_t)((n % 4410u) * 59u) - 130000; break;                 /* ramps across 0, +12 dB */
                default: x = (n / 4410u) & 1u ? (int32_t)(262143.0 * sin(ph * 0.05)) : (int32_t)(rnd >> 18) - 8192; break;
                }
                y = (sig == 1) ? -x : x;
                mlim_sample(&x, &y);
                if (fabs((double)x) > worst) worst = fabs((double)x);
                if (fabs((double)y) > worst) worst = fabs((double)y);
            }
        }
        printf("master_comp: CEIL -%.1f dB: largest |output| %.0f, the ceiling %.0f (%.3f dB below)\n", CEIL_DB[ce - 1], worst,
               cmax, db(cmax / worst));
        ok &= worst <= cmax;
    }
    check(ok, "limiter: no sample past CEIL (-0.1 .. -6 dB) on bursts, noise, impulses (+18 dB), sines, ramps");
    {   /* quiet: the input exactly, MLIM_N samples late */
        uint32_t n;
        int32_t hist[4096];
        lim_set(2);
        for (n = 0; n < 4096u; n++) {
            int32_t x = (int32_t)(n * 7919u % 50000u) - 25000, y = -x;
            hist[n] = x;
            mlim_sample(&x, &y);
            if (n >= MLIM_N)
                exact &= x == hist[n - MLIM_N] && y == -hist[n - MLIM_N];
        }
    }
    check(exact, "limiter below the ceiling: the input bit for bit, 64 samples (1.45 ms) late");
    lim_set(0);
}

/* ---- the project */
static void t_project(void)
{
    static project_t pj;
    static dlrec_t dl;
    static const int16_t V[6] = {-17, 5, 2, 3, 9, 4};
    uint32_t i;
    int ok = 1;
    host_tracks_init();
    for (i = 0; i < 6u; i++)
        song.g[G_CTHR + i] = V[i];
    proj_capture(&pj, &dl);
    for (i = 0; i < 6u; i++)
        song.g[G_CTHR + i] = GP[G_CTHR + i].def;
    proj_apply(&pj, &dl, 1);
    for (i = 0; i < 6u; i++) {
        if (song.g[G_CTHR + i] != V[i])
            printf("master_comp: project: G_CTHR + %u loaded %d, saved %d\n", i, song.g[G_CTHR + i], V[i]);
        ok &= song.g[G_CTHR + i] == V[i];
    }
    check(ok && sizeof pj.g == 32u * 2u, "project: THRS RATIO ATK REL GAIN CEIL saved and loaded (in the reserved bytes; g[] is 32 as before)");
    {   /* rsv[0] bits 0..2 are the reverb's algorithm (rev_type.c): every COMP bit set leaves them as rev_pack wrote */
        uint8_t r0 = rev_pack(), r1;
        song.g[G_CTHR] = -30, song.g[G_CRAT] = 7, song.g[G_CATK] = 5, song.g[G_CREL] = 5;
        song.g[G_CGAIN] = 15, song.g[G_CCEIL] = 7;
        proj_capture(&pj, &dl);
        r1 = (uint8_t)(pj.rsv[0] & 7u);
        proj_apply(&pj, &dl, 1);
        check(r1 == r0 && rev_pack() == r0 && song.g[G_CGAIN] == 15 && song.g[G_CCEIL] == 7 && song.g[G_CTHR] == -30,
              "project: the reverb's algorithm (rsv[0] bits 0..2) kept beside GAIN 15 / CEIL 7 / THRS -30");
        for (r0 = 0; r0 < 8u; r0++) {                   /* each of the reverb's 3 bits under mc_pack, any COMP value */
            pj.rsv[0] = r0;
            mc_pack(&pj);
            r1 = (uint8_t)(pj.rsv[0] & 7u);
            song.g[G_CGAIN] = song.g[G_CCEIL] = 0;
            mc_unpack(&pj);
            ok &= r1 == r0 && song.g[G_CGAIN] == 15 && song.g[G_CCEIL] == 7;
            pj.rsv[0] = r0, pj.rsv[1] = pj.rsv[2] = 0;  /* COMP all defaults: the byte is the reverb's alone */
            song.g[G_CTHR] = 0, song.g[G_CGAIN] = 0, song.g[G_CCEIL] = 0;
            song.g[G_CRAT] = GP[G_CRAT].def, song.g[G_CATK] = GP[G_CATK].def, song.g[G_CREL] = GP[G_CREL].def;
            mc_pack(&pj);
            ok &= pj.rsv[0] == r0 && pj.rsv[1] == 0 && pj.rsv[2] == 0;
            song.g[G_CTHR] = -30, song.g[G_CRAT] = 7, song.g[G_CATK] = 5, song.g[G_CREL] = 5;
            song.g[G_CGAIN] = 15, song.g[G_CCEIL] = 7;
        }
        check(ok, "project: mc_pack keeps rsv[0] bits 0..2 (each of 8 values) and writes 0 with COMP / LIMIT at their defaults");
    }
    pj.rsv[1] = pj.rsv[2] = 0, pj.rsv[0] &= 7u;          /* a project from before (the reverb's bits kept) */
    song.g[G_CTHR] = -5, song.g[G_CCEIL] = 3;
    proj_apply(&pj, &dl, 1);
    ok = song.g[G_CTHR] == 0 && song.g[G_CCEIL] == 0 && song.g[G_CGAIN] == 0;
    for (i = 0; i < 6u; i++)
        ok &= song.g[G_CTHR + i] == GP[G_CTHR + i].def;
    check(ok && GP[G_CTHR].def == 0 && GP[G_CCEIL].def == 0, "a project from before: THRS OFF, CEIL OFF, GAIN 0, RATIO / ATK / REL their defaults");
    song.g[G_CTHR] = -9;
    proj_apply(&pj, &dl, 0);
    check(song.g[G_CTHR] == -9, "a song section (globals but the drum level kept): the COMP stays");
    song.g[G_CTHR] = 0;
}

/* ---- the busy mix */
static uint64_t insn(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
static void busy_song(void)                             /* 120 BPM: acid bass, pad chords, a lead, a full kit */
{
    static const uint8_t ACID[16] = {36, 36, 48, 36, 0, 39, 36, 46, 36, 0, 48, 43, 36, 39, 0, 41};
    static const uint8_t AM[4] = {57, 60, 64, 67}, FM[4] = {53, 57, 60, 64};
    static const uint8_t LEAD[16] = {76, 0, 79, 0, 81, 0, 79, 76, 0, 74, 0, 76, 79, 0, 84, 0};
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = TDRUM;
    uint32_t i;
    seq_stop();
    transport_req = 0;
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_preset(t3, 3, 0);
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i];
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    t2->p[P_SLEN] = 32;
    t2->p[P_SGATE] = 120;
    for (i = 0; i < 32u; i++)
        put_step(t2, i, i % 8u == 0u ? 4u : 0u, i < 16u ? AM : FM, i % 8u == 0u ? ST_NOTE : i % 8u < 7u ? ST_TIE : ST_REST, 0);
    for (i = 0; i < 16u; i++) {
        uint8_t n = LEAD[i];
        put_step(t3, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
    }
    for (i = 0; i < 16u; i++) {                         /* kick 4 + offbeat ghost, snare + clap, 16th hats, open hats */
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u || i == 11u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38, n[k++] = 39;
        n[k++] = i % 4u == 2u ? 46 : 42;
        put_step(td, i, k, n, ST_NOTE, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    host_drum_rev(24);
    song.master_q12 = getenv("MASTER") ? (uint32_t)atoi(getenv("MASTER")) : 2048u;   /* (the power-on volume, main.c) */
    transport_req = 1;
}
typedef struct { const char *name; int thr, rat, atk, rel, gain, ceil; } mc_case_t;
static void render(const char *dir, const mc_case_t *c, FILE *rep)
{
    char path[600];
    FILE *w, *g;
    const uint32_t frames = 16u * FS;
    uint32_t f, i, over = 0;
    double sq = 0, pk = 0, cmax = c->ceil ? 32767.0 * pow(10.0, -(double[]){0.1, 0.3, 0.5, 1, 2, 3, 6}[c->ceil - 1] / 20.0) : 32767.0;
    double grc_max = 0, grl_max = 0, grc_sum = 0, gr_n = 0, grc_min = 1e9;
    int32_t wpc = 0, wpl = 0;
    snprintf(path, sizeof path, "%s/master-comp-%s.wav", dir, c->name);
    w = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/master-comp-%s-gr.csv", dir, c->name);
    g = fopen(path, "w");
    if (!w || !g) {
        printf("master_comp: cannot write in %s\n", dir);
        fails++;
        return;
    }
    fprintf(g, "time_s,comp_gr_db,limit_gr_db\n");
    busy_song();
    mlim.on = 0;
    mc.gr16 = mc.slow16 = mc.gr_pk = 0, mc.g13 = 8192, mlim.gr_pk = 0;
    song.g[G_CTHR] = (int16_t)c->thr, song.g[G_CRAT] = (int16_t)c->rat, song.g[G_CATK] = (int16_t)c->atk;
    song.g[G_CREL] = (int16_t)c->rel, song.g[G_CGAIN] = (int16_t)c->gain, song.g[G_CCEIL] = (int16_t)c->ceil;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        if (mc.gr_pk > wpc) wpc = mc.gr_pk;
        if (mlim.gr_pk > wpl) wpl = mlim.gr_pk;
        mc.gr_pk = mlim.gr_pk = 0;
        for (i = 0; i < 2u * CTL; i++) {
            double a = fabs((double)o[i]);
            sq += a * a;
            pk = a > pk ? a : pk;
            over += a > cmax;
        }
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
        if ((f / CTL) % 14u == 13u) {                   /* ~10 ms */
            double gc = gr_db16(wpc), gl = wpl * 6.0206 / 256.0;
            fprintf(g, "%.3f,%.2f,%.2f\n", (double)f / FS, gc, gl);
            if (f > 2u * FS) {                           /* (past the first bar) */
                grc_max = gc > grc_max ? gc : grc_max, grl_max = gl > grl_max ? gl : grl_max;
                grc_min = gc < grc_min ? gc : grc_min;
                grc_sum += gc, gr_n++;
            }
            wpc = wpl = 0;
        }
    }
    fclose(w);
    fclose(g);
    {
        double rms = sqrt(sq / (2.0 * frames)), pdb = db(pk / 32768.0), rdb = db(rms / 32768.0);
        printf("master_comp: render %-10s peak %6.2f dB  RMS %6.2f dB  crest %5.2f dB  COMP GR mean %5.2f (%.2f..%.2f) dB  LIMIT GR max %5.2f dB%s\n",
               c->name, pdb, rdb, pdb - rdb, gr_n ? grc_sum / gr_n : 0, gr_n ? grc_min : 0, grc_max, grl_max,
               c->ceil ? (over ? "  OVER THE CEILING" : "  (no sample past the ceiling)") : "");
        fprintf(rep, "| %s | %d | %s | %s | %s | %d | %s | %.2f | %.2f | %.2f | %.2f (%.2f..%.2f) | %.2f |\n", c->name, c->thr,
                N_CRAT[c->rat], N_CATK[c->atk], N_CREL[c->rel], c->gain, N_CCEIL[c->ceil], pdb, rdb, pdb - rdb,
                gr_n ? grc_sum / gr_n : 0, gr_n ? grc_min : 0, grc_max, grl_max);
        if (c->ceil)
            check(!over, "render: no sample past the ceiling in the busy mix");
    }
    song.g[G_CTHR] = song.g[G_CGAIN] = song.g[G_CCEIL] = 0;
}
static void t_cost(void)
{
    static int32_t L[FS], R[FS];
    uint32_t i, k, rnd = 7;
    uint64_t a, b;
    mc_t c = {0, 0, 8192, 0};
    mc_set_t s;
    for (i = 0; i < FS; i++) {
        rnd = rnd * 1664525u + 1013904223u;
        L[i] = (int32_t)(40000.0 * sin(i * 0.031)) + (int32_t)(rnd >> 20) - 2048, R[i] = -L[i] / 2;
    }
    mc_settings(&s, -20, 3, 4, 6, 3);
    a = insn();
    for (k = 0; k < 10u; k++)
        for (i = 0; i < FS; i += CTL)
            mc_run(&c, &s, L + i, R + i, L + i, R + i, CTL);
    b = insn();
    if (a && b) printf("master_comp: cost: compressor %.1f instructions / sample (host, cc -O2)\n", (b - a) / (10.0 * FS));
    lim_set(2);
    a = insn();
    for (k = 0; k < 10u; k++)
        for (i = 0; i < FS; i++) {
            int32_t x = L[i] * 2, y = R[i] * 2;
            mlim_sample(&x, &y);
        }
    b = insn();
    if (a && b) printf("master_comp: cost: limiter %.1f instructions / sample (host, cc -O2)\n", (b - a) / (10.0 * FS));
    lim_set(0);
    {
        uint32_t m, f;
        double ipc[2] = {0, 0};
        for (m = 0; m < 2u; m++) {
            int32_t o[2 * CTL];
            busy_song();
            song.g[G_CTHR] = m ? -12 : 0, song.g[G_CCEIL] = m ? 2 : 0, song.g[G_CGAIN] = m ? 3 : 0;
            for (f = 0; f < FS; f += CTL)
                mix_block(o, CTL);
            a = insn();
            for (f = 0; f < 4u * FS; f += CTL)
                mix_block(o, CTL);
            b = insn();
            ipc[m] = (b - a) / (4.0 * FS);
        }
        if (ipc[0] > 0)
            printf("master_comp: cost: the busy mix %.1f instructions / sample off, %.1f with COMP -12 dB + LIMIT -0.3 dB (+%.1f, +%.2f %%)\n",
                   ipc[0], ipc[1], ipc[1] - ipc[0], 100.0 * (ipc[1] - ipc[0]) / ipc[0]);
        song.g[G_CTHR] = song.g[G_CCEIL] = song.g[G_CGAIN] = 0;
    }
}

int main(void)
{
    const char *dir = getenv("RENDER");
    t_tables();
    t_curve();
    t_timing();
    t_unity();
    t_limiter();
    t_project();
    t_cost();
    if (dir) {
        static const mc_case_t CASES[] = {
            {"off", 0, 1, 4, 6, 0, 0},
            {"glue-light", -6, 1, 4, 6, 1, 0},           /* 2:1, -6 dB, 10 ms, AUTO, +1 dB */
            {"glue", -10, 1, 4, 6, 2, 0},                /* 2:1, -10 dB, 10 ms, AUTO, +2 dB */
            {"heavy", -20, 5, 3, 1, 6, 0},               /* 8:1, -20 dB, 3 ms, 100 ms, +6 dB */
            {"limit-g0", 0, 1, 4, 6, 0, 2},              /* the limiter only at GAIN 0, 1, 2: the mix as it is ... */
            {"limit-g1", 0, 1, 4, 6, 1, 2},
            {"limit-g2", 0, 1, 4, 6, 2, 2},              /* ... into -0.3 dB */
            {"limit", 0, 1, 4, 6, 6, 2},                 /* the limiter only: +6 dB into -0.3 dB */
            {"glue-limit", -10, 1, 4, 6, 4, 2},          /* glue, +4 dB into -0.3 dB */
        };
        char path[600];
        FILE *rep;
        uint32_t i;
        snprintf(path, sizeof path, "%s/measurements.md", dir);
        rep = fopen(path, "w");
        if (rep) {
            fprintf(rep, "| render | THRS | RATIO | ATK | REL | GAIN | CEIL | peak dB | RMS dB | crest dB | COMP GR mean (min..max) dB | LIMIT GR max dB |\n");
            fprintf(rep, "|---|---|---|---|---|---|---|---|---|---|---|---|\n");
            for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++)
                render(dir, &CASES[i], rep);
            fclose(rep);
        }
    }
    printf("master_comp: %d failed\n", fails);
    return fails;
}
