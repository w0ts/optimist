/* SPDX-License-Identifier: GPL-3.0-only */
/* bench 20's card sample (system/bench.c, FELUCCA_BENCH_PLUG_SAMPLE: a path from Optimist/, as the card's index holds
 * it): the parts on the plugin get it as their CARD sample, as the sample picker would (sd_mode.c: sst_part_set, SRC on
 * CARD; SLICE's SRC CARD, the others' SMP_CARD). Included after the card's code (felucca.c); main loop, once the index
 * has the file (the mount's index pass): 1 done */
#if FELUCCA_BENCH == 20 && FELUCCA_PLUGINS && FELUCCA_SD_STREAM && defined(FELUCCA_BENCH_PLUG_SAMPLE)
static int bench_card_sample(void)
{
    uint32_t p;
    if (sd_state != SD_READY || sdx_open() != FR_OK || sdx_find_path(FELUCCA_BENCH_PLUG_SAMPLE, &sdx.rec) < 0)
        return 0;
    for (p = 0; p < (uint32_t)FELUCCA_BENCH_PLUG_PARTS && p < NPART; p++) {
        sst_part_set(p, sdx.rec.hash);
        trk[p].p[P_E0] = (int16_t)(eng_uid(trk[p].engine) == ENG_UID_SLICE ? SLC_SRC_CARD : SMP_CARD);
    }
    return 1;
}
#endif
