/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's rulings of 2026-10-12 (docs/UI-OPTIMIST-DESIGN.md sections 4.2 and 4.8):
 *   the mixer's MASTER page   under GR the FX returns' meters, REV DLY CHO (those built), no T1 .. DR lines; the
 *                             returns measured in fx.c (fx_ret_tap, a sample a block), taken by ui/meters.c
 *   OCT on STEP's roll        the view an octave down / up from its fit, the keyboard octave untouched; both: the fit
 *                             again; at the end of 0..127 it stays; another track: its fit; a held step + OCT still
 *                             transposes; SEQ held (the keys play): the keyboard octave as before
 * Included after ui_optimist_step35.h (its s35_open, s35_note). */

static void c2_master_tests(void)
{
    static const int32_t zero[CTL];
    static int32_t in[CTL], wl[CTL], wr[CTL];
    uint8_t ret[3];
    uint32_t i, n, ok = 1, y;
    reset_ui();
    turn(EN_ALGO, -1);                                  /* MASTER, alone */
    ui.force = 1;
    frame();
    n = mx_rets(ret);
    check(mxd.first == MXR_MASTER && n == 3u && ret[0] == 2u && ret[1] == 1u && ret[2] == 0u,
          "MASTER page: three return lines, R1 R2 R3 = REV DLY CHO (the default; every FX built here)");
    for (i = 0; i < n; i++) {                           /* the names in C_HI where T1 .. DR were */
        y = (uint32_t)(OP_PY + MXM_CY + (int32_t)i * MXM_CP);
        ok &= (uint32_t)px_in(MXM_X, y, 24, 14, C_HI);
    }
    for (i = 0; i < NTRK; i++)
        ok &= (uint32_t)!px_in(0, (uint32_t)(OP_PY + MXM_CY), 240, (uint32_t)(OP_PH - MXM_CY), trk_col(i));
    y = (uint32_t)(OP_PY + MXM_CY + 3 * MXM_CP);        /* (where DR's line was: nothing) */
    ok &= (uint32_t)!px_in(MXM_X, y, 240u - MXM_X, 240u - y, C_HI);   /* (the MASTER chip at x 2 runs down: not counted) */
    check(ok, "MASTER page: the REV DLY CHO names, no track's colour under GR, nothing in DR's old place");
    check(px_in(MXM_GX, (uint32_t)(OP_PY + MXM_GY), MXM_GW, MXM_GH, C_LINE), "MASTER page: the GR line kept");
    ppm("opt-cards2-master");
    mt.ret[2] = 12000;                                  /* REV loud, DLY and CHO silent (meter_tap's side: set here) */
    mt.ret[1] = mt.ret[0] = 0;
    memset(fx_ret_pk, 0, sizeof fx_ret_pk);
    ui_draw();
    y = (uint32_t)(OP_PY + MXM_CY + MXM_CV);
    check(px_in(MXM_GX, y, 40, MXM_CH, C_OK) && !px_in(MXM_GX, y + MXM_CP, 40, MXM_CH - 1u, C_OK) &&
          !px_in(MXM_GX, y + 2u * MXM_CP, 40, MXM_CH - 1u, C_OK),
          "MASTER page: the REV return's meter fills, DLY's and CHO's stay empty");
    ppm("opt-cards2-master-rev");
    /* the tap: each bus's return seen apart (fx_buses, one block at a time, the sends set by hand) */
    memset(fx_ret_pk, 0, sizeof fx_ret_pk);
    for (i = 0; i < CTL; i++)
        in[i] = (i & 8u) ? 9000 : -9000;
    for (i = 0; i < 400u; i++)
        if (fx_buses(zero, zero, in, wl, wr, CTL) == 0)
            break;
    check(fx_ret_pk[2] > 500 && fx_ret_pk[1] == 0 && fx_ret_pk[0] == 0, "the tap: a reverb send, the REV return only");
    memset(fx_ret_pk, 0, sizeof fx_ret_pk);
    for (i = 0; i < 2000u; i++)                         /* (the delay's first repeat comes a delay later) */
        fx_buses(zero, in, zero, wl, wr, CTL);
    check(fx_ret_pk[1] > 500 && fx_ret_pk[0] == 0, "the tap: a delay send, the DLY return");
    memset(fx_ret_pk, 0, sizeof fx_ret_pk);
    for (i = 0; i < 400u; i++)
        fx_buses(in, zero, zero, wl, wr, CTL);
    check(fx_ret_pk[0] > 500, "the tap: a chorus send, the CHO return");
    mt.ret[0] = mt.ret[1] = mt.ret[2] = 0;
    audio_halves++;
    meter_tap();
    check(mt.ret[0] > 500 && fx_ret_pk[0] == 0, "meter_tap: the returns' peaks taken, the ISR's emptied");
    check(meter_ret_take(0) > 500 && meter_ret_take(0) == 0, "meter_ret_take: the peak once, then 0");
    for (i = 0; i < 4000u; i++)                         /* (the tails out: the tests after start from silence) */
        fx_buses(zero, zero, zero, wl, wr, CTL);
    memset(fx_ret_pk, 0, sizeof fx_ret_pk);
    turn(EN_ALGO, 1);
    frame();
    check(mxd.first == MXR_T1 && px_in(6, mx_row_y(MXR_T1) + 3u, 16, 14, C_WHITE),
          "the channels' view after MASTER: as before");
    reset_ui();
}

