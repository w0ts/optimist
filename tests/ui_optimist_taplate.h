/* STEP: a tap is judged by when the key went down and up, not by when a slow UI frame got to them (the user, 2026-10-21: "the screen
 * registers the press, not the step sequencer"). The sequencer stamps each key event as it queues it (seq.c lk_push, lk_t); op_drain hands
 * the stamp to step_key (lk_ev_ms), so a long frame (a heavy redraw, 100+ ms) between the press and the release no longer makes a tap a hold. */
static void taplate_tests(void)
{
    static int32_t o[CTL * 2];
    uint32_t q;
    reset_ui();
    song.sel = 0;
    memset(trk[0].step, 0, sizeof trk[0].step);
    op_enter(SCR_STEP);
    st.page = 0;
    st.follow = 0;                                      /* (the window on steps 1..16 whatever the tests before left) */
    frames(3);
    check(!step_has(&trk[0], 2), "late frame: step 3 empty to begin with");
    kdown(WK(2));                                       /* (the press: seen by the next frame) */
    fm1_in.notes &= ~(1u << WK(2));                     /* (let go 16 ms later, really) */
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);                              /* (the sequencer queues the release now) */
    fm1_ms += 600;                                      /* (the UI frame that gets to it is slow: 600 ms) */
    frame();
    frames(2);
    check(step_has(&trk[0], 2), "a tap whose release waited 600 ms in the queue (a slow frame): the step is set, not taken for a hold");
    memset(trk[0].step, 0, sizeof trk[0].step);
    frames(3);
    kdown(WK(2));
    frames(HOLD_FRAMES + 20u);                          /* (a real hold: longer than HOLD) */
    kup(WK(2));
    frames(2);
    check(!step_has(&trk[0], 2), "... a key really held past HOLD still only opens the cards: the step stays empty");
}
