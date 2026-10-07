/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM STEP (FELUCCA_DRUM_STEP, ui_drumstep.c; behaviour of SLOOP 2.4 "Drums with the keys", isod89, GPL-3.0),
 * included by tests/ui_pages_test.c: the DRUMS grid page's keys (white = the 16 steps of KNOB 1's sound, first
 * four black = the page, heard), SELECT, the knobs heard, the SEQ layer's sound heard, FOLLOW, and the screens
 * (DIR/ds-*.ppm). */
#if FELUCCA_DRUM_STEP

static void ds_clear_drums(uint32_t len)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        memset(&TDRUM->dstep[i], 0, sizeof(dstep_t));
    TDRUM->p[P_SLEN] = (int16_t)len;
    TDRUM->seq_idx = 0;
}
static uint32_t ds_count(uint32_t lane, uint32_t from, uint32_t to)
{
    uint32_t i, n = 0;
    for (i = from; i < to; i++)
        n += dstep_has(&TDRUM->dstep[i], lane) != 0;
    return n;
}
static void ds_open_grid(void)
{
    song.sel = TRK_DRUM; song.playing = 0; transport_req = 0; go_home(); frames(2);
    tap(B_SEQ); frames(2);
    drum_page = 0; ui.force = 1; frames(2);
}

