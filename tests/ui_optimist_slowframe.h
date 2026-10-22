/* A slow frame (a heavy redraw, 350 ms and more) between a press and its release, or several presses in one frame: what the
 * buttons and keys do is judged by when they went down and up (the HAL's button times, hal/fm1_input.h fm1_btn_t; the keys'
 * queued times, seq.c lk_t), not by when the frame got to them (the user, 2026-10-22). A tap stays a tap, a hold a hold,
 * the presses of one frame run in the order they came, a step key before a page button is handled first, two tap-tempo taps
 * keep their interval, and a full key queue (a lost release) no longer leaves a step held. */
static void sf_audio(void)                              /* the audio between two frames: the sequencer queues the keys now */
{
    static int32_t o[CTL * 2];
    uint32_t q;
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
}
static void sf_down(uint32_t b) { fm1_btn_t[0][panel.btn[b]] = fm1_ticks(); edges_btn |= BT(b); fm1_in.buttons |= BT(b); }   /* (no frame) */
static void sf_up(uint32_t b) { fm1_btn_t[1][panel.btn[b]] = fm1_ticks(); fm1_in.buttons &= ~BT(b); }
static void sf_step_setup(void)
{
    reset_ui();
    song.sel = 0;
    memset(trk[0].step, 0, sizeof trk[0].step);
    op_enter(SCR_STEP);
    st.page = 0;
    st.follow = 0;
    frames(3);
}
static void slowframe_tests(void)
{
    uint32_t q, k, scr0, row0;
    /* 1. a button's tap or hold by its own times */
    reset_ui();
    op_enter(SCR_HOME);
    frames(2);
    press(B_SEQ);
    fm1_ms += 80;
    sf_up(B_SEQ);                                       /* (let go 80 ms after the press ...) */
    fm1_ms += 600;                                      /* (... the frame that sees it 600 ms later) */
    frames(2);
    check(ui.scr == SCR_STEP, "slow frame: SEQ tapped off STEP (let go after 80 ms, seen 600 ms later): STEP, not SONG");
    op_enter(SCR_HOME);
    frames(2);
    press(B_SEQ);
    fm1_ms += 700;
    sf_up(B_SEQ);
    fm1_ms += 50;
    frames(2);
    check(ui.scr == SCR_SONG, "... SEQ really held 700 ms (no frame between): still the hold, SONG");
    op_enter(SCR_HOME);
    frames(2);
    tap(B_SCL);                                         /* (a quick tap: where it goes; SCL holds a layer) */
    frames(2);
    scr0 = ui.scr, row0 = ui.row[ui.scr];
    op_enter(SCR_HOME);
    frames(2);
    press(B_SCL);
    fm1_ms += 60;
    sf_up(B_SCL);
    fm1_ms += 700;                                      /* (past LAY_TAP_MS since the press: was dropped as a look at the map) */
    frames(2);
    check(scr0 != SCR_HOME && ui.scr == scr0 && ui.row[ui.scr] == row0, "slow frame: SCL tapped, seen 700 ms later: its page, as a quick tap");
    /* 4. the presses of one frame in the order they came */
    reset_ui();
    op_enter(SCR_HOME);
    frames(2);
    lay.chord = 0;
    sf_down(B_SAVE);
    fm1_ms += 30;
    sf_down(B_HOME);
    fm1_ms += 400;
    frame();
    check(lay.chord == 1u, "slow frame: SAVE then HOME 30 ms apart, one frame: the undo (their order), not the redo");
    lay.chord = 0;                                      /* (no undo done when let go: the tests after keep their tracks) */
    sf_up(B_SAVE);
    sf_up(B_HOME);
    frames(3);
    sf_down(B_HOME);
    fm1_ms += 30;
    sf_down(B_SAVE);
    fm1_ms += 400;
    frame();
    check(lay.chord == 2u, "... HOME then SAVE, one frame: the redo");
    lay.chord = 0;
    sf_up(B_SAVE);
    sf_up(B_HOME);
    frames(3);
    /* 5. the key events before a press first, with the buttons down then */
    sf_step_setup();
    kdown(WK(2));                                       /* (held: the press seen) */
    fm1_ms += 50;
    fm1_in.notes &= ~(1u << WK(2));
    sf_audio();                                         /* (let go: queued 50 ms later) */
    fm1_ms += 50;
    sf_down(B_ENV);                                     /* (then a page button, the same frame) */
    fm1_ms += 500;
    frame();
    sf_up(B_ENV);
    frames(3);
    check(step_has(&trk[0], 2) && !st.held && ui.scr != SCR_STEP,
          "slow frame: a step tapped, then a page button: the step set, the button a jump (no page lock: the key came first)");
    sf_step_setup();
    fm1_in.notes |= 1u << WK(3);
    sf_audio();                                         /* (a step key down ...) */
    fm1_ms += 50;
    sf_down(B_OCTDN);                                   /* (... SHIFT 50 ms after it, the same frame) */
    fm1_ms += 400;
    frame();
    check(((st.held >> 3) & 1u) != 0u, "slow frame: a step key, then SHIFT: the step held (SHIFT was up at the key's time)");
    fm1_in.notes &= ~(1u << WK(3));
    frames(2);
    sf_up(B_OCTDN);
    frames(2);
    /* 9. a step key pressed with the cards down: its column sent, not the whole grid (the frame between press and release) */
    sf_step_setup();
    for (q = 0; q < 400u && ui.cards_t; q++)
        frame();
    frames(2);
    blit_px = 0;
    kdown(WK(2));
    frame();                                            /* (the press and the frame after it) */
    k = blit_px;
    q = (st.held >> 2) & 1u;
    kup(WK(2));
    check(q && k > 0u && k < 240u * SP_H / 4u, "STEP: a step key pressed (the cards down): its column sent, not the whole grid");
    /* 2. tap tempo by the keys' times */
    reset_ui();
    op_enter(SCR_HOME);
    frames(2);
    press(B_GLO);
    song.g[G_BPM] = 90;
    k = WK(15);
    for (q = 0; q < 2u; q++) {                          /* (two taps 0.5 s apart, one frame sees both) */
        fm1_in.notes |= 1u << k;
        sf_audio();
        fm1_ms += 30;
        fm1_in.notes &= ~(1u << k);
        sf_audio();
        fm1_ms += 470;
    }
    frame();
    check(song.g[G_BPM] >= 115 && song.g[G_BPM] <= 125, "slow frame: GLO + key 16 tapped twice 0.5 s apart, one frame: 120 BPM");
    release(B_GLO);
    frames(2);
    song.g[G_BPM] = 120;
    /* 7. a full key queue: the lost release mended */
    sf_step_setup();
    kdown(WK(1));
    for (q = 0; q < 20u; q++) {                         /* (20 events for a queue of 16: the last ones dropped) */
        fm1_in.notes ^= 1u << WK(5);
        sf_audio();
    }
    fm1_in.notes &= ~(1u << WK(1));
    sf_audio();                                         /* (WK(1)'s release: dropped too) */
    frames(3);
    check(!((st.held >> 1) & 1u) && !((st.fdown >> 1) & 1u) && !step_has(&trk[0], 1),
          "key queue full, a release lost: the step let go (mended from the keys down), no tap");
    reset_ui();
    op_enter(SCR_HOME);
    frames(2);
}
