/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the limited insert types' instances (firmware/src/fx/insfx/fxi.c, fxi_kern.c: CHRS ECHO), the firmware's
 * own sources through hostsim.c, built with CHRS 2 and ECHO 1 instances (run_tests.sh):
 *   1. assign: a track picking CHRS gets an instance at the next fxi_poll (none before it: bypassed), zeroed first
 *   2. swap: S1 <-> S2 on the track keeps its instance
 *   3. exhaust: a third track picking CHRS gets none (fxi_state FULL, fxi_free 0, fxi_can 0) and is bypassed bit for bit
 *   4. free: a track dropping CHRS gives its instance back only after its fade-out (no click), then the waiting track
 *      takes it, cleared (no old line read)
 *   5. reload: a project load hands the instances out in track order T1 T2 T3 DR whatever came before, the same on
 *      every load; the FX record keeps the type, not the instance
 *   6. the UI's queries for types that are not limited (FXI_ANY) and not built (0) */
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
    fxi_init();
    fxi_poll();
}

static void t_assign(void)
{
    uint32_t i0;
    reset();
    sine();
    check("built: CHRS 2 instances, ECHO 1; both free", fxi_cap(FXT_CHRS) == 2u && fxi_cap(FXT_ECHO) == 1u &&
          fxi_free(FXT_CHRS) == 2u && fxi_used(FXT_ECHO) == 0u);
    memset(ifx_chrs, 0x5A, sizeof ifx_chrs);            /* (an old line in every instance) */
    slots(1, FXT_CHRS, FXT_NONE);
    check("picked: no instance before the main loop's pass (WAIT, bypassed)", fxi_state(1, 0) == FXI_WAIT &&
          ifx_lim_at(FXT_CHRS, 1) == FXI_NONE);
    play(1, 90);
    check("... the part bit for bit meanwhile", same());
    fxi_poll();
    i0 = ifx_lim_at(FXT_CHRS, 1);
    check("after fxi_poll: an instance (OK), one used, one free", i0 < 2u && fxi_state(1, 0) == FXI_OK &&
          fxi_used(FXT_CHRS) == 1u && fxi_free(FXT_CHRS) == 1u);
    {
        uint32_t b, z = 1;
        const uint8_t *p = (const uint8_t *)&ifx_chrs[i0];
        for (b = 0; b < sizeof ifx_chrs[i0]; b++)
            z &= p[b] == 0;
        check("... zeroed when handed out (the old line gone)", z && ifx_chrs[i0 ^ 1u].line[0] == 0x5A5A);
    }
    play(1, 90);
    check("... heard", !same());
    slots(1, FXT_NONE, FXT_CHRS);
    fxi_poll();
    check("S1 -> S2 on the track: the same instance", ifx_lim_at(FXT_CHRS, 1) == i0 && fxi_state(1, 1) == FXI_OK);
    slots(1, FXT_ECHO, FXT_CHRS);
    fxi_poll();
    check("ECHO beside it on the same track: its own instance", ifx_lim_at(FXT_ECHO, 1) == 0u && fxi_free(FXT_ECHO) == 0u);
}

static void t_exhaust(void)
{
    uint32_t i;
    reset();
    sine();
    slots(0, FXT_CHRS, FXT_NONE), slots(2, FXT_CHRS, FXT_NONE);
    fxi_poll();
    check("two tracks: both instances out", fxi_free(FXT_CHRS) == 0u && fxi_state(0, 0) == FXI_OK && fxi_state(2, 0) == FXI_OK);
    check("... a third track could not have one (fxi_can 0), one holding it can", !fxi_can(1, FXT_CHRS) &&
          fxi_can(0, FXT_CHRS) && fxi_can(1, FXT_DIST) == FELUCCA_FX_DIST);
    slots(TRK_DRUM, FXT_CHRS, FXT_NONE);
    fxi_poll();
    check("a third (the drum track): none left, FULL", fxi_state(TRK_DRUM, 0) == FXI_FULL &&
          ifx_lim_at(FXT_CHRS, TRK_DRUM) == FXI_NONE);
    play(TRK_DRUM, 127);
    check("... the drum bus bypassed bit for bit", same());
    /* T1 drops it: given back only once faded out */
    play(0, 127);
    slots(0, FXT_NONE, FXT_NONE);
    fxi_poll();
    check("T1 drops CHRS while it sounds: kept until it has faded (no click), the drum track still FULL",
          ifx_lim_at(FXT_CHRS, 0) != FXI_NONE && fxi_state(TRK_DRUM, 0) == FXI_FULL && fxi_pend);
    for (i = 0; i < 8u; i++)
        ins_part(&trk[0], y + i * CTL, CTL);            /* (in no slot: it fades out at 0) */
    fxi_poll();
    check("... faded: back, and the drum track has it now (OK)", ifx_lim_at(FXT_CHRS, 0) == FXI_NONE &&
          fxi_state(TRK_DRUM, 0) == FXI_OK && !fxi_pend);
    {
        uint32_t j = ifx_lim_at(FXT_CHRS, TRK_DRUM), b, z = 1;
        for (b = 0; b < IFX_CH_LEN; b++)
            z &= ifx_chrs[j].line[b] == 0;
        check("... its line cleared when handed on (T1's sound not in it)", z && !ifx_chrs[j].w);
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
    check("loaded: T1 and T3 have them (track order), DR FULL", a[0] && !a[1] && a[2] && !a[3] &&
          fxi_state(TRK_DRUM, 0) == FXI_FULL && trk[0].p[P_TCHRS] == 40 && ins_slot[0][1] == FXT_CHRS);
    /* another history, the same project loaded: the same owners */
    fxi_init();
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
    check("a project without a record (old, or new): no limited type held, every instance free",
          fxi_free(FXT_CHRS) == 2u && fxi_free(FXT_ECHO) == 1u && ins_slot[0][0] != FXT_CHRS);
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

int main(void)
{
    host_tracks_init();
    t_assign();
    t_exhaust();
    t_reload();
    t_query();
    printf("fxi: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
