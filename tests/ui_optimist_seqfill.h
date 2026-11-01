/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SEQ held on STEP = the FILL view of the selected track / drum lane (the user, 2026-11-01: "the SEQ held will be the
 * fills", "edit the fills for that lane/track"). With SEQ held a step key, for that synth track or that drum lane only:
 *   an empty step         a note / hit there marked FILL ONLY
 *   a FILL ONLY step      removed
 *   a normal step         NO FILL (silent in a fill)
 *   a NO FILL step        normal again
 * Each press is one undo level and a popup; the header says "FILLS T1" / "FILLS <lane>"; the old drum "SEQ + a step clears its lanes" is gone.
 * A drum lane's condition is the value (2 FILL ONLY, 3 NO FILL) of its AUTO_MUTE(l) event, so lanes of one step differ. Playback: FILL ONLY
 * sounds only in a fill (fill_held, GLO + key 9), NO FILL only out of one. Needs FELUCCA_FILLS (the song config). Included by
 * ui_optimist_test.c after ui_optimist_shiftcycle.h. */
#if FELUCCA_FILLS && FELUCCA_AUTO
static void sf_key(uint32_t w)                          /* SEQ held + step key w, then let go (SEQ stays down: nothing else happens) */
{
    kdown(WK(w));
    kup(WK(w));
}
static uint32_t sf_peak(track_t *t, uint32_t fill)      /* the most notes the track sounded at once over 90 frames, a fill held or not */
{
    uint32_t m = 0, n = 90;
    fill_held = (uint8_t)fill;
    tap(B_PLAY);
    while (n--) {
        frame();
        m = t->seq_n > m ? t->seq_n : m;
    }
    tap(B_PLAY);
    frames(4);
    fill_held = 0;
    return m;
}
static uint32_t sf_edge_white(uint32_t w)               /* the white pixels in the left edge column of step w's cells, the grid's rows */
{
    uint32_t n = 0, y;
    for (y = SP_Y; y < SP_Y + SG_H; y++)
        n += screen[y * 240u + (uint32_t)SG_X + w * SG_CW] == swap16(C_WHITE);
    return n;
}
static void sf_synth_tests(void)
{
    track_t *t;
    uint32_t before;
    sv_open();
    t = TSEL;
    song.octave = 0, pen_oct = 0;                       /* (the roll's view: the pattern's own octave) */
    fm1_irq_off();
    AL(t)->n = 0;
    memset(t->step, 0, sizeof t->step);
    fm1_irq_on();
    st.pk = PK_NONE;
    press(B_SEQ);
    frames(3);
    check(step_fillview() && strstr(ui.msg, "FILLS T1"), "seq fills: SEQ held on a synth track: the fill view, header 'FILLS T1'");
    sf_key(0);
    check(step_on(&t->step[0]) && step_fill(t, 0) == FC_FILL && strstr(ui.msg, "STEP 1 FILL ONLY"), "seq fills: an empty step: a note, FILL ONLY, popup");
    check(pk_ok() && st.pk == 0u, "seq fills: ... the note entered is the picked one");
    frames(4);
    sf_key(0);
    check(!step_on(&t->step[0]) && step_fill(t, 0) == FC_NORM && strstr(ui.msg, "REMOVED"), "seq fills: a FILL ONLY step: removed, no condition left");
    fm1_irq_off();
    se_set(t, 2, 64, 1);
    fm1_irq_on();
    frames(2);
    sf_key(2);
    check(step_on(&t->step[2]) && step_fill(t, 2) == FC_NORM && strstr(ui.msg, "STEP 3 NORMAL NOTE"), "seq fills: a normal note: unchanged, the popup says NORMAL NOTE");
    fm1_irq_off();
    (void)step_fill_set(t, 2, FC_NOFILL);
    fm1_irq_on();
    sf_key(2);
    check(step_on(&t->step[2]) && step_fill(t, 2) == FC_NOFILL && strstr(ui.msg, "NO FILL NOTE"), "seq fills: a NO FILL note (SAVE's): unchanged too");
    sf_key(0);
    release(B_SEQ);
    frames(3);
    check(!step_fillview() && step_fill(t, 0) == FC_FILL && step_fill(t, 2) == FC_NOFILL, "seq fills: SEQ let go: the normal view, the conditions kept");
    before = sf_edge_white(0);
    press(B_SEQ);
    frames(4);
    ppm("opt-seq-fills-synth");
    check(sf_edge_white(0) > before && sf_edge_white(2) == 0u, "seq fills: the fill view draws a FILL ONLY note framed white (a NO FILL one hollow, no white)");
    release(B_SEQ);
    frames(2);
    check(sf_peak(t, 0) == 1u && sf_peak(t, 1) == 1u, "seq fills: playback, out of a fill the NO FILL note is silent, in one the FILL ONLY one is heard");
    fm1_irq_off();
    step_clear(&t->step[2]);
    (void)step_fill_set(t, 2, FC_NORM);
    fm1_irq_on();
    check(sf_peak(t, 0) == 0u && sf_peak(t, 1) > 0u, "seq fills: playback, FILL ONLY alone: silent out of a fill, heard in one");
    st.pk = PK_NONE;
    press(B_SEQ);
    frames(2);
    sf_key(5);
    release(B_SEQ);
    check(step_on(&t->step[5]), "seq fills: another step set ...");
    sm_undo();
    check(!step_on(&t->step[5]) && step_fill(t, 5) == FC_NORM && step_fill(t, 0) == FC_FILL, "seq fills: undo: one press, the note and its condition both");
}
static void sf_drum_tests(void)
{
    track_t *t;
    uint32_t m;
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    AL(t)->n = 0;
    memset(t->dstep, 0, sizeof t->dstep);
    dstep_set(&t->dstep[1], 3u, LV_NORM, 0);
    dstep_set(&t->dstep[1], 2u, LV_NORM, 0);
    fm1_irq_on();
    lane_select(2);
    st.pk = PK_NONE;
    press(B_SEQ);
    frames(3);
    check(step_fillview() && strstr(ui.msg, "FILLS ") && strstr(ui.msg, lane_name(2)), "seq fills: SEQ held on the drum track: the fill view, header names the lane");
    sf_key(1);
    check(dstep_has(&t->dstep[1], 2) && dstep_has(&t->dstep[1], 3), "seq fills: the old clear is gone: SEQ + a step leaves the step's lanes ...");
    check(step_lane_fc(t, 1, 2) == FC_NORM && !step_lev(t, 1, 2) && strstr(ui.msg, "NORMAL HIT"), "seq fills: ... a normal hit is unchanged, the popup says so");
    check(step_lane_fc(t, 1, 3) == FC_NORM && step_fill(t, 1) == FC_NORM && !step_muted(t, 1, 2), "seq fills: ... the other lane and the step's own condition untouched, not muted");
    sf_key(4);
    check(dstep_has(&t->dstep[4], 2) && step_lane_fc(t, 4, 2) == FC_FILL && !dstep_has(&t->dstep[4], 3), "seq fills: an empty step: the lane's hit, FILL ONLY");
    check(strstr(ui.msg, "FILL ONLY") && pk_ok() && st.pk == 4u, "seq fills: ... popup, the hit picked");
    frames(4);
    release(B_SEQ);
    frames(2);
    m = sf_peak(t, 0);
    (void)m;
    fill_held = 0;
    {
        uint32_t a, b;
        fm1_irq_off();
        dstep_set(&t->dstep[6], 2u, LV_NORM, 0);
        (void)step_lane_fc_set(t, 6, 2, FC_NOFILL);
        fm1_irq_on();
        a = shc_lanes(90);
        fill_held = 1;
        b = shc_lanes(90);
        fill_held = 0;
        check((a & (1u << 2)) && (b & (1u << 2)) && (a & (1u << 3)) && (b & (1u << 3)), "seq fills: playback: the lane sounds out of a fill and in one (FILL ONLY / NO FILL hits among its normal ones)");
    }
    press(B_SEQ);
    frames(2);
    sf_key(4);
    check(!dstep_has(&t->dstep[4], 2) && !step_lev(t, 4, 2), "seq fills: a FILL ONLY hit: removed, its condition with it");
    check(dstep_has(&t->dstep[1], 3), "seq fills: ... the other lane's hit stays");
    sf_key(1);
    sf_key(7);
    release(B_SEQ);
    st.pk = PK_NONE;
    sm_undo();
    check(!dstep_has(&t->dstep[7], 2) && step_lane_fc(t, 1, 2) == FC_NORM, "seq fills: undo: the last press only");
    press(B_SEQ);
    frames(4);
    ppm("opt-seq-fills-drums");
    release(B_SEQ);
    frames(2);
}
/* playback in seq (auto.c step_play): a lane's FILL ONLY hit sounds only in a fill, its NO FILL hit only out of one, per lane */
static void sf_play_lane_tests(void)
{
    track_t *t;
    uint32_t a, b;
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    AL(t)->n = 0;
    memset(t->dstep, 0, sizeof t->dstep);
    dstep_set(&t->dstep[0], 2u, LV_NORM, 0);
    (void)step_lane_fc_set(t, 0, 2, FC_FILL);
    dstep_set(&t->dstep[0], 3u, LV_NORM, 0);
    (void)step_lane_fc_set(t, 0, 3, FC_NOFILL);
    dstep_set(&t->dstep[0], 4u, LV_NORM, 0);
    fm1_irq_on();
    a = shc_lanes(90);
    fill_held = 1;
    b = shc_lanes(90);
    fill_held = 0;
    check(!(a & (1u << 2)) && (a & (1u << 3)) && (a & (1u << 4)), "seq fills: out of a fill: the FILL ONLY lane is silent, the NO FILL and the normal one sound");
    check((b & (1u << 2)) && !(b & (1u << 3)) && (b & (1u << 4)), "seq fills: in a fill: the FILL ONLY lane sounds, the NO FILL one is silent, the normal one sounds");
    check(!step_muted(t, 0, 2) && !step_muted(t, 0, 3), "seq fills: ... neither is a mute");
}
static void seqfill_tests(void)
{
    sf_synth_tests();
    sf_drum_tests();
    sf_play_lane_tests();
}
#else
static void seqfill_tests(void) {}
#endif
