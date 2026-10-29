/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of CHO, DLY and REV as inserts (2026-10-29: one type each, a send bus in a master return and an insert in a
 * slot; firmware/src/fx/insfx/fxi_send.c, fxi.c, fx_slots.c), the firmware's own sources through hostsim.c. run_tests.sh
 * builds it four ways (INSEND_MODE):
 *   0  a fixed effects memory big enough for some of each (FELUCCA_DLY_ARENA, DLY_LEN 32768): the effects themselves
 *   1  the builder's memory (auto: none of their own; on a send's lent line)
 *   2  an arena (FELUCCA_ARENA_KB): each copy a block of the arena, as many as it holds, then FULL
 *   3  the reverb without the send's ROOM (REV_ROOM 0, FDN8): the REV insert's ROOM built all the same
 *  The checks:
 *   1. the sends bit for bit as before (hashes of fm1-next 47da1b37: cho_step, rev_step, room_step_long, dly_step and
 *      fx_buses with each send fed, all three at once)
 *   2. MIX 0: in S1 from rest, with the FX bypassed, in no slot, after 127 then 0: the block bit for bit; the drum bus too
 *   3. silence in, silence out (a part, the drum bus, the master); a loud burst then silence: back to exact 0
 *   4. CHO: the chorus send's own output on the drum bus at 127 (the same read points, RATE and DEPTH); a part's side
 *      REV: a part's side, the drum bus L != R, its tail follows the send's SIZE; DLY: a click back the send's TIME later
 *   5. a track holding the type as an insert sends nothing to that bus; out of the slot its level is a send's again
 *   6. the instances: as many as the memory holds, the next FULL, given back to a FULL one; the master, the USB input;
 *      not on a return (a return holding one is the send)
 *   7. the slots' lists (a track's, the master's, a return's) hold CHO DLY REV once each by those names, no CHRS ROOM TDLY
 *   8. the FX record: a save and a load round trip the slots and the MIX (tracks, master); records saved by fm1-next
 *      47da1b37 with CHRS ROOM TDLY load as CHO REV DLY with their amounts (CHRS past 64: 64)
 *   9. a played note: REV's tail heard after its voice ended, then silence
 *  10. cost: host instructions (macOS) or nanoseconds (elsewhere) a sample of a copy at 127 */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"           /* (the FX record: fx_rec.c) */
#include <time.h>
#ifndef INSEND_MODE
#define INSEND_MODE 0
#endif

