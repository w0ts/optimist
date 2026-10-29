/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of ROOM and TDLY (firmware/src/fx/insfx/fxi_send.c: the reverb send's ROOM and the delay send's delay as
 * limited inserts, fxi.c), the firmware's own sources through hostsim.c, built with FELUCCA_INS_REVERB=1
 * FELUCCA_INS_TDELAY=1 and a fixed effects memory (run_tests.sh: the delay's line, one ROOM and one TDLY of own room):
 *   1. MIX 0: in S1 from rest, in no slot, after 127 then 0 (within 5 blocks): the block bit for bit; the drum bus at 0
 *   2. silence in, silence out (a part, the drum bus, the master); a loud burst then silence: the tail ends at exact 0
 *   3. ROOM: a part gets the tank's left and right as mid and side (the side set, mixed after its level); the drum bus
 *      L != R; the decay follows the send's SIZE (90 against 30: a longer tail), above the knee too (SIZE 120)
 *   4. TDLY: a click comes back the send's TIME later (delay_samples), FDBK repeats it, all wet at 127
 *   5. the instances: the memory holds one of each: a second track holding ROOM is bypassed (FULL); the first lets go,
 *      the second gets it; the master's slot takes them; not on a return nor the USB input
 *   6. a load and save: the TLV and the FX record round trip the MIX (tracks, master) and the slots; the motion ids; the
 *      type lists hold both
 *   7. a played note: ROOM's tail heard after its voice ended, then silence
 *   8. the sends unchanged: the reverb send's tank run through room_core gives what it gave (a fixed hash of a burst)
 *   9. cost: host instructions a sample of an instance at 127, against the reverb and delay sends' (printed) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"           /* (the FX record: fx_rec.c) */

#define N 8192u
static int fails;
static void check(const char *what, int ok)
{
    printf("insend: %-100s %s\n", what, ok ? "ok" : "FAIL");
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
        if (FXT_AMT[FXT_ROOM] != 0xFFu)
            trk[k].p[FXT_AMT[FXT_ROOM]] = 0;
        if (FXT_AMT[FXT_TDLY] != 0xFFu)
            trk[k].p[FXT_AMT[FXT_TDLY]] = 0;
    }
    mst_put(E);
    mst_amt[FXT_ROOM] = mst_amt[FXT_TDLY] = 0;
    song.g[G_RSIZE] = 90, song.g[G_RDAMP] = 60;
    fxi_init();
    fxi_poll();
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
    fxi_poll();
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
/* the types this build has (one of the two a build: a track's 127 values, core.h P_COUNT; run_tests.sh builds each) */
#if INS_ROOM && INS_TDLY
#define NTY 2u
static const uint8_t TY[NTY] = {FXT_ROOM, FXT_TDLY};
static const char *const NM[NTY] = {"ROOM", "TDLY"};
#elif INS_ROOM
#define NTY 1u
static const uint8_t TY[NTY] = {FXT_ROOM};
static const char *const NM[NTY] = {"ROOM"};
#else
#define NTY 1u
static const uint8_t TY[NTY] = {FXT_TDLY};
static const char *const NM[NTY] = {"TDLY"};
#endif

static void t_dry(void)
{
    uint32_t j, k;
    char w[120];
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
    char w[120];
    for (j = 0; j < NTY; j++) {
        int ok;
        reset();
        memset(x, 0, sizeof x);
        run_part(TY[j], 127, 127, 0);
        ok = !peak(y, 0, N);
        run_bus(TY[j], 127);
        ok &= !peak(y, 0, N) && !peak(z, 0, N);
        reset();                                        /* (the memory: one TDLY) */
        mst_load(0, TY[j]);
        fxi_poll();
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
        snprintf(w, sizeof w, "%s: a loud burst then silence: back to exact 0 (the tail ends), the part's side too", NM[j]);
#if INS_ROOM
        ok &= TY[j] != FXT_ROOM || !ifx_rsd[0];
#endif
        check(w, ok);
    }
}

/* the samples until a burst's tail at 127 falls under thr (the part's mid) */
static uint32_t room_tail(int32_t size, int32_t thr)
{
    uint32_t b, i, last = 0;
    reset();
    song.g[G_RSIZE] = (int16_t)size;
    sine(200000, 440);
    for (i = 2048u; i < N; i++)
        x[i] = 0;
    run_part(FXT_ROOM, 127, 127, 0);
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
#if INS_ROOM
static void t_room(void)
{
    uint32_t i, t30, t90, t120;
    int diff = 0, side = 0;
    char w[140];
    reset();
    sine(200000, 220);
    run_part(FXT_ROOM, 100, 100, 0);
    for (i = 0; i < CTL; i++)
        side |= ifx_rside[0][i] != 0;
    check("ROOM on a part: its side set (left - right, mixed after the level)", ifx_rsd[0] && side);
    reset();
    run_bus(FXT_ROOM, 127);
    for (i = N / 2u; i < N; i++)
        diff |= y[i] != z[i];
    check("ROOM on the drum bus: the tank's left on L, right on R (L != R)", diff);
    t30 = room_tail(30, 200), t90 = room_tail(90, 200), t120 = room_tail(120, 200);
    snprintf(w, sizeof w, "ROOM follows the send's SIZE: tail to -60 dB-ish %u (30) < %u (90) < %u (120, above the knee)", t30,
             t90, t120);
    check(w, t30 < t90 && t90 < t120);
}

#endif
#if INS_TDLY
static void t_tdly(void)
{
    uint32_t dl, at, b2, c0 = 256u;                     /* (the click after the MIX's fade-in: all wet) */
    char w[140];
    reset();
    song.g[G_DFDBK] = 0, song.g[G_BPM] = 240;           /* (the default TIME at 240 BPM: under N) */
    gains();
    dl = delay_samples();
    memset(x, 0, sizeof x);
    x[c0] = 400000;
    run_part(FXT_TDLY, 127, 127, 0);
    at = first_after(y, 1, 1000);
    snprintf(w, sizeof w, "TDLY: a click (all wet: the dry gone) comes back the send's TIME later (%u samples, at %u)", dl,
             at - c0);
    check(w, c0 + dl < N && at + 2u >= c0 + dl && at <= c0 + dl + 2u);
    reset();
    song.g[G_DFDBK] = 80;
    run_part(FXT_TDLY, 127, 127, 0);
    memset(x, 0, sizeof x);
    more_part();
    b2 = first_after(y, 0, 1000) + N;
    snprintf(w, sizeof w, "TDLY: FDBK 80 repeats it (the second at %u)", b2 - c0);
    check(w, b2 + 4u >= c0 + 2u * dl && b2 <= c0 + 2u * dl + 4u);
    song.g[G_DFDBK] = 60, song.g[G_BPM] = GP[G_BPM].def;
}

#endif
static void t_instances(void)
{
    uint32_t j;
    char w[160];
    for (j = 0; j < NTY; j++) {
        reset();
        slot(0, TY[j]), slot(1, TY[j]);
        snprintf(w, sizeof w, "%s: the memory full (%u units, %u each): T1 has it, T2 bypassed (FULL), T4 can't take it",
                 NM[j], FXA_I, FXA_SZ(FXI_J(TY[j])));
        check(w, FXA_I == FXA_SZ(FXI_J(TY[j])) && fxi_state(0, 0) == FXI_OK && fxi_state(1, 0) == FXI_FULL &&
                     ifx_lim_at(TY[j], 1) == FXI_NONE && !fxi_can(TRK_DRUM, TY[j]) && fxi_free(TY[j]) == 0u);
        slot(0, FXT_NONE);
        fxi_poll();
        snprintf(w, sizeof w, "%s: T1 lets it go: T2 gets it", NM[j]);
        check(w, fxi_state(1, 0) == FXI_OK);
        reset();
        mst_load(0, TY[j]);
        fxi_poll();
        snprintf(w, sizeof w, "%s: the master's slot takes it; not on a return nor the USB input", NM[j]);
#ifdef UT_OK
        check(w, (MST_OK >> TY[j] & 1u) && ifx_lim_at(TY[j], IFX_MST) != FXI_NONE && !(RT_OK >> TY[j] & 1u) &&
                     !(UT_OK >> TY[j] & 1u));
#else
        check(w, (MST_OK >> TY[j] & 1u) && ifx_lim_at(TY[j], IFX_MST) != FXI_NONE && !(RT_OK >> TY[j] & 1u));
#endif
    }
}

static void t_store(void)
{
    uint8_t o[FXT_TLV_SNDI + 4], rec[FXR_MAX];
    uint32_t n, j, k, ok, sid;
    char w[140];
    for (j = 0; j < NTY; j++) {
        uint32_t p = FXT_AMT[TY[j]], mo = TY[j] == FXT_ROOM ? 9u : 10u;
        reset();
        ok = fxs_tlv(TY[j], o) == 0u;
        trk[2].p[p] = 90, mst_amt[TY[j]] = 33;
        n = fxs_tlv(TY[j], o);
        reset();
        fxs_untlv(TY[j], o, n, 1);
        ok &= n == FXT_TLV_SNDI && trk[2].p[p] == 90 && mst_amt[TY[j]] == 33 && !trk[1].p[p];
        snprintf(w, sizeof w, "%s: its TLV (none at 0) round trips the tracks' and the master's MIX", NM[j]);
        check(w, ok);
        reset();
        slot(1, TY[j]);
        mst_load(1, TY[j]);
        trk[1].p[p] = 77, mst_amt[TY[j]] = 55;
        n = fxr_encode(rec);
        reset();
        (void)fxr_decode(rec, n, 1);
        fxi_poll();
        snprintf(w, sizeof w, "%s: the FX record (a save, a load): T2 S1 and the master's S2 hold it, the MIX kept, instances out", NM[j]);
        check(w, n > 0u && ins_slot[1][0] == TY[j] && mst_slot[1] == TY[j] && trk[1].p[p] == 77 && mst_amt[TY[j]] == 55 &&
                     fxi_state(1, 0) == FXI_OK);
        sid = mot_sid(p);
        snprintf(w, sizeof w, "%s: the motion's stored id of the MIX: the second tail's + %u, both ways", NM[j], mo);
        check(w, sid == AUTO_TAIL2 + mo && mot_id(sid) == p);
        for (k = 0, ok = 0; k <= FXT_NALPHA; k++)
            ok |= FXT_LISTED(k) == TY[j];
        {
            int16_t *vp;
            uint32_t m, at = 0;
            const param_desc_t *d = ins_desc(0, 0, &vp);   /* (the picker's list: INSERTS > S1, the master's) */
            for (m = 0; d && m <= (uint32_t)d->max; m++)
                at |= ins_list[m] == TY[j] && !strcmp(ins_names[m], NM[j]);
            d = mst_desc(0, &vp);
            for (m = 0; d && m <= (uint32_t)d->max; m++)
                at |= (mst_list[m] == TY[j] && !strcmp(mst_names[m], NM[j])) << 1;
            ok &= at == 3u && d->max + 1 <= (int)MST_NLIST;
        }
        snprintf(w, sizeof w, "%s: the slots' type lists (a track's, the master's) hold it by name, an insert built, limited", NM[j]);
        check(w, ok && ((FXT_BUILT & FXT_INSERT) >> TY[j] & 1u) && FXT_IS_LIM(TY[j]) && !strcmp(TP[p].label, NM[j]));
    }
    check("the type ids: ROOM 30, TDLY 31 (EXGN's 29 kept)", FXT_ROOM == 30 && FXT_TDLY == 31 && FXT_N == 32);
}

#if INS_ROOM
static void t_tail(void)
{
    int32_t out[CTL * 2];
    uint32_t b, i, freed = 0, late = 0, end = 1;
    reset();
    host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_ATK] = 0, trk[0].p[P_DEC] = 10, trk[0].p[P_SUS] = 0, trk[0].p[P_REL] = 0;
    slot(0, FXT_ROOM);
    trk[0].p[FXT_AMT[FXT_ROOM]] = 100;
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
    trk[0].p[FXT_AMT[FXT_ROOM]] = 0;
    for (b = 0; b < FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; b > FS / CTL / 2u && i < 2u * CTL; i++)
            end &= abs(out[i]) < 8;
    }
    check("a played note: ROOM's tail heard after its voice ended, silence after MIX 0", freed && late && end);
}
#endif

