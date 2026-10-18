/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's FM6 tables against Dexed's (tools/gen_tables.py FM6_REF_TABLES: as Melodee generates them):
 * the sine table eng_fm6.c builds at boot and the 2^x / frequency values it figures from FM6_P2A x FM6_P2B, every entry
 * (MARK I's and OPL's tables went with those modes, 2026-10-18). */
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
    printf(bad ? "FM6 tables: %u entries differ from Dexed's\n"
               : "FM6 tables: sine, 2^x, frequency: every entry as Dexed's%.0u\n", bad);
    return bad != 0;
}
