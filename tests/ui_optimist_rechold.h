/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, REC held clears the selected track (SLOOP's hold, docs/UI-OPTIMIST-DESIGN.md 11.6).
 * Included after the helpers. */

static void rh_hold_ms(uint32_t ms)                     /* REC pressed, held ms, not let go */
{
    uint32_t t0;
    press(B_REC);
    t0 = fm1_ms;
    while (fm1_ms - t0 < ms)
        frame();
}
static void rec_hold_tests(void)
{
    track_t *t = &trk[1];
    reset_ui();
    if (song.playing)
        tap(B_PLAY), frames(3);
    song.sel = 1;
    frame();
    t->step[0].time = ST_NOTE, t->step[0].n = 1, t->step[0].note[0] = 60;
    tap(B_REC);
    check(rec_wait && !rh.ring, "REC tapped: armed (the first note starts it), as ever");
    tap(B_REC);
    check(!rec_wait && !song.rec, "REC tapped again: off");
    rh_hold_ms(500);
    check(rec_wait && !rh.ring, "REC held 0.5 s: still the press (armed), no ring yet");
    {
        uint32_t t0 = fm1_ms;
        while (fm1_ms - t0 < 300u)
            frame();
    }
    check(!rec_wait && !song.rec && rh.ring && op_overlay() == 3u,
          "held past 0.7 s: the press undone, the ring over the panel");
    {
        uint32_t t0 = fm1_ms;
        while (fm1_ms - t0 < 650u)
            frame();
        ui.force = 1;
        frame();
        ppm("opt-rec-hold");
        check(px_in(80, OP_PY + MODAL_H / 2u - 50u, 80, 14, trk_col(1)) && t->step[0].n == 1,
              "the ring half full, in T2's colour; nothing cleared yet");
    }
    release(B_REC);
    frames(2);
    check(!rh.ring && t->step[0].n == 1 && !rec_wait && op_overlay() != 3u, "let go before the end: nothing, the ring gone");
    rh_hold_ms(700 + 1300 + 50);
    check(t->step[0].n == 0 && !rh.ring && !strcmp(ui.msg, "T2 CLEARED") && !rec_wait,
          "held to the end (0.7 + 1.3 s): T2 cleared, said; not armed");
    frames(20);
    release(B_REC);
    frames(2);
    check(t->step[0].n == 0 && !rh.ring, "let go after: nothing more");
    press(B_SAVE);                                      /* SAVE then HOME: undo */
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(t->step[0].n == 1, "undo: the track back");
    lane_transport(1);
    rh_hold_ms(700 + 1300 + 50);
    release(B_REC);
    frames(2);
    check(t->step[0].n == 0 && !song.rec, "while playing too: cleared, recording not left on");
    lane_transport(0);
    {   /* HOME + REC: the clear behind the modal, kept as a second way */
        t->step[0].time = ST_NOTE, t->step[0].n = 1, t->step[0].note[0] = 60;
        fm1_in.buttons |= BT(B_HOME);
        frame();
        tap(B_REC);
        fm1_in.buttons &= ~BT(B_HOME);
        frame();
        check(op_armed() && !rh.ring, "HOME + REC: the question (no ring)");
        tap(B_SAVE);
        check(t->step[0].n == 0, "... YES: cleared");
    }
    track_defaults_steps(t);
    song.sel = 0;
    reset_ui();
}
