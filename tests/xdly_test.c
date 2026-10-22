/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of XDLY (firmware/src/fx/insfx/xdly.c: the cross-modulated dual delay, a limited insert type, fxi.c), the
 * firmware's own sources through hostsim.c, built with FELUCCA_INS_XDLY=1 (one instance; run_tests.sh):
 *   1. bypass: in S1 at 0, in no slot, the track's FX bypassed: the block bit for bit; 127 then 0: bit for bit again
 *      within 5 blocks; the drum bus at 0: L and R bit for bit
 *   2. silence in, silence out; a loud sine then silence: the tail ends at exact 0
 *   3. bounded: a full-scale square and noise at FDBK 127: within 2^21
 *   4. TIME: a click comes back TIME 1 / TIME 2 later (MOD 0, all wet); MOD changes the repeats; FDBK 127 sustains
 *      (still sounding 2 s after the click, bounded)
 *   5. stereo: the drum bus gets line 1 on L, line 2 on R (L != R with TIME 1 != TIME 2)
 *   6. the instances: one, so a second track holding XDLY is bypassed (fxi FULL); the master's slot takes it when free
 *   7. the TLV (fx_slots.c fxs_tlv) round trips the MIX and every owner's settings, none at the defaults; the motion's
 *      stored id of the MIX both ways; the slots' type lists hold it; the pages XDLY / MST XDLY shown with it
 *   8. a played note: its repeats heard after its voice has ended (fx.c mix_part keeps the part while XDLY rings),
 *      then silence once the lines are clear
 *   9. cost: host instructions a sample at 127 on a part (printed) */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define N 8192u
