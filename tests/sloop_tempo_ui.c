/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP UI stream "tempo", included by ui_pages_test.c after hold_ui.c:
 *   the TEMPO page (ui_tempo.c): PLAY held opens it, a tap still plays / stops (on its release), OCT- / OCT+ nudge
 *   undo / redo on SAVE + HOME (besides EDIT + OCT-), neither doing its own work
 *   the layer knob gate (knob_gate.h): one detent of jitter neither uses the layer nor takes the tap, two net do */
static void oct_press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void sel_turn(int32_t n) { encs[panel.enc[EN_SELECT]] = n; frame(); }
static void play_taps_tests(void)
{
    int16_t oct = song.octave, bpm, sw, sync;
    song.playing = 0; transport_req = 0; go_home(); frames(3);
    /* PLAY acts on the press, instantly, as ever */
    press(B_PLAY);
    check(transport_req == 1u && !tp.on, "PLAY pressed: play at once (no wait for the release), no page");
    frames(30);
    release(B_PLAY);
    check(!tp.on && song.playing, "PLAY held a second: nothing opens (the page is SELECT's)");
    tap(B_PLAY); frames(3);
    check(!song.playing, "PLAY again: stop");
    /* SELECT opens it */
    bpm = song.g[G_BPM];
    if (FELUCCA_BPM_LOCK) {                             /* (SELECT is the tempo only with GLO held: no page) */
        sel_turn(2);
        check(!tp.on && song.g[G_BPM] == bpm, "BPM LOCK: SELECT on TRACKS: locked, no TEMPO page");
        return;
    }
    sel_turn(2);
    check(tp.on && song.g[G_BPM] > bpm, "SELECT on TRACKS: the BPM changes and the TEMPO page shows");
    ui.force = 1; frame(); ppm("tempo-page");
    /* KNOB 1..4 */
    bpm = song.g[G_BPM]; sw = song.g[G_SWING];
    song.g[G_SYNC] = SYNC_USB; sync = SYNC_USB;
    encs[panel.enc[EN_K1]] = 3; frame();
    check(song.g[G_BPM] > bpm, "TEMPO + KNOB 1: the BPM");
    encs[panel.enc[EN_K2]] = 4; frame();
    check(song.g[G_SWING] > sw, "TEMPO + KNOB 2: the swing");
    encs[panel.enc[EN_K3]] = 1; frame();
    check(!FELUCCA_MIDI_CLOCK || song.g[G_SYNC] == SYNC_TRS, "TEMPO + KNOB 3: the sync source");
    encs[panel.enc[EN_K3]] = -1; frame(); encs[panel.enc[EN_K3]] = -1; frame(); encs[panel.enc[EN_K3]] = -1; frame();
    check(!FELUCCA_MIDI_CLOCK || song.g[G_SYNC] == SYNC_INT, "... and down to INT, stopping there");
    bpm = song.g[G_BPM];
    encs[panel.enc[EN_K4]] = 3; frame();
    check(song.g[G_BPM] == bpm && tp.on, "TEMPO + KNOB 4: a read-out, nothing edited");
    song.g[G_SYNC] = sync; song.g[G_SWING] = sw;
    /* the nudge */
    oct_press(B_OCTUP);
    check(clk_nudge == 10 && song.octave == oct, "OCT+ held on the page: +3.9 %, no octave step");
    ui.force = 1; frame(); ppm("tempo-nudge");
    release(B_OCTUP);
    check(clk_nudge == 0 && tp.on, "OCT+ let go: back to the tempo, the page stays");
    oct_press(B_OCTDN);
    check(clk_nudge == -10, "OCT- held: -3.9 %");
    oct_press(B_OCTUP);
    check(clk_nudge == 0, "both held: no nudge");
    release(B_OCTUP); release(B_OCTDN);
    check(song.octave == oct, "(the octave unchanged)");
    /* the timeout: ~3 s after the last SELECT / KNOB / OCT */
    frames(150);
    check(tp.on, "2.4 s idle: still up");
    oct_press(B_OCTUP); release(B_OCTUP);
    frames(150);
    check(tp.on, "an OCT press restarts the time");
    frames(60);
    check(!tp.on && clk_nudge == 0, "3 s after the last activity: the page closes");
    frames(2);
    check(!tp.shown, "... and the screen is the one before");
    /* another button closes it, and still acts */
    sel_turn(1);
    check(tp.on, "SELECT again: the page");
    press(B_PLAY);
    check(!tp.on && transport_req == 1u, "PLAY pressed with the page up: it closes, and PLAY plays");
    release(B_PLAY); frames(3); tap(B_PLAY); frames(3);
    sel_turn(1);
    press(B_EDIT);
    check(!tp.on && ui.layer == LY_ERASE || !tp.on, "EDIT pressed: the page closes");
    release(B_EDIT); frames(2);
    go_home(); frames(2);
    sel_turn(1);
    tap(B_ENV); frames(2);
    check(!tp.on && cur_fam() == FAM_ENV, "ENV tapped with the page up: it closes and the ENV pages open");
    go_home(); frames(2);
    /* a key does not close it */
    sel_turn(1);
    key(4);
    check(tp.on, "a key played: the page stays");
    frames(200);
    /* a layer held: SELECT is the layer's tempo, no page */
    press(B_FX); frames(3);
    sel_turn(1);
    check(!tp.on, "FX held + SELECT: the tempo as ever, no page");
    release(B_FX); frames(20);
    /* pages where SELECT pages: unchanged */
    open_family(FAM_ENV); frames(2);
    sel_turn(1);
    check(!tp.on, "SELECT on an ENV page: the next page, no TEMPO");
    go_home(); frames(2);
    /* BPM stays within the range */
    song.g[G_BPM] = 90; song.octave = oct;
}

