/* SPDX-License-Identifier: GPL-3.0-only */
/* A return's PRE and POST inserts (2026-10-30; fx_slots.c rp_ty, fx.c rt_line / rp_pre_bus / rp_post_bus; the mixer's RET A B C
 * rows, docs/UI-OPTIMIST-DESIGN.md 4.1.1): send -> PRE -> FX -> POST -> level, mute -> the mix.
 *   none     no stage holds a type: nothing of them runs (rp_m, rp_lvm 0), the record writes no RPP_TLV
 *   generic  a return holding another type (FILT open): PRE DIST at 0 and POST DYN at 0 pass it through (the same peak as
 *            without them); PRE DIST up changes it
 *   bus      the delay's return (B): POST DIST driven hard, its RET at one half: the output exactly half of RET at unity's (the
 *            level after POST, so the drive is the same); muted: 0, its bus running on; PRE DIST on its send: the wet changes
 *   lines    the inserts belong to the effect's line: REV swapped from C into A, its POST goes with it; an emptied return's
 *            inserts emptied; a stage never holds its effect's type; a type once a return (PRE <- POST's: they swap)
 *   record   RPP_TLV round trips; a record without it (every project of before) loads with no inserts, its levels and types
 *            as stored; a short or bad RPP_TLV loads none; nothing crashes
 * Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("fx_retins: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
/* one block of the returns (fx.c rets_mix) with a sine (amplitude a) on line j's send; -> the block's peak of the mix (L) */
static int32_t blk(uint32_t j, int32_t a, uint32_t b0)
{
    int32_t *snd[3] = {send_c, send_d, send_r}, pk = 0;
    uint32_t i, k;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < CTL; i++)
            snd[k][i] = k == j ? (int32_t)lrint(a * sin(2 * M_PI * 441.0 * (b0 * CTL + i) / FS)) : 0;
    for (i = 0; i < CTL; i++)
        mix_l[i] = mix_r[i] = 0;
    rets_mix(CTL);
    for (i = 0; i < CTL; i++)
        pk = abs(mix_l[i]) > pk ? abs(mix_l[i]) : pk;
    return pk;
}
/* the peak of the mix over blocks 120 .. 199 with the sine on line j (the ramps, the tails' start done); rsig: a sum of the
 * mix's samples over those blocks (to tell two renders apart) */
static uint32_t rsig, nblk = 200u;
static int32_t run(uint32_t j, int32_t a)
{
    uint32_t i, k;
    int32_t pk = 0, p;
    rsig = 0;
    for (i = 0; i < nblk; i++) {
        p = blk(j, a, i);
        pk = i >= nblk * 3u / 5u && p > pk ? p : pk;
        for (k = 0; i >= nblk * 3u / 5u && k < CTL; k++)
            rsig = rsig * 31u + (uint32_t)mix_l[k];
    }
    return pk;
}
static void quiet(void)                                 /* every tail out */
{
    uint32_t i;
    for (i = 0; i < 6000u; i++)
        blk(0, 0, i);
}
static void reset_all(void)
{
    fxs_set(FXS_DEF);
    rp_untlv(0, 0);
    rs_untlv(0, 0);
    fx_rmute = fx_imute = 0;
    song.g[G_CRET] = song.g[G_DRET] = song.g[G_RRET] = 127;
    quiet();
}