static void drum_step_tests(void)
{
    uint32_t w, ok;
    /* the pure mapping */
    check(ds_pages(1) == 1 && ds_pages(16) == 1 && ds_pages(17) == 2 && ds_pages(64) == 4, "DRUM STEP: pages of 16 cover the pattern (1, 16, 17, 64)");
    check(ds_follow_page(0, 64) == 0 && ds_follow_page(15, 64) == 0 && ds_follow_page(16, 64) == 1 && ds_follow_page(63, 64) == 3 &&
          ds_follow_page(40, 20) == 1, "DRUM STEP: the page of the playhead");
    check(ds_page_key(1, 64) == 0 && ds_page_key(3, 64) == 1 && ds_page_key(5, 64) == 2 && ds_page_key(8, 64) == 3 &&
          ds_page_key(8, 49) == 3 && ds_page_key(8, 48) == -1 && ds_page_key(0, 64) == -1 && ds_page_key(10, 64) == -1,
          "DRUM STEP: the first four black keys open pages 1..4, not past the length, no other key");
    check(ds_step_of(0, 15, 64) == 15 && ds_step_of(3, 15, 64) == 63 && ds_step_of(1, 5, 20) == -1 && ds_step_of(1, 3, 20) == 19,
          "DRUM STEP: white key w of page p = step 16 p + w, none beyond the length");

    /* the DRUMS grid page: the keys are steps */
    ds_clear_drums(64);
    ds_open_grid();
    check(on_drum_page() && !drum_page && grid_keys_on(), "DRUMS grid page: its keys are steps");
    drum_lane = 2; drum_cursor = 0; ui.force = 1; frames(2);
    drums.hits = 0;
    for (w = 0, ok = 1; w < 16u; w += 3u) {
        key(key_of_white(w));
        ok &= dstep_has(&TDRUM->dstep[w], 2) != 0 && drum_cursor == w;
    }
    check(ok && ds_count(2, 0, 16) == 6 && ds_count(2, 16, 64) == 0, "white keys 1, 4, 7 ..: steps of the snare (KNOB 1's sound), page 1, the cursor follows");
    check(kb_kind[key_of_white(0)] == KS_NONE && ds_count(0, 0, 64) == 0, "a step key plays no pad and sets no other sound");
    key(key_of_white(3));
    check(!dstep_has(&TDRUM->dstep[3], 2) && dstep_has(&TDRUM->dstep[0], 2), "a set step pressed again: cleared");
    {   /* the sound heard when a key sets a step */
        aud_lanes = 0;
        fm1_in.notes = 1u << key_of_white(10); frame();
        check(dstep_has(&TDRUM->dstep[10], 2) && aud_lanes == (1u << 2), "a key sets a step: the sound is heard (audition)");
        fm1_in.notes = 0; frames(2);
        aud_lanes = 0;
        fm1_in.notes = 1u << key_of_white(10); frame();
        check(!dstep_has(&TDRUM->dstep[10], 2) && aud_lanes == 0, "a key clears a step: silent");
        fm1_in.notes = 0; frames(2);
    }
    key(3);                                                     /* black key 2: page 2 */
    check(drum_cursor / 16u == 1, "black key 2: page 2");
    key(key_of_white(0)); key(key_of_white(15));
    check(dstep_has(&TDRUM->dstep[16], 2) && dstep_has(&TDRUM->dstep[31], 2), "page 2: white keys 1 / 16 are steps 17 / 32");
    key(8); key(key_of_white(15));
    check(drum_cursor / 16u == 3 && dstep_has(&TDRUM->dstep[63], 2), "black key 4, white key 16: step 64");
    TDRUM->p[P_SLEN] = 20; drum_cursor = 0; frames(2);
    key(8); key(5);
    check(drum_cursor / 16u == 0, "length 20: the page 3 / 4 keys open nothing");
    key(3); key(key_of_white(4)); key(key_of_white(5));
    check(!dstep_has(&TDRUM->dstep[20], 2) && !dstep_has(&TDRUM->dstep[21], 2), "length 20, page 2: white keys 5.. set nothing");
    TDRUM->p[P_SLEN] = 64; drum_cursor = 0; ui.force = 1; frames(2);

    /* the knobs, heard */
    aud_lanes = 0;
    encs[panel.enc[EN_K1]] = 2; frame();
    check(drum_lane == 4 && aud_lanes == (1u << 4), "KNOB 1: the sound, heard");
    encs[panel.enc[EN_K1]] = 0; aud_lanes = 0;
    dstep_set(&TDRUM->dstep[1], 0, LV_HARD, 0); dstep_set(&TDRUM->dstep[1], 5, LV_NORM, 0);
    drum_cursor = 0; encs[panel.enc[EN_K2]] = 1; frame();
    check(drum_cursor == 1 && aud_lanes == ((1u << 0) | (1u << 5)), "KNOB 2: the step, the sounds it holds heard");
    aud_lanes = 0;

    /* SELECT: grid <-> kit */
    encs[panel.enc[EN_SELECT]] = 1; frames(2);
    check(drum_page == 1 && !grid_keys_on(), "SELECT right: the kit page (its keys play the pads)");
    key(key_of_white(0));
    check(kb_kind[key_of_white(0)] == KS_NONE && ds_count(4, 0, 64) == 0, "kit page: a key sets no step");
    encs[panel.enc[EN_SELECT]] = -1; frames(2);
    check(drum_page == 0, "SELECT left: the grid again");

    /* FOLLOW on the grid page */
    ds_clear_drums(64);
    drum_cursor = 5; song.playing = 1; TDRUM->seq_idx = 37; ui.step_follow = 1; frames(3);
    check(drum_cursor / 16u == ds_follow_page(TDRUM->seq_idx, 64) && drum_cursor % 16u == 5, "playing: the page follows the playhead, in the same column");
    key(1);
    check(drum_cursor / 16u == 0 && !ui.step_follow, "a page key while playing: that page, follow off");
    frames(3);
    check(drum_cursor / 16u == 0, "follow off: the page stays");
    key(DS_FOLLOW_KEY);
    check(ui.step_follow, "black key 5: follow on");
    key(DS_FOLLOW_KEY);
    check(!ui.step_follow, "black key 5 again: off");
    song.playing = 0; frames(3);
    check(ui.step_follow, "stopped: follow armed for the next start");

    /* the SEQ layer (held on the grid page): unchanged steps, the sound heard */
    ds_clear_drums(64);
    song.sel = TRK_DRUM; go_home(); frames(2);
    pen_lane = 0;
    press(B_SEQ); frames(10);
    aud_lanes = 0;
    encs[panel.enc[EN_K1]] = 3; frame();
    check(pen_lane == 3 && aud_lanes == (1u << 3), "SEQ layer + KNOB 1: the sound, heard");
    key(key_of_white(2));
    check(dstep_has(&TDRUM->dstep[2], 3), "SEQ + white key: the step of that sound");
    fm1_in.notes = 1u << key_of_white(2); frame();
    encs[panel.enc[EN_K2]] = 1; frame(); encs[panel.enc[EN_K3]] = 2; frame();
    fm1_in.notes = 0; frame();
    check(dstep_lvl(&TDRUM->dstep[2], 3) == LV_HARD && dstep_rat(&TDRUM->dstep[2], 3) == 2u, "SEQ + step + KNOB 2 / 3: level / ratchet (as 2.4)");
    key(3);
    check(ui.step_page == 1, "SEQ + black key 2: page 2");
    song.playing = 1; TDRUM->seq_idx = 50; ui.step_follow = 1; frames(3);
    check(ui.step_page == 3, "SEQ layer, playing: the page follows the playhead");
    key(1); frames(3);
    check(ui.step_page == 0 && !ui.step_follow, "SEQ layer: a page key turns follow off");
    release(B_SEQ);
    song.playing = 0; frames(2);

    /* a synth track keeps everything */
    song.sel = 0; go_home(); frames(2);
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
    press(B_SEQ); frames(6);
    key(key_of_white(1));
    check(step_on(&trk[0].step[1]), "a synth track: SEQ + key sets its step as before");
    release(B_SEQ);
    steps_clear(&trk[0]);

    /* the screens, on a groove of 64 steps (DIR/ds-*.ppm) */
    {
        uint32_t i;
        ds_clear_drums(64);
        for (i = 0; i < 64u; i++) {
            dstep_t *d = &TDRUM->dstep[i];
            if (i % 4u == 0u) dstep_set(d, 0, i % 16u ? LV_NORM : LV_HARD, 0);
            if (i % 8u == 4u) dstep_set(d, 2, LV_NORM, i % 16u == 12u ? 2u : 0u);
            if (i % 2u == 0u) dstep_set(d, 4, i % 4u ? LV_SOFT : LV_NORM, 0);
            if (i % 16u == 14u) dstep_set(d, 5, LV_NORM, 0);
            if (i % 16u == 10u) dstep_set(d, 3, LV_GHOST, 0);
        }
        ds_open_grid();
        drum_lane = 2; drum_cursor = 4; ui.force = 1; frames(2); ppm("ds-1-grid-page1");
        key(3); ui.force = 1; frames(2); ppm("ds-2-grid-page2");
        song.playing = 1; TDRUM->seq_idx = 37; ui.step_follow = 1; frames(4); ui.force = 1; frames(2); ppm("ds-3-grid-follow-playing");
        song.playing = 0; frames(2);
        TDRUM->p[P_SLEN] = 24; drum_cursor = 18; ui.force = 1; frames(2); ppm("ds-4-grid-length-24-page2");
        TDRUM->p[P_SLEN] = 64;
        go_home(); frames(2);
        pen_lane = 2; ui.step_page = 0;
        press(B_SEQ); frames(10); ui.force = 1; frames(2); ppm("ds-5-seq-layer");
        release(B_SEQ);
        ds_clear_drums(16);
        drum_cursor = 0;
    }
    go_home(); frames(2);
}
#else
static void drum_step_tests(void) {}
#endif
