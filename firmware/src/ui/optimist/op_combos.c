/* SPDX-License-Identifier: GPL-3.0-only */
/* The held buttons with a button (docs/UI-OPTIMIST-DESIGN.md sections 2.1, 2.1.1): SAVE held + a button saves what it
 * owns, SHIFT (OCT- held: off, less) + a button clears it, SHIFT2 (OCT+ held: on, more) + a layer's button locks the
 * layer open (in either order):
 *   SAVE + SEQ    the working pattern into its slot (asked over a used one)
 *   SAVE + ENV LFO EDIT FX SCL ARP   the sound as a user preset (the first free slot); the drum track: its 10
 *                 lanes as a user kit                                         SHIFT + ENV: INIT (asked)
 *                                                                             SHIFT2 + FX EDIT ARP SCL GLO LFO (ENV on
 *                                                                             FM6): the layer locked open
 *   SAVE + REC    tapped: SONG REC; held + a scene key: the loop into it       SHIFT + REC   held, the ring closed: the
 *                                                                             track's motion and locks (a lane picked:
 *                                                                             the lane's; the notes stay); a SONG scene
 *                                                                             row: the scene cleared (asked)
 *   SAVE + PLAY   the song from its start                                     SHIFT + PLAY  stop, every voice off
 *   SAVE then HOME: undo                                                      HOME then SAVE: redo
 *                                                                             both OCT: track <-> MASTER (op_walk.c)
 * The undo and redo act when one of the two is let go (a key or a knob meanwhile: nothing). */
#if DL_UI && FELUCCA_DRUM_KITS
#define OP_UKITS 16u                    /* drum_kits.c UK_N */
#endif
static void op_undo(int redo);                          /* op_input.c */
static void rec_motion_ring(void);                      /* op_input.c: SHIFT + REC, the motion ring */
static void op_play(void);
static void name_user_free(void);                       /* op_name.c */

static void save_sound(void)                            /* the first free user preset slot, named first (NAME) */
{
    if (is_drum(TSEL)) {
#if DL_UI && FELUCCA_DRUM_KITS
        uint32_t i;
        for (i = 0; i < OP_UKITS && ukit_used(i); i++)
            ;
        if (i == OP_UKITS)
            ui_message("USER KITS FULL");
        else
            ukit_ui(0, i);                              /* (the 16 lanes as a user kit: drum_kits.c says SAVED) */
#else
        ui_message("NO USER KITS");
#endif
        return;
    }
    name_user_free();                                   /* (NAME first: op_name.c) */
}
static void save_pattern(void)                          /* SAVE + SEQ: the working copy into its source slot */
{
#if FELUCCA_PATTERNS
    uint32_t t = song.sel % NTRK, s = pat_cur[t] < PAT_N ? pat_cur[t] : pat_free(t);
    if (s >= PAT_N)
        ui_message("NO FREE PATTERN");
    else
        pat_store_ask(t, s);
#else
    ui_message("NO PATTERN SLOTS");
#endif
}
static void op_panic(void)                              /* SHIFT + PLAY: stop, every voice off (CC 120's effect) */
{
    uint32_t i;
    transport_req = 2;
    fm1_irq_off();
    for (i = 0; i < NTRK; i++)
        midi_silence_track(i);
    fm1_irq_on();
    ui_message("ALL SOUND OFF");
}
/* the track k's motion and locks (lane >= 0: that drum lane's), one undo level; the notes, their levels and recorded
 * lengths stay (SHIFT + REC's ring, op_input.c rec_held) */
static void op_clear_motion(uint32_t k, int32_t lane)
{
    track_t *t = &trk[k % NTRK];
#if FELUCCA_AUTO
    undo_mark(t, (undo_sess += 4u) | 3u);               /* (an undo level: the automation comes back, seq/undo.c) */
    fm1_irq_off();
    (void)auto_clear_where(t, -1, lane);                /* (motion and locks; the whole track: nudges, fills, chance too) */
    fm1_irq_on();
#endif
    sync_reload = 1;
    ui_say(lane >= 0 ? lane_name((uint32_t)lane) : trk_tag(k), " LOCKS CLEARED");
    (void)t;
}
/* the confirmed actions of this file (op_layers.c op_act) */
static void op_act_more(uint32_t a, uint32_t k)
{
    if (a == OA_INIT) {
        if (k == song.sel)
            op_global_go(G_INITSND);                    /* (the engine's defaults: the edits go) */
        return;
    }
    if (a == OA_DISCARD)
        ed_run(k);                                      /* "Discard T2 edits?" YES: the change asked for (op_edits.c) */
}

static uint32_t op_combo_bit(uint32_t b) { return 1u << panel.btn[b]; }
static uint32_t op_clean;                               /* (op_input.c: the buttons with nothing else done since) */
static void op_eat(uint32_t mask) { op_clean &= ~mask; }

