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
          "MASTER page: three return lines, REV DLY CHO (every FX built here)");
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
    y = (uint32_t)(OP_PY + MXM_CY + 6);
    check(px_in(MXM_GX, y, 40, MXM_CH, C_HI) && !px_in(MXM_GX, y + MXM_CP, 40, MXM_CH - 1u, C_HI) &&
          !px_in(MXM_GX, y + 2u * MXM_CP, 40, MXM_CH - 1u, C_HI),
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
    check(st.roll_oct == 0 && lo0 <= 60 && hi0 >= 64, "the roll: fitted to the pattern (no offset)");
    tap(B_OCTUP);
    roll_range(TSEL);
    check(rg.lo == lo0 + 12 && rg.hi == hi0 + 12 && song.octave == 0 && st.roll_oct == 1,
          "STEP's roll, OCT+: the view an octave up, the keyboard octave untouched");
    check(px_at(s35_x(4), s35_y(64), lvl_col(trk_col(0), LV_NORM)) || 64 < rg.lo,
          "... drawn there (E4 where the new view puts it, or out of it)");
    ppm("opt-cards2-roll-octup");
    tap(B_OCTDN);
    tap(B_OCTDN);
    roll_range(TSEL);
    check(rg.lo == lo0 - 12 && rg.hi == hi0 - 12 && song.octave == 0, "OCT- OCT-: an octave under the fit");
    press(B_OCTDN);                                     /* both together: the fit again */
    press(B_OCTUP);
    release(B_OCTUP);
    release(B_OCTDN);
    roll_range(TSEL);
    check(rg.lo == lo0 && st.roll_oct == 0 && song.octave == 0, "OCT- + OCT+ together: the fit again");
    for (i = 0; i < 14u; i++)                           /* the top: it stops, one press brings it back */
        tap(B_OCTUP);
    roll_range(TSEL);
    lo = rg.lo;
    check(rg.hi == 127 && st.roll_oct < 14, "OCT+ held on: the view stops at note 127 (the presses past it not counted)");
    tap(B_OCTDN);
    roll_range(TSEL);
    check(rg.lo < lo, "... one OCT- moves it down again");
    song.sel = 1;                                       /* another track: its fit */
    frames(2);
    roll_range(TSEL);
    check(st.roll_oct == 0, "another track: the roll's fit, no offset carried");
    song.sel = 0;
    frames(2);
    roll_range(TSEL);
    check(rg.lo == lo0 && st.roll_oct == 0, "... back on T1: its fit again");
    kdown(WK(0));                                       /* a held step + OCT: transposed, the view not moved */
    frames(HOLD_FRAMES);
    tap(B_OCTUP);
    check(TSEL->step[0].note[0] == 72 && st.roll_oct == 0 && song.octave == 0, "a step held + OCT+: the step transposed, the view and the octave as they were");
    kup(WK(0));
    frames(2);
    press(B_HOME);                                      /* HOME + OCT: the window, as before */
    tap(B_OCTUP);
    release(B_HOME);
    check(st.roll_oct == 0 && st.follow == 0, "HOME + OCT+: the window (FOLLOW off), the view not moved");
    press(B_HOME);
    press(B_OCTDN);
    press(B_OCTUP);
    release(B_OCTUP);
    release(B_OCTDN);
    release(B_HOME);
    check(st.follow == 1 && st.roll_oct == 0, "HOME + both OCT: FOLLOW again");
    st.page = 0;
    s35_open();
    song.sel = TRK_DRUM;                                /* the drum track: OCT is ghost / hard, no view moved */
    tap(B_OCTUP);
    check(st.roll_oct == 0 && song.octave == 0, "the drum track's STEP: OCT moves no view, no octave");
    song.sel = 0;
    s35_open();
}
