/* SPDX-License-Identifier: GPL-3.0-only */
/* The sends between the returns (2026-10-16; fx_slots.c rs_amt, fx.c rs_block / fx_buses / rt_line): left to right only,
 * A -> B, A -> C, B -> C, rendered in the returns' order whatever lines they own:
 *   order    three returns that pass their send through (FILT open, DIST and DYN at 0: 100 % wet, unity), a sine sent to A's
 *            line only: A alone 1x; A -> B 2x; A -> B -> C 3x (C hears B's output, A's send in it: B ran before C); A -> C
 *            and B -> C; at one half 1.5x; the lines in another order (A on CHO's line 0, C on REV's 2) the same
 *   level    a send takes its return's output after its RET (A's RET at 0: B hears nothing); ramped (no step at a change)
 *   buses    REV in A sending to DLY in B: the delay sounds from A's output alone (its own send silent: woken, rs_wake)
 *   idle     every send at 0: the chain plan ends (rs_on 0), the same samples as a build of before (no chain code run)
 *   record   RSND_TLV round trips; the default writes none; a record without it (an old project) loads every send at 0
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
    printf("fx_rsend: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
/* one block of the returns (fx.c rets_mix) with a sine (amplitude a) on line j's send; -> the block's peak of the mix (L) */
static int32_t blk(uint32_t j, int32_t a, uint32_t b0, int *lr)
{
    int32_t *snd[3] = {send_c, send_d, send_r}, pk = 0;
    uint32_t i, k;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < CTL; i++)
            snd[k][i] = k == j ? (int32_t)lrint(a * sin(2 * M_PI * 441.0 * (b0 * CTL + i) / FS)) : 0;
    for (i = 0; i < CTL; i++)
        mix_l[i] = mix_r[i] = 0;
    rets_mix(CTL);
    for (i = 0; i < CTL; i++) {
        pk = abs(mix_l[i]) > pk ? abs(mix_l[i]) : pk;
        if (lr)
            *lr &= mix_l[i] == mix_r[i];
    }
    return pk;
}
/* the steady peak of the mix with the sine on line j (40 blocks: the ramps done; the last 20 measured) */
static int32_t run(uint32_t j)
{
    uint32_t i;
    int32_t pk = 0, p;
    for (i = 0; i < 40u; i++) {
        p = blk(j, 8000, i, 0);
        pk = i >= 20u && p > pk ? p : pk;
    }
    return pk;
}
static int near(int32_t v, double want) { return fabs(v - want) <= want * 0.02 + 4.0; }
static void sends(int ab, int ac, int bc) { rs_amt[RS_AB] = (int16_t)ab, rs_amt[RS_AC] = (int16_t)ac, rs_amt[RS_BC] = (int16_t)bc; }
/* returns A B C passing their send through: three types RT_OK holds of DIST FILT DYN DUST, at their open point */
static int unity_layout(void)
{
    static const uint8_t T[] = {FXT_FILT, FXT_DIST, FXT_DYN};
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        if (!(RT_OK >> T[k] & 1u))
            return 0;
        fxs_load(1u + k, T[k]);
        fxs_ramt[1u + k] = 0;                            /* (FILT 0: open; DIST, DYN 0: through) */
    }
    return 1;
}

