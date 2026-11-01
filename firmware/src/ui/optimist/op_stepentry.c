/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP ENTRY (the user, 2026-11-01; docs/UI-OPTIMIST-DESIGN.md 4.2 "STEP ENTRY"): a synth track's STEP, the transport stopped
 * and REC on (armed: rec_wait). A cursor step instead of the playhead:
 *   KNOB 1        the cursor, a step a detent (stops at the first / last); the window follows it; the notes that start on the
 *                 step it lands on sound (audition_notes, ~200 ms)
 *   KNOB 2 3 4    as the note picker's (op_step.c pk_edit), on every note that starts on the cursor step (all selected, a chord
 *                 together): KNOB 2 moves them (SHIFT: the nudge) and the cursor follows, KNOB 3 their pitch (as the picker's:
 *                 semitones, FOLD's rows when FOLD is on), KNOB 4 their length, which new notes then take (no note there:
 *                 KNOB 4 sets that length alone, "LENGTH n")
 *   a key         toggles its notes (kb_notes: as the keys play them, QNT / ROOT / SCALE / TRN / OCT, a chord in chord mode)
 *                 on the cursor step: all start there -> removed (silent); else the missing ones added, the last length set
 *                 (else one step), at the keys' velocity (SHIFT2 + SELECT), and heard. Several keys down together: a chord,
 *                 one undo level. The cursor stays
 *   PLAY          arms the take (st.se_park): the transport stays stopped, the keys play as today's armed keyboard, the first
 *                 note starts the take (seq.c arm_start); PLAY again starts it at once (PLAY while armed, as before). REC
 *                 off or the take started ends the arm. With REC's START COUNT, PLAY counts in as before
 * The cursor's notes are the picked ones (st.pk, NSEL_ALL): SAVE tapped opens their cards, SAVE held mutes them, HOME held
 * deletes them, as the picker's. Main loop only; the sound is seq.c's (audition_notes, the audio context). */
static int se_on(void)
{
    return ui.scr == SCR_STEP && !is_drum(TSEL) && rec_wait && !song.playing && !song.rec && !transport_req && !ft_on &&
           !st.se_park && !st.play && (!st.held || st.latch)
#if FELUCCA_REC_MODES
           && !ci_on
#endif
        ;
}
static int32_t se_starts(const track_t *t, uint32_t a, uint32_t p)   /* the slot of pitch p among the notes starting at a, -1 none */
{
    uint8_t k[4];
    uint32_t i, n = a < trk_len(t) ? pk_heads(t, a, k) : 0u;
    for (i = 0; i < n; i++)
        if (t->step[a].note[k[i]] == p)
            return k[i];
    return -1;
}
static uint32_t se_ext(const track_t *t, uint32_t a, uint32_t k) { return (note_span(t, a, k) + 127u) / 128u; }
static int32_t se_through(const track_t *t, uint32_t a, uint32_t p, int32_t *slot)   /* the step pitch p starts at and sounds on into a from, -1 none */
{
    int32_t h, k;
    for (h = (int32_t)a - 1; h >= 0; h--)
        if ((k = se_starts(t, (uint32_t)h, p)) >= 0) {
            *slot = k;
            return (uint32_t)h + se_ext(t, (uint32_t)h, (uint32_t)k) > a ? h : -1;
        }
    return -1;
}
static void se_slot_cut(step_t *s, uint32_t k)          /* note slot k out of the step, the others moved down */
{
    uint32_t i, lo = (1u << (2u * k)) - 1u;
    for (i = k; i + 1u < s->n && i + 1u < 4u; i++)
        s->note[i] = s->note[i + 1u], s->ngate[i] = s->ngate[i + 1u];
    s->lvl = (uint8_t)((s->lvl & lo) | ((s->lvl >> 2) & ~lo));
    s->rat = (uint8_t)((s->rat & lo) | ((s->rat >> 2) & ~lo));
    s->n--;
    s->note[s->n] = 0, s->ngate[s->n] = 0;
}
/* pitch p added at step a, L steps (IRQ off). The same pitch sounding on into a ends before it (struck again here); the note goes in
 * as recording puts it (seq.c step_add: a chord, a carry step in a long note's middle, MONO one note a step); then its length
 * (olp_len: over the notes after it, the others sound as before). -> 0 done, else why not (the toast) */
