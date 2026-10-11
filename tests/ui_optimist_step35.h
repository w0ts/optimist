/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP batch 35 (the user, 2026-10-11): step entry follows the keyboard octave; a held step
 * and OCT- / OCT+ transpose it by an octave. Included after the helpers. */

static void s35_open(void)                              /* a synth track's STEP, stopped, the octave 0, steps empty */
{
    uint32_t i;
    reset_ui();
    song.sel = 0;
    song.octave = 0;
    for (i = 0; i < NSTEP; i++)
        step_clear(&TSEL->step[i]);
    tap(B_SEQ);
    frames(2);
}
static uint32_t s35_pick(uint32_t k)                    /* SEQ held + key k: the pen; its note */
{
    uint32_t n;
    press(B_SEQ);
    kdown(k);
    n = pen_note[0];
    kup(k);
    release(B_SEQ);
    frames(2);
    return n;
}
static void s35_entry_tests(void)
{
    uint32_t n, ok;
    char b[16];
    s35_open();
    n = s35_pick(WK(7));
    kdown(WK(0));
    kup(WK(0));
    check(step_on(&TSEL->step[0]) && TSEL->step[0].note[0] == n, "STEP entry: a tapped step takes the note played");
    tap(B_OCTUP);
    check(song.octave == 1, "STEP, OCT+ with no step held: the keyboard octave up");
    kdown(WK(1));
    kup(WK(1));
    check(TSEL->step[1].note[0] == n + 12u, "STEP entry after OCT+: the note an octave up (it follows the keyboard octave)");
    step_pick_name(b, sizeof b);
    {
        char e[8];
        note_name(e, n + 12u);
        check(!strncmp(b, e, strlen(e)) && (b[strlen(e)] == 0 || b[strlen(e)] == ' '), "... the pick's name says the octave too");
    }
    tap(B_OCTDN);
    tap(B_OCTDN);
    kdown(WK(2));
    kup(WK(2));
    check(TSEL->step[2].note[0] == n - 12u, "STEP entry after OCT- OCT-: the note an octave down");
    n = s35_pick(WK(7));                                /* played at -1: the pen as heard, no second shift */
    kdown(WK(3));
    kup(WK(3));
    ok = TSEL->step[3].note[0] == n;
    check(ok, "STEP entry: a note played at another octave is entered as played (not shifted twice)");
    song.octave = 0;
    s35_open();
}
static void s35_transpose_tests(void)
{
    uint32_t n, pg;
    s35_open();
    n = s35_pick(WK(7));
    kdown(WK(4));
    kup(WK(4));
    TSEL->step[4].note[1] = (uint8_t)(n + 4u);          /* (a chord: both notes move) */
    TSEL->step[4].n = 2;
    kdown(WK(4));
    frames(HOLD_FRAMES);
    tap(B_OCTUP);
    check(TSEL->step[4].note[0] == n + 12u && TSEL->step[4].note[1] == n + 16u && song.octave == 0,
          "a step held + OCT+: its notes an octave up, the keyboard octave untouched");
    tap(B_OCTDN);
    tap(B_OCTDN);
    check(TSEL->step[4].note[0] == n - 12u && TSEL->step[4].note[1] == n - 8u && song.octave == 0,
          "a step held + OCT- OCT-: two octaves down from there");
    check(ui.cards_t > 0u && ui.hot_lit && ui.hot == 0u, "... the held step's cards up, NOTE lit");
    kup(WK(4));
    frames(2);
    check(step_on(&TSEL->step[4]), "... let go: the step kept (edited, not a tap)");
    TSEL->step[4].note[0] = 120;
    TSEL->step[4].n = 1;
    kdown(WK(4));
    frames(HOLD_FRAMES);
    tap(B_OCTUP);
    check(TSEL->step[4].note[0] == 127u, "a step held + OCT+ near the top: clamped to the valid range");
    kup(WK(4));
    frames(2);
    pg = 0;
#if FELUCCA_PLOCK
    pg = 1;
#endif
    if (pg) {                                           /* on a p-lock page: back to the step's page, its NOTE lit */
        kdown(WK(4));
        frames(HOLD_FRAMES);
        tap(B_ENV);
        tap(B_OCTDN);
        check(st.hp == 0u && st.lock_pg == LOCK_NONE && TSEL->step[4].note[0] == 115u, "a step held on a lock page + OCT-: transposed, the cards back to the step's NOTE");
        kup(WK(4));
        frames(2);
    }
    s35_open();
}
