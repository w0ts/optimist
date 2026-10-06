/* SPDX-License-Identifier: GPL-3.0-only */
/* Every drum kit this build has makes sound: each lane of each built kit (drum_kit_built), hit alone at a step's
 * velocity on an otherwise silent drum track, is heard through drums_render. Built with the configuration's
 * switches (tools/builder/verify.py: -include its header; run_tests.sh: the X0X kits with GLIDE), so a switch that
 * silences a kit's path fails here (the X0X kits under FELUCCA_GLIDE were silent: drums_mix's glides returned
 * before the X0X channels when no other drum voice sounded).
 *   build/host/kits_sound_test        -> "N kits x 16 lanes heard" or the silent ones, exit 1 */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define RN (FS / 2u / CTL * CTL)         /* 0.5 s (whole blocks) */

static void reset(void)
{
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    rng_state = 0x1234567u;
#if DRUM_X0X
    memset(&xd, 0, sizeof xd);                  /* (the models from their power-on state) */
    memset(&xc, 0, sizeof xc);
#endif
}

/* the peak of note at kit over RN samples, the hit at the start */
static int32_t hit_peak(uint32_t kit, uint32_t note)
{
    uint32_t j, k;
    int32_t pk = 0;
    reset();
    TDRUM->p[P_E0] = (int16_t)kit;
    TDRUM->p[P_PAN] = 0;
    song.g[G_DRLVL] = 100;
    drum_on(note, 100);
    for (j = 0; j < RN / CTL; j++) {
        int32_t l[CTL] = {0}, r[CTL] = {0}, rev[CTL] = {0};
        drums_render(l, r, rev, CTL);
        for (k = 0; k < CTL; k++) {
            int32_t a = l[k] < 0 ? -l[k] : l[k], b = r[k] < 0 ? -r[k] : r[k];
            pk = a > pk ? a : pk;
            pk = b > pk ? b : pk;
        }
    }
    return pk;
}

int main(void)
{
    uint32_t kit, l, kits = 0, silent = 0;
    for (kit = 0; kit < DRUM_KITS; kit++) {
        if (!drum_kit_built(kit))
            continue;
        kits++;
        for (l = 0; l < DRUM_LANES; l++)
            if (hit_peak(kit, LANE_NOTE[l]) == 0) {
                printf("kit %2u %-10s lane %2u (note %u): silent  FAIL\n", kit, DRUM_KIT_NAMES[kit], l, LANE_NOTE[l]);
                silent++;
            }
    }
    if (!kits) {
        printf("no drum kit built  FAIL\n");
        return 1;
    }
    printf("%u kits x %u lanes: %s\n", kits, DRUM_LANES, silent ? "silent lanes  FAIL" : "every lane heard");
    return silent ? 1 : 0;
}
