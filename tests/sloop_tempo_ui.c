/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP UI stream "tempo", included by ui_pages_test.c after hold_ui.c:
 *   the TEMPO page (ui_tempo.c): PLAY held opens it, a tap still plays / stops (on its release), OCT- / OCT+ nudge
 *   undo / redo on SAVE + HOME (besides EDIT + OCT-), neither doing its own work
 *   the layer knob gate (knob_gate.h): one detent of jitter neither uses the layer nor takes the tap, two net do */
static void oct_press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void play_taps_tests(void)
{
    int16_t oct = song.octave;
    song.playing = 0; transport_req = 0; go_home(); frames(3);
    press(B_PLAY); frames(3);
    check(transport_req == 0 && !tp.on, "PLAY pressed: no transport yet (a tap is decided on its release)");
    release(B_PLAY);
    check(transport_req == 1u && !tp.on, "PLAY tapped: play, on the release");
    frames(3);
    check(song.playing, "... and the transport runs");
    tap(B_PLAY);
    check(transport_req == 2u, "PLAY tapped again: stop");
    frames(3);
    check(!song.playing && !tp.on, "... and it stops");
    /* held: the page, no transport, none on the release */
    press(B_PLAY); frames(10);
    check(!tp.on && transport_req == 0, "PLAY held 170 ms: not yet");
    frames(14);
    check(tp.on && transport_req == 0, "PLAY held 350 ms+: the TEMPO page, no transport");
    ui.force = 1; frame(); ppm("tempo-page");
    release(B_PLAY);
    check(!tp.on && transport_req == 0 && !song.playing, "PLAY let go: the page closes, the release does not play");
    frames(2);
    check(!tp.shown && clk_nudge == 0, "... the screen before is back, no nudge left");
    /* a long hold while playing: stays playing */
    song.playing = 1; transport_req = 0;
    press(B_PLAY); frames(30); release(B_PLAY); frames(2);
    check(song.playing && transport_req == 0, "playing, PLAY held a second: still playing (the page's release is no stop)");
    song.playing = 0;
    /* the nudge */
    press(B_PLAY); frames(24);
    check(tp.on, "TEMPO up");
    oct_press(B_OCTUP);
    check(clk_nudge == 10, "TEMPO + OCT+ held: +3.9 % (10 / 256)");
    ui.force = 1; frame(); ppm("tempo-nudge");
    release(B_OCTUP);
    check(clk_nudge == 0 && tp.on, "OCT+ let go: back to the tempo, the page stays");
    oct_press(B_OCTDN);
    check(clk_nudge == -10, "TEMPO + OCT- held: -3.9 %");
    oct_press(B_OCTUP);
    check(clk_nudge == 0, "both held: no nudge");
    release(B_OCTUP); release(B_OCTDN);
    oct_press(B_OCTDN);
    release(B_PLAY);
    check(clk_nudge == 0 && !tp.on, "PLAY let go with OCT- still down: the nudge is gone with the page");
    release(B_OCTDN);
    check(song.octave == oct && transport_req == 0, "the OCT presses were the nudge's: the octave unchanged");
    /* PLAY + OCT at once: the page without waiting */
    song.playing = 0;
    press(B_PLAY); frames(2);
    oct_press(B_OCTUP);
    check(tp.on && clk_nudge == 10 && song.octave == oct, "PLAY, then OCT+ at once: the page and the nudge, no octave step");
    release(B_OCTUP); release(B_PLAY); frames(2);
    /* the knobs: BPM, SWING, SYNC; SELECT stays the BPM */
    {
        int16_t bpm = song.g[G_BPM], sw = song.g[G_SWING], sync = SYNC_USB;
        song.g[G_SYNC] = SYNC_USB;
        press(B_PLAY); frames(24);
        encs[panel.enc[EN_K1]] = 3; frame();
        check(song.g[G_BPM] > bpm, "TEMPO + KNOB 1: the BPM");
        bpm = song.g[G_BPM];
        encs[panel.enc[EN_SELECT]] = 2; frame();
        check(song.g[G_BPM] > bpm, "TEMPO + SELECT: the BPM, as everywhere");
        encs[panel.enc[EN_K2]] = 4; frame();
        check(song.g[G_SWING] > sw, "TEMPO + KNOB 2: the swing");
        encs[panel.enc[EN_K3]] = 1; frame();
        check(!FELUCCA_MIDI_CLOCK || song.g[G_SYNC] == SYNC_TRS, "TEMPO + KNOB 3: the sync source");
        encs[panel.enc[EN_K3]] = -5; frame(); encs[panel.enc[EN_K3]] = -5; frame();
        check(!FELUCCA_MIDI_CLOCK || song.g[G_SYNC] == SYNC_INT, "... and back down to INT, stopping there");
        encs[panel.enc[EN_K4]] = 3; frame();
        check(song.g[G_BPM] >= bpm, "TEMPO + KNOB 4: a read-out (nothing is edited)");
        ui.force = 1; frame(); ppm("tempo-knobs");
        {   /* HOME held long: no menu on the page; a layer button while it is up does not open its layer */
            press(B_HOME); frames(50);
            check(!ui.menu, "TEMPO: HOME held 800 ms opens no menu");
            release(B_HOME);
        }
        release(B_PLAY); frames(2);
        song.g[G_BPM] = 90; song.g[G_SWING] = sw; song.g[G_SYNC] = sync;
    }
    /* a layer button held first: PLAY there is just PLAY (no page) */
    press(B_FX); frames(3);
    press(B_PLAY); frames(30);
    check(!tp.on, "FX held, then PLAY held: no TEMPO page");
    release(B_PLAY);
    check(transport_req == 1u, "... PLAY plays on its release");
    transport_req = 0; frames(2); song.playing = 0;
    release(B_FX); frames(24);
    song.playing = 0; transport_req = 0;
    clk_pos = 0; clk_beat = 0;                        /* (the transport ran for a few frames: the clock as the tests after expect it) */
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

static void sloop_tempo_tests(void)
{
    /* the transport runs in here (a tap, a held PLAY): the model as the tests after expect it is put back at the end */
    static track_t trk_keep[NTRK];
    static song_t song_keep;
    uint32_t beat = clk_beat, pos = clk_pos;
    memcpy(trk_keep, trk, sizeof trk_keep);
    song_keep = song;
    play_taps_tests();
    undo_alias_tests();
    knob_gate_ui_tests();
    frames(2);
    memcpy(trk, trk_keep, sizeof trk_keep);
    song = song_keep;
    clk_beat = beat;
    clk_pos = pos;
    transport_req = 0;
    clk_nudge = 0;
}
