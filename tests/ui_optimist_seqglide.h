/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's SEQ requests of 2026-10-29 (issue #104 "Where is the slide, accent, etc"):
 *   GLIDE      the held synth step's first card page is NOTE LEVEL RATCHET GLIDE (LENGTH left it: the note picker's KNOB 4):
 *              KNOB 4 right on, left off, the step's SLIDE (SF_SLIDE); an empty step "-"
 *   selected   a note held (its p-locks) is the selected note after: the picker's, KNOB 2 / 3 / 4 go on with it
 *   order      a chord (notes that start together): KNOB 1 (picker) and SELECT (held) pick all of it first, then the lowest
 *              alone, the one above ...; then the next step's
 *   move       a note moved (KNOB 2, PRESETS held) is the selected note(s) alone, each its whole length: the bug was a chord's
 *              other note moved with it and a note sounding on past its ties (carry steps) cut in two; at the first / last
 *              step it stays and says so; the same pitch where it would go: it stays
 * Included by ui_optimist_test.c after ui_optimist_overlap.h. */
static const char *sgl_notes(const track_t *t)          /* every note that starts, "step:pitch x steps", in step then pitch order */
{
    static char b[160];
    uint32_t i, j, n;
    uint8_t k[4];
    b[0] = 0;
    for (i = 0; i < trk_len(t); i++)
        for (j = 0, n = pk_heads(t, i, k); j < n; j++) {
            char w[24];
            snprintf(w, sizeof w, "%s%u:%ux%u", b[0] ? " " : "", i, t->step[i].note[k[j]], (note_span(t, i, k[j]) + 127u) / 128u);
            strncat(b, w, sizeof b - strlen(b) - 1u);
        }
    return b;
}
static void sgl_chord(track_t *t)                       /* step 3: 60 four steps and 64 two (64 ends, 60 goes on: a carry step) */
{
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    t->step[3].time = ST_NOTE, t->step[3].n = 2, t->step[3].note[0] = 60, t->step[3].note[1] = 64, t->step[3].vel = 100;
    t->step[4].time = ST_TIE;
    t->step[5].time = ST_NOTE, t->step[5].n = 1, t->step[5].note[0] = 60, t->step[5].vel = 100, t->step[5].flags = SF_CARRY;
    t->step[6].time = ST_TIE;
    fm1_irq_on();
}
static void seqglide_tests(void)
{
    track_t *t;
    cell_t c;
    reset_ui();
    song.sel = 0;
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    sgl_chord(t);
    check(!strcmp(sgl_notes(t), "3:60x4 3:64x2"), "seqglide: the chord as set: 60 four steps, 64 two, both at step 4");
    tap(B_SEQ);
    frames(OP_CARDS_FRAMES + 5u);
    /* order: all, the lowest, the one above (the picker) */
    turn(EN_K1, 1);
    check(st.pk == 3u && st.nsel == NSEL_ALL, "seqglide: KNOB 1: the chord's two notes first, together");
    turn(EN_K1, 1);
    check(st.pk == 3u && t->step[3].note[st.nsel] == 60u, "seqglide: KNOB 1 again: the lowest alone (60)");
    /* the bug: 60 moved left took 64 with it and left its carried part behind (two notes of 60) */
    turn(EN_K2, -1);
    check(!strcmp(sgl_notes(t), "2:60x4 3:64x2"), "seqglide: KNOB 2 left on 60 alone: 60 a step earlier, whole (not split), 64 stays");
    check(st.pk == 2u && t->step[2].note[st.nsel] == 60u, "seqglide: ... the pick goes with it");
    turn(EN_K2, 1);
    check(!strcmp(sgl_notes(t), "3:60x4 3:64x2"), "seqglide: KNOB 2 right: back, the chord as it was");
    turn(EN_K1, 1);
    check(st.pk == 3u && t->step[3].note[st.nsel] == 64u, "seqglide: KNOB 1 again: the one above (64) alone");
    turn(EN_K2, 1);
    check(!strcmp(sgl_notes(t), "3:60x4 4:64x2"), "seqglide: KNOB 2 right on 64: it alone, over the 60 that goes on (both sound)");
    turn(EN_K2, -2);
    check(!strcmp(sgl_notes(t), "2:64x2 3:60x4"), "seqglide: KNOB 2 left twice: 64 alone before the 60, the 60 whole");
    turn(EN_K2, 1);
    check(!strcmp(sgl_notes(t), "3:60x4 3:64x2"), "seqglide: ... and back into the chord");
    sgl_chord(t);
    turn(EN_K1, -2);
    check(st.pk == 3u && st.nsel == NSEL_ALL, "seqglide: KNOB 1 back twice from 64: 60, then all of the chord");
    turn(EN_K2, 1);
    check(!strcmp(sgl_notes(t), "4:60x4 4:64x2"), "seqglide: KNOB 2 with the chord all picked: both, each its whole length");
    turn(EN_K2, -1);
    check(!strcmp(sgl_notes(t), "3:60x4 3:64x2"), "seqglide: ... and back");
    /* the first step: it stays, says so */
    fm1_irq_off();
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->step[0].time = ST_NOTE, t->step[0].n = 2, t->step[0].note[0] = 60, t->step[0].note[1] = 64, t->step[0].vel = 100;
    t->step[1].time = ST_TIE;
    fm1_irq_on();
    turn(EN_K1, -8);
    turn(EN_K1, 1);
    check(st.pk == 0u && t->step[0].note[st.nsel] == 60u, "seqglide: a chord on step 1: 60 picked alone");
    turn(EN_K2, -1);
    check(!strcmp(sgl_notes(t), "0:60x2 0:64x2") && strstr(ui.msg, "FIRST STEP"), "seqglide: KNOB 2 left on step 1: it stays (FIRST STEP), no wrap");
    turn(EN_K2, 1);
    check(!strcmp(sgl_notes(t), "0:64x2 1:60x2"), "seqglide: KNOB 2 right: 60 alone a step on, 64 stays");
    /* held: the note held is the picked one after; PRESETS moves the selected notes alone; GLIDE on KNOB 4 */
    sgl_chord(t);
    kdown(WK(3));
    frames(HOLD_FRAMES);
    check(st.nsel == NSEL_ALL && st.pk == 3u, "seqglide: a chord held: all of it selected, the picked note too");
    step_cell(0, 3, &c);
    check(c.label && !strcmp(c.label, "GLIDE") && !strcmp(c.val, "OFF"), "seqglide: the held step's KNOB 4: GLIDE, OFF");
    step_cell(0, 0, &c);
    check(!strcmp(c.val, "C4+"), "seqglide: its NOTE card: the lowest and a + (both selected)");
    turn(EN_K4, 1);
    step_cell(0, 3, &c);
    check((t->step[3].flags & SF_SLIDE) && !strcmp(c.val, "ON") && !strcmp(sgl_notes(t), "3:60x4 3:64x2"),
          "seqglide: KNOB 4 right: GLIDE ON (the step's SLIDE), the notes' lengths as they were");
    turn(EN_SELECT, 2);
    check(st.nsel != NSEL_ALL && t->step[3].note[st.nsel] == 64u, "seqglide: SELECT twice: all, the lowest, then 64 alone");
    step_cell(0, 0, &c);
    check(!strcmp(c.val, "E4"), "seqglide: its NOTE card: the one selected");
    turn(EN_PRESET, -1);
    check(!strcmp(sgl_notes(t), "2:64x2 3:60x4") && st.held == 1u << 2, "seqglide: PRESETS held: 64 alone a step earlier, the 60 stays");
    kup(WK(3));
    check(st.pk == 2u && pk_ok(), "seqglide: let go: the note held is the picked one, where it went");
    kdown(WK(3));
    turn(EN_K4, -1);
    kup(WK(3));
    check(!(t->step[3].flags & SF_SLIDE), "seqglide: KNOB 4 left on the held step: GLIDE off");
    kdown(WK(9));
    frames(HOLD_FRAMES);
    step_cell(0, 3, &c);
    check(!strcmp(c.val, "-"), "seqglide: an empty step held: GLIDE says -");
    kup(WK(9));
    tap(B_HOME);
    track_defaults_steps(t);
    reset_ui();
}
