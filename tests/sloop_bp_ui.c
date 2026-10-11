/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP fixes backported into the SLOOP UI (isod89/sloop-fm1), included by tests/ui_pages_test.c:
 *   #41 (2.5 fa9ce57)   chord mode: a key on the STEP page writes the whole chord as it sounds, not only its root;
 *                       MONO writes one note as before */
static void bp_step_page(void)
{
    song.sel = 0;
    go_home();
    frame();
    ui.page = (uint8_t)page_first(FAM_SEQ);
    ui.fam_last[FAM_SEQ] = ui.page;
    page_entered();
    frame();
}

static void bp_chord_entry_tests(void)
{
    track_t *t = &trk[0];
    int16_t chord = t->p[P_CHORD], voice = t->p[P_VOICE];
    uint32_t k = key_of_white(2), note, m, j, same;
    uint8_t c[4];
    step_t keep[NSTEP];
    memcpy(keep, t->step, sizeof keep);
    t->p[P_CHORD] = 1;                                     /* TRIAD */
    t->p[P_VOICE] = V_POLY;
    bp_step_page();
    check(song.seq_mode && cur_page()->scope == SC_STEP, "#41: the STEP page is up");
    cursor_set(4);
    memset(&t->step[4], 0, sizeof t->step[4]);
    note = kb_map(t, k);
    m = chord_notes(t, note, c);
    key(k);
    same = t->step[4].n == m && m == 3u;
    for (j = 0; same && j < m; j++)
        same = t->step[4].note[j] == c[j];
    check(same && t->step[4].time == ST_NOTE, "#41: chord mode, a key on STEP: the step holds the whole chord (3 notes)");
    check(ui.cursor == 5u, "#41: the key let go: the cursor moves on");
    t->p[P_VOICE] = V_MONO;
    memset(&t->step[5], 0, sizeof t->step[5]);
    key(k);
    check(t->step[5].n == 1u && t->step[5].note[0] == note, "#41: chord mode on a MONO track: the root alone, as before");
    t->p[P_CHORD] = 0;
    t->p[P_VOICE] = V_POLY;
    memset(&t->step[6], 0, sizeof t->step[6]);
    key(k);
    check(t->step[6].n == 1u && t->step[6].note[0] == kb_map(t, k), "#41: no chord mode: one key, one note");
    t->p[P_CHORD] = chord;
    t->p[P_VOICE] = voice;
    memcpy(t->step, keep, sizeof keep);
    go_home();
    frame();
}

static void sloop_bp_tests(void)
{
    bp_chord_entry_tests();
}