static void undo_alias_tests(void)
{
    static track_t keep;
    uint32_t sv = saves;
    keep = *TDRUM;                                    /* (the drum track as the tests before left it: put back at the end) */
    song.sel = TRK_DRUM; go_home(); frame();
    track_defaults_steps(TDRUM);                      /* (no steps from the tests before) */
    key(4);                                           /* the snare: the SEQ layer's sound */
    press(B_SEQ); frames(10); key(0); release(B_SEQ); frames(2);
    check(dstep_has(&TDRUM->dstep[0], 2), "(setup: a snare step 1)");
    open_family(FAM_ENV);                             /* a page that HOME's tap would leave (go_home: TRACKS) */
    frames(2);
    /* SAVE pressed, then HOME while SAVE is held: undo */
    press(B_SAVE); frames(3);
    press(B_HOME); frames(2);
    check(!dstep_has(&TDRUM->dstep[0], 2), "SAVE, then HOME (SAVE held): undo");
    check(!strncmp(ui.msg, "UNDO", 4), "... says UNDO");
    frames(50);                                       /* HOME held past 700 ms: no menu */
    check(!ui.menu && ly_lock == LY_PLAY, "... HOME held on: no menu, no lock of the SAVE layer");
    release(B_HOME); frames(2);
    check(cur_fam() == FAM_ENV && !ui.menu, "... HOME let go: no HOME tap (the page stays)");
    release(B_SAVE); frames(2);
    check(cur_fam() == FAM_ENV && ui.layer == LY_PLAY && saves == sv && ly_lock == LY_PLAY, "... SAVE let go: no tap (no SAVE page, no save, no lock)");
    /* HOME pressed, then SAVE while HOME is held: redo */
    press(B_HOME); frames(3);
    press(B_SAVE); frames(2);
    check(dstep_has(&TDRUM->dstep[0], 2), "HOME, then SAVE (HOME held): redo");
    check(!strncmp(ui.msg, "REDO", 4) && ui.layer == LY_PLAY, "... says REDO, and the SAVE map does not flash up");
    release(B_SAVE); frames(2);
    check(cur_fam() == FAM_ENV && saves == sv, "... SAVE let go: no tap");
    frames(50);
    check(!ui.menu, "... HOME held on: no menu");
    release(B_HOME); frames(2);
    check(cur_fam() == FAM_ENV && !ui.menu && ui.layer == LY_PLAY, "... HOME let go: no HOME tap");
    /* nothing to redo: the message, and still no side effect */
    press(B_HOME); frames(2); press(B_SAVE); frames(2);
    check(!strcmp(ui.msg, "NOTHING TO REDO") && cur_fam() == FAM_ENV, "HOME, SAVE with nothing to redo: says so");
    release(B_SAVE); release(B_HOME); frames(2);
    /* the others: SAVE alone is still the song / SAVE pages, HOME alone still HOME, EDIT + OCT still undoes */
    tap(B_HOME); frames(2);
    check(cur_page()->scope == SC_TRK, "HOME tapped alone: TRACKS as ever");
    tap(B_SAVE); frames(2);
    check(on_song_page(), "SAVE tapped alone: the song page as ever");
    go_home(); frames(2);
    key(4);
    press(B_SEQ); frames(10); key(7); release(B_SEQ); frames(2);
    check(dstep_has(&TDRUM->dstep[4], 2), "(setup: a snare step 2)");
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(!dstep_has(&TDRUM->dstep[4], 2), "EDIT + OCT- still undoes");
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    release(B_EDIT); frames(2);
    check(dstep_has(&TDRUM->dstep[4], 2), "EDIT + OCT+ still redoes");
    /* HOME + SAVE in the same frame: not a chord (nothing undone) */
    press(B_SEQ); frames(10); key(14); release(B_SEQ); frames(2);
    edges_btn |= BT(B_SAVE) | BT(B_HOME); fm1_in.buttons |= BT(B_SAVE) | BT(B_HOME); frame();
    check(dstep_has(&TDRUM->dstep[8], 2), "SAVE and HOME in one frame: no undo");
    fm1_in.buttons &= ~(BT(B_SAVE) | BT(B_HOME)); frames(3);
    go_home(); frames(2);
    *TDRUM = keep;
    undo_clear();
}

