/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP ENTRY (the user, 2026-11-01): a synth track's STEP, stopped, REC on (armed). KNOB 1 the cursor (the
 * window follows, the notes starting there heard), KNOB 2 / 3 / 4 the picker's on every note starting on it (moved with the cursor,
 * pitch, length: the length new notes take), a key toggles its notes on the cursor step (added: heard, the last length, the keys'
 * velocity; removed: silent), several keys a chord, one undo level; the keys follow QNT / SCALE as they play; PLAY arms the take
 * (the transport stays, the first note starts it). REC off, the drum track, playing: as before. Included by ui_optimist_test.c after
 * ui_optimist_stepsave.h (sv_open, sv_kw, se_set, sgl_notes, sm_undo, sm_count). */
static uint32_t sen_key(const track_t *t, uint32_t note)   /* the key that plays note (27: none) */
{
    uint32_t k;
    for (k = 0; k < 27u && kb_map(t, k) != note; k++)
        ;
    return k;
}
static uint32_t sen_sounding(const track_t *t, uint32_t note)   /* the track's voices sounding note (gate on) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate && t->v[i].note == note;
    return n;
}
static void sen_open(uint32_t len)                     /* STEP on T1, chromatic keys, POLY, nothing picked, REC armed: STEP ENTRY */
{
    track_t *t;
    sv_open();
    t = TSEL;
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_QUANT] = Q_OFF, t->p[P_TRANS] = 0, t->p[P_VOICE] = V_POLY;
    trk[0].p[P_SCALE] = 0, trk[0].p[P_ROOT] = 0;
    song.octave = 0;
    roll_fold = 0;
    key_vel = 100;
    st.se_len = 0;
    st.pk = PK_NONE;
    st.page = 0;
    frames(2);
    tap(B_REC);
    frames(2);
    pk_cards_down();
}
static void sen_rec_off(void)
{
    if (song.playing)
        tap(B_PLAY), frames(3);
    if (rec_wait || song.rec)
        tap(B_REC);
    frames(2);
}
static void sen_toggle_tests(void)
{
    track_t *t;
    uint32_t n0, n1, n2, f, d, h, k;
    sen_open(16);
    t = TSEL;
    check(se_on() && st.cur == 0u && step_keys(fm1_in.buttons) && ly_lock == LY_STEP && !song.playing && rec_wait,
          "step entry: stopped, REC on: STEP ENTRY, the cursor on the window's first step, the keys the UI's");
    check(!strcmp(ui.msg, "STEP ENTRY"), "step entry: ... said once, STEP ENTRY");
    n0 = kb_map(t, WK(0));
    key(WK(0));
    check(se_starts(t, 0, n0) >= 0 && (note_span(t, 0, (uint32_t)se_starts(t, 0, n0)) + 127u) / 128u == 1u,
          "step entry: a key adds its note on the cursor step, one step long");
    check(st.cur == 0u && !song.playing && rec_wait && !song.rec, "step entry: ... the cursor stays, the transport stopped, nothing recorded");
    check(an_on_n == 1u && an_on_nt[0] == n0 && sen_sounding(t, n0) > 0u, "step entry: ... the note added is heard");
    frames(12);
    check(an_on_n == 0u && sen_sounding(t, n0) == 0u, "step entry: ... a short note, let go by itself");
    check(t->step[0].vel == 100u && ((t->step[0].lvl >> (2u * (uint32_t)se_starts(t, 0, n0))) & 3u) == vel_lvl(100), "step entry: ... at the keys' velocity");
    check(pk_ok() && st.pk == 0u && st.nsel == NSEL_ALL, "step entry: ... the cursor step's notes are the picked ones, all");
    key(WK(0));
    check(!step_on(&t->step[0]) && !pk_ok(), "step entry: the same key again: the note removed");
    frames(1);
    check(an_on_n == 0u && sen_sounding(t, n0) == 0u, "step entry: ... a removal plays nothing");
    /* a chord: three keys down together, one undo level */
    n1 = kb_map(t, WK(2)), n2 = kb_map(t, WK(4));
    kdown(WK(0));
    kdown(WK(2));
    kdown(WK(4));
    kup(WK(0)), kup(WK(2)), kup(WK(4));
    check(se_starts(t, 0, n0) >= 0 && se_starts(t, 0, n1) >= 0 && se_starts(t, 0, n2) >= 0 && t->step[0].n == 3u, "step entry: three keys: a chord on the step");
    keys_lit(&f, &d, &h);
    check((f & (1u << WK(0))) && (f & (1u << WK(2))) && (f & (1u << WK(4))) && !(f & (1u << WK(1))) && !(d & sv_whites()),
          "step entry: the keys of the cursor step's notes lit, no step dim");
    sm_undo();
    check(!step_on(&t->step[0]), "step entry: undo: the chord gone, one level");
    kdown(WK(0)), kdown(WK(2)), kdown(WK(4));
    kup(WK(0)), kup(WK(2)), kup(WK(4));
    key(WK(2));
    check(se_starts(t, 0, n1) < 0 && se_starts(t, 0, n0) >= 0 && se_starts(t, 0, n2) >= 0 && t->step[0].n == 2u, "step entry: one key of the chord again: that note alone removed");
    key(WK(2));
    check(t->step[0].n == 3u && se_starts(t, 0, n1) >= 0, "step entry: ... and back");
    /* the same pitch sounding on into the cursor step: ended before it, struck again here */
    fm1_irq_off();
    se_set(t, 8, 60, 4);
    fm1_irq_on();
    turn(EN_K1, 10);
    k = sen_key(t, 60);
    key(k);
    check(strstr(sgl_notes(t), "8:60x2 10:60x1") != 0, "step entry: a key whose pitch sounds into the step: the old note ends before it, the new one starts");
    sen_rec_off();
}
static void sen_cursor_tests(void)
{
    track_t *t;
    uint32_t c0;
    sen_open(32);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 5, 67, 1);
    fm1_irq_on();
    frames(12);
    turn(EN_K1, 3);
    check(st.cur == 3u && !pk_ok() && an_on_n == 0u, "step entry: KNOB 1: the cursor, an empty step: nothing heard");
    turn(EN_K1, 2);
    frames(1);
    check(st.cur == 5u && pk_ok() && st.pk == 5u && an_on_n == 1u && an_on_nt[0] == 67u && sen_sounding(t, 67) > 0u,
          "step entry: KNOB 1 on a note: its notes heard, picked");
    turn(EN_K1, -9);
    check(st.cur == 0u && !strcmp(ui.msg, "FIRST STEP"), "step entry: KNOB 1 stops at the first step");
    turn(EN_K1, 20);
    check(st.cur == 20u && st.page == 1u, "step entry: KNOB 1 past the window: the window follows (17-32)");
    turn(EN_K1, 40);
    check(st.cur == 31u && !strcmp(ui.msg, "LAST STEP"), "step entry: ... stops at LEN's last step");
    turn(EN_K1, -26);
    frames(2);
    c0 = sm_count(C_NOTE);
    check(st.cur == 5u && st.page == 0u && c0 > 0u, "step entry: the cursor step (a note on it) drawn framed in C_NOTE");
    turn(EN_K1, 2);
    frames(2);
    check(sm_count(C_NOTE) > 0u, "step entry: ... an empty cursor step framed too");
    ppm("step_entry");
    sen_rec_off();
    st.pk = PK_NONE;
    frames(2);
    check(sm_count(C_NOTE) == 0u, "step entry: REC off, nothing picked: no frame");
}
static void sen_knob_tests(void)
{
    track_t *t;
    sen_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 1, 60, 1);
    t->step[1].n = 3, t->step[1].note[1] = 64, t->step[1].note[2] = 67;
    se_set(t, 12, 72, 1);
    fm1_irq_on();
    turn(EN_K1, 1);
    turn(EN_K2, 2);
    check(!strcmp(sgl_notes(t), "3:60x1 3:64x1 3:67x1 12:72x1") && st.cur == 3u, "step entry: KNOB 2 moves every note of the step, the cursor follows");
    turn(EN_K3, 1);
    check(!strcmp(sgl_notes(t), "3:61x1 3:65x1 3:68x1 12:72x1"), "step entry: KNOB 3 (no scale): the chord a semitone up, its shape kept");
    turn(EN_K3, -1);
    turn(EN_K4, 2);
    check(!strcmp(sgl_notes(t), "3:60x3 3:64x3 3:67x3 12:72x1") && st.se_len == 3u, "step entry: KNOB 4: every note 3 steps; the length kept");
    turn(EN_K1, 4);
    key(sen_key(t, 62));
    check(strstr(sgl_notes(t), "7:62x3") != 0, "step entry: a note added then: the length KNOB 4 set");
    turn(EN_K1, 1);
    turn(EN_K4, -1);
    check(st.se_len == 2u && !strcmp(ui.msg, "LENGTH 2"), "step entry: KNOB 4 on an empty step: the length new notes take, said");
    key(sen_key(t, 65));
    check(strstr(sgl_notes(t), "8:65x2") != 0, "step entry: ... a note added: 2 steps");
    turn(EN_K2, 1);
    check(st.cur == 9u && strstr(sgl_notes(t), "9:65x2") != 0, "step entry: KNOB 2 on one note: moved, the cursor follows");
    sm_undo();
    check(strstr(sgl_notes(t), "8:65x2") != 0, "step entry: undo: the move alone (its own level)");
    sm_undo();
    check(!strstr(sgl_notes(t), ":65x") && strstr(sgl_notes(t), "7:62x3") != 0, "step entry: undo again: the key's note (its own level)");
    turn(EN_K1, 1);
    frames(2);
    turn(EN_K2, 1);
    check(!strcmp(ui.msg, "NO NOTE HERE"), "step entry: KNOB 2 on an empty step: nothing moves, said");
    sen_rec_off();
}
static void sen_scale_tests(void)
{
    track_t *t;
    uint32_t k, w, ok = 1, any = 0, nb, kb;
    uint8_t nt[4];
    sen_open(16);
    t = TSEL;
    trk[0].p[P_SCALE] = 2, trk[0].p[P_ROOT] = 2;        /* D minor */
    t->p[P_QUANT] = Q_WHITE;
    for (w = 0; w < 6u; w++) {                          /* the white keys: the notes they play, in the scale */
        uint32_t n = kb_map(t, WK(w));
        key(WK(w));
        ok &= se_starts(t, 0, n) >= 0 && roll_scale_has((int32_t)n);
        key(WK(w));
    }
    check(ok && !step_on(&t->step[0]), "step entry: QNT WHITE: each white key enters the scale note it plays (added, then removed)");
    for (k = 0, kb = 27u; k < 27u; k++)
        if (punch_key(k) < 0) {
            kb = k;
            break;
        }
    key(kb);
    check(!step_on(&t->step[0]) && !strcmp(ui.msg, "SILENT KEY"), "step entry: QNT WHITE: a black key (silent when played) adds nothing, said");
    t->p[P_QUANT] = Q_SNAP;
    nb = kb_map(t, kb);
    key(kb);
    check(se_starts(t, 0, nb) >= 0 && roll_scale_has((int32_t)nb) && nb != 53u + kb && kb_notes(t, kb, nt) == 1u && nt[0] == nb, "step entry: QNT SNAP: a black key enters its note rounded into the scale, as played");
    key(kb);
    t->p[P_QUANT] = Q_OFF;
    t->p[P_CHORD] = 1;
    key(WK(0));
    any = kb_notes(t, WK(0), nt);
    for (k = 0, ok = any > 1u; k < any; k++)
        ok &= se_starts(t, 0, nt[k]) >= 0;
    check(ok && t->step[0].n == any, "step entry: chord mode: a key enters the chord it plays");
    key(WK(0));
    check(!step_on(&t->step[0]), "step entry: ... again: the chord removed");
    t->p[P_CHORD] = 0;
    /* KNOB 3 as the picker's: semitones; with FOLD on and a scale, the scale's rows */
    t->p[P_QUANT] = Q_OFF;
    trk[0].p[P_SCALE] = 1, trk[0].p[P_ROOT] = 0;        /* C major */
    fm1_irq_off();
    se_set(t, 0, 64, 1);
    fm1_irq_on();
    frames(2);
    turn(EN_K3, 1);
    check(t->step[0].note[0] == 65u, "step entry: KNOB 3, FOLD off: a semitone (E -> F), as the picker");
    turn(EN_K3, 1);
    check(t->step[0].note[0] == 66u, "step entry: ... F -> F# (chromatic, as the picker's KNOB 3)");
    roll_fold = 1;
    frames(2);
    turn(EN_K3, 1);
    check(t->step[0].note[0] == 67u, "step entry: KNOB 3, FOLD on with a scale: the next scale row (F# -> G), as the picker");
    turn(EN_K3, 1);
    check(t->step[0].note[0] == 69u, "step entry: ... G -> A (G# not in C major)");
    roll_fold = 0;
    trk[0].p[P_SCALE] = 0;
    sen_rec_off();
}
static void sen_vel_tests(void)
{
    track_t *t;
    uint32_t n;
    sen_open(16);
    t = TSEL;
    key_vel = 40;
    n = kb_map(t, WK(3));
    key(WK(3));
    check(se_starts(t, 0, n) >= 0 && t->step[0].vel == 40u && ((t->step[0].lvl >> (2u * (uint32_t)se_starts(t, 0, n))) & 3u) == vel_lvl(40),
          "step entry: a note added takes the keys' velocity (SHIFT2 + SELECT)");
    key_vel = 100;
    sen_rec_off();
}
static void sen_play_tests(void)
{
    track_t *t;
    uint32_t n, i, f, d, h;
    sen_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 2, 60, 1);
    fm1_irq_on();
    tap(B_PLAY);
    frames(2);
    check(!song.playing && rec_wait && !song.rec && !se_on() && !step_keys(fm1_in.buttons) && ly_lock == LY_PLAY && !strcmp(ui.msg, "REC ARMED"),
          "step entry: PLAY arms the take: the transport stays stopped, the keys play, REC ARMED said");
    keys_lit(&f, &d, &h);
    check(!(d & sv_whites()), "step entry: ... the keys a keyboard again (no step dim)");
    n = pen_note[0];
    kdown(WK(9));
    frames(2);
    check(song.playing && song.rec, "step entry: ... the first note starts the take");
    kup(WK(9));
    frames(4);
    for (i = 0, n = 0; i < 16u; i++)
        n += step_on(&t->step[i]);
    check(n == 2u && !st.se_park, "step entry: ... the note recorded; the arm done");
    tap(B_PLAY);
    frames(4);
    check(!song.playing && !rec_wait && !se_on() && step_keys(fm1_in.buttons), "step entry: stopped: the take and REC end, the keys are steps");
    /* PLAY twice: the take at once (PLAY while armed, as before) */
    tap(B_REC);
    frames(2);
    check(se_on(), "step entry: REC again: STEP ENTRY");
    tap(B_PLAY);
    frames(2);
    tap(B_PLAY);
    frames(3);
    check(song.playing && song.rec && !se_on(), "step entry: PLAY again while armed: the transport and the recording start");
    frames(3);
    check(!se_on() && !step_keys(fm1_in.buttons), "step entry: playing with REC: the keys record as before (no STEP ENTRY)");
    sen_rec_off();
    /* PLAY's arm let go by REC off */
    tap(B_REC);
    tap(B_PLAY);
    frames(2);
    tap(B_REC);
    frames(2);
    check(!rec_wait && !st.se_park && !song.playing && step_keys(fm1_in.buttons), "step entry: armed by PLAY, REC off: the keys are steps, nothing armed");
    key(WK(7));
    check(step_on(&t->step[7]), "step entry: REC off: a key toggles its step as before");
}
static void sen_other_tests(void)
{
    sen_open(16);
    sen_rec_off();
    reset_ui();
    song.sel = TRK_DRUM;
    tap(B_SEQ);
    tap(B_REC);
    frames(2);
    check(!se_on() && step_keys(fm1_in.buttons), "step entry: the drum track, REC on: no STEP ENTRY, its white keys stay steps");
    tap(B_REC);
    frames(2);
    song.sel = 0;
    reset_ui();
}
static void sen_dbl_tests(void)                         /* the double SEQ tap: STEP ENTRY on / off, stopped or playing (op_step.c step_seq_tap) */
{
    track_t *t;
    uint32_t r0, n0, n1, c0;
    sen_open(32);
    sen_rec_off();
    t = TSEL;
    cards_up();
    frames(30);
    r0 = ui.row[SCR_STEP];
    check(!se_on() && !song.playing && !rec_wait, "step entry dbl: REC off, stopped: the step sequencer");
    tap(B_SEQ);
    frames(30);
    check(!se_on() && (step_rows() < 2u || ui.row[SCR_STEP] != r0), "step entry dbl: one SEQ tap: its next row as ever, no mode change");
    tap(B_SEQ);
    frames(30);
    r0 = ui.row[SCR_STEP];
    tap(B_SEQ);
    tap(B_SEQ);
    check(se_on() && ui.row[SCR_STEP] == r0 && !strcmp(ui.msg, "STEP ENTRY"), "step entry dbl: two SEQ taps: STEP ENTRY on (the first tap's row put back), said");
    n0 = kb_map(t, WK(0));
    key(WK(0));
    check(se_starts(t, st.cur, n0) >= 0 && !song.playing, "step entry dbl: stopped, REC off: a key adds its note on the cursor step");
    frames(30);
    tap(B_PLAY);
    frames(4);
    check(song.playing && !song.rec && se_on() && step_keys(fm1_in.buttons) && !st.follow, "step entry dbl: PLAY is the transport's: playing, STEP ENTRY stays, the cursor its own");
    pk_cards_down();
    turn(EN_K1, 3);
    n1 = kb_map(t, WK(2));
    key(WK(2));
    check(st.cur == 3u && se_starts(t, 3, n1) >= 0 && song.playing && !song.rec, "step entry dbl: playing: a key adds its note on the cursor step (K1), no take");
    check(an_on_n == 1u && an_on_nt[0] == n1, "step entry dbl: ... heard once");
    key(WK(2));
    check(!step_on(&t->step[3]), "step entry dbl: ... the same key again removes it");
    key(WK(2));
    frames(30);
    turn(EN_K1, -1);
    frames(30);
    turn(EN_K1, 1);
    check(st.cur == 3u && song.playing && an_on_n == 0u && !an_req, "step entry dbl: playing: a cursor move onto a step with notes sounds nothing");
    key(WK(2));
    frames(30);
    tap(B_SEQ);
    tap(B_SEQ);
    check(!se_on() && st.follow && song.playing && !strcmp(ui.msg, "STEP SEQ"), "step entry dbl: two SEQ taps again: the step sequencer, said");
    frames(30);
    tap(B_SEQ);
    tap(B_SEQ);
    check(se_on() && song.playing, "step entry dbl: ... and on again");
    tap(B_PLAY);
    frames(4);
    check(!song.playing && se_on(), "step entry dbl: PLAY stops, STEP ENTRY stays");
    frames(30);
    c0 = st.cur;
    if (!step_on(&t->step[c0]))
        key(WK(2));
    pk_cards_down();
    turn(EN_K1, c0 % 16u ? -1 : 1);
    frames(30);
    pk_cards_down();
    turn(EN_K1, c0 % 16u ? 1 : -1);
    check(st.cur == c0 && !song.playing && (an_on_n > 0u || an_req), "step entry dbl: stopped: a cursor move onto a step with notes previews it");
    tap(B_SEQ);
    tap(B_SEQ);
    check(!se_on(), "step entry dbl: off again");
    sen_rec_off();
    tap(B_REC);
    frames(2);
    check(rec_wait && !se_on(), "step entry dbl: REC armed, stopped, switched off by the toggle: it stays off (the keys play)");
    tap(B_SEQ);
    tap(B_SEQ);
    check(se_on() && rec_wait && !song.playing, "step entry dbl: ... a double tap: on again");
    sen_rec_off();
    reset_ui();
}
static int sen_toast_drawn(void)                        /* the toast's frame (draw_toast: C_HI, its top row in the middle) is on the screen */
{
    return ui.toast_t && px_at(120, (uint32_t)OP_PY + 44u, C_HI);
}
static void sen_dbl_toast_tests(void)                   /* the toggle's words are DRAWN over STEP's cards, cards up and down, stopped and playing */
{
    uint32_t c, pl;
    char nm[48];
    for (pl = 0; pl < 2u; pl++)
        for (c = 0; c < 2u; c++) {
            sen_open(32);
            sen_rec_off();
            if (pl)
                tap(B_PLAY), frames(4);
            if (c)
                cards_up();
            else
                pk_cards_down();
            frames(100);
            check(!se_on() && !sen_toast_drawn(), "step entry dbl toast: before, no toast on the screen");
            tap(B_SEQ);
            tap(B_SEQ);
            frames(2);
            check(se_on() && !strcmp(ui.msg, "STEP ENTRY") && sen_toast_drawn() && !ui.msg_t, "step entry dbl toast: STEP ENTRY drawn as a toast");
            snprintf(nm, sizeof nm, "stepplay-on-%s-cards%s", pl ? "playing" : "stopped", c ? "up" : "down");
            ppm(nm);
            frames(100);
            check(!sen_toast_drawn(), "step entry dbl toast: ... and gone after its time");
            tap(B_SEQ);
            tap(B_SEQ);
            frames(2);
            check(!se_on() && !strcmp(ui.msg, "STEP SEQ") && sen_toast_drawn(), "step entry dbl toast: STEP SEQ drawn as a toast");
            snprintf(nm, sizeof nm, "stepplay-off-%s-cards%s", pl ? "playing" : "stopped", c ? "up" : "down");
            ppm(nm);
            sen_rec_off();
            reset_ui();
        }
}
static void sen_ent_note(uint32_t step, uint32_t k1, uint32_t k2, uint32_t k3, uint32_t k4)   /* the notes of keys k1.. (27: none) entered on a step */
{
    uint32_t ks[4] = {k1, k2, k3, k4}, i;
    pk_cards_down();
    turn(EN_K1, (int32_t)step - (int32_t)st.cur);
    for (i = 0; i < 4u; i++)
        if (ks[i] < 27u)
            kdown(ks[i]);
    for (i = 0; i < 4u; i++)
        if (ks[i] < 27u)
            kup(ks[i]);
}
static void sen_at(uint32_t step)
{
    pk_cards_down();
    turn(EN_K1, (int32_t)step - (int32_t)st.cur);
}
static uint32_t sen_ext_at(const track_t *t, uint32_t step, uint32_t note)
{
    int32_t k = se_starts(t, step, note);
    return k < 0 ? 0u : se_ext(t, step, (uint32_t)k);
}
static void sen_move_tests(void)                        /* a note moves / lengthens over other pitches; only the same pitch (or a full step) stops it */
{
    track_t *t;
    uint32_t a, b, c, d, e;
    int32_t k;
    sen_rec_off();
    reset_ui();
    sen_open(16);
    t = TSEL;
    a = kb_map(t, WK(0)), b = kb_map(t, WK(2)), c = kb_map(t, WK(4)), d = kb_map(t, WK(5)), e = kb_map(t, WK(7));
    sen_ent_note(0, WK(0), 27, 27, 27);
    sen_ent_note(1, WK(2), 27, 27, 27);
    sen_at(0);
    turn(EN_K2, 1);
    check(se_starts(t, 1, a) >= 0 && se_starts(t, 1, b) >= 0 && se_starts(t, 0, a) < 0 && t->step[1].n == 2u, "step entry move: a note onto a step with another pitch: it joins it");
    /* a chord joins too */
    sen_rec_off();
    reset_ui();
    sen_open(16);
    t = TSEL;
    sen_ent_note(0, WK(0), WK(4), 27, 27);
    sen_ent_note(1, WK(2), 27, 27, 27);
    sen_at(0);
    turn(EN_K2, 1);
    check(t->step[0].n == 0u && t->step[1].n == 3u && se_starts(t, 1, a) >= 0 && se_starts(t, 1, b) >= 0 && se_starts(t, 1, c) >= 0, "step entry move: a chord onto a step with another pitch: all three there");
    /* the same pitch is passed over (SAME NOTE) */
    sen_rec_off();
    reset_ui();
    sen_open(16);
    t = TSEL;
    sen_ent_note(0, WK(0), 27, 27, 27);
    sen_ent_note(1, WK(0), 27, 27, 27);
    sen_at(0);
    turn(EN_K2, 1);
    check(t->step[1].n == 1u && se_starts(t, 1, a) >= 0 && se_starts(t, 2, a) >= 0 && se_starts(t, 0, a) < 0, "step entry move: the same pitch there: not merged, it goes past");
    /* a full step refuses */
    sen_rec_off();
    reset_ui();
    sen_open(16);
    t = TSEL;
    sen_ent_note(0, WK(0), 27, 27, 27);
    sen_ent_note(1, WK(2), WK(4), WK(5), WK(7));
    sen_at(0);
    turn(EN_K2, 1);
    check(t->step[1].n == 4u && se_starts(t, 0, a) >= 0 && se_starts(t, 1, a) < 0 && !strcmp(ui.msg, "STEP FULL"), "step entry move: onto a full step (four notes): refused, STEP FULL");
    (void)d; (void)e;
    /* LENGTH over a different pitch */
    sen_rec_off();
    reset_ui();
    sen_open(16);
    t = TSEL;
    sen_ent_note(0, WK(0), 27, 27, 27);
    sen_ent_note(2, WK(2), 27, 27, 27);
    sen_at(0);
    for (k = 0; k < 4; k++) {
        pk_cards_down();
        turn(EN_K4, 1);
    }
    check(sen_ext_at(t, 0, a) >= 3u && se_starts(t, 2, b) >= 0, "step entry length: past a note of another pitch (after the touching detent)");
    /* LENGTH toward the same pitch */
    sen_rec_off();
    reset_ui();
    sen_open(16);
    t = TSEL;
    sen_ent_note(0, WK(0), 27, 27, 27);
    sen_ent_note(2, WK(0), 27, 27, 27);
    sen_at(0);
    for (k = 0; k < 5; k++) {
        pk_cards_down();
        turn(EN_K4, 1);
    }
    check(sen_ext_at(t, 0, a) <= 2u && se_starts(t, 2, a) >= 0, "step entry length: toward the same pitch: it stops before it");
    sen_rec_off();
    reset_ui();
}
static void sen_dbl_cards_tests(void)                   /* a single SEQ tap's cards change waits for the double-tap window; a double tap never shows it */
{
    uint32_t r0, i, ok;
    sen_rec_off();
    reset_ui();
    sen_open(32);
    sen_rec_off();
    pk_cards_down();
    r0 = ui.row[SCR_STEP];
    tap(B_SEQ);
    for (ok = 1, i = 0; i < 15u; i++)
        frames(1), ok &= ui.cards_t == 0u && ui.row[SCR_STEP] == r0;
    check(ok && !se_on(), "step entry dbl cards: one SEQ tap, cards down: nothing shown inside the window");
    frames(12);
    check(ui.cards_t > 0u && !se_on(), "step entry dbl cards: ... the cards come back once the window has passed");
    pk_cards_down();
    tap(B_SEQ);
    tap(B_SEQ);
    for (ok = 1, i = 0; i < 40u; i++)
        frames(1), ok &= ui.cards_t == 0u && ui.row[SCR_STEP] == r0;
    check(ok && se_on(), "step entry dbl cards: two SEQ taps, cards down: the cards never show, STEP ENTRY on");
    sen_rec_off();
    reset_ui();
    sen_open(32);
    sen_rec_off();
    cards_up();
    r0 = ui.row[SCR_STEP];
    tap(B_SEQ);
    tap(B_SEQ);
    for (ok = 1, i = 0; i < 40u; i++)
        frames(1), ok &= ui.row[SCR_STEP] == r0 && (i > 1u || ui.toast_t);
    check(ok && se_on() && ui.toast_t, "step entry dbl cards: two SEQ taps, cards up: the row stays, the toast is shown");
    cards_up();
    tap(B_SEQ);
    for (ok = 1, i = 0; i < 15u; i++)
        frames(1), ok &= ui.row[SCR_STEP] == r0;
    check(ok, "step entry dbl cards: one SEQ tap, cards up: the row waits");
    frames(12);
    check(step_rows() < 2u || ui.row[SCR_STEP] != r0, "step entry dbl cards: ... the next row after the window");
    sen_rec_off();
    reset_ui();
}
static void stepentry_tests(void)
{
    sen_dbl_cards_tests();
    sen_move_tests();
    sen_dbl_toast_tests();
    sen_dbl_tests();
    sen_toggle_tests();
    sen_cursor_tests();
    sen_knob_tests();
    sen_scale_tests();
    sen_vel_tests();
    sen_play_tests();
    sen_other_tests();
    se_open(16);
    tap(B_HOME);
    reset_ui();
}
