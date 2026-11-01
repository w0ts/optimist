/* SPDX-License-Identifier: GPL-3.0-only */
/* A return whose limited insert has no memory is MUTED (ruling 2026-10-30; fx.c rt_line, rt_nomem / fx_rfull). Before: the
 * insert was bypassed and the return passed the line's send through, dry. Built with CHO and ECHO (the auto memory: ECHO
 * one instance, as fxi_test; run_tests.sh):
 *   1. T1 holds ECHO's only instance; ECHO into C (line 2) has none: FULL (fxi_state), fx_rfull(C), A not
 *   2. C outputs exactly 0 with a send on its line, its send bus fed silence, whatever its level; the user's mute (fx_rmute)
 *      is not touched (it is saved with the project)
 *   3. T1 lets go: after its fade the instance goes to C, C is no longer FULL and plays again
 *   4. a type that is not limited (PHSR is not in this build: DIST) is never muted by this rule
 * fx_rt_full_test */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("fx_rt_full: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int32_t any(const int32_t *b)
{
    int32_t i, o = 0;
    for (i = 0; i < CTL; i++)
        o |= b[i];
    return o;
}
/* one block of rt_run with the sine (amplitude a) sent on line j */
static void blk(uint32_t j, int32_t a, uint32_t b0)
{
    int32_t *snd[3] = {send_c, send_d, send_r};
    uint32_t i, k;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < CTL; i++)
            snd[k][i] = k == j ? (int32_t)lrint(a * sin(2 * M_PI * 330.0 * (b0 * CTL + i) / FS)) : 0;
    for (i = 0; i < CTL; i++)
        mix_l[i] = mix_r[i] = 0;
    rt_run(CTL);
}
static void slots(uint32_t k, uint32_t a, uint32_t b)
{
    uint8_t s[2] = {(uint8_t)a, (uint8_t)b};
    ins_put(k, s);
}

int main(void)
{
    uint32_t i, eq;
    static int32_t y[CTL];
    host_tracks_init();
    fxs_set(FXS_DEF);
    fxi_init();                                         /* (as fxi_test's reset(): the auto memory, ECHO's one line) */
    fxi_poll();
    slots(0, FXT_ECHO, FXT_NONE);                       /* (T1 takes ECHO's one instance: the owners come in order T1 .. DR, master, input, A C C) */
    fxi_poll();
    fxs_load(3, FXT_ECHO);                              /* C (line 2: REV, whose line is not lent: only CHO and DLY lend theirs) */
    fxi_poll();
    check("1. T1 holds ECHO's only instance; C's ECHO has none: FULL (fxi_state, the UI's fx_rfull); A is not",
          fxi_free(FXT_ECHO) == 0u && fxi_ostate(0, FXT_ECHO) == FXI_OK && fxs_rty[2] == FXT_ECHO &&
          fxi_ostate(FXI_RT0 + 2u, FXT_ECHO) == FXI_FULL && fx_rfull(2) == 1u && fx_rfull(0) == 0u);
    for (i = 0, eq = 1; i < 30u; i++) {
        blk(2, 40000, i);
        eq &= !any(mix_l) && !any(mix_r) && !any(send_r);
    }
    check("2. C outputs exactly 0 with a send in (no pass-through), its send bus fed silence", eq);
    check("... fx_rmute (the user's, saved with the project) is not touched", fx_rmute == 0);
    song.g[G_CRET + 2u] = 127;
    blk(2, 40000, 40);
    check("... at full RET as well", !any(mix_l) && !any(mix_r));
    slots(0, FXT_NONE, FXT_NONE);                       /* T1 lets go: its insert fades out at 0 first, then C is placed */
    for (i = 0; i < 40u; i++) {
        ins_part(&trk[0], y, CTL);
        fxi_poll();
    }
    check("3. T1 gave its instance back after the fade: C's ECHO is placed (OK), no longer FULL",
          ifx_lim_at(FXT_ECHO, IFX_RT(2)) != FXI_NONE && fxi_ostate(FXI_RT0 + 2u, FXT_ECHO) == FXI_OK && !fx_rfull(2));
    for (i = 0, eq = 0; i < 20u; i++) {
        blk(2, 40000, i);
        eq |= (uint32_t)any(mix_l) | (uint32_t)any(mix_r);
    }
    check("... C plays again (its send through ECHO is heard)", eq != 0);
    fxs_load(3, FXT_DIST);
    fxi_poll();
    for (i = 0, eq = 0; i < 20u; i++) {
        blk(2, 40000, i);
        eq |= (uint32_t)any(mix_l);
    }
    check("4. a type that is not limited (DIST) on C: heard, never muted by the rule", eq != 0 && !fx_rfull(2));
    printf("fx_rt_full: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