int main(void)
{
    uint8_t b[FXR_MAX + 8];
    uint32_t n, j;
    int32_t p0, p1;
    host_tracks_init();
    reset_all();
    /* none */
    blk(1, 8000, 0);
    check("none: no stage holds a type, nothing of them runs (rp_m, rp_lvm 0), no RPP_TLV in the record",
          !rp_m && !rp_lvm && rp_tlv(b) == 0u && fxr_encode(b) == 0u);
    /* generic: A holds FILT (open), its line's PRE and POST */
    if ((RT_OK >> FXT_FILT & 1u) && (RP_OK >> FXT_DIST & 1u) && (RP_OK >> FXT_DYN & 1u)) {
        fxs_load(1, FXT_FILT);
        fxs_ramt[1] = 0;
        j = fxs_line[1];
        quiet();                                        /* (the same history before both measures) */
        (void)run(j, 8000);
        p0 = run(j, 8000);
        rp_load(j, RP_PRE, FXT_DIST);
        rp_load(j, RP_POST, FXT_DYN);
        rp_amt[j][RP_PRE] = rp_amt[j][RP_POST] = 0;
        quiet();
        (void)run(j, 8000);
        p1 = run(j, 8000);
        printf("fx_retins: (generic: the peak without %d, with PRE DIST 0, POST DYN 0 %d)\n", (int)p0, (int)p1);
        check("generic (A FILT open): PRE DIST at 0, POST DYN at 0: through, the same peak", p0 > 1000 && abs(p1 - p0) <= p0 / 100 + 2);
        rp_amt[j][RP_PRE] = 120;
        p1 = run(j, 8000);
        check("... PRE DIST up: the return's output changes", abs(p1 - p0) > p0 / 20);
        check("... a type once a return: DYN into PRE swaps with POST", (rp_load(j, RP_PRE, FXT_DYN), rp_ty[j][RP_PRE] == FXT_DYN && rp_ty[j][RP_POST] == FXT_DIST));
        rp_load(j, RP_POST, FXT_FILT);
        check("... a stage never takes its effect's type (FILT): unchanged", rp_ty[j][RP_POST] == FXT_DIST);
        reset_all();
    }
#if FELUCCA_FX_DELAY && FELUCCA_FX_DIST
    /* bus: B holds the delay (line 1), its POST DIST driven */
    song.g[G_DMIX] = 127;
    nblk = 3000u;                                       /* (the delay's time: its repeats, then the measure over a second) */
    j = fxs_line[2];
    rp_load(j, RP_POST, FXT_DIST);
    rp_amt[j][RP_POST] = 110;
    p0 = run(j, 12000);
    song.g[G_CRET + j] = 64;
    quiet();
    p1 = run(j, 12000);
    printf("fx_retins: (bus: RET unity %d, RET half %d, rp_lvm %u)\n", (int)p0, (int)p1, (unsigned)rp_lvm);
    check("bus (B, the delay): POST DIST driven; RET at one half: the output half of unity's (the level after POST)",
          j == 1u && rp_lvm == 2u && p0 > 2000 && abs(2 * p1 - p0) <= p0 / 50 + 4);
    fx_rmute = 1u << j;
    p1 = run(j, 12000);
    check("... muted: 0, its bus running on", p1 == 0 && fx.dly_q < DLY_LEN);
    fx_rmute = 0;
    rp_load(j, RP_POST, FXT_NONE);
    song.g[G_CRET + j] = 127;
    quiet();
    p0 = run(j, 12000);
    n = rsig;
    rp_load(j, RP_PRE, FXT_DIST);
    rp_amt[j][RP_PRE] = 127;
    quiet();
    p1 = run(j, 12000);
    printf("fx_retins: (bus PRE: the peak without %d, with %d)\n", (int)p0, (int)p1);
    check("... PRE DIST on its send: the delay's wet changes", p0 > 2000 && rsig != n);
    rp_load(j, RP_PRE, FXT_NONE);
    for (n = 0; n < 200u; n++)
        blk(j, 0, n);
    check("... both stages gone: the wrapper lets go after its tail (rp_lvm 0)", !rp_m && !rp_lvm);
    nblk = 200u;
    reset_all();
#endif
#if FELUCCA_FX_REVERB && FELUCCA_FX_DIST
    /* lines: REV in C (line 2) with a POST, swapped into A */
    rp_load(2, RP_POST, FXT_DIST);
    fxs_load(1, FXT_REV);
    check("lines: REV from C into A: its POST goes with it (A's line is REV's, 2)", fxs_line[1] == 2u && rp_ty[2][RP_POST] == FXT_DIST && RP_HAS(2, RP_POST));
    rp_load(2, RP_PRE, FXT_REV);
    check("... a stage never takes its line's effect (REV): unchanged", rp_ty[2][RP_PRE] == FXT_NONE);
    fxs_load(1, FXT_NONE);
    check("... A emptied: its inserts emptied", rp_ty[2][RP_POST] == FXT_NONE && !RP_HAS(2, RP_POST));
    reset_all();
#endif
    /* record */
    if (RP_OK >> FXT_DIST & 1u) {
        rp_load(1, RP_PRE, FXT_DIST);
        rp_amt[1][RP_PRE] = 77;
        song.g[G_RRET] = 90;
        n = fxr_encode(b);
        rp_untlv(0, 0);
        song.g[G_RRET] = 127;
        check("record: RPP_TLV round trips (PRE DIST 77 on DLY's line, REV RET 90)",
              n > 0u && fxr_decode(b, n, 1) && rp_ty[1][RP_PRE] == FXT_DIST && rp_amt[1][RP_PRE] == 77 && RP_HAS(1, RP_PRE) && song.g[G_RRET] == 90);
        rp_untlv(0, 0);
        n = fxr_encode(b);                              /* (no insert: the form every project of before has) */
        rp_load(0, RP_POST, FXT_DIST);
        check("an old project (no RPP_TLV): loads, no inserts, its RET level as stored",
              n > 0u && fxr_decode(b, n, 1) && rp_ty[0][RP_POST] == FXT_NONE && !rp_m && song.g[G_RRET] == 90);
        b[n] = (uint8_t)RPP_TLV, b[n + 1u] = 3u, b[n + 2u] = FXT_DIST, b[n + 3u] = 200u, b[n + 4u] = 99u;
        check("... a short RPP_TLV: loads, no inserts", fxr_decode(b, n + 5u, 1) && !rp_m);
        rp_load(0, RP_PRE, FXT_DIST);
        n = fxr_encode(b);
        for (j = 0; j + 2u < n; j++)
            if (b[j] == RPP_TLV && b[j + 1u] == RP_TLV_N) {
                b[j + 2u] = 250u, b[j + 6u] = FXT_GATE;   /* (a type no build has; GATER: no return holds it) */
                break;
            }
        check("... a bad RPP_TLV (an unknown type, GATER): loads, those stages empty",
              fxr_decode(b, n, 1) && rp_ty[0][RP_PRE] == FXT_NONE && rp_ty[1][RP_PRE] == FXT_NONE);
        reset_all();
    }
    printf("fx_retins: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