static const char *se_add(track_t *t, uint32_t a, uint32_t p, uint32_t L)
{
    step_t *s = &t->step[a];
    int32_t h, k;
    uint32_t len = trk_len(t);
    const char *why;
    if (step_on(s) && s->n >= 4u && rec_poly(t)) {
        for (k = 0; k < (int32_t)s->n && k < 4 && s->note[k] != p; k++)
            ;
        if (k == (int32_t)s->n || k == 4)
            return "STEP FULL";
    }
    if ((h = se_through(t, a, p, &k)) >= 0 && (why = olp_len(t, (uint32_t)h, 1u << k, a - (uint32_t)h)) != 0)
        return why;
    step_add(t, a, p, key_vel, vel_lvl(key_vel), 0);
    t->seq_active = 1;
    for (k = 0; k < (int32_t)s->n && k < 4 && s->note[k] != p; k++)
        ;
    L = L < 1u ? 1u : L > len - a ? len - a : L;
    return k < (int32_t)s->n && k < 4 && L != se_ext(t, a, (uint32_t)k) ? olp_len(t, a, 1u << k, L) : 0;
}
/* pitch p starting at a removed (IRQ off): the step's only note as a tap clears it (its ties, its step-only events); one of
 * several first shortened to its step (the others sound on as before), then taken out. -> 0 done, else why not */
