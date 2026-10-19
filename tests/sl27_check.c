/* SPDX-License-Identifier: GPL-3.0-only */
/* Reads a SLOOP project file (a FUN6 golden, tests/sl27_fun6.bin; or what tests/sl24_export_test.c and
 * tests/sl27_import_test.c export: build/host/sl24_export.bin, build/host/sl27_export.bin) with SLOOP 2.7.1's OWN
 * project.c (isod89/sloop-fm1 v2.7.1 4ec1ac82, its host test harness), as a 2.7 FM-1 loads it: valid or not, saved by
 * 2.7, each drum sound's TUNE / DECAY (its drum_tw_get), the drum hits, each part's arp MODE / HOLD and its steps'
 * flags (2.7's held notes SF_CONT, bits 3..6). Not run by run_tests.sh (it needs 2.7.1's tree); to run it:
 *
 *   W=<a scratch directory>; git archive v2.7.1 firmware tests tools assets | tar -x -C $W   (the SLOOP remote's tag)
 *   cd $W && mkdir -p build/gen && for g in tables fm6_patches samples drumkits font icons; do
 *       python3 tools/gen_$g.py build/gen/felucca_$g.h; done   (gen_fm6_patches -> felucca_fm6.h; Pillow for the font)
 *   cp <optimist>/tests/sl27_check.c tests/   (its includes are SLOOP's tests/hostsim.c and project.c)
 *   cc -w -Ibuild/gen -Ifirmware/src -o sl27_check tests/sl27_check.c -lm && ./sl27_check <file>
 *
 * Measured 2026-10-19: tests/sl27_fun6.bin and build/host/sl27_export.bin both read as written (proj_import 1; the
 * 7 drum hits on their sounds with level and ratchet; TUNE +5 -8 0 +7 .. -1 and DECAY 0 0 -4 +3 .. +2 exact). */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

int main(int argc, char **argv)
{
    static uint8_t b[4096];
    static project_t q;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    size_t n = f ? fread(b, 1, sizeof b, f) : 0;
    uint32_t l, i, k;
    if (f)
        fclose(f);
    printf("read %u B, proj_import %d, saved by 2.7: %d\n", (unsigned)n, proj_import(&q, b, (int)n), (q.rsv[0] & 0x40) != 0);
    drum_tw_get(&q);
    for (l = 0; l < DRUM_LANES; l++)
        printf("sound %2u %-9s tune %+d decay %+d\n", l, LANE_NAME[l], drum_tune[l], drum_dec[l]);
    for (i = 0; i < NSTEP; i++) {
        const dstep_t *s = &q.t[TRK_DRUM].dstep[i];
        for (l = 0; l < DRUM_LANES; l++)
            if (dstep_has(s, l))
                printf("drum step %2u: %s lvl %u rat %u\n", i, LANE_NAME[l], dstep_lvl(s, l), dstep_rat(s, l));
    }
    for (k = 0; k < NPART; k++)
        printf("part %u: AMODE %d (%s) AHOLD %d, step 4 flags 0x%02X, step 5 flags 0x%02X, step 6 flags 0x%02X\n", k,
               q.t[k].p[P_AMODE], N_AMODE[q.t[k].p[P_AMODE] % 15], q.t[k].p[P_AHOLD], q.t[k].step[4].flags,
               q.t[k].step[5].flags, q.t[k].step[6].flags);
    return 0;
}