#define N 8192u
static int fails;
static void check(const char *what, int ok)
{
    printf("insend%d: %-100s %s\n", INSEND_MODE, what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int32_t x[N], y[N], z[N];
#define NTY 3u
static const uint8_t TY[NTY] = {FXT_CHO, FXT_DLY, FXT_REV};
static const char *const NM[NTY] = {"CHO", "DLY", "REV"};
_Static_assert(INS_CHO && INS_DLY && INS_REV, "the test: the chorus, the delay and the reverb built");

static void buses_quiet(void)                          /* the send buses' tails gone (a test's before) */
{
    int16_t *c = cho_buf, *d = dly_buf;
#if !FXM_NEED
    rev_tank_clear();                                  /* (with an arena its line may not be placed) */
#endif
    if (c)
        memset(c, 0, CHO_BUF_N * sizeof *c);
    if (d)
        memset(d, 0, DLY_BUF_N * sizeof *d);
    memset(fx.dly_f, 0, sizeof fx.dly_f);
    fx.dly_ec = 0, fx.cho_q = fx.dly_q = fx.rev_q = FX_Q_MAX;
}
static void sends_def(void)                            /* the returns as a new project: CHO DLY REV */
{
    fxs_set(FXS_DEF);
}
static void poll(void)
{
#if FXM_NEED
    fxm_poll();
#endif
    fxi_poll();
}
static void reset(void)
{
    static const uint8_t E[2] = {FXT_NONE, FXT_NONE};
    uint32_t k, j;
    for (k = 0; k < NTRK; k++) {
        ins_put(k, E);
        trk[k].p[P_FXOFF] = 0;
        for (j = 0; j < NTY; j++)
            trk[k].p[FXT_AMT[TY[j]]] = 0;
    }
    mst_put(E);
#if FELUCCA_USB_AUDIO
    ut.slot[0] = ut.slot[1] = FXT_NONE;
#endif
    for (j = 0; j < NTY; j++)
        mst_amt[TY[j]] = 0;
    song.g[G_RSIZE] = 90, song.g[G_RDAMP] = 60, song.g[G_CRATE] = GP[G_CRATE].def, song.g[G_CDEPTH] = GP[G_CDEPTH].def;
    sends_def();
    fxi_init();
    poll();
    buses_quiet();
}
static void gains(void)                                /* the send's ROOM gains for the SIZE DAMP now (fx_buses does) */
{
    int32_t g, lpk;
    room_gains(&g, &lpk);
}
static void slot(uint32_t k, uint32_t t)
{
    uint8_t s[2] = {(uint8_t)t, FXT_NONE};
    ins_put(k, s);
    poll();
}
static void amt(uint32_t k, uint32_t t, int32_t a) { trk[k].p[FXT_AMT[t]] = (int16_t)a; }
/* type t on part 0 (S1, fx.c ins_part) over x -> y, at a0 before sample switch_at, a1 from there */
static void run_part(uint32_t t, int32_t a0, int32_t a1, uint32_t switch_at)
{
    uint32_t i;
    slot(0, t);
    gains();
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL) {
        amt(0, t, i < switch_at ? a0 : a1);
        ins_part(&trk[0], y + i, CTL);
    }
}
static void more_part(void)
{
    uint32_t i;
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL)
        ins_part(&trk[0], y + i, CTL);
}
static void run_bus(uint32_t t, int32_t a)             /* the drum bus: x on L -> y, its copy on R -> z */
{
    uint32_t i;
    slot(TRK_DRUM, t);
    gains();
    amt(TRK_DRUM, t, a);
    memcpy(y, x, sizeof y), memcpy(z, x, sizeof z);
    for (i = 0; i + CTL <= N; i += CTL)
        ifx_bus(t, a, y + i, z + i, CTL);
}
static int same(const int32_t *a, const int32_t *b, uint32_t from)
{
    uint32_t i;
    for (i = from; i < N; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int32_t peak(const int32_t *v, uint32_t from, uint32_t to)
{
    int32_t m = 0;
    uint32_t i;
    for (i = from; i < to; i++)
        m = abs(v[i]) > m ? abs(v[i]) : m;
    return m;
}
static void sine(double amp, double f)
{
    uint32_t i;
    for (i = 0; i < N; i++)
        x[i] = (int32_t)lrint(amp * sin(2 * M_PI * f * i / FS));
}
static uint32_t first_after(const int32_t *v, uint32_t from, int32_t thr)
{
    uint32_t i;
    for (i = from; i < N; i++)
        if (abs(v[i]) > thr)
            return i;
    return N;
}
static uint64_t hs(uint64_t h, int32_t v) { return (h ^ (uint32_t)v) * 1099511628211ull; }

/* the reverb send's ROOM kernels (rev_step, room_step_long) from boot (rm's rounding generator untouched): the hashes of
 * before the refactor (fm1-next 2261f580) */
static __typeof__(rm) rm_boot;                         /* (rm as at boot: its rounding generator's start) */
static void t_rev_same(void)
{
    uint32_t i;
#if FELUCCA_REV_ROOM && INSEND_MODE == 0
    reset();
    rm = rm_boot;
    {
        uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
        uint32_t r;
        int32_t wv = 0, g, lpk;
        for (r = 0; r < 2u; r++) {
            song.g[G_RSIZE] = r ? 120 : 90, song.g[G_RDAMP] = 60;
            rev_tank_clear();
            memset(fx.ap_i, 0, sizeof fx.ap_i), memset(fx.line_i, 0, sizeof fx.line_i);
            room_gains(&g, &lpk);
            for (i = 0; i < 20000u; i++) {
                int32_t in = i < 3000u ? (int32_t)lrint(30000 * sin(2 * M_PI * 300 * i / 44100.0)) : 0, yr, yl;
                yl = r ? room_step_long(in, REV_MOD << 8, g, lpk, &yr, &wv) : rev_step(in, REV_MOD << 8, g, lpk | 1, &yr, &wv);
                h[r] = hs(hs(h[r], yl), yr);
            }
        }
        check("the reverb send's ROOM (rev_step, room_step_long through room_core): its hashes of before",
              h[0] == 0xae9fb75932ce469bull && h[1] == 0xc2755ee1300b852dull);
        rev_tank_clear();
        memset(fx.ap_i, 0, sizeof fx.ap_i), memset(fx.line_i, 0, sizeof fx.line_i);
        song.g[G_RSIZE] = 90;
    }
#endif
    (void)i;
}
/* ---- 1. the sends: their kernels and fx_buses as fm1-next 47da1b37 rendered them */
static void t_send_same(void)
{
    uint64_t hc = 1469598103934665603ull, hb[4] = {1469598103934665603ull, 1469598103934665603ull,
                                                   1469598103934665603ull, 1469598103934665603ull};
    uint32_t i, k, m;
    int32_t wr = 0;
    reset();
    for (i = 0; i < 20000u; i++) {                      /* (the chorus's kernel, read points swept at random-ish) */
        int32_t in = i < 3000u ? (int32_t)lrint(30000 * sin(2 * M_PI * 300 * i / 44100.0)) : 0, yr, yl;
        int32_t r0 = (400 << 8) + (int32_t)(i % 700u) * 97, r1 = (400 << 8) + (int32_t)((i * 3u) % 900u) * 61;
        yl = cho_step(in, r0, r1, &yr, &wr);
        hc = hs(hs(hc, yl), yr);
    }
    for (m = 0; m < 4u; m++) {                          /* (fx_buses: 0 the chorus fed, 1 the delay, 2 the reverb, 3 all) */
        static int32_t c[CTL], d[CTL], r[CTL], wl[CTL], wrr[CTL];
        song.g[G_CRATE] = 40, song.g[G_CDEPTH] = 90, song.g[G_DFDBK] = 80, song.g[G_RSIZE] = m == 3u ? 110 : 90;
        for (k = 0; k < 400u; k++) {
            for (i = 0; i < CTL; i++) {
                int32_t v = k < 60u ? (int32_t)lrint(40000 * sin(2 * M_PI * 220 * (k * CTL + i) / 44100.0)) : 0;
                c[i] = m == 0u || m == 3u ? v : 0, d[i] = m == 1u || m == 3u ? v : 0, r[i] = m == 2u || m == 3u ? v : 0;
            }
            memset(wl, 0, sizeof wl), memset(wrr, 0, sizeof wrr);
            if (fx_buses(c, d, r, wl, wrr, CTL))
                for (i = 0; i < CTL; i++)
                    hb[m] = hs(hs(hb[m], wl[i]), wrr[i]);
        }
    }
    song.g[G_DFDBK] = 60;
#if INSEND_MODE == 0
    check("the sends as before (fm1-next 47da1b37's hashes): cho_step; fx_buses with the chorus, the delay, the reverb, all three",
          hc == 0x6d58f06f1296d559ull && hb[0] == 0xc0509ad6b76b314dull && hb[1] == 0x278c375fff0bddb7ull &&
          hb[2] == 0x7c23d129fd5fa487ull && hb[3] == 0xc7af0d3d4e11c8f1ull);
#elif INSEND_MODE == 3
    check("the sends as before without the send's ROOM (FDN8; fm1-next 47da1b37's hashes)",
          hc == 0x6d58f06f1296d559ull && hb[0] == 0xc0509ad6b76b314dull && hb[1] == 0x278c375fff0bddb7ull &&
          hb[2] == 0x3700f8a194529bf4ull && hb[3] == 0x7d59d470fbeadd63ull);
#else
    (void)hb;
#endif
}

#if INSEND_MODE == 0 || INSEND_MODE == 3
/* ---- 2, 3. dry at 0, silence */
static void t_dry(void)
{
    uint32_t j, k;
    char w[140];
    for (j = 0; j < NTY; j++) {
        int ok;
        reset();
        sine(60000, 220);
        run_part(TY[j], 0, 0, 0);
        ok = same(x, y, 0);
        reset();
        trk[0].p[P_FXOFF] = 1;
        run_part(TY[j], 127, 127, 0);
        ok &= same(x, y, 0);
        reset();
        memcpy(y, x, sizeof y);
        amt(0, TY[j], 127);
        for (k = 0; k + CTL <= N; k += CTL)
            ins_part(&trk[0], y + k, CTL);
        ok &= same(x, y, 0);
        snprintf(w, sizeof w, "%s: MIX 0 in S1, MIX 127 with the FX bypassed or in no slot: the block bit for bit", NM[j]);
        check(w, ok);
        reset();
        run_part(TY[j], 127, 0, N / 2u);
        snprintf(w, sizeof w, "%s: 127, then 0: wet, then bit for bit again within 5 blocks", NM[j]);
        check(w, !same(x, y, 0) && same(x, y, N / 2u + 5u * CTL));
        reset();
        run_bus(TY[j], 0);
        snprintf(w, sizeof w, "%s: on the drum bus at 0: L and R bit for bit", NM[j]);
        check(w, same(x, y, 0) && same(x, z, 0));
    }
}

static void t_silence(void)
{
    uint32_t j, b, i;
    char w[140];
    for (j = 0; j < NTY; j++) {
        int ok;
        reset();
        memset(x, 0, sizeof x);
        run_part(TY[j], 127, 127, 0);
        ok = !peak(y, 0, N);
        run_bus(TY[j], 127);
        ok &= !peak(y, 0, N) && !peak(z, 0, N);
        reset();
        mst_load(0, TY[j]);
        poll();
        mst_amt[TY[j]] = 127;
        memset(y, 0, sizeof y), memset(z, 0, sizeof z);
        for (i = 0; i + CTL <= N; i += CTL)
            ifx_mst(TY[j], 127, y + i, z + i, CTL);
        ok &= !peak(y, 0, N) && !peak(z, 0, N) && ifx_lim_at(TY[j], IFX_MST) != FXI_NONE;
        snprintf(w, sizeof w, "%s: silence in at 127 (a part, the drum bus, the master): silence out", NM[j]);
        check(w, ok);
        reset();
        song.g[G_DFDBK] = 60;
        sine(200000, 330);
        for (i = N / 4u; i < N; i++)
            x[i] = 0;
        run_part(TY[j], 127, 127, 0);
        memset(x, 0, sizeof x);
        for (b = 0; b < 12u * FS / N; b++)              /* (12 s of silence) */
            more_part();
        for (i = 0, ok = 1; i < N; i++)
            ok &= y[i] == 0;
        ok &= TY[j] != FXT_REV || !ifx_rsd[0];
        ok &= TY[j] != FXT_CHO || !ifx_csd[0];
        snprintf(w, sizeof w, "%s: a loud burst then silence: back to exact 0 (the tail ends), the part's side too", NM[j]);
        check(w, ok);
    }
}

/* ---- 4. each effect */
static uint32_t room_tail(int32_t size, int32_t thr)    /* the samples until a burst's tail at 127 falls under thr */
{
    uint32_t b, i, last = 0;
    reset();
    song.g[G_RSIZE] = (int16_t)size;
    sine(200000, 440);
    for (i = 2048u; i < N; i++)
        x[i] = 0;
    run_part(FXT_REV, 127, 127, 0);
    for (i = 0; i < N; i++)
        if (abs(y[i]) > thr)
            last = i;
    memset(x, 0, sizeof x);
    for (b = 1; b < 8u; b++) {
        more_part();
        for (i = 0; i < N; i++)
            if (abs(y[i]) > thr)
                last = b * N + i;
    }
    return last;
}
static void t_rev(void)
{
    uint32_t i, t30, t90, t120;
    int diff = 0, side = 0;
    char w[140];
    reset();
    sine(200000, 220);
    run_part(FXT_REV, 100, 100, 0);
    for (i = 0; i < CTL; i++)
        side |= ifx_rside[0][i] != 0;
    check("REV on a part: its side set (left - right, mixed after the level)", ifx_rsd[0] && side);
    reset();
    run_bus(FXT_REV, 127);
    for (i = N / 2u; i < N; i++)
        diff |= y[i] != z[i];
    check("REV on the drum bus: the tank's left on L, right on R (L != R)", diff);
    t30 = room_tail(30, 200), t90 = room_tail(90, 200), t120 = room_tail(120, 200);
    snprintf(w, sizeof w, "REV follows the send's SIZE: tail %u (30) < %u (90) < %u (120, above the knee)", t30, t90, t120);
    check(w, t30 < t90 && t90 < t120);
}
static void t_dly(void)
{
    uint32_t dl, at, b2, c0 = 256u;                     /* (the click after the MIX's fade-in: all wet) */
    char w[140];
    reset();
    song.g[G_DFDBK] = 0, song.g[G_BPM] = 240;           /* (the default TIME at 240 BPM: under N) */
    dl = delay_samples();
    memset(x, 0, sizeof x);
    x[c0] = 400000;
    run_part(FXT_DLY, 127, 127, 0);
    at = first_after(y, 1, 1000);
    snprintf(w, sizeof w, "DLY: a click (all wet: the dry gone) comes back the send's TIME later (%u samples, at %u)", dl,
             at - c0);
    check(w, c0 + dl < N && at + 2u >= c0 + dl && at <= c0 + dl + 2u);
    reset();
    song.g[G_DFDBK] = 80;
    run_part(FXT_DLY, 127, 127, 0);
    memset(x, 0, sizeof x);
    more_part();
    b2 = first_after(y, 0, 1000) + N;
    snprintf(w, sizeof w, "DLY: FDBK 80 repeats it (the second at %u)", b2 - c0);
    check(w, b2 + 4u >= c0 + 2u * dl && b2 <= c0 + 2u * dl + 4u);
    song.g[G_DFDBK] = 60, song.g[G_BPM] = GP[G_BPM].def;
}
/* CHO on the drum bus at 127 (fed its mid) against the chorus send fed the same: the send's own left and right */
static void t_cho(void)
{
    static int32_t c[CTL], d[CTL], r[CTL], wl[CTL], wr[CTL];
    uint32_t i, k, side = 0;
    int ok = 1, moved;
    reset();
    song.g[G_CRATE] = 50, song.g[G_CDEPTH] = 80;        /* (the delay and the reverb quiet: reset, buses_quiet) */
    for (i = 0; i < N; i++)
        x[i] = 2 * (int32_t)lrint(15000 * sin(2 * M_PI * 330 * i / FS) + 9000 * sin(2 * M_PI * 1210 * i / FS));   /* (even:
                                                        * the bus's mid is the sample itself) */
    run_bus(FXT_CHO, 127);
    memset(cho_buf, 0, CHO_LEN * sizeof *cho_buf);      /* (the send from a clean line, its LFO at 0, as the insert's) */
    fx.cho_w = 0, fx.cho_ph = 0, fx.cho_q = FX_Q_MAX;
    for (k = 0; k + CTL <= N; k += CTL) {
        memcpy(c, x + k, sizeof c), memset(d, 0, sizeof d), memset(r, 0, sizeof r);
        memset(wl, 0, sizeof wl), memset(wr, 0, sizeof wr);
        (void)fx_buses(c, d, r, wl, wr, CTL);
        for (i = 0; k >= 2048u && i < CTL; i++)          /* (after the insert's fade-in: all wet) */
            ok &= y[k + i] == wl[i] && z[k + i] == wr[i];
    }
    check("CHO on the drum bus at 127: the chorus send's own left and right, sample for sample (its RATE, DEPTH, read points)",
          ok && peak(y, 2048u, N) > 1000);
    reset();
    sine(200000, 220);
    run_part(FXT_CHO, 127, 127, 0);
    for (i = 0; i < CTL; i++)
        side |= ifx_cside[0][i] != 0;
    memcpy(z, y, sizeof z);
    reset();
    song.g[G_CDEPTH] = 127;
    run_part(FXT_CHO, 127, 127, 0);
    moved = !same(y, z, 0);
    check("CHO on a part: its side set (mixed after the level); DEPTH changes what it plays (the send's)", ifx_csd[0] && side && moved);
}

/* ---- 5. no send from a track holding the type as an insert */
static void t_no_send(void)
{
    int32_t out[CTL * 2];
    uint32_t j, b, sent[2];
    char w[140];
    for (j = 0; j < NTY; j++) {
        uint32_t s;
        for (s = 0; s < 2u; s++) {
            int32_t *bus = TY[j] == FXT_CHO ? send_c : TY[j] == FXT_DLY ? send_d : send_r;
            reset();
            host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            amt(0, TY[j], 100);
            if (s)
                slot(0, TY[j]);
            trk_note_on(&trk[0], 60, 120);
            for (b = 0, sent[s] = 0; b < 20u; b++) {
                mix_block(out, CTL);
                sent[s] |= peak(bus, 0, CTL) != 0;
            }
            trk_note_off(&trk[0], 60);
            for (b = 0; b < 200u; b++)
                mix_block(out, CTL);
        }
        snprintf(w, sizeof w, "%s 100 on T1: sent to its bus; with %s in T1's S1 (its MIX 100): no send", NM[j], NM[j]);
        check(w, sent[0] && !sent[1]);
    }
}

/* ---- 9. a played note's tail */
static void t_tail(void)
{
    int32_t out[CTL * 2];
    uint32_t b, i, freed = 0, late = 0, end = 1;
    reset();
    host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_ATK] = 0, trk[0].p[P_DEC] = 10, trk[0].p[P_SUS] = 0, trk[0].p[P_REL] = 0;
    slot(0, FXT_REV);
    trk[0].p[P_REV] = 100;
    trk_note_on(&trk[0], 60, 120);
    for (b = 0; b < FS / CTL; b++) {
        uint32_t act = 0, k;
        if (b == 8u)
            trk_note_off(&trk[0], 60);
        mix_block(out, CTL);
        for (k = 0; k < NVOICE; k++)
            act |= trk[0].v[k].active;
        if (!act && !freed)
            freed = b;
        for (i = 0; freed && b > freed + 40u && b < freed + 200u && i < 2u * CTL; i++)
            late |= abs(out[i]) > 300;
    }
    trk[0].p[P_REV] = 0;
    for (b = 0; b < FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; b > FS / CTL / 2u && i < 2u * CTL; i++)
            end &= abs(out[i]) < 8;
    }
    check("a played note: REV's tail heard after its voice ended, silence after MIX 0", freed && late && end);
}
#endif