static void knob_gate_ui_tests(void)
{
    int16_t filt;
    /* a jitter detent while FX is held: no map, nothing moves, the tap stays */
    go_home(); frames(2);
    filt = song.g[G_FILT];
    press(B_FX); frames(3);
    encs[panel.enc[EN_K1]] = 1; frame();
    check(ui.layer == LY_PLAY && song.g[G_FILT] == filt, "FX held + one detent: no map, nothing moves");
    release(B_FX); frames(2);
    check(cur_fam() == FAM_FX, "... the release is still a tap (the FX pages)");
    go_home(); frames(2);
    /* +1 then -1 (a knob that jitters both ways) */
    press(B_ARP); frames(2);
    encs[panel.enc[EN_K2]] = 1; frame(); encs[panel.enc[EN_K2]] = -1; frame();
    encs[panel.enc[EN_K2]] = 1; frame(); encs[panel.enc[EN_K2]] = -1; frame();
    release(B_ARP); frames(2);
    check(cur_fam() == FAM_ARP, "ARP held + jitter both ways: the release is a tap");
    go_home(); frames(2);
    /* two detents, either way: used, the map shows, no tap */
    press(B_FX); frames(3);
    encs[panel.enc[EN_K1]] = -1; frame();
    check(ui.layer == LY_PLAY, "FX held + one detent down: held back");
    encs[panel.enc[EN_K1]] = -1; frame();
    check(ui.layer == LY_FX && song.g[G_FILT] < filt, "... a second the same way: the map shows, the filter moved");
    release(B_FX); frames(2);
    check(cur_fam() != FAM_FX, "... and the release is no tap");
    song.g[G_FILT] = filt;
    go_home(); frames(2);
    /* a key counts at once */
    press(B_FX); frames(2);
    fm1_in.notes = 1u << 4; frame();
    check(ui.layer == LY_FX, "FX held + a key: the map at once (key edges count immediately)");
    fm1_in.notes = 0; frame();
    release(B_FX); frames(20);
    go_home(); frames(2);
    /* the position counts from each press: a held-back detent does not carry into the next hold */
    press(B_ARP); frames(2);
    encs[panel.enc[EN_K2]] = 1; frame();
    release(B_ARP); frames(2);
    go_home(); frames(30);
    press(B_ARP); frames(2);
    encs[panel.enc[EN_K2]] = 1; frame();
    check(ui.layer == LY_PLAY, "one detent per hold, twice: still jitter (the count restarts at each press)");
    release(B_ARP); frames(2);
    go_home(); frames(30);
#if FELUCCA_LAYER_QUIET
    {   /* the release frame: one detent is jitter (the tap stays, the detent is dropped), two are a combo */
        int16_t mode0 = TSEL->p[P_AMODE];
        press(B_ARP); frames(2);
        encs[panel.enc[EN_K1]] = 1;
        release(B_ARP); frames(3);
        check(cur_fam() == FAM_ARP && TSEL->p[P_AMODE] == mode0, "ARP let go with one detent in that frame: a tap, the detent dropped");
        go_home(); frames(30);
        press(B_ARP); frames(2);
        encs[panel.enc[EN_K1]] = -2;
        release(B_ARP); frames(3);
        check(cur_fam() != FAM_ARP && TSEL->p[P_AMODE] == mode0, "ARP let go with two detents in that frame: a combo, no tap");
        go_home(); frames(30);
        press(B_ARP); frames(2);
        encs[panel.enc[EN_K1]] = 1; frame();
        encs[panel.enc[EN_K1]] = 1;
        release(B_ARP); frames(3);
        check(cur_fam() != FAM_ARP, "ARP: one detent held, one in the release frame: two net, a combo");
        go_home(); frames(30);
    }
#endif
}

