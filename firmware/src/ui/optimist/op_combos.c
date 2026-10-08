/* SPDX-License-Identifier: GPL-3.0-only */
/* The two held buttons with a button (docs/UI-OPTIMIST-DESIGN.md section 2.1, phase 4): SAVE held + a button saves
 * what it owns, HOME held + a button clears it or, on a layer's button, locks the layer open:
 *   SAVE + SEQ    the working pattern into its slot (asked over a used one)   HOME + SEQ   its locks, nudges, fills
 *                                                                                           and motion (asked)
 *   SAVE + ENV LFO EDIT FX SCL ARP   the sound as a user preset (the first free slot); the drum track: its 16
 *                 lanes as a user kit                                         HOME + FX EDIT ARP SCL GLO LFO: the
 *                                                                             layer locked; HOME + ENV: INIT (asked)
 *   SAVE + REC    tapped: SONG REC; held + a scene key: the loop into it       HOME + REC   the track cleared (asked;
 *                                                                             a SONG scene row: the scene)
 *   SAVE + PLAY   the song from its start                                     HOME + PLAY  stop, every voice off
 *   SAVE then HOME: undo                                                      HOME then SAVE: redo
 *                                                                             HOME + OCT   STEP: the window; else
 *                                                                             the octave back to 0
 * The layer lock wins where the two meet (HOME + a layer's button). SAVE then HOME stays undo, so the SAVE layer
 * (scenes) is not locked open. The undo and redo act when one of the two is let go, so that SAVE + HOME + a scene
 * key clears the scene and undoes nothing. */
#if DL_UI && FELUCCA_DRUM_KITS
#define OP_UKITS 16u                    /* drum_kits.c UK_N */
#endif
static void op_undo(int redo);                          /* op_input.c */
static void op_play(void);

static void save_sound(void)                            /* the first free user preset slot (the NAME screen: later) */
{
    uint32_t i;
    if (is_drum(TSEL)) {
#if DL_UI && FELUCCA_DRUM_KITS
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
    for (i = 0; i < UP_SLOTS && up_used(i); i++)
        ;
    if (i == UP_SLOTS)
        ui_message("USER PRESETS FULL");
    else
        up_ui(2u, i);
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
static void op_panic(void)                              /* HOME + PLAY: stop, every voice off (CC 120's effect) */
{
    uint32_t i;
    transport_req = 2;
    fm1_irq_off();
    for (i = 0; i < NTRK; i++)
        midi_silence_track(i);
    fm1_irq_on();
    ui_message("ALL SOUND OFF");
}
/* the confirmed actions of this file (op_layers.c op_act) */
static void op_act_more(uint32_t a, uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    if (a == OA_INIT) {
        if (k == song.sel)
            op_global_go(G_INITSND);                    /* (the engine's defaults: the edits go) */
        return;
    }
    if (a != OA_EXTRAS)
        return;
#if SL24_STEPX
    undo_mark(t, (undo_sess += 4u) | 3u);               /* (an undo level: the extras come back, seq/undo.c) */
    fm1_irq_off();
    stepx_clear(TX(t));                                 /* (nudges, locks, fill conditions) */
    fm1_irq_on();
#endif
#if FELUCCA_MOTION
    motion_clear(t);                                    /* (its recorded motion; the patch comes back) */
#endif
    sync_reload = 1;
    ui_say(trk_tag(k), " EXTRAS CLEARED");
    (void)t;
}

static uint32_t op_combo_bit(uint32_t b) { return 1u << panel.btn[b]; }
static uint32_t op_clean;                               /* (op_input.c: the buttons with nothing else done since) */
static void op_eat(uint32_t mask) { op_clean &= ~mask; }

/* HOME held + button b (pressed now): 1 taken */
static int home_combo(uint32_t b)
{
    uint32_t l = lay_btn_layer(b);
    if (l != LY_PLAY && l != LY_SONG) {                 /* a layer's button: locked open (the lock wins) */
        lay_lock(l);
        return 1;
    }
    switch (b) {
    case B_SAVE:
        lay.chord = 2;                                  /* HOME then SAVE: redo, when one of them is let go */
        return 1;
    case B_ENV:
        if (!is_drum(TSEL))
            op_arm(ARM_OP, OA_INIT, song.sel, "INIT", trk_tag(song.sel), 1);
        return 1;
    case B_SEQ: {
        char a[10];
        str_cpy(a, trk_tag(song.sel), sizeof a);
        str_cpy(a + 2, " LOCKS", sizeof a - 2);
        op_arm(ARM_OP, OA_EXTRAS, song.sel, "CLEAR", a, 1);   /* ("Clear T1 locks?") */
        return 1;
    }
    case B_PLAY:
        op_panic();
        tp.pend = 0;
        return 1;
    case B_REC:
        if (!song_rec(1))                               /* (a SONG scene row: that scene) */
            op_arm(ARM_TRACK, 0, song.sel, "CLEAR", trk_tag(song.sel), 1);
        return 1;
    default:
        return 0;
    }
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
        lay.rec_pend = 1;                               /* tapped: SONG REC (at its release); held + a key: store */
        lay.rec_used = 0;
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
    if (b == B_HOME && lay.held != LY_PLAY && lay.held != LY_SONG) {   /* a layer held, HOME: locked open */
        l = lay.held;
        lay_lock(l);
        op_eat(op_combo_bit(B_HOME) | op_combo_bit(LAYER_BTN[l]));
        return 1;
    }
    if ((others & op_combo_bit(B_HOME)) && home_combo(b)) {
        op_eat(op_combo_bit(b) | op_combo_bit(B_HOME));
        return 1;
    }
    if ((others & op_combo_bit(B_SAVE)) && !op_armed() && save_combo(b)) {
        op_eat(op_combo_bit(b) | op_combo_bit(B_SAVE));
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
        if (!lay.rec_used)
            song_rec_toggle();
    }
    if (rel & op_combo_bit(B_PLAY)) {
        if (tp.pend)
            op_play();                                  /* a tap: start / stop */
        tp.pend = 0;
        tempo_close();
    }
}