/* ---- 6. the instances: fill T1 T2 T3 DR and the master with type t, in that order: the first fit (as many as the
 * memory holds, *n of them), the others FULL; one let go: the first FULL takes it */
static const uint32_t OWN[5] = {0, 1, 2, TRK_DRUM, FXI_MST};
static uint32_t owner_state(uint32_t o, uint32_t t) { return o == FXI_MST ? fxi_ostate(FXI_MST, t) : fxi_state(o, 0); }
static int fill(uint32_t t, uint32_t *n)
{
    uint32_t free0 = fxi_free(t), k, ok = 1, full = 5;
    for (k = 0; k < 5u; k++) {
        if (OWN[k] == FXI_MST)
            mst_load(0, t);
        else
            slot(OWN[k], t);
        poll();
    }
    for (k = 0, *n = 0; k < 5u; k++) {
        uint32_t st = owner_state(OWN[k], t);
        if (st == FXI_OK && full == 5u)
            ++*n;
        else if (st == FXI_FULL)
            full = full < k ? full : k;
        else
            ok = 0;
    }
    ok &= *n == (free0 < 5u ? free0 : 5u) && (*n == 5u || (full == *n && fxi_free(t) == 0u));
    if (ok && *n < 5u && *n) {                         /* (T1 lets go: the first FULL one takes it) */
        slot(0, FXT_NONE);
        poll();
        poll();
        ok &= owner_state(OWN[*n], t) == FXI_OK;
    }
    return ok;
}
static void t_instances(void)
{
    uint32_t j, n;
    char w[180];
    for (j = 0; j < NTY; j++) {
        reset();
        uint32_t z0 = FXA_SZ(FXI_J(TY[j])) * FXA_U * 2u;
        int ok = fill(TY[j], &n);
        snprintf(w, sizeof w, "%s: T1 T2 T3 DR and the master take it while the memory holds one (%u B each): %u, the rest FULL; "
                 "one let go: a FULL one takes it", NM[j], z0, n);
#if INSEND_MODE == 1
        ok &= n == 0u;                                 /* (the builder's memory: none of their own, every send in a return) */
#elif INSEND_MODE == 2
        ok &= n >= 1u && (TY[j] == FXT_CHO || n < 5u);   /* (the arena: some, then FULL; CHO's 4 KB: all five fit) */
#else
        ok &= n >= 1u;
#endif
        check(w, ok);
        reset();
        snprintf(w, sizeof w, "%s: in the master's lists and the USB input's; not a return's other type (a return's %s is the send)",
                 NM[j], NM[j]);
#ifdef UT_OK
        check(w, (MST_OK >> TY[j] & 1u) && (UT_OK >> TY[j] & 1u) && !(RT_OK >> TY[j] & 1u) && FXT_SEND_OK(TY[j]));
#else
        check(w, (MST_OK >> TY[j] & 1u) && !(RT_OK >> TY[j] & 1u) && FXT_SEND_OK(TY[j]));
#endif
    }
#if INSEND_MODE == 1
    {                                                  /* the delay out of the returns: its line lent, a REV and a DLY on it */
        static const uint8_t L[FX_NSLOT] = {FXT_NONE, FXT_CHO, FXT_NONE, FXT_REV};
        int32_t out[CTL * 2];
        uint32_t b;
        reset();
        fxs_set(L);
        for (b = 0; b < 4u * DLY_LEN / CTL; b++)        /* (its tail: idle) */
            mix_block(out, CTL);
        slot(0, FXT_REV);
        poll(), poll();
        check("the builder's memory: with the delay in no return, a REV takes the delay's line (lent)",
              fxi_state(0, 0) == FXI_OK && fxi_lent(FXT_REV) == 1u);
        slot(0, FXT_DLY);
        poll(), poll(), poll();
        check("... a DLY takes it in turn (the REV let go)", fxi_state(0, 0) == FXI_OK && fxi_lent(FXT_DLY) == 1u);
    }
    {                                                  /* the chorus out of the returns: its line lent, a CHO on it */
        static const uint8_t L[FX_NSLOT] = {FXT_NONE, FXT_NONE, FXT_DLY, FXT_REV};
        int32_t out[CTL * 2];
        uint32_t b;
        reset();
        fxs_set(L);
        for (b = 0; b < 4u * CHO_LEN / CTL; b++)
            mix_block(out, CTL);
        slot(0, FXT_CHO);
        poll(), poll();
        check("the builder's memory: with the chorus in no return, a CHO takes the chorus's line (lent)",
              fxi_state(0, 0) == FXI_OK && fxi_lent(FXT_CHO) == 1u && ifx_choi[ifx_lim_at(FXT_CHO, 0)].line == cho_buf);
    }
#endif
#if INSEND_MODE == 2
    {
        uint32_t c, any = 0;
        reset();
        slot(0, FXT_REV), slot(1, FXT_REV);
        for (c = 0; c < FXI_CPN; c++)
            any += fxm_cp_p[c] != 0 && arn_ptr(FXM_CP_K(c)) == (void *)fxm_cp_p[c];
        check("the arena: each REV copy a block of its own (two, their pointers the arena's)", any == 2u &&
              ifx_room[ifx_lim_at(FXT_REV, 0)].line != ifx_room[ifx_lim_at(FXT_REV, 1)].line);
        slot(0, FXT_NONE), slot(1, FXT_NONE);
        poll(), poll();
        for (c = 0, any = 0; c < FXI_CPN; c++)
            any |= fxm_cp_p[c] != 0;
        check("... let go: their blocks given back", !any);
    }
#endif
}

