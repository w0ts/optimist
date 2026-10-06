/* SPDX-License-Identifier: GPL-3.0-only */
/* Every synthesised kit x every sound: one hit at velocity 110, 2 s each, mono int32 into argv[1]
 * (kit-major, the 16 lanes in gen_drumkits.py order). tools/level_drumkits.py measures them and
 * writes tools/drumkit_levels.json.
 *   cc -O2 -Ibuild/gen-host -Ifirmware/src -Ifirmware/hal tests/drum_level.c -lm -o build/host/drum_level */
#define main hostsim_main
#include "hostsim.c"
#undef main
static const uint8_t LANE_GM[16] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "wb");
    uint32_t kit, ln, b;
    int i;
    if (!f)
        return 1;
    host_tracks_init();
    song.g[G_DRLVL] = 100;
    for (kit = DRUM_SAMPLED; kit < DRUM_SYNTH_END; kit++)
        for (ln = 0; ln < 16u; ln++) {
            memset(&drums, 0, sizeof drums);
            drums.set = -2;
            TDRUM->p[P_E0] = (int16_t)kit;
            drum_on(LANE_GM[ln], 110);
            for (b = 0; b < FS * 2u / CTL; b++) {
                int32_t l[CTL] = {0}, r[CTL] = {0}, rv[CTL] = {0};
                drums_render(l, r, rv, CTL);
                for (i = 0; i < CTL; i++) {
                    int32_t m = (l[i] + r[i]) / 2;
                    fwrite(&m, 4, 1, f);
                }
            }
        }
    fclose(f);
    printf("%u kits x 16 sounds -> %s\n", DRUM_SYNTH_END - DRUM_SAMPLED, argv[1]);
    return 0;
}
