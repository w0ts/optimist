/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the limited insert types' instances and the effects memory (firmware/src/fx/insfx/fxi.c, fxi_kern.c,
 * fxi_mem.h, 2026-10-23: one memory for the delay send's line and every insert's line), the firmware's own sources
 * through hostsim.c, built with CHRS and ECHO (run_tests.sh; more types in the other builds). The own room is cut to a
 * known size where a check needs one (room(): units taken as if other lines held them):
 *   1. assign: an owner picking CHRS gets an instance and 2 units at the next fxi_poll (none before it: bypassed),
 *      zeroed first; the free count is what the memory still holds (CHRS 5 in the auto 10 KB, ECHO 1)
 *   2. swap: S1 <-> S2 on the track keeps its instance
 *   3. exhaust: a track picking CHRS with no room gets none (fxi_state FULL, fxi_free 0, fxi_can 0) and is bypassed
 *   4. free: a track dropping CHRS gives its line back only after its fade-out (no click), then the waiting track
 *      takes it, cleared (no old line read)
 *   5. reload: a project load hands the instances out in owner order T1 T2 T3 DR whatever came before
 *   6. the UI's queries for types that are not limited (FXI_ANY) and not built (0)
 *   7. the memory: the line sizes; FULL while the free units are not in one run (8 free, no ECHO), placed once they
 *      are; never two lines on a unit
 *   8. lending: the chorus send's line holds CHRS while CHO is in no master slot, the delay send's ECHO
 *   9. JUNO CHORUS, FLNG (the second build): a track and the master, FULL, the side, the TLV; both on the chorus send's
 *      lent line, evicted with a fade when CHO comes back
 *  10. XDLY (the third build: DLY_ARENA 72 KB, the delay send's line 64 KB): only on the send's line, beside an ECHO */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"           /* (the FX record: fx_rec.c) */

static int fails;
static void check(const char *what, int ok)
{
    printf("fxi: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#define NB 64u
static int32_t x[NB * CTL], y[NB * CTL];

static void sine(void)
{
    uint32_t i;
    for (i = 0; i < NB * CTL; i++)
        x[i] = (int32_t)lrint(60000 * sin(2 * M_PI * 220.0 * i / FS));
}
static void slots(uint32_t k, uint32_t a, uint32_t b)
{
    uint8_t s[2] = {(uint8_t)a, (uint8_t)b};
    ins_put(k, s);
}
/* part k's (or the drum bus's) inserts over x -> y, amount a of every limited type on every track */
static void play(uint32_t k, int32_t a)
{
    uint32_t i, j;
    static int32_t r[NB * CTL];
    for (j = 0; j < NTRK; j++)
        trk[j].p[P_TCHRS] = trk[j].p[P_TECHO] = (int16_t)a;
    memcpy(y, x, sizeof y), memcpy(r, x, sizeof r);
    for (i = 0; i < NB; i++) {
        if (k == TRK_DRUM) {
            uint32_t s;
            for (s = 0; s < INS_N; s++)
                if (ins_slot[TRK_DRUM][s] >= FXT_NEW0)
                    ifx_bus(ins_slot[TRK_DRUM][s], fx_on(TDRUM) ? a : 0, y + i * CTL, r + i * CTL, CTL);
        } else {
            ins_part(&trk[k], y + i * CTL, CTL);
        }
    }
}
static int same(void)
{
    return !memcmp(x, y, sizeof x);
}
static void reset(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        slots(k, FXT_NONE, FXT_NONE), trk[k].p[P_FXOFF] = 0;
    fxs_set(FXS_DEF);                                   /* (the master slots: ---- CHO DLY REV) */
    fxi_init();
    fxi_poll();
}
/* the own room cut to r units: the bottom ones taken (as if other lines held them), the top r free */
static void room(uint32_t r)
{
    fxa_mark(FXA_S, FXA_I - r, 1);
}
static int16_t *inst_line(uint32_t j, uint32_t i)      /* instance i of type j's line pointer */
{
#if FELUCCA_INS_CHORUS
    if (j == 0u)
        return ifx_chrs[i].line;
#endif
#if FELUCCA_INS_DELAY
    if (j == 1u)
        return ifx_echo[i].line;
#endif
#if FELUCCA_INS_JCHORUS
    if (j == 2u)
        return ifx_jcho[i].c.ln;
#endif
#if FELUCCA_INS_FLANGER
    if (j == 3u)
        return ifx_flng[i].line;
#endif
#if FELUCCA_INS_XDLY
    if (j == 4u)
        return ifx_xdly[i].line;
#endif
    (void)i;
    return 0;
}
/* every instance out on its own units (marked in use, no unit twice), its line where its first unit is, published */
static int mem_ok(void)
{
    static uint8_t seen[FXA_T + 1u];
    uint32_t j, i, n, u;
    memset(seen, 0, sizeof seen);
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++) {
            if (fxi_own[j][i] == FXI_NONE)
                continue;
            u = fxa_u[j][i];
            if (u + FXA_SZ(j) > FXA_T || inst_line(j, i) != fxa_line(u) || fxi_at[fxi_own[j][i]][j] != i)
                return 0;
            for (n = 0; n < FXA_SZ(j); n++) {
                if (seen[u + n] || !fxa_busy(u + n))
                    return 0;
                seen[u + n] = 1;
            }
        }
    return 1;
}
static int zeros16(const int16_t *p, uint32_t n)
{
    while (n--)
        if (*p++)
            return 0;
    return 1;
}

#if FELUCCA_INS_CHORUS && FELUCCA_INS_DELAY
static void t_assign(void)
{
    uint32_t i0;
    int16_t *top = fxa_line(FXA_N - FXA_Z0);
    reset();
    sine();
    check("built: the auto memory (one line of each type: 10 KB) holds CHRS 5 at once, ECHO 1; nothing out",
          FXA_I == 10u && fxi_cap(FXT_CHRS) == 5u && fxi_cap(FXT_ECHO) == 1u && fxi_free(FXT_CHRS) == 5u &&
          fxi_used(FXT_ECHO) == 0u && FXI_TN[0] == FXI_NK);
    memset(fxa_line(FXA_S), 0x5A, FXA_I * FXA_U * 2u);  /* (an old line in every unit of the own room) */
    slots(1, FXT_CHRS, FXT_NONE);
    check("picked: no instance before the main loop's pass (WAIT, bypassed)", fxi_state(1, 0) == FXI_WAIT &&
          ifx_lim_at(FXT_CHRS, 1) == FXI_NONE);
    play(1, 90);
    check("... the part bit for bit meanwhile", same());
    fxi_poll();
    i0 = ifx_lim_at(FXT_CHRS, 1);
    check("after fxi_poll: an instance (OK), one used, 4 more fit", i0 != FXI_NONE && fxi_state(1, 0) == FXI_OK &&
          fxi_used(FXT_CHRS) == 1u && fxi_free(FXT_CHRS) == 4u && fxi_free(FXT_ECHO) == 1u);
    check("... its line the own room's top 2 units, zeroed when handed out, the units below untouched (0x5A)",
          ifx_chrs[i0].line == top && zeros16(top, IFX_CH_LEN) && !ifx_chrs[i0].w && top[-1] == 0x5A5A && mem_ok());
    play(1, 90);
    check("... heard", !same());
    slots(1, FXT_NONE, FXT_CHRS);
    fxi_poll();
    check("S1 -> S2 on the track: the same instance", ifx_lim_at(FXT_CHRS, 1) == i0 && fxi_state(1, 1) == FXI_OK);
    slots(1, FXT_ECHO, FXT_CHRS);
    fxi_poll();
    check("ECHO beside it on the same track: its own 8 units, the memory full (CHRS 1 of 1, ECHO 0 free)",
          ifx_lim_at(FXT_ECHO, 1) != FXI_NONE && fxi_free(FXT_ECHO) == 0u && fxi_free(FXT_CHRS) == 0u &&
          fxi_cap(FXT_CHRS) == 1u && mem_ok() && zeros16(ifx_echo[ifx_lim_at(FXT_ECHO, 1)].line, IFX_EC_LEN));
}

static void t_exhaust(void)
{
    uint32_t i;
    reset();
    room(2u * FXA_Z0);                                  /* (room for two CHRS) */
    sine();
    slots(0, FXT_CHRS, FXT_NONE), slots(2, FXT_CHRS, FXT_NONE);
    fxi_poll();
    check("room for two: both out", fxi_free(FXT_CHRS) == 0u && fxi_state(0, 0) == FXI_OK && fxi_state(2, 0) == FXI_OK);
    check("... a third track could not have one (fxi_can 0), one holding it can", !fxi_can(1, FXT_CHRS) &&
          fxi_can(0, FXT_CHRS) && fxi_can(1, FXT_DIST) == FELUCCA_FX_DIST);
    slots(TRK_DRUM, FXT_CHRS, FXT_NONE);
    fxi_poll();
    check("a third (the drum track): no room left, FULL", fxi_state(TRK_DRUM, 0) == FXI_FULL &&
          ifx_lim_at(FXT_CHRS, TRK_DRUM) == FXI_NONE);
    play(TRK_DRUM, 127);
    check("... the drum bus bypassed bit for bit", same());
    play(0, 127);
    slots(0, FXT_NONE, FXT_NONE);
    fxi_poll();
    check("T1 drops CHRS while it sounds: kept until it has faded (no click), the drum track still FULL",
          ifx_lim_at(FXT_CHRS, 0) != FXI_NONE && fxi_state(TRK_DRUM, 0) == FXI_FULL && fxi_pend);
    for (i = 0; i < 8u; i++)
        ins_part(&trk[0], y + i * CTL, CTL);            /* (in no slot: it fades out at 0) */
    fxi_poll();
    check("... faded: back, and the drum track has it now (OK)", ifx_lim_at(FXT_CHRS, 0) == FXI_NONE &&
          fxi_state(TRK_DRUM, 0) == FXI_OK && !fxi_pend && mem_ok());
    {
        uint32_t j = ifx_lim_at(FXT_CHRS, TRK_DRUM);
        check("... its line cleared when handed on (T1's sound not in it)", zeros16(ifx_chrs[j].line, IFX_CH_LEN) &&
              !ifx_chrs[j].w);
    }
    play(TRK_DRUM, 127);
    check("... the drum bus heard", !same());
}

/* the instances' owners after a load of the current slots */
static void owners(uint8_t *o)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        o[k] = (uint8_t)(ifx_lim_at(FXT_CHRS, k) != FXI_NONE);
}
static void t_reload(void)
{
    uint8_t a[NTRK], b[NTRK], rec[FXR_MAX];
    uint32_t n, k, ok = 1;
    reset();
    room(2u * FXA_Z0);
    /* history: T3 and DR take the two first, then T1 asks (FULL) */
    slots(2, FXT_CHRS, FXT_NONE), slots(TRK_DRUM, FXT_CHRS, FXT_NONE);
    fxi_poll();
    slots(0, FXT_DIST, FXT_CHRS);
    fxi_poll();
    check("before the load: T3 and DR have them, T1 FULL", fxi_state(0, 1) == FXI_FULL && fxi_state(2, 0) == FXI_OK &&
          fxi_state(TRK_DRUM, 0) == FXI_OK);
    trk[0].p[P_TCHRS] = 40;
    n = fxr_encode(rec);
    check("the FX record keeps the slots' types (no instance index): T1 S2 CHRS", n > 0u);
    (void)fxr_decode(rec, n, 1);                        /* (a load) */
    fxi_poll();
    owners(a);
    check("loaded: T1 and T3 have them (owner order), DR FULL", a[0] && !a[1] && a[2] && !a[3] &&
          fxi_state(TRK_DRUM, 0) == FXI_FULL && trk[0].p[P_TCHRS] == 40 && ins_slot[0][1] == FXT_CHRS && mem_ok());
    /* another history, the same project loaded: the same owners */
    fxi_init();
    room(2u * FXA_Z0);
    slots(TRK_DRUM, FXT_CHRS, FXT_NONE);
    fxi_poll();
    (void)fxr_decode(rec, n, 1);
    fxi_poll();
    owners(b);
    for (k = 0; k < NTRK; k++)
        ok &= a[k] == b[k];
    check("the same project loaded after another history: the same tracks have them", ok);
    (void)fxr_decode(0, 0, 1);                          /* (a project without a record: the defaults) */
    fxi_poll();
    check("a project without a record (old, or new): no limited type held, the memory free again",
          fxi_free(FXT_CHRS) == 2u && fxi_used(FXT_CHRS) == 0u && fxi_used(FXT_ECHO) == 0u && ins_slot[0][0] != FXT_CHRS);
}

/* 7. the memory: sizes, a run of free units, never two lines on a unit */
static void t_mem(void)
{
    uint32_t k, e;
    check("the units: 1 KB (512 samples); CHRS 2, ECHO 8, JUNO CHORUS 2, FLNG 1; the delay send's line its first units, "
          "the chorus send's its last", FXA_U == 512u && FXA_Z0 == 2u && FXA_Z1 == 8u && FXA_Z2 == 2u && FXA_Z3 == 1u &&
          FXA_S == FELUCCA_DLY_LEN / 512u && FXC_S == CHO_LEN / 512u && fxa_line(0) == dly_buf &&
          fxa_line(FXA_N) == cho_buf && fxa_line(FXA_S) == fxa_ins);
    reset();
    for (k = 0; k < NTRK; k++)
        slots(k, FXT_CHRS, FXT_NONE);
    fxi_poll();
    slots(0, FXT_CHRS, FXT_ECHO);
    fxi_poll();
    check("4 CHRS (8 units of 10): ECHO FULL (2 free)", fxi_state(0, 1) == FXI_FULL && fxi_free(FXT_ECHO) == 0u &&
          fxi_free(FXT_CHRS) == 1u && mem_ok());
    slots(1, FXT_NONE, FXT_NONE), slots(TRK_DRUM, FXT_NONE, FXT_NONE), slots(0, FXT_NONE, FXT_ECHO);
    fxi_poll();
    check("T1 T2 DR give theirs back (silent: at once): 8 units free but not in one run, ECHO still FULL",
          fxi_used(FXT_CHRS) == 1u && fxi_state(0, 1) == FXI_FULL && fxi_free(FXT_ECHO) == 0u && fxi_free(FXT_CHRS) == 4u &&
          mem_ok());
    slots(2, FXT_NONE, FXT_NONE);
    fxi_poll();
    fxi_poll();
    e = ifx_lim_at(FXT_ECHO, 0);
    check("T3 gives its back: one run of 10, ECHO placed (OK) from the top of the room, zeroed", e != FXI_NONE &&
          fxi_state(0, 1) == FXI_OK && ifx_echo[e].line == fxa_line(FXA_N - FXA_Z1) && zeros16(ifx_echo[e].line, IFX_EC_LEN) &&
          fxi_free(FXT_CHRS) == 1u && mem_ok());
    reset();
}

static void t_query(void)
{
    reset();
    check("not limited: FXI_ANY (DIST), NA in its slot; a type not built: 0 (cap)",
          fxi_cap(FXT_DIST) == (FELUCCA_FX_DIST ? FXI_ANY : 0u) && fxi_free(FXT_FOLD) == 0u && fxi_cap(FXT_FOLD) == 0u);
    slots(0, FXT_DIST, FXT_FILT);
    fxi_poll();
    check("... fxi_state of an empty slot and of DIST: NA", fxi_state(0, 0) == FXI_NA && fxi_state(1, 0) == FXI_NA);
}
#endif

/* ---- Lending (fxi.c): a send's line as insert lines while its type is in no master slot */
static int32_t mixbuf[2 * CTL];
static void blocks(uint32_t nb)                         /* the whole mix, nb blocks, the main loop's pass between them */
{
    while (nb--) {
        mix_block(mixbuf, CTL);
        fxi_poll();
    }
}
#if FELUCCA_INS_CHORUS && FELUCCA_INS_DELAY
static void t_lend(void)
{
    uint32_t own = 2u, k = own, i, b, ok;
    int32_t w0, w1;
    reset();
    room(own * FXA_Z0);
    host_preset(&trk[k], 0, 0);
    trk[k].p[P_SUS] = 127, trk[k].p[P_LEVEL] = 100, trk[k].p[P_TCHRS] = 100;
    trk_note_on(&trk[k], 57, 100);
    check("the chorus send's line: 4 units (2 CHRS lines), the delay's every one of its own", FXC_S == 4u &&
          FXA_S == DLY_LEN / FXA_U);
    for (i = 0; i < own; i++)
        slots(i, FXT_CHRS, FXT_NONE);
    slots(k, FXT_CHRS, FXT_NONE);
    blocks(4);
    check("CHO in a master slot: nothing lent, the track past the room FULL",
          !fxl_lent && fxi_state(k, 0) == FXI_FULL && fxi_cap(FXT_CHRS) == own);
    fxs_load(1, FXT_NONE);
    fx.cho_q = 100;                                     /* (as if its tail still rang) */
    fxi_poll();
    check("CHO out of the slots, its tail ringing: not lent yet (WAIT), cap counts the send's line",
          !fxl_lent && fxi_state(k, 0) == FXI_WAIT && fxi_cap(FXT_CHRS) == own + 2u);
    fx.cho_q = FX_Q_MAX;
    blocks(1);
    i = ifx_lim_at(FXT_CHRS, k);
    check("... idle: lent (the gate set), the track has the send's line's first 2 units (OK)", (fxl_lent & 1u) &&
          i != FXI_NONE && ifx_chrs[i].line == cho_buf && fxi_state(k, 0) == FXI_OK && fxi_lent(FXT_CHRS) == 1u && mem_ok());
    blocks(64);
    check("... heard: the insert writes its slice", !zeros16(cho_buf, IFX_CH_LEN));
    {   /* the bus gated: fed a loud send it neither steps nor clears the lent line */
        static int16_t snap[CHO_LEN];
        int32_t ci[CTL], z[CTL] = {0}, wl[CTL], wr[CTL];
        for (b = 0; b < CTL; b++)
            ci[b] = 40000;
        memcpy(snap, cho_buf, sizeof snap);
        song.g[G_CRET] = 0;                             /* (CHO RET 0 would clear the line: not while lent) */
        (void)fx_buses(ci, z, z, wl, wr, CTL);
        song.g[G_CRET] = 127;
        check("the chorus bus gated while lent: a loud send, RET 0: its line untouched", !memcmp(snap, cho_buf, sizeof snap));
    }
    w0 = ifx_chrs[i].w;
    fxs_load(1, FXT_CHO);
    fxi_poll();
    check("CHO back in a slot: taking it back (the insert still on it, evicted, the gate still set)",
          fxl_st[0] == FXL_BACK && ifx_lim_at(FXT_CHRS, k) == i && ifx_chrs[i].evict && (fxl_lent & 1u));
    mix_block(mixbuf, CTL);
    w1 = ifx_chrs[i].w;
    check("... a fade-out, not a cut (one block: down by 32 x the slope)", w0 == 32768 && w1 == w0 - (int32_t)CTL * IFX_SLOPE);
    blocks(8);
    check("... faded: the line back to the bus, all zeros, the gate cleared; the track FULL again",
          fxl_st[0] == FXL_SEND && !(fxl_lent & 1u) && zeros16(cho_buf, CHO_LEN) && fx.cho_q >= CHO_LEN &&
          fxi_state(k, 0) == FXI_FULL && ifx_lim_at(FXT_CHRS, k) == FXI_NONE);
    fxs_load(1, FXT_NONE);
    blocks(64);
    i = ifx_lim_at(FXT_CHRS, k);
    ok = (fxl_lent & 1u) && i != FXI_NONE && ifx_chrs[i].w == 32768;
    fxs_load(1, FXT_CHO);
    fxi_poll();                                         /* (the main loop's pass right after the change) */
    blocks(1);
    w0 = ifx_chrs[i].w;
    fxs_load(1, FXT_NONE);
    fxi_poll();
    blocks(1);
    w1 = ifx_chrs[i].w;
    check("lent again; CHO in and out within a block or two: it stays lent, the insert fades back in (no gap)",
          ok && fxl_st[0] == FXL_LENT && !ifx_chrs[i].evict && w0 < 32768 && w1 > w0 && ifx_lim_at(FXT_CHRS, k) == i);
    slots(k, FXT_NONE, FXT_NONE);
    blocks(8);
    check("the track drops CHRS: faded, the line back to the bus (zeros, gate cleared)",
          fxl_st[0] == FXL_SEND && !fxl_lent && zeros16(cho_buf, CHO_LEN));
    slots(k, FXT_CHRS, FXT_NONE);
    blocks(4);
    i = ifx_lim_at(FXT_CHRS, k);
    trk[k].p[P_MUTE] = 1;
    blocks(16);
    fxs_load(1, FXT_CHO);
    blocks(4);
    check("a muted track's lent insert: given back all the same (no stuck line)", i != FXI_NONE &&
          fxl_st[0] == FXL_SEND && !fxl_lent);
    trk[k].p[P_MUTE] = 0;
    trk_note_off(&trk[k], 57);
    /* ECHO from the delay send's line */
    reset();
    room(FXA_Z1);
    slots(0, FXT_ECHO, FXT_NONE), slots(1, FXT_ECHO, FXT_NONE);
    fxs_load(2, FXT_NONE);
    fx.dly_q = FX_Q_MAX, memset(fx.dly_f, 0, sizeof fx.dly_f);
    blocks(2);
    i = ifx_lim_at(FXT_ECHO, 1);
    check("ECHO: T1 the own room, T2 on the delay send's line (lent: its first 8 units), cap all the line holds",
          (fxl_lent & 2u) && i != FXI_NONE && ifx_echo[i].line == dly_buf && fxi_state(0, 0) == FXI_OK &&
          fxi_cap(FXT_ECHO) == FXI_TN[1] && mem_ok());
    fxs_load(2, FXT_DLY);
    blocks(8);
    check("... DLY back: given back, zeroed, the gate cleared", !fxl_lent && zeros16(dly_buf, FXA_Z1 * FXA_U) &&
          fxi_state(1, 0) == FXI_FULL);
    reset();
}

/* a load with CHO in no master slot and more tracks holding CHRS than the own room holds: the same tracks every time */
static void t_lend_load(void)
{
    uint8_t rec[FXR_MAX], a[NTRK], c[NTRK];
    uint32_t n, k, ok = 1;
    reset();
    for (k = 0; k < NTRK; k++)
        slots(k, FXT_CHRS, FXT_NONE);
    fxs_load(1, FXT_NONE);
    trk[0].p[P_TCHRS] = 30;
    n = fxr_encode(rec);
    for (k = 0; k < 2u; k++) {
        fxi_init();
        room(2u * FXA_Z0);
        if (k)
            slots(TRK_DRUM, FXT_CHRS, FXT_NONE), fxi_poll();   /* (another history) */
        (void)fxr_decode(rec, n, 1);
        blocks(4);
        owners(k ? c : a);
    }
    for (k = 0; k < NTRK; k++)
        ok &= a[k] == c[k] && a[k];
    check("a load, CHO in no slot, every track CHRS: 2 in the own room, 2 on the send's line, the same every time", ok &&
          fxi_state(TRK_DRUM, 0) == FXI_OK && fxi_lent(FXT_CHRS) == 2u && mem_ok());
    reset();
}
#endif

#if FELUCCA_INS_JCHORUS
/* the master's inserts over x -> y (L), r (R) */
static int32_t mr[NB * CTL];
static void play_mst(int32_t a, uint32_t mode)
{
    uint32_t i;
    mst_amt[FXT_JUNO] = (int16_t)a, mst_jmode = (int16_t)mode;
    memcpy(y, x, sizeof y), memcpy(mr, x, sizeof mr);
    for (i = 0; i < NB; i++)
        mst_run(y + i * CTL, mr + i * CTL, CTL);
}
static void t_juno(void)
{
    uint32_t i, k, side = 0, lr = 0;
    uint8_t b[FXR_MAX], m[2] = {FXT_JUNO, FXT_NONE};
    uint32_t n;
    reset();
    room(2u * FXA_Z2);                                  /* (room for two) */
    mst_put(m);
    fxi_poll();
    check("JUNO: room for 2; the master holding it takes one", fxi_cap(FXT_JUNO) == 2u && fxi_used(FXT_JUNO) == 1u &&
          ifx_lim_at(FXT_JUNO, IFX_MST) != FXI_NONE && mem_ok());
    sine();
    play_mst(127, 1);
    for (i = 0; i < NB * CTL; i++)
        lr |= y[i] != mr[i];
    check("JUNO on the master at MIX 127: L and R differ (the antiphase taps)", lr);
    slots(0, FXT_JUNO, FXT_NONE), slots(1, FXT_JUNO, FXT_NONE);
    fxi_poll();
    check("T1 takes the second, T2 none: FULL, bypassed", ifx_lim_at(FXT_JUNO, 0) != FXI_NONE &&
          fxi_state(1, 0) == FXI_FULL && !fxi_can(1, FXT_JUNO));
    trk[1].p[P_TJCHO] = 127;
    memcpy(y, x, sizeof y);
    for (i = 0; i < NB; i++)
        ins_part(&trk[1], y + i * CTL, CTL);
    check("... T2 without one: its block the input, bit for bit", same());
    trk[0].p[P_TJCHO] = 100, trk[0].p[P_TJMODE] = 2;
    memcpy(y, x, sizeof y);
    for (i = 0; i < NB; i++) {
        ins_part(&trk[0], y + i * CTL, CTL);
        side |= ifx_jsd[0] && ifx_jside[0][5] != 0;
    }
    check("T1 at MIX 100, I+II: a side each block (stereo), the mid changed", side && !same());
    m[0] = FXT_NONE;
    mst_put(m);
    fxi_poll();
    check("the master drops it: given back only after its fade (T2 still FULL)", fxi_state(1, 0) == FXI_FULL);
    play_mst(0, 1);                                     /* (its fade-out runs: at 0) */
    for (k = 0; k < 8u && fxi_state(1, 0) != FXI_OK; k++)
        fxi_poll();
    check("... faded: T2 has it now", fxi_state(1, 0) == FXI_OK && ifx_lim_at(FXT_JUNO, IFX_MST) == FXI_NONE);
    trk[2].p[P_TJCHO] = 77, trk[2].p[P_TJMODE] = 1, mst_amt[FXT_JUNO] = 33, mst_jmode = 2;
    n = fxs_tlv(FXT_JUNO, b);
    fxs_untlv_none(1);
    fxs_untlv(FXT_JUNO, b, n, 1);
    check("the FX record's JUNO TLV: MIX and MODE of the tracks and the master back", n == 2u * NTRK + 2u &&
          trk[2].p[P_TJCHO] == 77 && trk[2].p[P_TJMODE] == 1 && trk[0].p[P_TJCHO] == 100 && trk[0].p[P_TJMODE] == 2 &&
          mst_amt[FXT_JUNO] == 33 && mst_jmode == 2);
    fxs_untlv(FXT_JUNO, b, 4u, 1);
    check("... a shorter one: the rest 0", trk[2].p[P_TJCHO] == 77 && trk[2].p[P_TJMODE] == 0 && mst_amt[FXT_JUNO] == 0);
}
#endif

#if FELUCCA_INS_FLANGER
/* FLNG in the effects memory (a unit an instance): any owner takes one (the master after the tracks); CHRS on the master
 * takes a CHRS line */
static int32_t fr[NB * CTL];
static void play_mst2(uint32_t t, int32_t a)
{
    uint32_t i;
    mst_amt[t] = (int16_t)a;
    memcpy(y, x, sizeof y), memcpy(fr, x, sizeof fr);
    for (i = 0; i < NB; i++)
        mst_run(y + i * CTL, fr + i * CTL, CTL);
}
static void t_flng(void)
{
    uint32_t i, k, lr = 0;
    uint8_t m[2] = {FXT_FLNG, FXT_NONE};
    reset();
    room(FXA_Z3);                                       /* (room for one) */
    sine();
    slots(0, FXT_FLNG, FXT_NONE);
    mst_put(m);
    fxi_poll();
    check("FLNG, room for 1; T1 and the master hold it: T1 has it (owner order), the master FULL",
          fxi_cap(FXT_FLNG) == 1u && fxi_state(0, 0) == FXI_OK && fxi_ostate(FXI_MST, FXT_FLNG) == FXI_FULL && mem_ok());
    play_mst2(FXT_FLNG, 100);
    check("... the master without one: L and R bit for bit", same() && !memcmp(x, fr, sizeof fr));
    slots(0, FXT_NONE, FXT_NONE);
    trk[0].p[P_TFLNG] = 100;
    memcpy(y, x, sizeof y);
    for (i = 0; i < NB; i++)
        ins_part(&trk[0], y + i * CTL, CTL);            /* (T1's fade-out: in no slot, at 0) */
    for (k = 0; k < 4u; k++)
        fxi_poll();
    check("T1 drops it: after its fade the master has it", fxi_ostate(FXI_MST, FXT_FLNG) == FXI_OK &&
          ifx_lim_at(FXT_FLNG, 0) == FXI_NONE);
    play_mst2(FXT_FLNG, 100);
    for (i = 0; i < NB * CTL; i++)
        lr |= y[i] != fr[i];
    check("... heard on the master; L and R the same sweep (the same input: the same output)", !same() && !lr);
    fxa_mark(FXA_N - FXA_Z3 - FXA_Z0, FXA_Z0, 0);       /* (2 units more free: room for a CHRS line) */
    m[0] = FXT_CHRS;
    mst_put(m);
    fxi_poll();
    check("CHRS on the master: a CHRS line (the master is an owner of every limited type)",
          fxi_ostate(FXI_MST, FXT_CHRS) == FXI_OK && ifx_lim_at(FXT_CHRS, IFX_MST) != FXI_NONE && mem_ok());
    play_mst2(FXT_CHRS, 90);
    check("... heard", !same());
    m[0] = FXT_NONE;
    mst_put(m);
    play_mst2(FXT_CHRS, 0);
    fxi_poll();
}
#endif

#if FELUCCA_INS_JCHORUS && FELUCCA_INS_FLANGER
/* JUNO CHORUS and FLNG on the chorus send's lent line (3 of its 4 units), evicted with a fade when CHO comes back */
static void t_lend_jf(void)
{
    uint32_t j, f;
    reset();
    room(0);
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_SUS] = 127, trk[0].p[P_LEVEL] = 100, trk[0].p[P_TJCHO] = 127, trk[0].p[P_TFLNG] = 60;
    trk_note_on(&trk[0], 57, 100);
    slots(0, FXT_JUNO, FXT_FLNG);
    blocks(2);
    check("no own room: JUNO and FLNG on T1 FULL", fxi_state(0, 0) == FXI_FULL && fxi_state(0, 1) == FXI_FULL);
    fxs_load(1, FXT_NONE);
    fx.cho_q = FX_Q_MAX;
    blocks(2);
    j = ifx_lim_at(FXT_JUNO, 0), f = ifx_lim_at(FXT_FLNG, 0);
    check("CHO out of the slots: both on its lent line, JUNO its first 2 units, FLNG the next, never one unit twice",
          j != FXI_NONE && f != FXI_NONE && ifx_jcho[j].c.ln == cho_buf && ifx_flng[f].line == cho_buf + 2u * FXA_U &&
          (fxl_lent & 1u) && fxi_lent(FXT_JUNO) == 1u && fxi_lent(FXT_FLNG) == 1u && mem_ok());
    blocks(64);
    check("... heard: they write the line", !zeros16(cho_buf, 3u * FXA_U));
    fxs_load(1, FXT_CHO);
    blocks(1);
    check("CHO back: both evicted (fading out), still on the line", ifx_jcho[j].evict && ifx_flng[f].evict &&
          ifx_lim_at(FXT_JUNO, 0) == j && fxl_st[0] == FXL_BACK);
    blocks(24);                                         /* (JUNO's MIX fades over 16 blocks) */
    check("... faded: given back, the line the bus's again (zeros), both FULL", fxl_st[0] == FXL_SEND && !fxl_lent &&
          zeros16(cho_buf, CHO_LEN) && fxi_state(0, 0) == FXI_FULL && fxi_state(0, 1) == FXI_FULL);
    trk_note_off(&trk[0], 57);
    reset();
}
#endif