static void c2_roll_tests(void)
{
    int32_t lo0, hi0, lo;
    uint32_t i;
    s35_open();
    s35_note(0, 60, LV_NORM);
    s35_note(4, 64, LV_NORM);
    sync_reload = 1;
    frames(2);
    roll_range(TSEL);
    lo0 = rg.lo, hi0 = rg.hi;
    check(song.octave == 0 && lo0 <= 60 && hi0 >= 64, "the roll: fitted to the pattern (octave 0)");
    tap(B_OCTUP);
    roll_range(TSEL);
    check(rg.lo == lo0 + 12 && rg.hi == hi0 + 12 && song.octave == 1,
          "STEP's roll, OCT+ once: exactly 12 semitones up, the view an octave up AND the keyboard octave (one value)");
    check(px_at(s35_x(4), s35_y(64), lvl_col(trk_col(0), LV_NORM)) || 64 < rg.lo,
          "... drawn there (E4 where the new view puts it, or out of it)");
    ppm("opt-cards2-roll-octup");
    tap(B_OCTDN);
    tap(B_OCTDN);
    roll_range(TSEL);
    check(rg.lo == lo0 - 12 && rg.hi == hi0 - 12 && song.octave == -1, "OCT- OCT-: an octave under the fit, the keyboard octave -1");
    tap(B_OCTUP);
    roll_range(TSEL);
    check(rg.lo == lo0 && song.octave == 0, "OCT+ from -1: the fit again, octave 0 (one tap, one octave)");
    for (i = 0; i < 6u; i++)                            /* the top: the keyboard octave stops at +3, the view with it */
        tap(B_OCTUP);
    roll_range(TSEL);
    lo = rg.lo;
    check(song.octave == 3 && rg.lo == lo0 + 36, "OCT+ held on: octave +3 and the view three octaves up, no further");
    tap(B_OCTDN);
    roll_range(TSEL);
    check(song.octave == 2 && rg.lo < lo, "... one OCT- moves both down again (no drift between the view and the octave)");
    song.sel = 1;                                       /* another track: the same octave, its own fit */
    frames(2);
    check(song.octave == 2, "another track: the octave (and so the view's shift) is the same value");
    song.sel = 0;
    frames(2);
    tap(B_OCTDN);
    tap(B_OCTDN);
    roll_range(TSEL);
    check(rg.lo == lo0 && song.octave == 0, "... back on T1 at octave 0: its fit again");
    song.sel = 2;                                       /* an EMPTY pattern: the view is around the pick, which the octave moved too (the roll moved 2 octaves a tap) */
    frames(2);
    roll_range(TSEL);
    lo = rg.lo;
    tap(B_OCTUP);
    roll_range(TSEL);
    check(song.octave == 1 && rg.lo == lo + 12, "an empty pattern, OCT+ once: the view exactly 12 semitones up");
    tap(B_OCTDN);
    tap(B_OCTDN);
    roll_range(TSEL);
    check(song.octave == -1 && rg.lo == lo - 12, "... OCT- twice: 12 down from the start, one a tap");
    tap(B_OCTUP);
    song.sel = 0;
    frames(2);
    kdown(WK(0));                                       /* a held step + OCT: transposed, the view not moved */
    frames(HOLD_FRAMES);
    tap(B_OCTUP);
    check(TSEL->step[0].note[0] == 72 && song.octave == 0, "a step held + OCT+: the step transposed, the octave (and so the view) as it was");
    kup(WK(0));
    frames(2);
    turn(EN_PRESET, 1);                                 /* PRESETS: the window, the octave and view not moved */
    check(song.octave == 0 && st.follow == 0, "PRESETS: the window (FOLLOW off), the octave and view not moved");
    press(B_OCTDN);
    turn(EN_PRESET, 1);
    release(B_OCTDN);
    check(st.follow == 1 && song.octave == 0, "OCT- (SHIFT) + PRESETS: FOLLOW again");
    song.octave = 0;
    press(B_OCTUP);
    check(song.octave == 0, "OCT+ pressed: the octave changes when it is let go, not at the press");
    release(B_OCTUP);
    check(song.octave == 1 && ui.toast_t && !strcmp(ui.msg, "OCTAVE: 1"), "OCT+ let go: the octave 1 and its toast \"Octave: 1\"");
    press(B_OCTUP);
    kdown(WK(0));
    kup(WK(0));
    release(B_OCTUP);
    check(song.octave == 1, "OCT+ held while a key is played (SHIFT2): the octave stays");
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(song.octave == 1, "OCT- held while a knob is turned (SHIFT): the octave stays");
    song.octave = 0;
    st.page = 0;
    s35_open();
    song.sel = TRK_DRUM;                                /* the drum track: OCT is ghost / hard, no view moved */
    tap(B_OCTUP);
    check(song.octave == 0, "the drum track's STEP: OCT moves no octave");
    song.sel = 0;
    s35_open();
}