static int fails;
static void check(const char *what, int ok)
{
    printf("xdly: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int32_t x[N], y[N], z[N];

static void reset(void)
{
    static const uint8_t E[2] = {FXT_NONE, FXT_NONE};
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        ins_put(k, E);
        trk[k].p[P_FXOFF] = 0;
        trk[k].p[P_TXDLY] = 0;
    }
    mst_put(E);
    mst_amt[FXT_XDLY] = 0;
    xdl_defaults();
    fxi_init();
    fxi_poll();
}
static void set(uint32_t o, int t1, int t2, int mod, int fb)
{
    XDL_V(o)[XDV_T1] = (int16_t)t1, XDL_V(o)[XDV_T2] = (int16_t)t2;
    XDL_V(o)[XDV_MOD] = (int16_t)mod, XDL_V(o)[XDV_FB] = (int16_t)fb;
}
/* XDLY on part 1 (S1, fx.c ins_part) over x -> y, at a0 before sample switch_at, a1 from there */
static void run_part(int32_t a0, int32_t a1, uint32_t switch_at)
{
    uint8_t s[2] = {FXT_XDLY, FXT_NONE};
    uint32_t i;
    ins_put(0, s);
    fxi_poll();
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL) {
        trk[0].p[P_TXDLY] = (int16_t)(i < switch_at ? a0 : a1);
        ins_part(&trk[0], y + i, CTL);
    }
}
static void more_part(void)                            /* N more samples of x through it */
{
    uint32_t i;
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL)
        ins_part(&trk[0], y + i, CTL);
}
static void run_bus(int32_t a)                          /* the drum bus: x on L -> y, its copy on R -> z */
{
    uint8_t s[2] = {FXT_XDLY, FXT_NONE};
    uint32_t i;
    ins_put(TRK_DRUM, s);
    fxi_poll();
    TDRUM->p[P_TXDLY] = (int16_t)a;
    memcpy(y, x, sizeof y), memcpy(z, x, sizeof z);
    for (i = 0; i + CTL <= N; i += CTL)
        ifx_bus(FXT_XDLY, a, y + i, z + i, CTL);
}
static int same(const int32_t *a, const int32_t *b, uint32_t from)
{
    uint32_t i;
    for (i = from; i < N; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int32_t peak(const int32_t *v, uint32_t from)
{
    int32_t m = 0;
    uint32_t i;
    for (i = from; i < N; i++)
        m = abs(v[i]) > m ? abs(v[i]) : m;
    return m;
}
static void sine(double amp, double f)
{
    uint32_t i;
    for (i = 0; i < N; i++)
        x[i] = (int32_t)lrint(amp * sin(2 * M_PI * f * i / FS));
}
static uint32_t first_after(const int32_t *v, uint32_t from, int32_t thr)   /* the first sample past from above thr */
{
    uint32_t i;
    for (i = from; i < N; i++)
        if (abs(v[i]) > thr)
            return i;
    return N;
}

static void t_bypass(void)
{
    uint32_t k;
    reset();
    sine(60000, 220);
    run_part(0, 0, 0);
    check("in S1 at 0 from rest: the block bit for bit", same(x, y, 0));
    reset();
    trk[0].p[P_FXOFF] = 1;
    run_part(127, 127, 0);
    check("at 127 with the track's FX bypassed: bit for bit", same(x, y, 0));
    reset();
    memcpy(y, x, sizeof y);
    trk[0].p[P_TXDLY] = 127;
    for (k = 0; k + CTL <= N; k += CTL)
        ins_part(&trk[0], y + k, CTL);
    check("at 127 in no slot: bit for bit", same(x, y, 0));
    reset();
    run_part(127, 0, N / 2u);
    check("127, then 0: faded out and bit for bit again within 5 blocks", !same(x, y, 0) && same(x, y, N / 2u + 5u * CTL));
    reset();
    run_bus(0);
    check("on the drum bus at 0: L and R bit for bit", same(x, y, 0) && same(x, z, 0));
}

static void t_silence(void)
{
    uint32_t b, i;
    int ok;
    reset();
    memset(x, 0, sizeof x);
    run_part(127, 127, 0);
    ok = !peak(y, 0);
    run_bus(127);
    check("silence in at 127 (a part, the drum bus): silence out", ok && !peak(y, 0) && !peak(z, 0));
    reset();
    sine(200000, 330);
    for (i = N / 2u; i < N; i++)
        x[i] = 0;
    run_part(127, 127, 0);
    memset(x, 0, sizeof x);
    for (b = 0; b < 6u * FS / N; b++)                   /* (FDBK 64: 6 s of silence) */
        more_part();
    for (i = N - 64u, ok = 1; i < N; i++)
        ok &= y[i] == 0;
    check("a loud sine then silence (FDBK 64): back to exact 0 (the tail ends)", ok);
}

static void t_bounded(void)
{
    uint32_t i, st = 12345;
    int ok;
    reset();
    set(0, 60, 80, 127, 127), set(TRK_DRUM, 60, 80, 127, 127);
    for (i = 0; i < N; i++)
        x[i] = (i / 50u) & 1u ? 884000 : -884000;
    run_part(127, 127, 0);
    more_part();
    ok = peak(y, 0) < (1 << 21);
    run_bus(127);
    ok &= peak(y, 0) < (1 << 21) && peak(z, 0) < (1 << 21);
    reset();
    set(0, 60, 80, 127, 127);
    for (i = 0; i < N; i++) {
        st = st * 1664525u + 1013904223u;
        x[i] = (int32_t)(st >> 8) % 884000;
    }
    run_part(127, 127, 0);
    ok &= peak(y, 0) < (1 << 21);
    check("a full-scale square and noise at FDBK 127, MOD 127: within 2^21", ok);
}

/* a click at sample 0, all wet: the first repeat TIME 1 / TIME 2 later (the part hears their mean) */
static void t_time(void)
{
    uint32_t d1 = xd_samples(64), d2 = xd_samples(90), at;
    uint64_t ha, hb;
    int32_t pk;
    uint32_t i, b;
    char w[120];
    reset();
    set(0, 64, 90, 0, 0);
    memset(x, 0, sizeof x);
    x[0] = 400000;
    run_part(127, 127, 0);
    at = first_after(y, 1, 1000);
    snprintf(w, sizeof w, "TIME 1 64 (%u samples), TIME 2 90 (%u): the first repeat at %u, the second's after it", d1, d2, at);
    check(w, at + 3u >= d1 && at <= d1 + 3u && first_after(y, at + 8u, 1000) + 3u >= d2 && first_after(y, at + 8u, 1000) <= d2 + 3u);
    /* MOD: the same input, MOD 0 and 100, FDBK 90: their repeats differ */
    reset();
    set(0, 40, 47, 0, 90);
    sine(200000, 220);
    run_part(127, 127, 0);
    for (i = 0, ha = 1469598103934665603ull; i < N; i++)
        ha = (ha ^ (uint32_t)y[i]) * 1099511628211ull;
    reset();
    set(0, 40, 47, 100, 90);
    run_part(127, 127, 0);
    for (i = 0, hb = 1469598103934665603ull; i < N; i++)
        hb = (hb ^ (uint32_t)y[i]) * 1099511628211ull;
    check("MOD 100 changes the repeats (vs MOD 0)", ha != hb);
    /* FDBK 127: a click sustains (the soft clip holds it), bounded */
    reset();
    set(0, 50, 61, 60, 127);
    memset(x, 0, sizeof x);
    x[0] = 400000;
    run_part(127, 127, 0);
    memset(x, 0, sizeof x);
    for (b = 0; b < 2u * FS / N; b++)
        more_part();
    pk = peak(y, 0);
    snprintf(w, sizeof w, "FDBK 127: a click still sounds 2 s after (peak %d), bounded", pk);
    check(w, pk > 2000 && pk < (1 << 21));
}

static void t_stereo(void)
{
    uint32_t i;
    int diff = 0;
    reset();
    set(TRK_DRUM, 50, 80, 30, 60);
    sine(200000, 220);
    run_bus(127);
    for (i = N / 2u; i < N; i++)
        diff |= y[i] != z[i];
    check("the drum bus: line 1 on L, line 2 on R (L != R, TIME 1 50, TIME 2 80)", diff);
}

static void t_instances(void)
{
    uint8_t s[2] = {FXT_XDLY, FXT_NONE};
    reset();
    ins_put(0, s), ins_put(1, s);
    fxi_poll();
    check("one instance: T1 holds it, T2 is bypassed (FULL)", fxi_state(0, 0) == FXI_OK && fxi_state(1, 0) == FXI_FULL &&
                                                                   ifx_lim_at(FXT_XDLY, 0) != FXI_NONE &&
                                                                   ifx_lim_at(FXT_XDLY, 1) == FXI_NONE);
    reset();
    mst_load(0, FXT_XDLY);
    fxi_poll();
    check("the master's slot takes the free instance (MST_OK)", (MST_OK >> FXT_XDLY & 1u) && ifx_lim_at(FXT_XDLY, IFX_MST) != FXI_NONE);
#ifdef UT_OK
    check("not on a return nor the USB input (RT_OK, UT_OK)", !(RT_OK >> FXT_XDLY & 1u) && !(UT_OK >> FXT_XDLY & 1u));
#else
    check("not on a return (RT_OK)", !(RT_OK >> FXT_XDLY & 1u));
#endif
}

static void t_store(void)
{
    uint8_t o[FXT_TLV_XDLY + 4];
    uint32_t n, k, ok = 1, sid;
    int16_t *vp;
    const param_desc_t *d;
    page_t pg = {"XDLY", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_TXD, FXS_ID_TXD + 1, FXS_ID_TXD + 2, FXS_ID_TXD + 3}};
    reset();
    check("at the defaults: no TLV", fxs_tlv(FXT_XDLY, o) == 0u);
    trk[2].p[P_TXDLY] = 90, mst_amt[FXT_XDLY] = 33;
    set(1, 10, 20, 30, 40), set(NTRK, 127, 0, 5, 126);
    n = fxs_tlv(FXT_XDLY, o);
    reset();
    fxs_untlv(FXT_XDLY, o, n, 1);
    ok = n == FXT_TLV_XDLY && trk[2].p[P_TXDLY] == 90 && mst_amt[FXT_XDLY] == 33 && XDL_V(1)[XDV_T1] == 10 &&
         XDL_V(1)[XDV_FB] == 40 && XDL_V(NTRK)[XDV_T1] == 127 && XDL_V(NTRK)[XDV_FB] == 126 && XDL_V(0)[XDV_T1] == 70;
    check("the TLV round trips the MIX and every owner's TIME 1 TIME 2 MOD FDBK", ok);
    sid = mot_sid(P_TXDLY);
    check("the motion's stored id of the MIX: the second tail's + 5, both ways", sid == AUTO_TAIL2 + 5u && mot_id(sid) == P_TXDLY);
    for (k = 0, ok = 0; k <= FXT_NALPHA; k++)
        ok |= FXT_LISTED(k) == FXT_XDLY;
    check("the slots' type list holds XDLY", ok && (FXT_BUILT & FXT_INSERT) >> FXT_XDLY & 1u);
    reset();
    song.sel = 0;
    ok = !page_shown(&pg);
    {
        uint8_t s[2] = {FXT_XDLY, FXT_NONE};
        ins_put(0, s);
    }
    ok &= page_shown(&pg);
    d = fxs_desc(FXS_ID_TXD, &vp);
    ok &= d && vp == &XDL_V(0)[XDV_T1] && !strcmp(d->label, "TIME 1");
    d = fxs_desc(FXS_ID_MXD + 3u, &vp);
    ok &= d && vp == &XDL_V(NTRK)[XDV_FB];
    check("the XDLY page: shown while a slot holds it, its cells the track's and the master's settings", ok);
    {
        char val[16];
        const char *u = 0;
        param_format(&XD_DESC[XDV_T1], 127, val, &u);
        ok = !strcmp(val, "371") && u && !strcmp(u, "ms");
        param_format(&XD_DESC[XDV_T1], 0, val, &u);
        check("TIME shows ms: 127 371 ms, 0 1.0 ms", ok && !strcmp(val, "1.0"));
    }
}

/* a short note on T1 with XDLY in S1: heard after the voice is free, then silent */
static void t_tail(void)
{
    uint8_t s[2] = {FXT_XDLY, FXT_NONE};
    int32_t out[CTL * 2];
    uint32_t b, i, freed = 0, late = 0, end = 1;
    reset();
    host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_ATK] = 0, trk[0].p[P_DEC] = 10, trk[0].p[P_SUS] = 0, trk[0].p[P_REL] = 0;
    ins_put(0, s);
    fxi_poll();
    trk[0].p[P_TXDLY] = 100;
    set(0, 110, 117, 20, 80);
    trk_note_on(&trk[0], 60, 120);
    for (b = 0; b < 2u * FS / CTL; b++) {
        uint32_t act = 0, k;
        if (b == 8u)
            trk_note_off(&trk[0], 60);
        mix_block(out, CTL);
        for (k = 0; k < NVOICE; k++)
            act |= trk[0].v[k].active;
        if (!act && !freed)
            freed = b;
        for (i = 0; freed && b > freed + 40u && i < 2u * CTL; i++)
            late |= abs(out[i]) > 300;                  /* (the mix's dither: a few LSB of its own) */
    }
    trk[0].p[P_TXDLY] = 0;                              /* (MIX 0: the part goes quiet) */
    for (b = 0; b < FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; b > FS / CTL / 2u && i < 2u * CTL; i++)
            end &= abs(out[i]) < 8;
    }
    check("a played note: its repeats heard after its voice ended, silence after MIX 0", freed && late && end);
}

#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
#else
static uint64_t instr(void) { return 0; }
#endif
static void t_cost(void)
{
    uint64_t a, b;
    uint32_t r;
    reset();
    set(0, 60, 80, 60, 100);
    sine(200000, 220);
    run_part(127, 127, 0);
    a = instr();
    for (r = 0; r < 20u; r++)
        more_part();
    b = instr();
    printf("xdly: cost on a part at 127: %.1f host instructions a sample (with the copy)\n", (double)(b - a) / (20.0 * N));
    if (a && b)
        check("the cost within the CPU guard's CG_COST_XDLY", (double)(b - a) / (20.0 * N) <= CG_COST_XDLY);
}

int main(void)
{
    host_tracks_init();
    t_bypass();
    t_silence();
    t_bounded();
    t_time();
    t_stereo();
    t_instances();
    t_store();
    t_tail();
    t_cost();
    printf("xdly: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
