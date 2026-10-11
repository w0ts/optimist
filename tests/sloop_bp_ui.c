/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP fixes backported into the SLOOP UI (isod89/sloop-fm1), included by tests/ui_pages_test.c:
 *   #41 (2.5 fa9ce57)   chord mode: a key on the STEP page writes the whole chord as it sounds, not only its root;
 *                       MONO writes one note as before
 *   #64 (2.5 fa9ce57)   the DRUMS grid's LEVEL dial runs ghost (left) .. hard (right)
 *   #33 / #107          SLOOP's SEQ convention, kept (the user's ruling): SONG is the SEQ family's last page, a tap of
 *                       SEQ walks into it, and SEQ from TRACKS comes back to it (the SAVE LED lit there) */
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

static void bp_level_dial_tests(void)
{
    check(lvl_dial(LV_GHOST) == 0 && lvl_dial(LV_SOFT) < lvl_dial(LV_NORM) && lvl_dial(LV_GHOST) < lvl_dial(LV_SOFT) &&
              lvl_dial(LV_NORM) < lvl_dial(LV_HARD) && lvl_dial(LV_HARD) == 999,
          "#64: the DRUMS LEVEL dial: ghost left, soft, norm, hard right");
}

static void bp_seq_song_tests(void)
{
    uint32_t i, song_pg = NPAGES;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].scope == SC_SONG)
            song_pg = i;
    check(song_pg < NPAGES && PAGES[song_pg].fam == FAM_SEQ, "#33: SONG is a page of the SEQ family (SLOOP's order)");
    bp_step_page();
    for (i = 0; i < 8u && cur_page()->scope != SC_SONG; i++)
        tap(B_SEQ);
    check(cur_page()->scope == SC_SONG, "#33: SEQ tapped again and again: it walks into SONG");
    check(cur_btn() == B_SAVE, "#33: on SONG the SAVE LED is lit (SLOOP)");
    go_home();
    frame();
    tap(B_SEQ);
    check(cur_page()->scope == SC_SONG, "#33: SEQ from TRACKS: back on SONG, the family's last page");
    ppm("bp-seq-song");
    tap(B_SEQ);
    check(cur_page()->scope == SC_STEP, "#33: SEQ on SONG: round to STEP");
    go_home();
    frame();
    tap(B_SEQ);
    check(cur_page()->scope == SC_STEP, "#33: SEQ from TRACKS after STEP: STEP");
    go_home();
    frame();
}

static void sloop_bp_tests(void)
{
    bp_chord_entry_tests();
    bp_level_dial_tests();
    bp_seq_song_tests();
}
