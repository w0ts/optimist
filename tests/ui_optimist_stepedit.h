/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's note editing on the device (the user, 2026-10-31: "sequencer editor is still bugged.
 * sometimes i cannot move a note past a certain point. if i move a note on the right and there is another note, it is
 * erased. move past it :) i extended the lowest note, it created the gap. no idea why."):
 *   stop     a note moved (KNOB 2, PRESETS held) goes on over and past other notes; only the pattern's first / last step
 *            stops it (was: SAME NOTE, NOTES OVERLAP at a note sounding over another)
 *   past     onto a note of its own pitch it skips to the first step it fits whole; the other note stays where it is (was:
 *            the snap, then the two swapped); another pitch's note: they share the step, the other stays
 *   length   LENGTH on one note of a chord (the lowest picked alone) lengthens that note alone; LENGTH up to a note of the
 *            same pitch never writes it away (was: the pass took it for the lengthened note's own and erased it)
 *   fuzz     random patterns: a note moved or lengthened, every other note as it was
 * Included by ui_optimist_test.c after ui_optimist_seqglide.h (sgl_notes). */
static void se_set(track_t *t, uint32_t i, uint32_t n, uint32_t len)   /* note n at step i, len steps (its TIEs) */
{
    uint32_t j;
    memset(&t->step[i], 0, sizeof t->step[i]);
    t->step[i].time = ST_NOTE, t->step[i].n = 1, t->step[i].note[0] = (uint8_t)n, t->step[i].vel = 100;
    for (j = 1; j < len; j++)
        memset(&t->step[i + j], 0, sizeof t->step[0]), t->step[i + j].time = ST_TIE;
}
static void se_open(uint32_t len)
{
    track_t *t;
    reset_ui();
    if (song.playing)
        tap(B_PLAY), frames(3);
    song.sel = 0;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = (int16_t)len;
#if FELUCCA_AUTO
    AL(t)->n = 0;
    auto_touch(trk_index(t));
#endif
    tap(B_SEQ);
    pk_cards_down();
}
typedef struct {
    uint8_t s, p, l;
} se_n_t;
static uint32_t se_list(const track_t *t, se_n_t *o)    /* every note that starts: step, pitch, steps */
{
    uint32_t i, j, n, c = 0;
    uint8_t k[4];
    for (i = 0; i < trk_len(t); i++)
        for (j = 0, n = pk_heads(t, i, k); j < n && c < 64u; j++, c++)
            o[c].s = (uint8_t)i, o[c].p = t->step[i].note[k[j]], o[c].l = (uint8_t)((note_span(t, i, k[j]) + 127u) / 128u);
    return c;
}
static int se_has(const se_n_t *o, uint32_t n, uint32_t s, uint32_t p, uint32_t l)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (o[i].s == s && o[i].p == p && o[i].l == l)
            return 1;
    return 0;
}
static uint32_t se_rnd = 1;
static uint32_t se_r(uint32_t m)
{
    se_rnd = se_rnd * 1103515245u + 12345u;
    return (se_rnd >> 16) % m;
}
/* random patterns of notes, chords and ties; a note picked (alone, or its whole chord), knob 2 (moved right until it
 * stops) or 4 (LENGTH +2): -> the runs where another note changed or the picked one did not do what was asked */