/* SHIFT (OCT- held) + button b (pressed now): 1 taken */
#if FELUCCA_BLE
static int dev_home_combo(void);                        /* op_project.c: SHIFT + a button on the BLE devices: FORGET LAST */
#endif
static int info_clear(void);                            /* op_global.c: SHIFT + YES on SYSTEM's INFO page clears DROPS */
static int shift_combo(uint32_t b)
{
    uint32_t l = lay_btn_layer(b);
#if FELUCCA_BLE
    if (dev_home_combo())
        return 1;
#endif
    if (l != LY_PLAY && b != B_ENV)
        return 1;                                       /* (a layer's button: nothing; SHIFT2 locks it) */
    switch (b) {
    case B_SAVE:
        return info_clear();                            /* (SYSTEM's INFO page: DROPS cleared, op_global.c; elsewhere nothing) */
    case B_ENV:
        if (!is_drum(TSEL) && snd_edited(song.sel))      /* (asked over edits; none: done at once, op_edits.c) */
            op_arm(ARM_OP, OA_INIT, song.sel, "INIT", trk_tag(song.sel), 1);
        else if (!is_drum(TSEL))
            op_global_go(G_INITSND);
        return 1;
    case B_PLAY:
        op_panic();
        tp.pend = 0;
        return 1;
    case B_REC:
        if (!song_rec(1))                               /* (a SONG scene row: that scene cleared, asked) */
            rec_motion_ring();                          /* else held, the ring closed: the motion and locks (op_input.c) */
        return 1;
    default:
        return 0;
    }
}
/* SHIFT2 (OCT+ held) + button b: a layer's button locks its layer open (the shifts' language: on). 1 taken */
static int shift2_combo(uint32_t b)
{
    uint32_t l = lay_btn_layer(b);
    if (l == LY_PLAY)
        return 0;
    lay_lock(l);
    return 1;
}
/* SAVE held + button b: 1 taken */
static int save_combo(uint32_t b)
{
    switch (b) {
    case B_HOME:
        lay.chord = 1;                                  /* SAVE then HOME: undo, when one of them is let go */
        return 1;
    case B_SEQ:
        save_pattern();
        return 1;
    case B_ENV:
    case B_LFO:
    case B_EDIT:
    case B_FX:
    case B_SCL:
    case B_ARP:
        lay.quiet = 1;                                  /* (no layer map for the button: it saved) */
        save_sound();
        return 1;
    case B_REC:
        lay.rec_pend = 1;                               /* SONG REC (at its release) */
        return 1;
    case B_PLAY:
        song_play();
        tp.pend = 0;
        return 1;
    default:
        return 0;
    }
}
/* a button pressed with others held (op_input.c op_press, first): a locked layer let go, a layer locked, the SAVE
 * and HOME combinations. 1: taken (nothing else happens; the buttons involved are no taps) */
static int op_combo(uint32_t b, uint32_t held)
{
    uint32_t others = held & ~op_combo_bit(b), l;
    if (b != B_SAVE && b != B_HOME)
        lay.chord = 0;                                  /* (another button: no undo / redo) */
    if (lay.lock != LY_PLAY && b != B_PLAY && b != B_REC && b != B_OCTDN && b != B_OCTUP) {
        lay_unlock();                                   /* locked: any other button lets it go, and does only that */
        op_eat(op_combo_bit(b));
        return 1;
    }
    if (ui.scr == SCR_STEP && st.held)
        return 0;                                       /* (a step held: SAVE its fill, a page button its locks) */
    if (b == B_OCTUP && lay.held != LY_PLAY && lay.held != LY_OPS && lay.held != LY_ROLL && FIF(FELUCCA_PATTERNS)(lay.held != LY_PAT &&) 1) {
        l = lay.held;                                   /* a layer held, then SHIFT2: locked open (ENV on FM6, ARP, LFO: their OCT+
                                                         * keeps its job, the next page, a hard roll, launch now: SHIFT2 first) */
        lay_lock(l);
        op_eat(op_combo_bit(B_OCTUP) | op_combo_bit(LAYER_BTN[l]));
        return 1;
    }
    if (op_shift(others) && shift_combo(b)) {
        op_eat(op_combo_bit(b) | op_combo_bit(B_OCTDN));
        return 1;
    }
    if (op_shift2(others) && shift2_combo(b)) {
        op_eat(op_combo_bit(b) | op_combo_bit(B_OCTUP));
        return 1;
    }
    if ((others & op_combo_bit(B_SAVE)) && !op_armed() && save_combo(b)) {
        op_eat(op_combo_bit(b) | op_combo_bit(B_SAVE));
        return 1;
    }
    if (b == B_SAVE && (others & op_combo_bit(B_HOME)) && !op_armed()) {
        lay.chord = 2;                                  /* HOME then SAVE: redo, when one of them is let go */
        op_eat(op_combo_bit(B_SAVE) | op_combo_bit(B_HOME));
        return 1;
    }
    return 0;
}
/* buttons let go (op_input.c): the undo / redo chord, SAVE + REC's SONG REC, PLAY's tap and the TEMPO page */
static void op_released(uint32_t rel)
{
    if (lay.chord && (rel & (op_combo_bit(B_SAVE) | op_combo_bit(B_HOME)))) {
        op_undo(lay.chord == 2u);
        lay.chord = 0;
    }
    if (lay.rec_pend && (rel & op_combo_bit(B_REC))) {
        lay.rec_pend = 0;
        song_rec_toggle();
    }
    if (rel & op_combo_bit(B_PLAY)) {
        if (tp.pend)
            op_play();                                  /* a tap: start / stop */
        tp.pend = 0;
        tempo_close();
    }
}
