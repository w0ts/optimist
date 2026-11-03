/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's grid (2026-11-01, the user):
 *   - the drum grid's playing step is a moving column (a faint white ground, its hits framed white), two columns redrawn a step, as the roll's;
 *   - a FILL ONLY note / hit is drawn nowhere but with SEQ held (the fill view): the roll, the drum grid, the keys, the mixer's rows, the pattern's
 *     preview; a NO FILL one looks like any other;
 *   - a step's automation is a dot (white ring, black centre, 6 x 6) inside its drum lane's cell / its synth note bar; a fill condition or a mute is none.
 * Included by ui_optimist_test.c after ui_optimist_seqfill.h. */
#if FELUCCA_AUTO
#define DP_X0(w) ((uint32_t)(SG_X + (int32_t)(w) * SG_CW - 1))
#define DP_N (SG_H + 1)
static uint16_t dp_snap[3][240 * (SG_H + 1)];
static void dp_grab(uint32_t k)                         /* the grid's rows of the screen */
{
    uint32_t y;
    for (y = 0; y < DP_N; y++)
        memcpy(&dp_snap[k][y * 240u], &screen[(SP_Y + y) * 240u], 240u * sizeof(uint16_t));
}
static uint32_t dp_diff(uint32_t a, uint32_t b, uint32_t x0, uint32_t x1, int outside)   /* the pixels that differ inside [x0, x1) (or outside it) */
{
    uint32_t n = 0, x, y;
    for (y = 0; y < DP_N; y++)
        for (x = 0; x < 240u; x++)
            if ((x >= x0 && x < x1) != (outside != 0) && dp_snap[a][y * 240u + x] != dp_snap[b][y * 240u + x])
                n++;
    return n;
}
static uint32_t dp_count(uint32_t k, uint16_t c, uint32_t x0, uint32_t x1)
{
    uint32_t n = 0, x, y;
    for (y = 0; y < DP_N; y++)
        for (x = x0; x < x1; x++)
            n += dp_snap[k][y * 240u + x] == swap16(c);
    return n;
}
static int dp_pix(uint32_t x, int32_t y, uint16_t c) { return px_at(x, (uint32_t)(SP_Y + y), c); }
static int dp_dot(uint32_t l, uint32_t w)               /* lane l's cell of step w holds the dot: its white ring's corner and its black centre */
{
    int32_t y = SG_LY(l), lh = SG_LY(l + 1u) - y, s = 6, o = (lh - 1 - s) / 2;
    uint32_t x = (uint32_t)(SG_X + (int32_t)w * SG_CW) + 1u;
    return dp_pix(x, y + o, C_WHITE) && dp_pix(x + 5u, y + o + 5, C_WHITE) && dp_pix(x + 2u, y + o + 2, C_BLACK) && dp_pix(x + 3u, y + o + 3, C_BLACK);
}
static void dp_to(track_t *t, uint32_t col)             /* play until the playhead is on column col of the window, drawn */
{
    uint32_t n = 0;
    if (!song.playing)
        tap(B_PLAY);
    do
        frame();
    while ((t->seq_idx % trk_len(t)) % 16u != col && ++n < 2000u);
}
static void dp_stop(void)
{
    if (song.playing)
        tap(B_PLAY);
    frames(6);
}
static void dp_drum_tests(void)
{
    track_t *t;
    uint32_t b, w0, w1, cx;
    int32_t y;
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    AL(t)->n = 0;
    memset(t->dstep, 0, sizeof t->dstep);
    dstep_set(&t->dstep[4], 2u, LV_NORM, 0);
    dstep_set(&t->dstep[5], 2u, LV_HARD, 0);
    dstep_set(&t->dstep[5], 3u, LV_NORM, 0);
    dstep_set(&t->dstep[6], 2u, LV_NORM, 0);
    dstep_set(&t->dstep[8], 4u, LV_NORM, 0);
    dstep_set(&t->dstep[9], 4u, LV_NORM, 0);
    (void)step_mute_set(t, 6, 2, 1);
    (void)auto_put(AL(t), 5u | AUTO_ONLY, AUTO_LANE(2, 1), 40);   /* a lock on lane 2's step 6 (index 5), and on the muted hit of step 7 */
    (void)auto_put(AL(t), 6u | AUTO_ONLY, AUTO_LANE(2, 1), 30);
#if FELUCCA_FILLS
    (void)step_lane_fc_set(t, 8, 4, FC_FILL);           /* lane 4: step 9 FILL ONLY, step 10 NO FILL */
    (void)step_lane_fc_set(t, 9, 4, FC_NOFILL);
#endif
    auto_touch(trk_index(t));
    fm1_irq_on();
    lane_select(2);
    st.pk = PK_NONE;
    frames(4);
    ui.force = 1;
    frame();
    dp_grab(0);
    check(dp_dot(2, 5) && dp_dot(2, 6), "drum dots: a lane's lock on step 6: the dot in that lane's cell, on the muted hit of step 7 too");
    check(!dp_dot(3, 5) && !dp_dot(2, 4) && !dp_dot(2, 7) && !dp_dot(4, 8) && !dp_dot(4, 9), "... no dot in the other lane's cell of the step, nor in the lane's other steps");
    check(dp_pix((uint32_t)(SG_X + 6 * SG_CW) + 9u, SG_LY(2) + 5, mute_col()), "... the muted hit stays grey around its dot");
    {
        uint32_t i, ink = 0;
        for (i = 0; i < 240u; i++)                      /* the marks row under the grid: nothing for a lane's lock */
            ink += px_at(i, (uint32_t)(SP_Y + SG_MK_Y + 1), C_AUTO);
        check(ink == 0u, "... the marks row under the grid shows no dot for it");
    }
    ppm("opt-drum-dots");
#if FELUCCA_FILLS
    check(!op_step_auto(t, 8, 4) && !op_step_auto(t, 9, 4) && op_step_auto(t, 5, 2), "dots: a FILL ONLY or NO FILL hit is no automation, a lock is");
    check(!dp_pix((uint32_t)(SG_X + 8 * SG_CW) + 1u, SG_LY(4) + 1, mute_col()) && !dp_pix((uint32_t)(SG_X + 8 * SG_CW) + 1u, SG_LY(4) + 1, lvl_col(op_lane_col(4), LV_NORM)),
          "fills: a FILL ONLY hit is not drawn in the normal view ...");
    check(dp_pix((uint32_t)(SG_X + 9 * SG_CW) + 1u, SG_LY(4) + 1, lvl_col(op_lane_col(4), LV_NORM)), "... a NO FILL hit is drawn as a normal one");
#endif
    /* the playhead */
    dp_to(t, 5);
    dp_grab(1);
    w0 = DP_X0(5), w1 = w0 + SG_CW;
    check(dp_diff(0, 1, w0, w1, 0) > 0u && dp_diff(0, 1, w0, w1, 1) == 0u, "drum playhead: the playing column is highlighted, nothing else on the grid changed");
    check(dp_dot(2, 5) && dp_dot(3, 5) == 0, "... its dot stays visible under the highlight");
    b = 0;
    do {
        uint32_t p0 = blit_px;
        frame();
        if ((t->seq_idx % 16u) == 6u)
            b = blit_px - p0;
    } while (b == 0u && (t->seq_idx % 16u) != 7u);
    dp_grab(2);
    w0 = DP_X0(6), w1 = w0 + SG_CW;
    check(dp_diff(0, 2, w0, w1, 0) > 0u && dp_diff(0, 2, w0, w1, 1) == 0u, "... a step on: the next column highlighted, the one left restored exactly (the grid as stopped elsewhere)");
    check(b > 0u && b <= 240u * 4u + 2u * SG_CW * (SG_H + 2u) + 600u, "... the step sent about two columns and the strip");
    printf("  drum playhead: %u px sent for the step (two columns %u)\n", b, 2u * SG_CW * (SG_H + 2u));
    cx = (uint32_t)(SG_X + 6 * SG_CW);
    y = SG_LY(2);
    check(dp_pix(cx + 9u, y + 5, mute_col()) && dp_dot(2, 6), "... the muted hit under the playhead stays grey, its dot visible");
    ppm("opt-drum-playhead");
    dp_stop();
    /* the playhead is not drawn while the cards are up: nothing of it on the stopped grid */
    ui.force = 1;
    frame();
    dp_grab(1);
    check(dp_diff(0, 1, 0, 240, 0) == 0u, "... stopped: the grid as before the playhead");
}
#if FELUCCA_FILLS
static void dp_mixer_on(uint32_t r, uint32_t s, uint32_t *on)
{
    *on = mx_on(r, s);
}
static void dp_fill_synth_tests(void)
{
    track_t *t;
    uint32_t on, i, r;
#if FELUCCA_PATTERNS
    pat_peek_t d;
#endif
    se_open(16);
    song.sel = 0;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    song.octave = 0, pen_oct = 0;
    fm1_irq_off();
    AL(t)->n = 0;
    memset(t->step, 0, sizeof t->step);
    for (i = 0; i < 5u; i += 2u) {                      /* notes on steps 1, 3 (the fill one) and 5 */
        t->step[i].time = ST_NOTE, t->step[i].n = 1, t->step[i].vel = 100;
        t->step[i].note[0] = (uint8_t)(i == 2u ? 52u : i ? 55u : 48u);
    }
    t->seq_active = 1;
    (void)step_fill_set(t, 2, FC_FILL);
    (void)step_fill_set(t, 4, FC_NOFILL);
    auto_touch(trk_index(t));
    fm1_irq_on();
    st.pk = PK_NONE;
    frames(4);
    ui.force = 1;
    frame();
    dp_grab(0);
    check(!step_shown(t, 2) && step_shown(t, 0) && step_shown(t, 4), "fills: a FILL ONLY note's key is not lit in the normal view, the others are (a NO FILL one is)");
    check(!trk_on_step(0, 2) && trk_on_step(0, 0) && trk_on_step(0, 4), "... the mixer's step row skips it (the channel's), a NO FILL one is a step");
    dp_mixer_on(MXR_T1, 2, &on);
    check(!on && (dp_mixer_on(MXR_T1, 0, &on), on), "... mx_on: not on the channel's row");
    /* the roll: the same screen as without the fill note */
    fm1_irq_off();
    t->step[2].n = 0, t->step[2].time = ST_REST;
    (void)step_fill_set(t, 2, FC_NORM);
    fm1_irq_on();
    ui.force = 1;
    frame();
    dp_grab(1);
    check(dp_diff(0, 1, 0, 240, 0) == 0u, "... the roll in the normal view: the fill note is not drawn (the screen as with no note there)");
    fm1_irq_off();
    t->step[2].n = 1, t->step[2].time = ST_NOTE, t->step[2].vel = 100, t->step[2].note[0] = 52;
    (void)step_fill_set(t, 2, FC_FILL);
    fm1_irq_on();
    press(B_SEQ);
    frames(3);
    ui.force = 1;
    frame();
    dp_grab(2);
    check(step_fillview() && dp_diff(0, 2, 0, 240, 0) > 0u && step_shown(t, 2), "... with SEQ held: the fill view draws it, its key lit");
    frames(op_hold_ms() / 16u + 2u);                    /* (held on past the hold time, as a look at the fills is: let go sooner it is a
                                                         * tap, whose page change comes after the double-tap window, op_step.c SEQ_DBL_MS) */
    release(B_SEQ);
    frames(3);
    pk_cards_down();
    frames(3);
    /* the automation dot on the roll */
    ui.force = 1;
    frame();
    dp_grab(0);
    fm1_irq_off();
    (void)auto_put(AL(t), 4u | AUTO_ONLY, 0, 11);
    auto_touch(trk_index(t));
    fm1_irq_on();
    check(!op_step_auto(t, 2, 16u) && op_step_auto(t, 4, 16u), "roll dots: a FILL ONLY note is no automation, a lock is");
    ui.force = 1;
    frame();
    dp_grab(1);
    r = DP_X0(4);
    check(dp_count(1, C_WHITE, r, r + SG_CW) >= dp_count(0, C_WHITE, r, r + SG_CW) + 12u && dp_diff(0, 1, r, r + SG_CW, 1) == 0u,
          "... a lock on a step: the white ring in its note bar, nothing outside its column");
    ppm("opt-roll-dot");
    dp_to(t, 4);
    dp_grab(2);
    check(dp_count(2, C_BLACK, r, r + SG_CW) >= 12u, "... under the playhead (the bar white) the black centre shows");
    dp_stop();
    /* a lock on a step with no note: the dot on the roll's bottom row */
    fm1_irq_off();
    (void)auto_put(AL(t), 9u | AUTO_ONLY, 0, 12);
    auto_touch(trk_index(t));
    fm1_irq_on();
    ui.force = 1;
    frame();
    dp_grab(2);
    r = DP_X0(9);
    check(dp_count(2, C_WHITE, r, r + SG_CW) >= 16u, "... a lock on a step without a note: the dot on the roll's bottom row");
    /* the preview of the stored slot */
#if FELUCCA_PATTERNS
    key(WK(3));
    pat_store_slot(0, 3);
    check(pat_peek(0, 3, &d) && !((d.hit[0] >> 2) & 1u) && ((d.hit[0] >> 0) & 1u) && ((d.hit[0] >> 4) & 1u) && !d.mut[0], "the pattern preview: the fill-only note is not in it, a NO FILL one is, none shown muted");
#endif
}
static void dp_fill_drum_tests(void)
{
    track_t *t;
    uint32_t on, r, i;
#if FELUCCA_PATTERNS
    pat_peek_t d;
#endif
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    AL(t)->n = 0;
    memset(t->dstep, 0, sizeof t->dstep);
    dstep_set(&t->dstep[1], 2u, LV_NORM, 0);
    dstep_set(&t->dstep[2], 2u, LV_NORM, 0);
    dstep_set(&t->dstep[2], 3u, LV_NORM, 0);
    (void)step_lane_fc_set(t, 2, 2, FC_FILL);
    (void)step_lane_fc_set(t, 1, 2, FC_NOFILL);
    auto_touch(trk_index(t));
    fm1_irq_on();
    lane_select(2);
    st.pk = PK_NONE;
    frames(4);
    check(step_shown(t, 1) && !step_shown(t, 2), "drum fills: the selected lane's FILL ONLY hit: its key not lit in the normal view, a NO FILL one lit");
    r = MXR_LANE0 + 2u;
    check(mx_on(r, 1) && !mx_on(r, 2), "... the lane's mixer row: the FILL ONLY hit not on it");
    for (on = 0, r = MXR_DR; r < MXR_DR + 1u; r++)
        on = mx_on(r, 2) | (mx_on(r, 1) << 1);
    check(mx_on(MXR_DR, 2) && (on >> 1) == 1u, "... the DR row: step 3 still on by lane 4's hit, step 2's NO FILL one on");
    fm1_irq_off();
    memset(&t->dstep[2], 0, sizeof t->dstep[2]);
    dstep_set(&t->dstep[2], 2u, LV_NORM, 0);
    (void)step_lane_fc_set(t, 2, 2, FC_FILL);
    fm1_irq_on();
    check(!mx_on(MXR_DR, 2), "... a step with only a FILL ONLY hit is off on the DR row");
    (void)i;
#if FELUCCA_PATTERNS
    pat_store_slot(TRK_DRUM, 5);
    check(pat_peek(TRK_DRUM, 5, &d) && !((d.hit[2] >> 2) & 1u) && ((d.hit[2] >> 1) & 1u) && !d.mut[2], "... the pattern preview: the lane's FILL ONLY hit not in it, its NO FILL one is, not grey");
#endif
}
#endif
static void drumph_tests(void)
{
    dp_drum_tests();
#if FELUCCA_FILLS
    dp_fill_synth_tests();
    dp_fill_drum_tests();
#endif
}
#else
static void drumph_tests(void) {}
#endif