static void home_tests(void)
{
    int16_t b;
    go_home(); frames(40);
    ui.menu = 0;
    /* a tap acts on its release, no waiting */
    open_family(FAM_ENV); frames(2);
    press(B_HOME); frames(2);
    check(cur_fam() == FAM_ENV, "HOME pressed: nothing yet");
    release(B_HOME);
    check(cur_fam() == FAM_TRK && !ui.menu, "HOME tapped: acts on the release (go home), no menu");
    frames(30);                                       /* (a slow second tap: more than 300 ms after) */
    tap(B_HOME); frames(2);
    check(!ui.menu, "a second tap 300 ms+ later: no menu");
    frames(30);
#if FELUCCA_SCOPE
    scope_on = 0;
#endif
#if FELUCCA_VIS
    vis_on = 0;
#endif
    /* tap + tap: the menu */
    go_home(); frames(30);
    tap(B_HOME);
    press(B_HOME);
    check(ui.menu == 1, "HOME tap + tap: the SYSTEM menu opens on the second press");
    release(B_HOME); frames(2);
    check(ui.menu == 1, "... and its release does nothing");
    ui.menu = 0; ui.force = 1; frames(2);
    /* the menu closes by a double tap and by OCT- as before */
    test_open_menu();
    check(ui.menu == 1, "test_open_menu(): the menu is open");
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(!ui.menu, "OCT- in the menu: it closes");
    test_open_menu();
    tap(B_HOME); press(B_HOME); release(B_HOME); frames(2);
    check(!ui.menu, "HOME double tap with the menu open: it closes");
    go_home(); frames(30);
    /* a hold alone does nothing: no menu, no tap on its release */
    open_family(FAM_ENV); frames(2);
    press(B_HOME); frames(60);
    check(home_shift && !ui.menu, "HOME held: shift, no menu");
    release(B_HOME); frames(2);
    check(!ui.menu && cur_fam() == FAM_ENV, "... released with nothing else: no menu, no tap");
    frames(30);
    tap(B_HOME); frames(2);
    check(!ui.menu, "... and a tap after a hold is not a double");
    go_home(); frames(30);
    /* HOME + a layer: the lock, both for a tap and for a held HOME */
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    check(ly_lock == LY_FX || ui.layer == LY_FX, "FX held + HOME tap: the layer locks");
    frames(40); press(B_PLAY); release(B_PLAY); frames(3); ly_lock = LY_PLAY; ui.layer = LY_PLAY; frames(30);
    press(B_FX); frames(3); press(B_HOME); frames(30); release(B_HOME); release(B_FX); frames(3);
    check(ly_lock == LY_FX, "FX held + HOME held (shift), then let go: the layer locks");
    ly_lock = LY_PLAY; ui.layer = LY_PLAY; frames(40);
    /* HOME then SAVE: redo still */
    {
        uint32_t sv = saves;
        press(B_HOME); frames(3); press(B_SAVE); frames(2);
        check(ui.msg[0] == 'R' || ui.msg[0] == 'N', "HOME then SAVE: redo (its message)");
        release(B_SAVE); release(B_HOME); frames(2);
        check(!ui.menu && saves == sv && ly_lock == LY_PLAY, "... nothing else");
    }
    b = song.g[G_BPM]; (void)b;
    ui.menu = 0; go_home(); frames(40);
}

static void scope_tests(void)
{
#if FELUCCA_SCOPE
    int16_t sw = song.g[G_SWING];
    go_home(); frames(3);
    frames(20); tap(B_HOME); frames(3);
    check(scope_on && scope_shown(), "HOME tapped on TRACKS: the scope screen");
    ui.force = 1; frame(); ppm("scope");
    encs[panel.enc[EN_K1]] = 3; frames(2);
    check(song.g[G_SWING] == sw, "scope: KNOB 1..4 edit nothing");
    frames(20); tap(B_HOME); frames(3);
    check(!scope_on && !scope_shown() && cur_page()->scope == SC_TRK, "HOME again: back to TRACKS");
    frames(20); tap(B_HOME); frames(3);
    open_family(FAM_ENV); frames(3);
    check(!scope_shown(), "a page opened: the scope is gone");
    go_home(); frames(3);
#endif
}

static void sloop_tempo_tests(void)
{
    /* the transport runs in here (a tap, a held PLAY): the model as the tests after expect it is put back at the end */
    static track_t trk_keep[NTRK];
    static song_t song_keep;
    uint32_t beat = clk_beat, pos = clk_pos;
    uint8_t fam_keep[FAM_COUNT], page_keep = ui.page;
    memcpy(fam_keep, ui.fam_last, sizeof fam_keep);
    memcpy(trk_keep, trk, sizeof trk_keep);
    song_keep = song;
    play_taps_tests();
    undo_alias_tests();
    knob_gate_ui_tests();
    scope_tests();
    home_tests();
    frames(2);
    memcpy(trk, trk_keep, sizeof trk_keep);
    song = song_keep;
    clk_beat = beat;
    clk_pos = pos;
    transport_req = 0;
    clk_nudge = 0;
    memcpy(ui.fam_last, fam_keep, sizeof fam_keep);
    ui.page = page_keep;
    tp.on = 0;
}