static uint32_t se_fuzz(int knob, uint32_t runs)
{
    uint32_t it, bad = 0;
    for (it = 0; it < runs; it++) {
        track_t *t;
        se_n_t a[64], b[64];
        uint32_t na, nb, i, j, pick, ps, pp, pl, ns, ok = 1, c;
        se_open(16);
        t = TSEL;
        fm1_irq_off();
        for (i = se_r(3); i < 15u;) {
            uint32_t L = 1u + se_r(3);
            L = i + L > 16u ? 16u - i : L;
            se_set(t, i, 60u + 2u * se_r(3), L);
            if (!se_r(3))
                t->step[i].n = 2, t->step[i].note[1] = (uint8_t)(t->step[i].note[0] + 5u);
            i += L + se_r(3);
        }
        fm1_irq_on();
        na = se_list(t, a);
        if (!na)
            continue;
        pick = se_r(na);
        ps = a[pick].s, pp = a[pick].p, pl = a[pick].l;
        st.pk = (uint8_t)ps, st.pk_trk = song.sel;
        for (j = 0; j < t->step[ps].n && t->step[ps].note[j] != pp; j++)
            ;
        st.nsel = t->step[ps].n > 1u && se_r(2) ? (uint8_t)j : (uint8_t)NSEL_ALL;
        ns = st.nsel;
        if (knob == 4) {
            turn(EN_K4, 1), turn(EN_K4, 1);
        } else {
            for (c = 0; c < 20u; c++)
                turn(EN_K2, 1);
        }
        nb = se_list(t, b);
        for (i = 0; i < na; i++)
            if (!(ns == NSEL_ALL ? a[i].s == ps : (a[i].s == ps && a[i].p == pp)) && !se_has(b, nb, a[i].s, a[i].p, a[i].l))
                ok = 0;                                 /* (another note moved, changed or gone) */
        if (nb != na)
            ok = 0;
        if (knob == 4 && !se_has(b, nb, ps, pp, pl + 2u) && !se_has(b, nb, ps, pp, pl + 1u) && !se_has(b, nb, ps, pp, pl))
            ok = 0;
        if (knob == 2 && ns != NSEL_ALL && !se_has(b, nb, st.pk, pp, pl))
            ok = 0;                                     /* (the note moved whole) */
        if (knob == 2 && ns != NSEL_ALL && st.pk + pl < 16u && strcmp(ui.msg, "LAST STEP"))
            ok = 0;                                     /* (moved right until it stops: only at the pattern's end) */
        bad += !ok;
    }
    return bad;
}
static void stepedit_tests(void)
{
    track_t *t;
    /* past: a note of its own pitch in the way */
    se_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 3, 60, 1);
    se_set(t, 5, 60, 2);
    fm1_irq_on();
    turn(EN_K1, 1);
    turn(EN_K2, 1);
    check(st.pk == 4u && !strcmp(sgl_notes(t), "4:60x1 5:60x2"), "stepedit: KNOB 2 right: the note a step on, touching one of its pitch");
    turn(EN_K2, 1);
    check(st.pk == 7u && !strcmp(sgl_notes(t), "5:60x2 7:60x1"), "stepedit: ... the next detent: past it whole, the other note where it was");
    turn(EN_K2, -1);
    check(st.pk == 4u && !strcmp(sgl_notes(t), "4:60x1 5:60x2"), "stepedit: ... and back past it");
    /* past: a held step (PRESETS), its pitch in the way, across the window's page */
    se_open(32);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 14, 62, 1);
    se_set(t, 15, 62, 2);
    fm1_irq_on();
    kdown(WK(14));
    turn(EN_PRESET, 1);
    check(!strcmp(sgl_notes(t), "15:62x2 17:62x1") && st.page == 1u && st.held == 1u << 1,
          "stepedit: PRESETS on a held step: past a note of its pitch, onto the next page, the hold with it");
    kup(WK(14));
    /* stop: a note sounding over another's start was a wall (NOTES OVERLAP) */
    se_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 3, 60, 1);
    se_set(t, 4, 64, 3);
    t->step[5].time = ST_NOTE, t->step[5].flags = SF_CARRY, t->step[5].n = 2, t->step[5].note[0] = 64, t->step[5].note[1] = 67;
    t->step[5].vel = 100;                               /* (64 from 4, 67 from 5 on: a carry step) */
    fm1_irq_on();
    check(!strcmp(sgl_notes(t), "3:60x1 4:64x3 5:67x2"), "stepedit: set: 64 sounds over 67's start");
    turn(EN_K1, 1);
    turn(EN_K2, 2);
    check(st.pk == 5u && !strcmp(sgl_notes(t), "4:64x3 5:60x1 5:67x2"), "stepedit: a note moved over notes sounding over each other: on, they stay");
    /* stop: one note of a chord into its own pitch was a wall (SAME NOTE) */
    se_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 3, 60, 1);
    t->step[3].n = 2, t->step[3].note[1] = 64;
    se_set(t, 5, 64, 1);
    fm1_irq_on();
    turn(EN_K1, 3);
    check(st.pk == 3u && st.nsel != NSEL_ALL && t->step[3].note[st.nsel] == 64u, "stepedit: a chord's upper note picked alone");
    turn(EN_K2, 2);
    check(st.pk == 6u && !strcmp(sgl_notes(t), "3:60x1 5:64x1 6:64x1"), "stepedit: ... moved onto its pitch: past it, the chord's other note and the other 64 stay");
    /* length: the lowest of a chord alone */
    se_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 3, 60, 1);
    t->step[3].n = 2, t->step[3].note[1] = 65;
    fm1_irq_on();
    turn(EN_K1, 2);
    check(st.pk == 3u && st.nsel != NSEL_ALL && t->step[3].note[st.nsel] == 60u, "stepedit: a chord's lowest note picked alone");
    turn(EN_K4, 1);
    check(!strcmp(sgl_notes(t), "3:60x2 3:65x1"), "stepedit: LENGTH on it: it alone longer, the other note as it was");
    turn(EN_K4, 1);
    check(!strcmp(sgl_notes(t), "3:60x3 3:65x1"), "stepedit: ... again");
    turn(EN_K4, -2);
    check(!strcmp(sgl_notes(t), "3:60x1 3:65x1"), "stepedit: ... and back");
    /* length: up to a note of the same pitch: it stays */
    se_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 2, 62, 2);
    se_set(t, 5, 62, 2);
    fm1_irq_on();
    turn(EN_K1, 1);
    turn(EN_K4, 3);
    check(!strcmp(sgl_notes(t), "2:62x3 5:62x2"), "stepedit: LENGTH up to a note of its pitch: the gap filled, the other note kept (was erased)");
    /* the fuzz */
    check(se_fuzz(2, 300) == 0u, "stepedit: random patterns, a note moved right to the end: every other note as it was");
    check(se_fuzz(4, 300) == 0u, "stepedit: random patterns, a note lengthened: every other note as it was");
    se_open(16);
    tap(B_HOME);
    reset_ui();
}