#if FELUCCA_INS_XDLY
/* ---- XDLY (built (run_tests.sh) with the delay send's line of 32768 samples (64 units) and DLY_ARENA 72 KB (8 units
 * of its own): ECHO's one line beside the send's, and XDLY (2 x 16384 samples, 64 units) with no room of its own: it
 * lives in the send's line while the send is in no master slot */
static uint32_t xd_lent(uint32_t k)                     /* track k's XDLY on the send's line, the gate set */
{
    uint32_t i = ifx_lim_at(FXT_XDLY, k);
    return i != FXI_NONE && ifx_xdly[i].line == dly_buf && (fxl_lent & 2u) && fxl_st[1] == FXL_LENT;
}
static void dly_out(void)                               /* DLY out of the master slots, its bus idle */
{
    fxs_load(2, FXT_NONE);
    fx.dly_q = FX_Q_MAX, memset(fx.dly_f, 0, sizeof fx.dly_f);
}
static void t_arena(void)
{
    uint32_t k = 1, e = 2, i, n, ok;
    check("the memory: the send's 64 units, 8 of the inserts' own; XDLY 64 units an instance, 1 at most (the send's line)",
          FXA_S == 64u && FXA_N == 72u && FXA_SZ(4u) == 64u && FXI_TN[4] == 1u);
    reset();
    host_preset(&trk[k], 0, 0);
    trk[k].p[P_SUS] = 127, trk[k].p[P_LEVEL] = 100, trk[k].p[P_TXDLY] = 100;
    trk_note_on(&trk[k], 57, 100);
    slots(k, FXT_XDLY, FXT_NONE);
    blocks(2);
    check("XDLY picked, DLY in a master slot: no room for it (FULL), nothing lent, the send's line untouched",
          fxi_state(k, 0) == FXI_FULL && ifx_lim_at(FXT_XDLY, k) == FXI_NONE && !fxl_lent && fxi_cap(FXT_XDLY) == 0u &&
          fxi_free(FXT_XDLY) == 0u && !fxi_can(k, FXT_XDLY));
    dly_out();
    memset(dly_buf, 0x5A, sizeof dly_buf);              /* (an old line: not heard, not left behind) */
    fxi_poll();
    i = ifx_lim_at(FXT_XDLY, k);
    check("DLY out of the slots, its bus idle: lent, XDLY on the send's whole line (OK), zeroed when handed out",
          xd_lent(k) && fxi_state(k, 0) == FXI_OK && zeros16(dly_buf, DLY_LEN) && !ifx_xdly[i].on && !ifx_xdly[i].w &&
          fxi_lent(FXT_XDLY) == 1u);
    slots(e, FXT_ECHO, FXT_NONE), slots(TRK_DRUM, FXT_ECHO, FXT_NONE);
    fxi_poll();
    n = ifx_lim_at(FXT_ECHO, e);
    check("... ECHO beside it: T3 the memory's own 8 units, the drum track none: the send's line is XDLY's (FULL)",
          n != FXI_NONE && ifx_echo[n].line == fxa_ins && fxi_state(TRK_DRUM, 0) == FXI_FULL && xd_lent(k) && mem_ok());
    blocks(64);
    check("... XDLY heard: it writes the send's line", !zeros16(dly_buf, DLY_LEN));
    fxs_load(2, FXT_DLY);
    fxi_poll();
    check("DLY back in a slot: XDLY evicted (fading out), the gate still set", fxl_st[1] == FXL_BACK &&
          ifx_lim_at(FXT_XDLY, k) == i && ifx_xdly[i].evict && (fxl_lent & 2u));
    blocks(16);
    check("... faded: the line back to the bus, all zeros, the gate cleared; XDLY FULL again, the drum track's ECHO FULL",
          fxl_st[1] == FXL_SEND && !fxl_lent && zeros16(dly_buf, DLY_LEN) && fx.dly_q >= DLY_LEN &&
          ifx_lim_at(FXT_XDLY, k) == FXI_NONE && fxi_state(k, 0) == FXI_FULL && fxi_state(TRK_DRUM, 0) == FXI_FULL);
    dly_out();
    fxi_poll();
    n = ifx_lim_at(FXT_ECHO, TRK_DRUM);
    check("DLY out again, ECHO and XDLY both waiting: ECHO takes 8 units (type order), XDLY finds 64 no more: "
          "FULL (never both on a unit)", n != FXI_NONE && ifx_echo[n].line == dly_buf && (fxl_lent & 2u) &&
          fxi_state(k, 0) == FXI_FULL && ifx_lim_at(FXT_XDLY, k) == FXI_NONE && fxi_free(FXT_XDLY) == 0u && mem_ok());
    blocks(8);
    check("... XDLY stays bypassed meanwhile", ifx_lim_at(FXT_XDLY, k) == FXI_NONE && fxi_state(k, 0) == FXI_FULL);
    slots(TRK_DRUM, FXT_NONE, FXT_NONE);
    for (n = 0; n < 64u && ifx_lim_at(FXT_XDLY, k) == FXI_NONE; n++)
        blocks(1);                                      /* (to the pass that hands it out, no block after it) */
    i = ifx_lim_at(FXT_XDLY, k);
    ok = i != FXI_NONE && ifx_lim_at(FXT_ECHO, TRK_DRUM) == FXI_NONE;
    check("the drum track drops ECHO: its units given back after the fade, XDLY takes the line, clean (no old echo: "
          "line and state zero)", ok && xd_lent(k) && zeros16(dly_buf, DLY_LEN) && !ifx_xdly[i].on && !ifx_xdly[i].w);
    blocks(16);
    check("... heard again", !zeros16(dly_buf, DLY_LEN) && fxi_state(k, 0) == FXI_OK);
    slots(k, FXT_NONE, FXT_NONE);
    trk_note_off(&trk[k], 57);
    blocks(32);
    check("T2 drops XDLY: faded, given back, the line the bus's again (zeros, the gate cleared)",
          ifx_lim_at(FXT_XDLY, k) == FXI_NONE && fxl_st[1] == FXL_SEND && !fxl_lent && zeros16(dly_buf, DLY_LEN));
    reset();
    check("reset: every unit free, the line the send's", !fxl_lent && fxl_st[1] == FXL_SEND && fxi_free(FXT_ECHO) == 1u);
}
#endif

int main(void)
{
    host_tracks_init();
#if FELUCCA_INS_CHORUS && FELUCCA_INS_DELAY && !FELUCCA_INS_XDLY && !FELUCCA_DLY_ARENA
    if (!FELUCCA_INS_JCHORUS && !FELUCCA_INS_FLANGER) {   /* (the counts below: the auto memory of CHRS and ECHO) */
        t_assign();
        t_mem();
    }
#endif
#if FELUCCA_INS_CHORUS && FELUCCA_INS_DELAY
    t_exhaust();
    t_reload();
    t_query();
    t_lend();
    t_lend_load();
#endif
#if FELUCCA_INS_JCHORUS
    t_juno();
#endif
#if FELUCCA_INS_FLANGER
    t_flng();
#endif
#if FELUCCA_INS_JCHORUS && FELUCCA_INS_FLANGER
    t_lend_jf();
#endif
#if FELUCCA_INS_XDLY
    t_arena();
#endif
    printf("fxi: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
