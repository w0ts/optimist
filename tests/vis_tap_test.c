/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 phase 5, the visualiser's audio tap (fx.c vis_tap_block); build with FELUCCA_VIS=1 (tests/run_tests.sh):
 * a block's mix is copied whole (two copies, no work per sample), the write position stays block aligned.
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#if !FELUCCA_VIS
#error "build with -DFELUCCA_VIS=1"
#endif

static int fails;
static void check(int ok, const char *what)
{
    printf("vis_tap: %-80s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint32_t ok = 1, i, wr0;
    int32_t l[CTL], r[CTL];
    for (i = 0; i < CTL; i++) { l[i] = (int32_t)i * 100 - 1500; r[i] = -l[i]; }
    wr0 = vis_wr;
    vis_tap_block(l, r, CTL);
    check(vis_wr == wr0 + CTL, "the tap: one block advances the write position by CTL");
    ok = 1;
    for (i = 0; i < CTL; i++)
        ok &= vis_pcm[0][(wr0 + i) & (VIS_RING - 1u)] == l[i] && vis_pcm[1][(wr0 + i) & (VIS_RING - 1u)] == r[i];
    check(ok, "the tap: left and right copied whole, before the volume (raw mix values)");
    ok = 1;
    for (i = 0; i < 100u; i++)
        vis_tap_block(l, r, CTL);
    check(vis_wr == wr0 + 101u * CTL && (vis_wr & (CTL - 1u)) == 0u, "the tap: stays block aligned over 100 more blocks (the ring wraps)");
    printf("vis_tap: %d failed\n", fails);
    return fails;
}
