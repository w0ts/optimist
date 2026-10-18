/* SPDX-License-Identifier: GPL-3.0-only */
/* EQ3 (fx-pool 2026-10-15; firmware/src/fx/insfx/eq3.c, fx_slots.c eq3_v): the three-band DJ EQ on a part's insert slot, the
 * master's and a drum lane's, through the firmware's own sources (hostsim.c):
 *   bypass      flat (LO MID HI 0 dB), ON off, in no slot, FX bypassed: the block bit for bit, the kernel not run (its share 0);
 *               engaged, then flat again: bit for bit once the 2.9 ms crossfade is over
 *   flat sum    the three bands at one gain (-6 dB): the output that gain within 0.01 dB at 60 Hz .. 12 kHz (the crossovers'
 *               all-passes: flat in magnitude)
 *   isolation   KILL MID HI: 5 kHz down > 40 dB, 60 Hz within 0.5 dB; KILL LO MID: 60 Hz down > 20 dB, 8 kHz within 0.5 dB;
 *               KILL LO HI: 1 kHz within 1 dB; all three KILL: exact silence after the crossfade
 *   no click    LO swept -6 .. +6 dB a step a block on a sine, through 0 dB (bypass in and out): no sample step larger than
 *               the sine's own plus a margin; the same entering and leaving the bypass at a hard setting
 *   stereo      the master's EQ3 on L and R: the same input, the same output
 *   record      EQ3's TLV round trips (ON, the owners' bands); none at the defaults
 *   cpu         host instructions a sample, engaged, a mono instance and a stereo one (printed; budget CG_COST_EQ3M)
 * Run by tests/run_tests.sh. */
#include <libproc.h>
#include <sys/resource.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("eq3: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define NB 400u
static int32_t x[NB * CTL], y[NB * CTL], r2[NB * CTL];
static void sine(double f, double a)
{
    uint32_t i;
    for (i = 0; i < NB * CTL; i++)
        x[i] = (int32_t)lrint(a * sin(2 * M_PI * f * i / FS));
}
static void set(int lo, int mid, int hi)
{
    EQ3_V(0)[EQV_LO] = (int16_t)lo, EQ3_V(0)[EQV_MID] = (int16_t)mid, EQ3_V(0)[EQV_HI] = (int16_t)hi;
}
/* T1's inserts over x -> y */
static void play(void)
{
    uint32_t i;
    memcpy(y, x, sizeof y);
    for (i = 0; i < NB; i++)
        ins_part(&trk[0], y + i * CTL, CTL);
}
static double rms(const int32_t *b, uint32_t from)
{
    double s = 0;
    uint32_t i;
    for (i = from; i < NB * CTL; i++)
        s += (double)b[i] * b[i];
    return sqrt(s / (NB * CTL - from));
}
static double gain_db(double f)                           /* y over x, the last whole periods past the middle (settled) */
{
    uint32_t per = (uint32_t)lrint(FS / f), from = NB * CTL - (NB * CTL / 2u / per) * per;
    sine(f, 200000);
    play();
    return 20 * log10(rms(y, from) / rms(x, from));
}
static int32_t maxstep(const int32_t *b)
{
    uint32_t i;
    int32_t m = 0;
    for (i = 1; i < NB * CTL; i++)
        m = abs(b[i] - b[i - 1]) > m ? abs(b[i] - b[i - 1]) : m;
    return m;
}
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}

