/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP: a step key toggles its step only on a tap (the user, 2026-10-11: "long press on step
 * to open plock, should not toggle the step on/off"). The toggle happens when the key is let go, and only when it was
 * let go before the hold time with nothing edited meanwhile: a long press (the held step's cards), a knob turned, a
 * lock written or a card page changed leave the step as it was. Included by ui_optimist_test.c. */
static void sh_open(uint32_t drum)
{
    reset_ui();
    song.sel = drum ? TRK_DRUM : 0;
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;                               /* (a LEN left by an earlier test or the fuzz: one bar) */
    if (drum)
        lane_select(2);
    tap(B_SEQ);
    frames(2);
}
static int sh_on(uint32_t idx) { return step_has(TSEL, idx); }
static void step_hold_tests(void)
{
    uint32_t d;
    for (d = 0; d < 2u; d++) {
        sh_open(d);
        /* a tap: toggled when let go */
        kdown(WK(0));
        check(!sh_on(0), d ? "step hold, drums: an empty step pressed: nothing yet (set when let go)" : "step hold, synth: an empty step pressed: nothing yet (set when let go)");
        kup(WK(0));
        check(sh_on(0), d ? "step hold, drums: ... let go (a tap): set" : "step hold, synth: ... let go (a tap): set");
        if (d)
            key(WK(0)), key(WK(0));                     /* (drums: a tap cycles normal -> hard -> ghost -> off) */
        key(WK(0));
        check(!sh_on(0), d ? "step hold, drums: a set step tapped: cleared" : "step hold, synth: a set step tapped: cleared");
        /* a long press: the cards, no toggle */
        kdown(WK(1));
        frames(HOLD_FRAMES);
        check(st.held && ui.cards_t > 0u && !sh_on(1), d ? "step hold, drums: an empty step held long: its cards, no note made" : "step hold, synth: an empty step held long: its cards, no note made");
        kup(WK(1));
        check(!sh_on(1), d ? "step hold, drums: ... let go: still empty" : "step hold, synth: ... let go: still empty");
        key(WK(2));
        kdown(WK(2));
        frames(HOLD_FRAMES);
        kup(WK(2));
        check(sh_on(2), d ? "step hold, drums: a set step held long, let go: still set" : "step hold, synth: a set step held long, let go: still set");
        /* a short press with a knob turned: no toggle */
        kdown(WK(2));
        turn(EN_K2, 1);
        kup(WK(2));
        check(sh_on(2), d ? "step hold, drums: a set step, a short press + a knob: kept" : "step hold, synth: a set step, a short press + a knob: kept");
        kdown(WK(3));
        turn(EN_K2, 1);
        kup(WK(3));
        check(!sh_on(3), d ? "step hold, drums: an empty step, a short press + a knob: no note made" : "step hold, synth: an empty step, a short press + a knob: no note made");
        kdown(WK(2));
        turn(EN_SELECT, 1);                             /* (a card page changed) */
        kup(WK(2));
        check(sh_on(2), d ? "step hold, drums: a set step, a short press + SELECT (a card page): kept" : "step hold, synth: a set step, a short press + SELECT (a card page): kept");
        /* a step held + another key tapped: the other toggles as before; the one held stays */
        kdown(WK(2));
        frames(HOLD_FRAMES);
        key(WK(5));
        check(st.held == 1u << 2 && sh_on(5), d ? "step hold, drums: a step held long + another key tapped: that one set" : "step hold, synth: a step held long + another key tapped: that one set");
        if (d)
            key(WK(5)), key(WK(5));
        key(WK(5));
        check(!sh_on(5), d ? "step hold, drums: ... tapped again: cleared" : "step hold, synth: ... tapped again: cleared");
        kup(WK(2));
        check(sh_on(2) && !st.held, d ? "step hold, drums: ... the one held let go: still set" : "step hold, synth: ... the one held let go: still set");
        /* the held key's LED is lit from the press (the feedback before the toggle) */
        kdown(WK(7));
        check((step_leds(0) >> key_of_lane(7)) & 1u, d ? "step hold, drums: a key pressed: its LED lit at once" : "step hold, synth: a key pressed: its LED lit at once");
        kup(WK(7));
        check(sh_on(7) && ((step_leds(0) >> key_of_lane(7)) & 1u), d ? "step hold, drums: ... let go: set, lit" : "step hold, synth: ... let go: set, lit");
    }
#if FELUCCA_PLOCK
    /* an empty step held + a lock: the lock is written on the rest, no note made (as lock_turn always did) */
    sh_open(0);
    kdown(WK(4));
    frames(HOLD_FRAMES);
    tap(B_ENV);
    if (st.lock_pg != LOCK_NONE) {
        const param_desc_t *dd = 0;
        int32_t id = -1, v;
        uint32_t i;
        for (i = 0; i < 4u && id < 0; i++)
            if ((id = lock_param(&PAGES[st.lock_pg], i, &dd)) >= 0)
                turn(EN_K1 + i, 1);
        kup(WK(4));
        check(id >= 0 && lock_get(TSEL, 4, (uint32_t)id, &v) && !sh_on(4), "step hold: an empty step held + a lock: the lock on the rest, no note made");
    } else {
        kup(WK(4));
    }
    /* a short press + a page button: no toggle */
    key(WK(6));
    kdown(WK(6));
    tap(B_ENV);
    kup(WK(6));
    check(sh_on(6), "step hold: a set step, a short press + a page button (its locks): kept");
#endif
    reset_ui();
}
/* a synth step tapped off keeps its pitch; tapped on again it comes back at that pitch, not the pick's (the user,
 * 2026-10-12: "click a step remove the note, clicking again add the note at the same pitch for that step") */
static void step_pitch_tests(void)
{
    uint8_t n0;
    sh_open(0);
    song.octave = 0;
    key(WK(3));
    n0 = TSEL->step[3].note[0];
    TSEL->step[3].note[0] = (uint8_t)(n0 + 5u);         /* (the note moved, as a turn of its NOTE card would) */
    key(WK(3));
    check(!sh_on(3) && TSEL->step[3].time == ST_REST, "step pitch: a set step tapped: off (a rest, silent)");
    song.octave = 1;                                    /* (the pick elsewhere meanwhile) */
    key(WK(3));
    check(sh_on(3) && TSEL->step[3].note[0] == n0 + 5u, "step pitch: ... tapped again: back at the pitch it had, not the pick's");
    song.octave = 0;
    key(WK(4));
    check(sh_on(4) && TSEL->step[4].note[0] == n0, "step pitch: a step never set: the pick, as before");
    fm1_in.buttons |= BT(B_OCTDN);
    key(WK(3));
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    check(sh_on(3) && TSEL->step[3].note[0] == n0 + 5u, "SHIFT (OCT- held) + the step: the step stays (its p-locks only go)");
}
