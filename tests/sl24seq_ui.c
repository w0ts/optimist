/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.4 sequencer's UI (firmware/src/backports24seq.h), included by ui_pages_test.c, each with its switch:
 *   micro  SEQ + a step held + KNOB 4: its nudge (the dial "nudge"), clamped -32..31; the step's dot
 *   fills  SEQ + a step held + OCT+: normal -> fill only -> no fill -> normal; OCT-: nudge, locks, fill cleared;
 *          GLO + key 9 held: a fill while held; GLO + key 10: the next bar (again: off) */
static void sl24seq_ui_tests(void)
{
#if FELUCCA_MICRO || FELUCCA_FILLS
    song.sel = 0; go_home(); frame();
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
    press(B_SEQ); frames(10);
    key(0);                                           /* step 1 on */
    fm1_in.notes = 1u << 0; frame();                  /* step 1 held */
#if FELUCCA_MICRO
    encs[panel.enc[EN_K4]] = 5; frame();
    check(TX(&trk[0])->micro[0] == 5, "micro: step 1 held + KNOB 4 +5: nudged +5/64");
    encs[panel.enc[EN_K4]] = -60; frame();
    check(TX(&trk[0])->micro[0] == MICRO_MIN, "micro: KNOB 4 -60: clamped at -32 (half a step early)");
#endif
#if FELUCCA_PLOCK
    lock_par = P_ED_FLT;
    encs[panel.enc[EN_PRESET]] = 3; frame();
    {
        int q = stepx_lock_find(TX(&trk[0]), 0, P_ED_FLT);
        check(q >= 0 && TX(&trk[0])->lock[q].val == trk[0].p[P_ED_FLT] + 3 && !strncmp(layer_sub_shown, "lock flt +", 10),
              "locks: step held + PRESETS +3: ENV DEST FLT locked at the track's value + 3, the title: lock flt +n");
    }
    encs[panel.enc[EN_ALGO]] = 1; frame();
    check(lock_par != P_ED_FLT && lock_ok(&trk[0], lock_par), "locks: ALGORITHM: the next lockable parameter");
    lock_par = P_SDIV;
    encs[panel.enc[EN_PRESET]] = 1; frame();
    check(str_eq(ui.msg, "NOT LOCKABLE"), "locks: a parameter that does not lock: NOT LOCKABLE");
#endif
#if FELUCCA_FILLS
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(step_fill(&trk[0], 0) == FC_FILL && str_eq(ui.msg, "FILL ONLY"), "fills: step held + OCT+: FILL ONLY");
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(step_fill(&trk[0], 0) == FC_NOFILL, "fills: OCT+ again: NO FILL");
#endif
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(!TX(&trk[0])->micro[0] && stepx_fill(TX(&trk[0]), 0) == FC_NORM && step_on(&trk[0].step[0]) && ui.step_page == 0,
          "step held + OCT-: its nudge and fill cleared, the step and the page kept");
    fm1_in.notes = 0; frame();
    release(B_SEQ);
    steps_clear(&trk[0]);
#endif
#if FELUCCA_FILLS
    press(B_GLO); frames(10);
    fm1_in.notes = 1u << key_of_white(8); frame();
    check(fill_held == 1, "fills: GLO + key 9 held: a fill");
    fm1_in.notes = 0; frame();
    check(fill_held == 0, "fills: key 9 let go: the fill ends");
    key(key_of_white(9));
    check(fill_arm == 1 && str_eq(ui.msg, "FILL: NEXT BAR"), "fills: GLO + key 10: the next bar is a fill");
    key(key_of_white(9));
    check(fill_arm == 0, "fills: GLO + key 10 again: off");
    release(B_GLO);
#endif
}