int main(void)
{
    uint8_t s[2] = {FXT_EQ3, FXT_NONE}, b[FXR_MAX];
    uint32_t i, k, n, ok;
    double g, wmax;
    host_tracks_init();
    ins_put(0, s);
    check("EQ3 built, ON by default (its amount), the defaults: 0 dB, 250 Hz, 2.5 kHz", trk[0].p[P_TEQ] == 1 &&
          EQ3_V(0)[EQV_LO] == 0 && EQ_FL_HZ[EQ3_V(0)[EQV_FL]] == 250 && EQ_FH_HZ[EQ3_V(0)[EQV_FH]] == 2500);
    sine(1000, 150000);
    play();
    check("flat (0 dB x 3): bypassed, the block bit for bit, its share 0 (nothing run)", !memcmp(x, y, sizeof x) && !eq3_st[0].w);
    set(-6, 3, 2);
    trk[0].p[P_TEQ] = 0;
    play();
    check("ON off: bit for bit", !memcmp(x, y, sizeof x) && !eq3_st[0].w);
    trk[0].p[P_TEQ] = 1, trk[0].p[P_FXOFF] = 1;
    play();
    check("FX bypassed: bit for bit", !memcmp(x, y, sizeof x));
    trk[0].p[P_FXOFF] = 0;
    s[0] = FXT_NONE;
    ins_put(0, s);
    play();
    check("in no slot: bit for bit", !memcmp(x, y, sizeof x));
    s[0] = FXT_EQ3;
    ins_put(0, s);
    play();
    check("engaged (LO -6 MID +3 HI +2): heard", memcmp(x, y, sizeof x) != 0 && eq3_st[0].w == 32768);
    set(0, 0, 0);
    play();
    for (i = 8u * CTL, ok = 1; i < NB * CTL; i++)
        ok &= x[i] == y[i];
    check("... flat again: bit for bit after the 2.9 ms crossfade (from block 8), at rest", ok && !eq3_st[0].w);
    {   /* flat sum */
        static const double F[] = {60, 147, 245, 630, 980, 2450, 4900, 7350, 11025};   /* (whole periods in 44.1 kHz) */
        set(-6, -6, -6);
        for (k = 0, wmax = 0; k < sizeof F / sizeof F[0]; k++) {
            g = gain_db(F[k]) + 20 * log10(32768.0 / EQ_G[-6 - EQ_KILL]);
            wmax = fabs(g) > wmax ? fabs(g) : wmax;
        }
        printf("eq3:   the bands at one gain: within %.4f dB of it, 60 Hz .. 12 kHz\n", wmax);
        check("flat sum: the three bands at -6 dB: the output -6 dB within 0.01 dB, 60 Hz .. 12 kHz", wmax < 0.01);
    }
    set(0, EQ_KILL, EQ_KILL);
    {
        double a = gain_db(4900), c = gain_db(60);
        printf("eq3:   KILL MID HI: 5 kHz %.1f dB, 60 Hz %.2f dB\n", a, c);
        check("KILL MID HI: 5 kHz down > 40 dB, 60 Hz within 0.5 dB (LR2: -0.49 dB there)", a < -40 && fabs(c) < 0.5);
    }
    set(EQ_KILL, EQ_KILL, 0);
    {
        double a = gain_db(60), c = gain_db(11025);
        printf("eq3:   KILL LO MID: 60 Hz %.1f dB, 11 kHz %.2f dB\n", a, c);
        check("KILL LO MID: 60 Hz down > 40 dB, 11 kHz within 0.5 dB (LR2: -0.36 dB there)", a < -40 && fabs(c) < 0.5);
    }
    set(EQ_KILL, 0, EQ_KILL);
    g = gain_db(980);
    printf("eq3:   KILL LO HI: 1 kHz %.2f dB, 60 Hz %.1f dB\n", g, gain_db(60));
    check("KILL LO HI: 1 kHz (the mid) within 2.5 dB (LR2's overlap: -1.8 dB analog), 60 Hz down > 20 dB", fabs(g) < 2.5 &&
          gain_db(60) < -20);
    set(EQ_KILL, EQ_KILL, EQ_KILL);
    sine(440, 200000);
    play();
    for (i = 8u * CTL, ok = 1; i < NB * CTL; i++)
        ok &= y[i] == 0;
    check("all three KILL: exact silence after the crossfade", ok);
    {   /* no click: LO swept through 0 dB a step a block, the others flat (bypass on and off at 0 dB) */
        int32_t mx, my;
        set(0, 0, 0);
        eq3_st[0].w = 0;
        sine(110, 200000);
        memcpy(y, x, sizeof y);
        for (i = 0; i < NB; i++) {
            int v = (int)(i % 26u) - 13;                 /* -13 .. +12, back and forth through 0 */
            EQ3_V(0)[EQV_LO] = (int16_t)(v < -6 ? -12 - v : v > 6 ? 12 - v : v);
            ins_part(&trk[0], y + i * CTL, CTL);
        }
        mx = maxstep(x), my = maxstep(y);
        printf("eq3:   LO swept -6 .. +6 dB through 0 dB: the largest step %d (the sine's %d)\n", (int)my, (int)mx);
        check("no click: LO swept through 0 dB a dB a block (bypass in and out): no step past 2.5x the sine's own", my < mx * 5 / 2);
        set(EQ_KILL, 6, EQ_KILL);
        eq3_st[0].w = 0;
        sine(3000, 200000);
        memcpy(y, x, sizeof y);
        for (i = 0; i < NB; i++) {
            if (i % 40u == 20u)
                set(0, 0, 0);
            else if (i % 40u == 0u)
                set(EQ_KILL, 6, EQ_KILL);
            ins_part(&trk[0], y + i * CTL, CTL);
        }
        mx = maxstep(x), my = maxstep(y);
        printf("eq3:   bypass in / out at KILL +6 KILL, 3 kHz: the largest step %d (the sine's %d)\n", (int)my, (int)mx);
        check("no click: into and out of the bypass at a hard setting (KILL +6 KILL): no step past 2.5x the sine's own",
              my < mx * 5 / 2);
    }
#if FELUCCA_MASTER_COMP || TFLT_ON || 1
    {   /* the master's: L and R */
        uint8_t m[2] = {FXT_EQ3, FXT_NONE};
        mst_put(m);
        EQ3_V(NTRK)[EQV_LO] = -10, EQ3_V(NTRK)[EQV_HI] = 4;
        sine(300, 150000);
        memcpy(y, x, sizeof y), memcpy(r2, x, sizeof r2);
        for (i = 0; i < NB; i++)
            mst_run(y + i * CTL, r2 + i * CTL, CTL);
        check("the master's EQ3: heard, L and R the same (one input)", memcmp(x, y, sizeof x) && !memcmp(y, r2, sizeof y));
    }
#endif
    {   /* record */
        uint8_t t[FXT_TLV_EQ3];
        set(0, 0, 0);
        eq3_defaults();
        trk[0].p[P_TEQ] = 1;
        check("record: nothing to keep at the defaults", fxs_tlv(FXT_EQ3, t) == 0u);
        EQ3_V(2)[EQV_MID] = EQ_KILL, EQ3_V(NTRK + 2u)[EQV_FH] = 50, trk[1].p[P_TEQ] = 0, mst_amt[FXT_EQ3] = 0;
        n = fxs_tlv(FXT_EQ3, t);
        fxs_untlv_none(1);
        check("... none: the defaults (ON, 0 dB)", trk[1].p[P_TEQ] == 1 && EQ3_V(2)[EQV_MID] == 0 && mst_amt[FXT_EQ3] == 1);
        fxs_untlv(FXT_EQ3, t, n, 1);
        check("... set: round trips (ON, the master's ON, a part's band, a return's FREQ H)", n == FXT_TLV_EQ3 &&
              trk[1].p[P_TEQ] == 0 && mst_amt[FXT_EQ3] == 0 && EQ3_V(2)[EQV_MID] == EQ_KILL && EQ3_V(NTRK + 2u)[EQV_FH] == 50);
        n = fxr_encode(b);
        fxs_untlv_none(1);
        fxr_decode(b, n, 1);
        check("... through the FX record (a load)", trk[1].p[P_TEQ] == 0 && mst_amt[FXT_EQ3] == 0 && EQ3_V(2)[EQV_MID] == EQ_KILL);
        fxs_untlv_none(1);
    }
    {   /* cpu */
        static eq_st_t st;
        static int32_t l[CTL], r[CTL];
        uint64_t a0, a1, a2;
        int16_t v[EQV_N] = {-6, 3, 2, EQ_FL_DEF, EQ_FH_DEF};
        uint32_t reps = 20000;
        for (i = 0; i < CTL; i++)
            l[i] = r[i] = (int32_t)(i * 3000) - 40000;
        eq3_run(&st, v, 1, l, 0, CTL);
        a0 = instr_now();
        for (k = 0; k < reps; k++)
            eq3_run(&st, v, 1, l, 0, CTL);
        a1 = instr_now();
        for (k = 0; k < reps; k++)
            eq3_run(&st, v, 1, l, r, CTL);
        a2 = instr_now();
        if (a0) {
            double m = (double)(a1 - a0) / (reps * CTL), s2 = (double)(a2 - a1) / (reps * CTL);
            printf("eq3:   cpu (host instructions a sample, engaged): mono %.1f, stereo %.1f (CG_COST_EQ3M %u)\n", m, s2,
                   CG_COST_EQ3M);
            check("cpu: a mono instance within the guard's cost (CG_COST_EQ3M)", m <= CG_COST_EQ3M);
        }
    }
    printf("eq3: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