static const char *se_drop(track_t *t, uint32_t a, uint32_t p)
{
    step_t *s = &t->step[a];
    int32_t k = se_starts(t, a, p);
    const char *why;
    if (k < 0)
        return 0;
    if (s->n == 1u) {
        step_wipe(t, a, 0);
        return 0;
    }
    if (se_ext(t, a, (uint32_t)k) != 1u && (why = olp_len(t, a, 1u << k, 1u)) != 0)
        return why;
    se_slot_cut(s, (uint32_t)k);
    return 0;
}
static void se_sync(void)                               /* the cursor's notes are the picked ones, all of them */
{
    if (st.latch)
        return;
    st.pk = pk_note(tsel(), st.cur) ? st.cur : PK_NONE;
    st.pk_trk = song.sel, st.nsel = NSEL_ALL, st.snapl = 0;
}
static void se_enter(void)                              /* STEP ENTRY begins: the cursor on the picked note, else the window's first step */
{
    if (st.se_was)
        return;
    st.se_was = 1;
    st.cur = (uint8_t)(pk_ok() ? st.pk : st.page * 16u);
    st.sess = (undo_sess += 4u) | 3u;
    ui_message("STEP ENTRY");
}
static void se_tick(void)                               /* once a frame (op_step.c step_tick) */
{
    uint32_t len = trk_len(tsel());
    if (st.se_park && (!rec_wait || song.playing))
        st.se_park = 0;                                 /* (the take started, or REC off: PLAY's arm done) */
    if (!se_on()) {
        st.se_was = 0, st.se_down = 0;
        return;
    }
    se_enter();
    if (st.cur >= len)
        st.cur = (uint8_t)(len - 1u);
    se_sync();
}
static void se_hear(void)                               /* the notes starting on the cursor step, heard */
{
    const track_t *t = tsel();
    const step_t *s = &t->step[st.cur];
    uint8_t k[4], nt[4];
    uint32_t i, n = pk_heads(t, st.cur, k);
#if FELUCCA_AUTO
    if (step_muted(t, st.cur, 0))
        return;                                         /* (a muted step plays as a rest) */
#endif
    for (i = 0; i < n; i++)
        nt[i] = s->note[k[i]];
    if (n)
        audition_notes(song.sel % NTRK, nt, n, step_vel(s, k[0]));
}
static void se_key(uint32_t k, uint32_t down)
{
    track_t *t = tsel();
    uint8_t nt[4], add[4];
    uint32_t n, i, a = 0, all = 1, bit = 1u << k;
    const char *why = 0;
    if (!down) {
        st.se_down &= ~bit;
        return;
    }
    if (op_armed())
        return;                                         /* (a question waits: the keys do nothing) */
    se_enter();
    if (st.latch)
        pk_unlatch();                                   /* (the cursor notes' cards: a key lets them go, as a step key) */
    if (!(n = kb_notes(t, k, nt))) {
        ui_message("SILENT KEY");                       /* (QNT WHITE: a black key; ALL: past the range) */
        return;
    }
    if (!(st.se_down & ~bit))
        st.se_ksess = (undo_sess += 4u) | 3u;           /* (keys down together: a chord, one undo level) */
    st.se_down |= bit;
    for (i = 0; i < n; i++)
        all &= se_starts(t, st.cur, nt[i]) >= 0;
    undo_mark(t, st.se_ksess);
    st.sess = (undo_sess += 4u) | 3u;                   /* (the knobs' edits after it: a level of their own) */
    fm1_irq_off();
    for (i = 0; i < n && !why; i++) {
        if (all)
            why = se_drop(t, st.cur, nt[i]);
        else if (se_starts(t, st.cur, nt[i]) < 0 && !(why = se_add(t, st.cur, nt[i], st.se_len)))
            add[a++] = nt[i];
    }
    fm1_irq_on();
    sync_reload = 1;
    if (why)
        ui_message(why);
    if (a)
        audition_notes(song.sel % NTRK, add, a, key_vel);
    se_sync();
    op_repaint();
}
static void se_knob(uint32_t k, int32_t s, int shift)
{
    track_t *t = tsel();
    int32_t len = (int32_t)trk_len(t), c;
    char m[16];
    se_enter();
    if (k == 0u) {
        c = ui_clamp((int32_t)st.cur + s, 0, len - 1);
        if (c != (int32_t)st.cur + s)
            ui_message(s < 0 ? "FIRST STEP" : "LAST STEP");   /* (at the end it stays, no wrap, as the picker) */
        if (c == (int32_t)st.cur)
            return;
        st.cur = (uint8_t)c;
        st.page = (uint8_t)(c / 16), st.follow = 0;
        st.sess = (undo_sess += 4u) | 3u;               /* (another step: its edits one undo level) */
        se_sync();
        se_hear();
        op_repaint();
        return;
    }
    if (!pk_note(t, st.cur)) {
        if (k != 3u) {
            ui_message("NO NOTE HERE");
            return;
        }
        st.se_len = (uint8_t)ui_clamp((st.se_len ? st.se_len : 1) + s, 1, len);   /* (KNOB 4 alone: the length new notes take) */
        str_cpy(m, "LENGTH ", sizeof m);
        fmt_app(m, st.se_len);
        ui_message(m);
        return;
    }
    se_sync();
    pk_edit(k, s, shift);                               /* (the picker's own edits, the cursor's notes picked whole) */
    if (st.pk != PK_NONE)
        st.cur = st.pk;                                 /* (KNOB 2: the cursor follows the notes moved) */
    se_sync();
    if (k == 3u && st.pk != PK_NONE)
        st.se_len = (uint8_t)se_ext(t, st.cur, sel_one(t, st.cur));   /* (KNOB 4: the length new notes take now) */
}
/* the keys lit in STEP ENTRY: those whose notes all start on the cursor step (and the keys down) */
static uint32_t se_leds(void)
{
    const track_t *t = tsel();
    uint8_t nt[4];
    uint32_t k, i, n, on, m = 0;
    if (!pk_note(t, st.cur))
        return 0;
    for (k = 0; k < 27u; k++) {
        n = kb_notes(t, k, nt);
        for (on = n > 0u, i = 0; i < n; i++)
            on &= se_starts(t, st.cur, nt[i]) >= 0;
        m |= on << k;
    }
    return m;
}
static int se_play(void)                                /* PLAY in STEP ENTRY: the take armed, the transport stays; 1 done */
{
    if (!se_on())
        return 0;
#if FELUCCA_REC_MODES
    if (rec_count)
        return 0;                                       /* (START COUNT: PLAY counts in, as before) */
#endif
    st.se_park = 1;
    st.se_was = 0;
    ui_message("REC ARMED");
    return 1;
}
