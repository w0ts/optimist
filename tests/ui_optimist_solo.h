/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c: no solo in the Optimist UI (the user, 2026-10-16: "remove solo"). SHIFT + SHIFT2 on the volume
 * card solos nothing, the GLO layer's keys 5-8 neither, and a project's lane solo (the drum mix TLV, drum_mix.c) is not
 * loaded: nothing could undo it here. Included after ui_optimist_strike.h. */
static void solo_tests(void)
{
    uint8_t tlv[DLM_TLV_N + 4u];
    uint32_t i, n;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    song.solo = 0;
    dlm_solo_clear();
    reset_ui();
    song.sel = 1;
    frames(2);
    mx.set = 0;
    ui.row[SCR_HOME] = MXR_T1 + 1u;
    press(B_OCTDN);
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    release(B_OCTDN);
    frames(2);
    check(song.solo == 0u && !trk_silent(&trk[0]), "no solo: SHIFT + SHIFT2 on T2's VOLUME card solos nothing");
    reset_ui();
    press(B_GLO);
    frames(2);
    for (i = 4; i < 8u; i++)
        key(WK(i));
    release(B_GLO);
    check(song.solo == 0u, "no solo: GLO + keys 5-8 solo nothing");
    dlm_set_solo(3, 1);                                 /* a project saved with a lane soloed (SLOOP's UI, an older build) */
    n = dlm_tlv(tlv);
    dlm_solo_clear();
    dlm_untlv(tlv, n);
    check(n >= DLM_TLV_N && !dlm_soloed(3) && !(song.solo & SOLO_LANE), "no solo: a project's lane solo is not loaded (nothing here undoes it)");
    reset_ui();
}