/* ---- 7. the lists */
static void t_lists(void)
{
    int16_t *vp;
    uint32_t j, m, k, ok = 1, rt = 0;
    const param_desc_t *d;
    for (j = 0; j < NTY; j++) {
        uint32_t at = 0;
        d = ins_desc(0, 0, &vp);
        for (m = 0; d && m <= (uint32_t)d->max; m++)
            at += ins_list[m] == TY[j] && !strcmp(ins_names[m], NM[j]);
        d = mst_desc(0, &vp);
        for (m = 0; d && m <= (uint32_t)d->max; m++)
            at += (mst_list[m] == TY[j] && !strcmp(mst_names[m], NM[j])) << 2;
        ok &= at == 5u;
    }
    d = ins_desc(0, 0, &vp);
    for (m = 0; d && m <= (uint32_t)d->max; m++)
        ok &= strcmp(ins_names[m], "CHRS") && strcmp(ins_names[m], "ROOM") && strcmp(ins_names[m], "TDLY") &&
              ins_list[m] != FXT_CHRS && ins_list[m] != FXT_ROOM && ins_list[m] != FXT_TDLY;
    d = fxs_desc(1, &vp);                              /* (return A's list: the sends once each) */
    for (m = 0; d && m <= (uint32_t)d->max; m++)
        for (k = 0; k < NTY; k++)
            rt += fxs_list[m] == TY[k] && !strcmp(fxs_names[m], NM[k]);
    check("the slots' lists: a track's and the master's hold CHO DLY REV once each by name (no CHRS ROOM TDLY); a return's the "
          "three sends once each", ok && rt == 3u);
    check("the type ids: CHO 2 DLY 3 REV 4; CHRS 13 ROOM 30 TDLY 31 kept as aliases, no slot holds them",
          FXT_CHO == 2 && FXT_DLY == 3 && FXT_REV == 4 && FXT_CHRS == 13 && FXT_ROOM == 30 && FXT_TDLY == 31 && FXT_N == 32 &&
          FXT_LEGACY(FXT_CHRS) == FXT_CHO && FXT_LEGACY(FXT_ROOM) == FXT_REV && FXT_LEGACY(FXT_TDLY) == FXT_DLY);
}