/* the sends' kernels on a burst (ROOM at SIZE 90 and 120: rev_step, room_step_long; the delay at FDBK 90: dly_step):
 * the hashes of before the refactor (fm1-next 2261f580, the probe's numbers), the sends as they were */
static uint64_t hs(uint64_t h, int32_t v) { return (h ^ (uint32_t)v) * 1099511628211ull; }
static void t_send_same(void)
{
    uint64_t h[3] = {1469598103934665603ull, 1469598103934665603ull, 1469598103934665603ull};
    uint32_t i, r;
    int32_t wv = 0, g, lpk;
    reset();
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
#if FELUCCA_DLY_LEN == 65536
    {
        int32_t fb, dmix, y;
        dlyf_t f;
        uint32_t dl;
        song.g[G_DFDBK] = 90;
        dl = dly_block(&fb, &f, &dmix);
        for (i = 0; i < 40000u; i++) {
            int32_t in = i < 3000u ? (int32_t)lrint(30000 * sin(2 * M_PI * 300 * i / 44100.0)) : 0;
            y = dly_step(in, dl, &f, fb, dmix, &wv);
            h[2] = hs(h[2], y);
        }
        check("the delay send (dly_step through dly_core): its hash of before", h[2] == 0x838b4ade64160940ull);
        song.g[G_DFDBK] = 60;
    }
#endif
    rev_tank_clear();
    memset(fx.ap_i, 0, sizeof fx.ap_i), memset(fx.line_i, 0, sizeof fx.line_i);
    song.g[G_RSIZE] = 90;
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
#if INS_ROOM
#define CG_COST_ROOM_T CG_COST_ROOM
#else
#define CG_COST_ROOM_T 0u
#endif
#if INS_TDLY
#define CG_COST_TDLY_T CG_COST_TDLY
#else
#define CG_COST_TDLY_T 0u
#endif
static double cost_part(uint32_t t)
{
    uint64_t a, b;
    uint32_t r;
    reset();
    sine(200000, 220);
    run_part(t, 127, 127, 0);
    a = instr();
    for (r = 0; r < 20u; r++)
        more_part();
    b = instr();
    return a && b ? (double)(b - a) / (20.0 * N) : 0.0;
}
static double cost_copy(void)                          /* the loop's own copy and ins_part's call, no insert */
{
    uint64_t a, b;
    uint32_t r;
    reset();
    a = instr();
    for (r = 0; r < 20u; r++)
        more_part();
    b = instr();
    return a && b ? (double)(b - a) / (20.0 * N) : 0.0;
}
static double cost_send(int rev)                       /* fx_buses with only the reverb's (or the delay's) send in */
{
    static int32_t c[CTL], d[CTL], r[CTL], wl[CTL], wr[CTL];
    uint64_t a, b;
    uint32_t i, k;
    reset();
    sine(200000, 220);
    a = instr();
    for (k = 0; k < 20u; k++)
        for (i = 0; i + CTL <= N; i += CTL) {
            memset(c, 0, sizeof c);
            memcpy(rev ? r : d, x + i, sizeof r);
            memset(rev ? d : r, 0, sizeof d);
            (void)fx_buses(c, d, r, wl, wr, CTL);
        }
    b = instr();
    return a && b ? (double)(b - a) / (20.0 * N) : 0.0;
}
static void t_cost(void)
{
    double cp = cost_copy(), sr = cost_send(1), sd = cost_send(0), c[NTY];
    uint32_t j;
    for (j = 0; j < NTY; j++) {
        c[j] = cost_part(TY[j]) - cp;
        printf("insend: host instructions a sample: %s insert %.1f (less the copy's %.1f)\n", NM[j], c[j], cp);
    }
    printf("insend: the sends (fx_buses, one bus fed): reverb ROOM %.1f, delay %.1f host instructions a sample\n", sr, sd);
#if INS_ROOM
    printf("insend: ROOM an instance: %u B of the effects memory (%u units) + %u B state\n", FXA_Z5 * FXA_U * 2u, FXA_Z5,
           (unsigned)sizeof(ifx_rm_t));
#endif
#if INS_TDLY
    printf("insend: TDLY an instance: %u B of the effects memory (%u units) + %u B state\n", FXA_Z6 * FXA_U * 2u, FXA_Z6,
           (unsigned)sizeof(ifx_td_t));
#endif
    for (j = 0; cp > 0.0 && j < NTY; j++)
        check(TY[j] == FXT_ROOM ? "ROOM's cost within the CPU guard's CG_COST_ROOM" : "TDLY's cost within the CPU guard's CG_COST_TDLY",
              c[j] <= (TY[j] == FXT_ROOM ? CG_COST_ROOM_T : CG_COST_TDLY_T));
}

int main(void)
{
    host_tracks_init();
    t_send_same();
    t_dry();
    t_silence();
#if INS_ROOM
    t_room();
    t_tail();
#endif
#if INS_TDLY
    t_tdly();
#endif
    t_instances();
    t_store();
    t_cost();
    printf("insend: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
