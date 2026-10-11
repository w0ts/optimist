/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP UI, the SEQ layer's step keys toggle their step only on a tap (the user, 2026-10-11: "long press on step to open
 * plock, should not toggle the step on/off"): the toggle happens when the key is let go, before HOLD_MS, with no knob
 * turned and no page changed meanwhile; a long press, a knob or a page leave the step as it was (an empty one gets no
 * note); a step held + another key tapped toggles that one. Every build (synth track 1). Included by ui_pages_test.c;
 * the DRUMS grid's keys: tests/drum_step_ui.c. */
static int ssh_on(uint32_t idx) { return step_on(&trk[0].step[idx]); }
static void ssh_down(uint32_t w) { fm1_in.notes |= 1u << key_of_white(w); frame(); }
static void ssh_up(uint32_t w) { fm1_in.notes &= ~(1u << key_of_white(w)); frames(2); }
static void sloop_step_hold_tests(void)
{
    song.sel = 0; song.playing = 0; go_home(); frame();
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 32;
    press(B_SEQ); frames(10);
    ui.step_page = 0; frame();
    /* a tap: toggled when let go */
    ssh_down(0);
    check(!ssh_on(0), "SEQ step hold: an empty step pressed: nothing yet (set when let go)");
    ssh_up(0);
    check(ssh_on(0), "SEQ step hold: ... let go (a tap): set");
    key(key_of_white(0));
    check(!ssh_on(0), "SEQ step hold: a set step tapped: cleared");
    /* a long press: no toggle */
    ssh_down(1); frames(HOLD_FRAMES);
    check(!ssh_on(1) && ((ui.step_held >> 1) & 1u), "SEQ step hold: an empty step held long: a held step, no note made");
    ssh_up(1);
    check(!ssh_on(1), "SEQ step hold: ... let go: still empty");
    key(key_of_white(2));
    ssh_down(2); frames(HOLD_FRAMES);
    ssh_up(2);
    check(ssh_on(2), "SEQ step hold: a set step held long, let go: still set");
    /* a short press + a knob: no toggle */
    ssh_down(2);
    encs[panel.enc[EN_K2]] = 3; frame();
    ssh_up(2);
    check(ssh_on(2), "SEQ step hold: a set step, a short press + a knob: kept");
    ssh_down(3);
    encs[panel.enc[EN_K2]] = 3; frame();
    ssh_up(3);
    check(!ssh_on(3), "SEQ step hold: an empty step, a short press + a knob: no note made");
    /* a short press + a page: no toggle (the key let go on the other page touches nothing there) */
    ssh_down(2);
    key(3);                                             /* (black key 2: page 2) */
    ssh_up(2);
    check(ssh_on(2) && !ssh_on(18) && ui.step_page == 1u, "SEQ step hold: a set step, a short press + a page key: kept, nothing on the new page");
    key(1);                                             /* (page 1 again) */
    ssh_down(2);
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    ssh_up(2);
    check(ssh_on(2), "SEQ step hold: a set step, a short press + OCT+ (the page, or its fill): kept");
    ui.step_page = 0; frame();
    /* a step held + another key tapped: that one toggles, the one held stays */
    ssh_down(2); frames(HOLD_FRAMES);
    key(key_of_white(5));
    check(ssh_on(5) && ((ui.step_held >> 2) & 1u), "SEQ step hold: a step held + another key tapped: that one set");
    key(key_of_white(5));
    check(!ssh_on(5), "SEQ step hold: ... tapped again: cleared");
    ssh_up(2);
    check(ssh_on(2), "SEQ step hold: ... the one held let go: still set");
    release(B_SEQ); frames(2);
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
    go_home(); frame();
}
