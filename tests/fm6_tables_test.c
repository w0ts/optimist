/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's FM6 tables against Dexed's (tools/gen_tables.py FM6_REF_TABLES: as Melodee generates them):
 * the RAM tables eng_fm6.c builds at boot (sine, MARK I log-sine and exponent, OPL log-sine) and the
 * 2^x / frequency values it figures from FM6_P2A x FM6_P2B, every entry. MARK I's log sine is a quarter
 * folded onto the half cycle (FM6_MKI_FOLD). */
#define FM6_REF_TABLES 1
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(void)
{
    uint32_t i, bad = 0;
    fm6_tables_init();
    for (i = 0; i < 1025u; i++) {
        bad += FM6_SIN[i] != FM6_SIN_REF[i];
        bad += fm6_exp2_at(i) != FM6_EXP2_REF[i];
        bad += fm6_freq_at(i) != FM6_FREQ_REF[i];
    }
    for (i = 0; i < 2048u; i++) {                        /* MARK I: the quarter table, folded, is the half cycle */
        bad += FM6_MKI_LOG[FM6_MKI_FOLD(i)] != FM6_MKI_LOG_REF[i];
        bad += FM6_MKI_LOGQ[FM6_MKI_FOLD(i)] != FM6_MKI_LOG_REF[i];   /* (the flash quarter it copies) */
    }
    for (i = 0; i < 1024u; i++)
        bad += FM6_MKI_EXP[i] != FM6_MKI_EXP_REF[i];
    for (i = 0; i < 512u; i++)
        bad += FM6_OPL_LOG[i] != FM6_OPL_LOG_REF[i];
    printf(bad ? "FM6 tables: %u entries differ from Dexed's\n"
               : "FM6 tables: sine, 2^x, frequency, MARK I, OPL: every entry as Dexed's%.0u\n", bad);
    return bad != 0;
}
