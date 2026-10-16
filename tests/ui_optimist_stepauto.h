/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP and the mixer: a step that carries automation (a p-lock or motion event of the sound) is
 * drawn in the automation colour (C_AUTO), a plain step in its lane's / the track's own; the step extras (NUDGE, FILL, CHANCE,
 * COND) are not automation. Renders opt-step-auto-drums, opt-step-auto-roll and opt-mixer-auto (DIR/*.ppm). Included by
 * ui_optimist_test.c. */
#if FELUCCA_AUTO
static uint32_t sa_count(uint16_t col, int32_t y0, int32_t y1)   /* the pixels of colour col in rows y0 .. y1 of the screen */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 240; x++)
            n += screen[y * 240 + x] == swap16(col);
    return n;
}
static uint32_t sa_count_auto(int32_t y0, int32_t y1)   /* ... of the automation colour at any level */
{
    uint32_t lv, n = 0;
    for (lv = 0; lv < 4u; lv++)
        n += sa_count(lvl_acol(lv), y0, y1);
    return n;
}
static void sa_open(uint32_t drum)
{
    reset_ui();
    song.sel = drum ? TRK_DRUM : 0;
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
    AL(TSEL)->n = 0;
    if (drum)
        lane_select(2);
    tap(B_SEQ);
    frames(2);
}
static void step_auto_tests(void)
{
    track_t *t;
    int32_t x1, y;
    uint16_t plain, auto_c;
    /* the predicate: locks and motion mark a step, the extras do not, a lane's event marks its lane's cell only */
    sa_open(1);
    t = TSEL;
    check(!op_step_auto(t, 1, 2), "step auto: a step with no event is plain");
    (void)auto_put(AL(t), 1, AUTO_CHANCE, 50);
    (void)auto_put(AL(t), 3, AUTO_NUDGE, 2);
    check(!op_step_auto(t, 1, 2) && !op_step_auto(t, 3, 2), "step auto: chance and nudge (step extras) do not mark a step");
    (void)auto_put(AL(t), 5, AUTO_LANE(7, 0), 12);
    check(op_step_auto(t, 5, 7) && !op_step_auto(t, 5, 2) && !op_step_auto(t, 5, 16u), "step auto: a lane's event marks that lane's cell only");
    (void)auto_put(AL(t), 6 | 0, 0, 10);
    check(op_step_auto(t, 6, 2) && op_step_auto(t, 6, 9) && op_step_auto(t, 6, 16u), "step auto: a track's event (motion or lock) marks the step on every lane");
    /* the drum grid: step 0 plain, step 1 locked, on lane 2, both hard */
    sa_open(1);
    t = TSEL;
    key(WK(0));
    key(WK(1));
    key(WK(0));
    key(WK(1));                                         /* (set, then hard: the tap cycle) */
    AL(t)->n = 0;
    (void)auto_put(AL(t), 1, 0, 10);
    auto_touch(trk_index(TSEL));
    ui.force = 1;
    frames(2);
    check(dstep_has(&t->dstep[0], 2) && dstep_has(&t->dstep[1], 2) && dstep_lvl(&t->dstep[0], 2) == dstep_lvl(&t->dstep[1], 2),
          "step auto: two steps set alike on lane 2");
    plain = lvl_col(op_lane_col(2), dstep_lvl(&t->dstep[0], 2));
    auto_c = lvl_acol(dstep_lvl(&t->dstep[1], 2));
    y = SP_Y + SG_LY0(2) + 2;
    x1 = SG_X + SG_CW + 3;
    check(screen[y * 240 + SG_X + 3] == swap16(plain), "step auto: the plain step is drawn in its lane's colour");
    check(screen[y * 240 + x1] == swap16(auto_c), "step auto: the step with a lock is drawn in the automation colour");
    check(plain != auto_c, "step auto: the two colours differ");
    ppm("opt-step-auto-drums");
    /* the synth roll: no automation, none of the colour; a lock on step 2, the colour appears in the roll */
    sa_open(0);
    t = TSEL;
    fm1_irq_off();
    step_set(t, 0);
    step_set(t, 2);
    fm1_irq_on();
    t->step[0].note[0] = 60, t->step[2].note[0] = 64;   /* (the view: OCT 0, around the notes) */
    song.octave = 0;
    check(step_has(t, 0) && step_has(t, 2), "step auto: two notes set on a synth track");
    ui.force = 1;
    frames(2);
    check(sa_count_auto(SP_Y, SP_Y + SP_H) == 0u, "step auto: a synth roll without a lock has no automation colour");
    (void)auto_put(AL(t), 2, 0, 10);
    auto_touch(trk_index(TSEL));
    ui.force = 1;
    frames(2);
    check(sa_count_auto(SP_Y, SP_Y + SP_H) > 0u, "step auto: a synth step with a lock is drawn in the automation colour");
    ppm("opt-step-auto-roll");
    /* the mixer's strips: the drum track's lane 2, steps 0 (plain) and 1 (a lane lock) */
    reset_ui();
    song.sel = TRK_DRUM;
    track_defaults_steps(&trk[TRK_DRUM]);
    memset(&trk[TRK_DRUM].dstep[0], 0, 2u * sizeof(dstep_t));
    AL(&trk[TRK_DRUM])->n = 0;
    ui.force = 1;
    frames(3);
    check(sa_count_auto(OP_PY, 240) == 0u, "step auto: the mixer without a lock has no automation colour");
    dstep_set(&trk[TRK_DRUM].dstep[0], 2, LV_HARD, 0);
    dstep_set(&trk[TRK_DRUM].dstep[1], 2, LV_HARD, 0);
    (void)auto_put(AL(&trk[TRK_DRUM]), 1, AUTO_LANE(2, 0), 10);
    auto_touch(TRK_DRUM);
    ui.force = 1;
    frames(3);
    check(sa_count_auto(OP_PY, 240) > 0u, "step auto: the mixer's strip draws a step with a lock in the automation colour");
    ppm("opt-mixer-auto");
    AL(&trk[TRK_DRUM])->n = 0;
}
#endif