/* ---- 8. the FX record; fm1-next 47da1b37's records (its tests/zz_gen.c, kept in this test's history): OLD_CR, a
 * build with INS_CHORUS INS_REVERB: T1 S1 CHRS 40 (its CHO send 11), T2 S1 ROOM 90 S2 DIST (its REV send 20), T3 S1
 * FILT S2 CHRS 100 (its CHO send 12), the master S1 ROOM 33 S2 CHRS 50; OLD_TD, INS_TDELAY: T1 S1 DIST S2 TDLY 70 (its
 * DLY send 13), the master S1 FILT S2 TDLY 44 */
static const uint8_t OLD_CR[75] = {0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x0d, 0x04, 0x28, 0x00, 0x64, 0x00, 0x1e, 0x05, 0x00, 0x5a,
    0x00, 0x00, 0x21, 0x44, 0x15, 0x1e, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x1f, 0x0d, 0x00, 0x1e, 0x01,
    0x06, 0x0d, 0x01, 0x06, 0x01, 0x04, 0x06, 0x01, 0x04, 0x06, 0x01, 0x04, 0x06, 0x01, 0x04, 0x06,
    0x7f, 0x7f, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t OLD_TD[69] = {0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x1f, 0x05, 0x46, 0x00, 0x00, 0x00, 0x2c, 0x44, 0x15, 0x06,
    0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x42, 0x1f, 0x01, 0x1f, 0x01, 0x06, 0x01, 0x06, 0x01, 0x06, 0x01, 0x04,
    0x06, 0x01, 0x04, 0x06, 0x01, 0x04, 0x06, 0x01, 0x04, 0x06, 0x7f, 0x7f, 0x7f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00};
static void t_store(void)
{
    uint8_t o[8], rec[FXR_MAX];
    uint32_t n, j;
    int ok;
    char w[160];
    for (j = 0; j < NTY; j++) {
        uint32_t p = FXT_AMT[TY[j]];
        reset();
        TDRUM->p[p] = 0;
        ok = fxs_tlv(TY[j], o) == 0u;
        mst_amt[TY[j]] = 33;
        n = fxs_tlv(TY[j], o);
        reset();
        fxs_untlv(TY[j], o, n, 1);
        ok &= n == 2u && mst_amt[TY[j]] == 33 && o[0] == 0u;
        reset();
        slot(1, TY[j]);
        mst_load(1, TY[j]);
        trk[1].p[p] = 77, mst_amt[TY[j]] = 55;
        n = fxr_encode(rec);
        reset();
        trk[1].p[p] = 77;                              /* (a track's own values: the project's, not the FX record's) */
        (void)fxr_decode(rec, n, 1);
        poll();
        snprintf(w, sizeof w, "%s: the FX record (a save, a load): T2 S1 and the master's S2 hold it, the MIX kept (the track's "
                 "own value, the master's in the type's TLV)", NM[j]);
        check(w, ok && n > 0u && ins_slot[1][0] == TY[j] && mst_slot[1] == TY[j] && trk[1].p[p] == 77 && mst_amt[TY[j]] == 55);
    }
    reset();
    trk[0].p[P_CHOR] = 11, trk[1].p[P_REV] = 20, trk[2].p[P_CHOR] = 12;   /* (the sends the old project had: project.c's) */
    ok = fxr_decode(OLD_CR, sizeof OLD_CR, 1);
    poll();
    check("fm1-next 47da1b37's record with CHRS and ROOM: T1 S1 CHO MIX 40, T2 S1 REV MIX 90 (S2 DIST), T3 S2 CHO MIX 64 (100: "
          "past the middle its depth), the master REV 33 and CHO 50",
          ok && ins_slot[0][0] == FXT_CHO && trk[0].p[P_CHOR] == 40 && ins_slot[1][0] == FXT_REV && ins_slot[1][1] == FXT_DIST &&
              trk[1].p[P_REV] == 90 && ins_slot[2][1] == FXT_CHO && trk[2].p[P_CHOR] == 64 && mst_slot[0] == FXT_REV &&
              mst_amt[FXT_REV] == 33 && mst_slot[1] == FXT_CHO && mst_amt[FXT_CHO] == 50 && !mst_amt[FXT_CHRS]);
    n = fxr_encode(rec);
    reset();
    trk[2].p[P_CHOR] = 64;                             /* (the track's own value, as the project keeps it) */
    ok = fxr_decode(rec, n, 1);
    check("... saved again and loaded: the same (CHO REV ids, no CHRS ROOM TLV written)",
          ok && ins_slot[0][0] == FXT_CHO && ins_slot[1][0] == FXT_REV && mst_slot[0] == FXT_REV && mst_amt[FXT_REV] == 33 &&
              mst_amt[FXT_CHO] == 50 && trk[2].p[P_CHOR] == 64 && !(leg_has & 3u));
    reset();
    trk[0].p[P_DLY] = 13;
    ok = fxr_decode(OLD_TD, sizeof OLD_TD, 1);
    check("fm1-next 47da1b37's record with TDLY: T1 S2 DLY MIX 70, the master S2 DLY 44",
          ok && ins_slot[0][0] == FXT_DIST && ins_slot[0][1] == FXT_DLY && trk[0].p[P_DLY] == 70 && mst_slot[1] == FXT_DLY &&
              mst_amt[FXT_DLY] == 44);
    {                                                  /* (a record with CHRS in a slot but no CHRS TLV: its amount was 0) */
        static const uint8_t S[2] = {FXT_CHRS, FXT_NONE};
        reset();
        ins_put(0, S);
        poll();
        n = fxr_encode(rec);
        reset();
        trk[0].p[P_CHOR] = 99;
        ok = fxr_decode(rec, n, 1) && ins_slot[0][0] == FXT_CHO;
        check("an old CHRS loaded by the editor (an alias in ins_put) is CHO; a record holding it keeps its own MIX", ok);
    }
}

/* ---- 10. cost */
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
#define COST_UNIT "host instructions"
#else
static uint64_t instr(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
#define COST_UNIT "host ns"
#endif
#define COST_ROUNDS 200u
static double cost_part(uint32_t t)
{
    uint64_t a, b, best = ~0ull;
    uint32_t r, k;
    reset();
    sine(200000, 220);
    if (t != FXT_NONE)
        run_part(t, 127, 127, 0);
    for (k = 0; k < 5u; k++) {                          /* (the fastest of five: the least disturbed) */
        a = instr();
        for (r = 0; r < COST_ROUNDS; r++)
            more_part();
        b = instr();
        best = b - a < best ? b - a : best;
    }
    return (double)best / (COST_ROUNDS * (double)N);
}
static void t_cost(void)
{
    double cp = cost_part(FXT_NONE), c[NTY];
    static const uint32_t CG[NTY] = {CG_COST_CHOI, CG_COST_DLYI, CG_COST_REVI};
    static const uint32_t Z[NTY] = {FXA_Z0, FXA_Z6, FXA_Z5};
    static const uint32_t ST[NTY] = {sizeof(ifx_co_t), sizeof(ifx_td_t), sizeof(ifx_rm_t)};
    uint32_t j;
    for (j = 0; j < NTY; j++) {
        c[j] = cost_part(TY[j]) - cp;
        printf("insend%d: %s insert, a copy: %.2f %s a sample (less the copy loop's %.2f); %u B of line (%u units) + %u B state\n",
               INSEND_MODE, NM[j], c[j], COST_UNIT, cp, Z[j] * FXA_U * 2u, Z[j], ST[j]);
    }
#ifdef __APPLE__
    for (j = 0; j < NTY; j++)
        check("the cost within the CPU guard's CG_COST_*I", c[j] <= CG[j]);
#else
    (void)CG;
#endif
}

int main(void)
{
    host_tracks_init();
    rm_boot = rm;
    t_send_same();
    t_rev_same();
#if INSEND_MODE == 0 || INSEND_MODE == 3
    t_dry();
    t_silence();
    t_rev();
    t_dly();
    t_cho();
    t_no_send();
    t_tail();
#endif
    t_instances();
    t_lists();
    t_store();
#if INSEND_MODE == 0 || INSEND_MODE == 3
    t_cost();
#endif
    printf("insend%d: %s\n", INSEND_MODE, fails ? "FAILED" : "all ok");
    return fails != 0;
}