int main(void)
{
    uint8_t b[FXR_MAX];
    uint32_t n, la, i;
    int32_t a1, p;
    int lr = 1;
    host_tracks_init();
    fxs_set(FXS_DEF);
    song.g[G_CRET] = song.g[G_DRET] = song.g[G_RRET] = 127;
    check("default: every send between the returns at 0, no chain planned, nothing to keep", !rs_amt[0] && !rs_amt[1] &&
          !rs_amt[2] && !rs_on && fxr_encode(b) == 0u);
    if (!unity_layout()) {
        printf("fx_rsend: FILT DIST DYN not all built: the order checks skipped\n");
    } else {
        la = fxs_line[1];
        check("FILT DIST DYN into A B C: on lines 2 1 0 (REV's, DLY's, CHO's: A renders last in the buses' own order)",
              fxs_line[1] == 2u && fxs_line[2] == 1u && fxs_line[3] == 0u);
        sends(0, 0, 0);
        a1 = run(la);
        check("A alone: its send through (100 % wet, unity)", near(a1, 8000));
        sends(127, 0, 0);
        p = run(la);
        check("A -> B at 127: A's output and B's (A's in it): 2x", near(p, 2.0 * a1 * 32766 / 32768));
        sends(127, 0, 127);
        p = run(la);
        check("A -> B -> C: C hears B, which heard A first (one order: A, B, C): 3x", near(p, a1 * (1.0 + 2.0 * 32766 / 32768)));
        sends(0, 127, 0);
        p = run(la);
        check("A -> C alone: 2x", near(p, 2.0 * a1 * 32766 / 32768));
        sends(127, 127, 127);
        p = run(la);
        check("A -> B, A -> C, B -> C: A + B (A) + C (A + B (A)): 4x", near(p, a1 * (1.0 + 3.0 * 32766 / 32768)));
        sends(64, 0, 0);
        p = run(la);
        check("A -> B at 64: 1.5x (a track's send law, 64 x 258 / 32768)", near(p, a1 * (1.0 + 64.0 * 258 / 32768)));
        for (i = 0, lr = 1; i < 4u; i++)
            blk(la, 8000, i, &lr);
        check("... mono in, L = R out", lr);
        sends(0, 0, 127);
        p = run(la);
        check("B -> C with nothing in B: A alone (B's send carries B's output only)", near(p, a1));
        sends(127, 0, 0);
        song.g[G_CRET + la] = 0;
        p = run(la);
        check("A's RET at 0: A silent, and B hears nothing from it (a send is after its return's level)", p == 0);
        song.g[G_CRET + la] = 127;
        sends(0, 0, 0);
        run(la);
        p = blk(la, 8000, 0, 0);
        check("every send back at 0: the chain plan ends (rs_on 0: no chain code runs), A alone", !rs_on && near(run(la), a1));
        {   /* a send's ramp: the first block after a change does not jump to the new gain */
            int32_t s0;
            sends(127, 0, 0);
            s0 = blk(la, 8000, 0, 0);
            p = run(la);
            check("... a send ramped over its first block (no step)", s0 < p && s0 > a1 / 2);
        }
        /* the lines in another order: A on line 0 (CHO's), B on 1, C on 2: swap A and C's types (lines follow the slots) */
        sends(0, 0, 0);
        fxs_load(1, FXT_DYN);                           /* (DYN from C: the two swap, each keeping its line) */
        fxs_ramt[1] = fxs_ramt[3] = 0;
        check("swapped: DYN into A, FILT into C, each keeping its line (A on line 0, C on 2); the sends stay positional", fxs_slot[1] == FXT_DYN && fxs_slot[3] == FXT_FILT);
        sends(127, 0, 127);
        p = run(fxs_line[1]);
        check("... A -> B -> C after the swap: 3x again", near(p, a1 * (1.0 + 2.0 * 32766 / 32768)));
        sends(0, 0, 0);
        run(0);
        fxs_set(FXS_DEF);
        for (i = 0; i < RT_TAIL + 2u; i++)
            blk(0, 0, i, 0);
    }
    {   /* the buses: REV in A (line 2), DLY in B (line 1); only the reverb's line is sent; the delay wakes from A's output */
        int32_t pd = 0, pn = 0;
        fxs_set(FXS_DEF);
        song.g[G_DTIME] = 10;
        sends(0, 0, 0);
        for (i = 0; i < 400u; i++) {
            fx.dly_w += 0;
            p = blk(2, i < 40u ? 8000 : 0, i, 0);
            pn = i > 300u && p > pn ? p : pn;
        }
        for (i = 0; i < 2000u; i++)
            blk(2, 0, i, 0);
        sends(127, 0, 0);
        for (i = 0; i < 400u; i++) {
            p = blk(2, i < 40u ? 8000 : 0, i, 0);
            pd = i > 300u && p > pd ? p : pd;
        }
        check("REV in A sending to DLY in B: the delay sounds from A's output (woken: its own send silent), louder than A alone",
              !FELUCCA_FX_REVERB || !FELUCCA_FX_DELAY || pd > pn);
        sends(0, 0, 0);
    }
    {   /* the record */
        uint32_t m;
        fxs_set(FXS_DEF);
        sends(100, 0, 7);
        n = fxr_encode(b);
        check("record: sends set: a record (the default layout), RSND_TLV 0x4B of 3", n > FXR_HEAD);
        for (m = FXR_HEAD; m + 1u < n && b[m] != RSND_TLV; m += 2u + b[m + 1u])
            ;
        check("... the TLV: A -> B, A -> C, B -> C", m + 4u < n + 1u && b[m] == RSND_TLV && b[m + 1u] == 3u && b[m + 2u] == 100u &&
              b[m + 3u] == 0u && b[m + 4u] == 7u);
        sends(0, 0, 0);
        fxr_decode(b, n, 1);
        check("... decoded (a load)", rs_amt[RS_AB] == 100 && rs_amt[RS_AC] == 0 && rs_amt[RS_BC] == 7);
        fxr_decode(b, n, 0);
        check("... a song section's apply keeps them (as the returns' levels)", rs_amt[RS_AB] == 100 && rs_amt[RS_BC] == 7);
        b[m + 1u] = 6u;                                  /* (a longer form: a reader takes the three it knows) */
        {
            uint8_t c[FXR_MAX];
            memcpy(c, b, m + 5u);
            c[m + 5u] = 9, c[m + 6u] = 9, c[m + 7u] = 9;
            memcpy(c + m + 8u, b + m + 5u, n - (m + 5u));
            fxr_decode(c, n + 3u, 1);
            check("... a longer RSND_TLV (a later build's further sends): the three known taken, the rest skipped",
                  rs_amt[RS_AB] == 100 && rs_amt[RS_BC] == 7);
        }
        fxr_decode(0, 0, 1);
        check("no record (a project of before): every send at 0", !rs_amt[0] && !rs_amt[1] && !rs_amt[2]);
        sends(5, 6, 7);
        fxr_decode(b, FXR_HEAD, 1);                      /* (a header alone: the sends of an old record) */
        check("an old record (no RSND_TLV): every send at 0", !rs_amt[0] && !rs_amt[1] && !rs_amt[2]);
    }
    printf("fx_rsend: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
