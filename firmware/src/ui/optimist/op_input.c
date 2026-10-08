/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel (docs/UI-OPTIMIST-DESIGN.md sections 2, 2.1) and the entry points the core calls (system/main.c).
 *   SELECT       the cursor: the row               ALGORITHM   the track, on every screen
 *   PRESETS      the hot cell, one unit a detent    KNOB 1..4   the cursor row's cells; a turn makes its cell hot
 *   SAVE tapped  YES: enter, toggle, do, confirm    HOME tapped NO: cancel, back; at the root nothing
 *   HOME held + a knob: the cell to its default; + a drum key: pick the lane; + a button: op_combos.c
 *   SAVE held + a button: save what it owns (op_combos.c); SAVE then HOME: undo, HOME then SAVE: redo
 *   ENV LFO EDIT FX SCL ARP SEQ tapped: their rows of SOUND (again: the family's next); GLO: the FX screen
 *   FX EDIT ARP SCL GLO LFO SAVE held: the performance layers (op_layers.c); PLAY held: the TEMPO page (op_tempo.c)
 *   PLAY tapped: start / stop (when let go); REC: record the selected track (stopped it arms)
 * SAVE, HOME and the page buttons act when let go, and only when nothing else was pressed, turned or played while
 * they were down (a page button: and held less than LAY_TAP_MS): that is how a tap, a layer and a combination are
 * told apart. The drum track's OCT- / OCT+ stay ghost / hard (seq.c). */

/* ---- moving */
static void op_rows_fix(void)                          /* the cursor inside the rows (another track: other rows) */
{
    uint32_t n;
    if (ui.scr == SCR_SOUND && snd_fam != SND_ALL)
        snd_family(snd_fam);                            /* (another track: the family may have no rows there) */
    n = SCR->rows();
    if (ui.scr == SCR_STEP && is_drum(TSEL))
        ui.row[SCR_STEP] = (uint8_t)lane_selected();    /* (STEP's drum rows are the lanes: the selected one) */
    if (ui.row[ui.scr] >= n)
        ui.row[ui.scr] = (uint8_t)(n ? n - 1u : 0u);
}
static void op_row_pick(uint32_t r)
{
    if (r == ui.row[ui.scr])
        return;
    ui.row[ui.scr] = (uint8_t)r;
    if (ui.scr == SCR_STEP && is_drum(TSEL))
        lane_select(r);                                 /* (SELECT on STEP's drum rows: the slow pick) */
    ui.hot = 0;                                         /* (the SOUND row: PRESETS is the preset again) */
    ui.hot_lit = 0;
    op_disarm();                                        /* (the question was about the other row) */
}
static void op_enter(uint32_t scr)
{
    if (ui.scr == SCR_SYSTEM && scr != SCR_SYSTEM)
        sys_leave();
    if (tp.on)                                          /* (a screen entered from the TEMPO page: it is closed) */
        tp.on = 0, clk_nudge = 0;
    ui.scr = (uint8_t)(scr % SCR_N);
    step_reset();                                       /* (STEP: the keys are steps again, nothing held) */
    snd_fam = SND_ALL;                                  /* (SOUND entered: every row; a page button narrows it) */
    ui.toast_t = 0;                                     /* (a result belongs to the screen it was done on) */
    ui.hot = 0;
    ui.hot_lit = 0;
    op_disarm();
    op_rows_fix();
    ui.force = 1;
}
static void go_home(void)                               /* power-on, a new project: the mixer, MASTER */
{
    op_enter(SCR_HOME);
    ui.row[SCR_HOME] = 0;
}
/* the PAGES entry the cursor is on (main.c's breadcrumb; miss.c counts on TOOLS) */
static uint32_t op_cursor_page(void)
{
    uint32_t r = ui.row[ui.scr];
    if (ui.scr == SCR_SOUND && snd_page(r))
        return (uint32_t)(snd_page(r) - PAGES);
    if (ui.scr == SCR_FX)
        return fx_ix[r % OP_MAXROWS];
    if (ui.scr == SCR_PROJECT && prj_kind(r) == PR_TOOLS)
        return (uint32_t)(tools_page() - PAGES);
    return page_first(FAM_TRK);
}
/* a page button tapped: SOUND with only its family's rows; again: the family's next row, from its last the first
 * (GLO: the FX screen's GLO rows, the next one again) */
static uint32_t op_row_fam(uint32_t scr, uint32_t r)   /* the family of a SOUND / FX row's page, 0xFF none */
{
    if (scr == SCR_SOUND)
        return snd_page(r) ? snd_page(r)->fam : 0xFFu;
    return PAGES[fx_ix[r % OP_MAXROWS]].fam;
}
static int snd_has_fam(uint32_t fam)                    /* the selected track has rows of family fam */
{
    uint8_t keep = snd_fam;
    uint32_t n;
    if (fam == SND_FM6)
        return fm6_sel();
    snd_fam = (uint8_t)fam;
    n = page_rows(snd_row_page, snd_ix);
    snd_fam = keep;
    page_rows(snd_row_page, snd_ix);
    return n != 0;
}
static void op_jump_sound(uint32_t fam)
{
    if (ui.scr == SCR_SOUND && snd_fam == fam) {        /* again: the next row, round */
        op_row_pick((ui.row[SCR_SOUND] + 1u) % SCR->rows());
        return;
    }
    if (ui.scr != SCR_SOUND)
        op_enter(SCR_SOUND);
    if (!snd_has_fam(fam))
        return;                                         /* (none on this track, the drum track's ENV: every row) */
    snd_fam = (uint8_t)fam;
    ui.row[SCR_SOUND] = 0xFF;                           /* (so that the pick below is a change) */
    op_row_pick(0);
    ui.force = 1;
}
static void op_jump(uint32_t fam)
{
    uint32_t scr = fam == FAM_GLO ? SCR_FX : SCR_SOUND, n, r, first = 0xFF, next = 0xFF, cur, on;
    if (scr == SCR_SOUND) {
        op_jump_sound(fam == FAM_ENV && fm6_sel() ? SND_FM6 : fam);   /* (an FM6 track: its operators, op_fm6.c) */
        return;
    }
    if (ui.scr != scr)
        op_enter(scr);
    n = SCR->rows();
    cur = ui.row[scr];
    on = op_row_fam(scr, cur) == fam;                   /* already on the family: its next row */
    for (r = 0; r < n; r++) {
        if (op_row_fam(scr, r) != fam)
            continue;
        if (first == 0xFF)
            first = r;
        if (on && r > cur && next == 0xFF)
            next = r;
    }
    if (first == 0xFF)
        return;                                         /* (none on this track: the drum track has no ENV) */
    op_row_pick(next != 0xFF ? next : first);
}

/* ---- undo / redo: the message says which and where (as SLOOP's ui.c undo_say) */
static void op_undo(int redo)
{
    char b[32];
    if (!undo_apply(redo)) {
        ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    sync_reload = 1;
    ui.force = 1;
    str_cpy(b, redo ? "REDO" : "UNDO", sizeof b);
#if FELUCCA_UNDO_HISTORY
    {
        uint32_t n, m, tk, k;
        undo_status(&n, &m, &tk);
        k = str_len(b);
        b[k++] = ' ';
        fmt_int(b + k, (int32_t)n);
        k = str_len(b);
        b[k++] = '/';
        fmt_int(b + k, (int32_t)m);
        k = str_len(b);
        b[k++] = ' ';
        str_cpy(b + k, trk_tag(tk), sizeof b - k);
    }
#endif
    ui_message(b);
}

/* ---- YES and NO */
static void op_yes(void)
{
    if (op_armed()) {                                   /* confirm */
        uint32_t scr = ui.arm_scr, row = ui.arm_row, k = ui.arm_k;
        op_disarm();
        ui.toast_next = 1;                              /* (what it says: a toast in the middle) */
        if (scr == ARM_TRACK)
            op_clear_track(k);
        else if (scr == ARM_OP)
            op_act(row, k);
        else
            SCREENS[scr % SCR_N].yes(row, k, 1);
        ui.toast_next = 0;
        return;
    }
    if (ui.scr == SCR_STEP && st.held) {                /* a step held: its fill condition */
#if FELUCCA_FILLS
        held_fill();
#endif
        return;
    }
    SCR->yes(ui.row[ui.scr], ui.hot, 0);
}
static void op_no(void)
{
    if (op_armed()) {
        op_disarm();                                    /* cancel */
        return;
    }
    if (ui.scr == SCR_STEP && st.held) {                /* a step held, HOME tapped: the step cleared */
        held_clear();
        return;
    }
    if (ui.scr != SCR_HOME)
        op_enter(SCR_HOME);                             /* back: the mixer, on the row it was left on */
}

/* ---- transport (as SLOOP's ui_input.c B_PLAY and ui_studio.c rec_toggle) */
static void op_play(void)
{
#if FELUCCA_ARRANGER
    if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) {
        ui_message("EMPTY SECTION: REC");
        return;
    }
#endif
    if (!ft_owns_press())                               /* (it closed or dropped a free take: seq.c) */
        transport_req = song.playing ? 2 : 1;
}
static void op_rec(void)                                /* one record arm, on the selected track */
{
    if (ft_owns_press() || song_rec(0))                 /* (SONG: a scene row stores the loop, PATTERNS duplicates) */
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                                    /* playing: record now */
    else
        rec_wait = 1;                                   /* stopped: the first note starts it */
}

/* select track i (ALGORITHM, the editor): its sound and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    rec_follow(i);                                      /* recording follows the selected track */
    op_disarm();
    sync_reload = 1;
    ui.force = 1;
}

/* the layers the keyboard knows (seq.c): the performance layers' buttons (op_layers.c lay_bits), OCT- / OCT+ the
 * drums' ghost / hard, REC closes a free take, PLAY drops it */
static void layers_init(void)
{
    ly_bit[LY_PLAY] = 0;
    lay_bits();                                         /* (LY_STEP: no button, STEP locks the layer: ui_input) */
    dyn_bit[0] = 1u << panel.btn[B_OCTDN];
    dyn_bit[1] = 1u << panel.btn[B_OCTUP];
    ft_btn_mask = 1u << panel.btn[B_REC];
    ft_drop_mask = 1u << panel.btn[B_PLAY];
}

/* ---- the input, every main-loop pass */
static uint32_t op_held;                                /* the buttons down (op_clean: op_combos.c) */
static uint32_t op_t0[14];                              /* each button's press (fm1_ms), by panel id */
static const uint8_t JUMP_FAM[NB] = {[B_FX] = FAM_FX, [B_SCL] = FAM_SCL, [B_ENV] = FAM_ENV, [B_LFO] = FAM_LFO,
                                     [B_EDIT] = FAM_EDIT, [B_GLO] = FAM_GLO, [B_ARP] = FAM_ARP, [B_SEQ] = 0xFF,
                                     [B_HOME] = 0xFF, [B_SAVE] = 0xFF, [B_PLAY] = 0xFF, [B_REC] = 0xFF,
                                     [B_OCTDN] = 0xFF, [B_OCTUP] = 0xFF};
#define BIT(b) (1u << panel.btn[b])

static void op_press(uint32_t b, uint32_t held)
{
    if (op_combo(b, held))                              /* a locked layer let go, a lock, SAVE / HOME + it */
        return;
    if (ui.scr == SCR_STEP && st.held && b < NB && JUMP_FAM[b] != 0xFF) {   /* a step held + a page button: locks */
        if (b != B_EDIT && b != B_GLO)
            step_lock_page(JUMP_FAM[b]);
        op_clean &= ~BIT(b);
        return;
    }
    switch (b) {
    case B_PLAY:
        if (ft_on) {
            op_play();                                  /* (a free take: PLAY drops it, seq.c; at once) */
            op_clean &= ~BIT(B_PLAY);
        } else {
            tp.pend = 1;                                /* a tap starts / stops when let go; held: TEMPO */
            tp.t0 = fm1_ms;
        }
        break;
    case B_REC:
        op_rec();
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (ui.scr == SCR_STEP && (held & BIT(B_HOME))) {   /* HOME + OCT: the window; both OCT: FOLLOW again */
            uint32_t both = BIT(B_OCTDN) | BIT(B_OCTUP);
            if ((held & both) == both)
                st.follow = 1;
            else
                step_scroll(b == B_OCTDN ? -1 : 1);
            op_clean &= ~BIT(B_HOME);
        } else if (held & BIT(B_EDIT)) {                       /* EDIT + OCT- / OCT+: undo / redo (SLOOP's) */
            op_undo(b == B_OCTUP);
        } else if (held & BIT(B_HOME)) {                /* HOME + OCT off STEP: the octave back to 0 */
            song.octave = 0;
            op_clean &= ~BIT(B_HOME);
        } else if (lay_is(LY_OPS)) {                    /* ENV held on FM6: the page back / on (op_fm6.c) */
            fm6_lay_page(b == B_OCTDN ? -1 : 1);
            lay.used = 1;
            op_clean &= ~BIT(B_ENV);
        } else if (tp.on || (held & BIT(B_PLAY))) {    /* (the TEMPO page: the nudge, op_tempo.c) */
            tp.pend = 0;
        } else if (lay_is(LY_FX) && b == B_OCTDN) {
#if FELUCCA_PUNCH_LATCH
            punch.keybit = 0;                           /* FX + OCT-: the latched effect off (punch.c) */
            punch.req = -1;
#endif
        } else if (!is_drum(TSEL)) {                    /* (the drum track: ghost / hard while held, seq.c) */
            uint32_t both = BIT(B_OCTDN) | BIT(B_OCTUP);
            if ((held & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
        }
        break;
    default:
        break;
    }
}
static void op_tap(uint32_t b, uint32_t id)
{
    if (lay_btn_layer(b) != LY_PLAY && fm1_ms - op_t0[id % 14u] > LAY_TAP_MS && !(b == B_SAVE && op_armed()))
        return;                                         /* (held: the layer was looked at, no tap) */
    if (b == B_SAVE)
        op_yes();
    else if (b == B_HOME)
        op_no();
    else if (b == B_SEQ)
        step_seq_tap();                                 /* STEP; on STEP: the keys steps / playing */
    else if (b < NB && JUMP_FAM[b] != 0xFF)
        op_jump(JUMP_FAM[b]);
}

static void op_knobs(uint32_t home)
{
    uint32_t k, row = ui.row[ui.scr], n = SCR->rows(), turned = 0;
    int32_t s;
    cell_t c;
    if ((s = lay_knobs()) != 0) {                       /* a layer at work: its knobs (op_layers.c) */
        if (s == 2)
            op_clean &= ~op_held;                       /* (turned: the layer's button is no tap) */
        return;
    }
    if (step_knobs())                                   /* STEP, a step held: SELECT, ALGORITHM, PRESETS its own */
        turned = 1;
    if ((s = panel_enc(EN_SELECT)) != 0) {              /* the cursor: a row a detent, stopping at the ends */
        op_row_pick((uint32_t)clamp((int32_t)row + s, 0, (int32_t)n - 1));
        turned = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {      /* the track (not in a free take) */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
        turned = 1;
    }
    op_rows_fix();
    row = ui.row[ui.scr];
    if ((s = panel_enc(EN_PRESET)) != 0) {              /* the hot cell's value, one unit a detent */
        SCR->cell(row, ui.hot, &c);
        if (c.kind == CK_VAL || c.kind == CK_RO)        /* (the mixer's SOUND row: names, PRESETS the sound) */
            SCR->turn(row, ui.hot, s, 1);
        ui.hot_lit = c.kind == CK_VAL ? 1u : ui.hot_lit;
        turned = 1;
    }
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        turned = 1;
        SCR->cell(row, k, &c);
        if (c.kind == CK_NONE)
            continue;
        ui.hot = (uint8_t)k;                            /* (an action cell: the turn only picks it) */
        ui.hot_lit = 1;
        if (c.kind == CK_VAL)
            SCR->turn(row, k, home ? OP_RESET : s, 0);  /* HOME held: back to its default */
    }
    if (turned) {
        op_clean &= ~op_held;                           /* (a button held while a knob turned: no tap) */
        lay.chord = 0;
        tp.pend = 0;
    }
}
/* lk_q (seq.c): STEP's keys and the layers' keys, in order */
static void op_drain(uint32_t held)
{
    while (lk_r != lk_w) {
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        if ((e >> 8) == LY_STEP)
            step_key(e & 0x7Fu, (e >> 7) & 1u, held);
        else
            lay_key(e >> 8, e & 0x7Fu, (e >> 7) & 1u);
    }
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), held = fm1_in.buttons, id, taps, keys;
    enc_hold = 0;                                       /* (panel.c: every knob readable this pass) */
    if (pressed || notes)
        ui_input_ms = fm1_ms;
    if (name_on()) {                                    /* NAME: the keys type, SAVE done, HOME delete / cancel */
        name_frame(pressed, held);
        op_clean = 0;                                   /* (nothing pressed in NAME is a tap after it) */
        op_held = held;
        if (ly_lock != LY_STEP)
            ly_lock = LY_STEP;                          /* (the keys reach the UI and play nothing: seq.c) */
        return;
    }
    op_rows_fix();
    if (pressed)
        op_clean &= ~op_held;                           /* a press: what was already down is no tap now */
    op_clean |= pressed;
    for (id = 0; id < 14u; id++)
        if ((pressed >> id) & 1u) {
            op_t0[id] = fm1_ms;
            op_press(panel_btn_of(id), held | pressed);
        }
    lay_frame(held);                                    /* the layer held, shown (op_layers.c) */
    tempo_frame(held);                                  /* PLAY held: the TEMPO page, the nudge (op_tempo.c) */
    op_drain(held | pressed);                           /* STEP's and the layers' keys (seq.c lk_q) */
    keys = lay_now() != LY_PLAY || (held & BIT(B_PLAY)) || song_on_pat_row();   /* (the keys are a layer's) */
    if (notes) {
        lay.chord = 0;
        tp.pend = 0;
    }
    if (notes && keys) {
        lay.used = 1;
        op_clean &= ~held;
    } else if (notes && ui.scr == SCR_STEP && step_keys(held)) {   /* the keys are steps: what they do came from lk_q */
        op_clean &= ~held;
    } else if (notes && ui.scr == SCR_STEP && !(held & BIT(B_HOME))) {   /* SEQ held (the pick) or the keys play */
        step_played(notes);
        op_clean &= ~held;
    } else if (notes && (held & BIT(B_HOME))) {         /* HOME held + a key: the pick (the key still plays) */
        if (is_drum(TSEL))
            for (id = 0; id < 27u; id++)
                if ((notes >> id) & 1u)
                    lane_select(lane_of_key(id));       /* the selected lane: SOUND's rows, the footer */
        op_clean &= ~BIT(B_HOME);
    } else if (notes) {
        op_clean &= ~held;
    }
    op_knobs((held & BIT(B_HOME)) != 0u);
    taps = op_clean & ~held & (op_held | pressed);      /* let go with nothing else done meanwhile: a tap */
    op_clean &= held;
    op_released((op_held | pressed) & ~held);           /* the chords, SAVE + REC, PLAY (op_combos.c) */
    op_held = held;
    for (id = 0; id < 14u; id++)
        if ((taps >> id) & 1u && panel_btn_of(id) != B_PLAY)
            op_tap(panel_btn_of(id), id);
    held = fm1_in.buttons;                              /* the ISR's keys: a layer's, STEP's steps, else they play */
    id = name_on() ? LY_STEP : lay.lock != LY_PLAY ? lay.lock : lay.held != LY_PLAY || (held & BIT(B_PLAY)) ? LY_PLAY
       : step_keys(held) ? LY_STEP : FIF(FELUCCA_PATTERNS)(song_on_pat_row() ? LY_PAT :) LY_PLAY;
    if (ly_lock != id)
        ly_lock = (uint8_t)id;
}

/* ---- the LEDs (as SLOOP's ui_input.c: the picture built off-line, copied one byte a column) */
static uint8_t led_pos[41];                             /* (col << 3) | row bit, 0xFF = none */
static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
static int play_led(void)                               /* PLAY flashes on every beat, as heard */
{
    uint32_t p = clk_pos, lat = HALF_FRAMES * 3u / 2u * (uint32_t)song.g[G_BPM];
    if (!song.playing)
        return 0;
    p = p >= lat ? p - lat : p + BEAT_U - lat;
    return p < BEAT_U / 4u;
}
static uint32_t op_screen_btn(void)                     /* the button of what the screen shows */
{
    static const uint8_t FAM_B[FAM_COUNT] = {[FAM_ENV] = B_ENV, [FAM_LFO] = B_LFO, [FAM_FX] = B_FX, [FAM_SCL] = B_SCL,
                                             [FAM_EDIT] = B_EDIT, [FAM_GLO] = B_GLO, [FAM_ARP] = B_ARP,
                                             [FAM_SEQ] = B_SEQ};
    if (ui.scr == SCR_SOUND && snd_fam == SND_FM6)
        return B_ENV;
    if (ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND]))
        return FAM_B[snd_page(ui.row[SCR_SOUND])->fam % FAM_COUNT];
    return ui.scr == SCR_FX ? B_GLO : ui.scr == SCR_STEP ? B_SEQ : B_HOME;
}
/* the keys on STEP: the window's set steps (the drum track: the selected lane's), the playhead's key blinking, the
 * steps held; toggled to playing (or SEQ held): the keys down and the notes the track sounds (as KEYLIT) */
static uint32_t step_leds(uint32_t blink)
{
    const track_t *t = TSEL;
    uint32_t m = 0, w, len = trk_len(t), at = t->seq_idx % len;
    if (!step_keys(fm1_in.buttons)) {
        m = fm1_in.notes;
        if (is_drum(t))
            m |= 1u << key_of_lane(lane_selected());
#if FELUCCA_KEYLIT
        else
            m |= op_keys_sounding(t);
#endif
        return m;
    }
    for (w = 0; w < 16u; w++) {
        uint32_t idx = st.page * 16u + w, on = idx < len && step_has(t, idx);
        if (song.playing && idx == at)
            on = blink;                                 /* the playhead's key */
        if ((st.held >> w) & 1u)
            on = 1;
        m |= (uint32_t)on << key_of_lane(w);
    }
    return m;
}
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0};
    uint32_t c, blink = (fm1_ms / 125u) & 1u, armed = op_armed();
    static uint8_t ready;
    if (!ready) {
        uint32_t id, p, r;
        for (id = 0; id < 41u; id++) {
            led_pos[id] = 0xFF;
            for (p = 0; p < FM1_NCOL; p++)
                for (r = 1; r < 5u; r++)
                    if (FM1_KEYMAP[r][p] == (int8_t)id)
                        led_pos[id] = (uint8_t)((p << 3) | r);
        }
        ready = 1;
    }
    led_put(nl, panel.btn[op_screen_btn()], 1);
    if (lay.shown != LY_PLAY)                           /* the layer shown: its button (locked: blinking) */
        led_put(nl, panel.btn[LAYER_BTN[lay.shown]], lay.lock == LY_PLAY || ((fm1_ms / 300u) & 1u));
    led_put(nl, panel.btn[B_SAVE], armed && blink);     /* YES is asked for */
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on));
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && blink) || (armed && ui.arm_scr == ARM_TRACK && !blink));
    if (is_drum(TSEL)) {                                /* the drum track: OCT lit while ghost / hard, the lane's key */
        led_put(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
        if (ui.scr != SCR_STEP)
            led_put(nl, 14u + key_of_lane(lane_selected()), 1);
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
    if (ui.scr == SCR_STEP || name_on() || lay.shown == LY_OPS) {
        uint32_t m = name_on() ? name_leds() : lay.shown == LY_OPS ? fm6_keys_lit() : step_leds((fm1_ms / 250u) & 1u), k;
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((m >> k) & 1u));
    }
#if FELUCCA_LIGHTS
    {   /* menu LIGHTS / KEYS: the backlight layer under what is lit */
        uint8_t bl[FM1_NCOL] = {0};
        uint32_t back = lights_keys_mask(), k;
        for (k = 0; k < 27u; k++)
            led_put(bl, 14u + k, (int)((back >> k) & 1u));
        if (lights_lvl)
            for (k = 0; k < NB; k++)
                led_put(bl, panel.btn[k], 1);
        for (c = 0; c < FM1_NCOL; c++)
            fm1_led_bg[c] = (uint8_t)(bl[c] & ~nl[c]);
        fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
    }
#endif
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = 0;
    }
}

/* ---- the frame (main.c, ~60 a second): what the core says, then the screen */
static void ui_draw(void)
{
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.toast_t)
        ui.toast_t--;
    op_dsnd_tick();                                     /* the drum lanes (user kits, user samples) */
    if (rec_go) {                                       /* the take started: say so */
        rec_go = 0;
        ui_message("RECORDING");
    }
    if (ft_bars) {                                      /* a free take closed: the loop it made */
        char m[24];
        uint32_t n = ft_bars;
        ft_bars = 0;
        if (n == 0xFFu) {
            ui_message("TAKE DROPPED");
        } else {
            str_cpy(m, n == 1u ? "LOOP 1 BAR " : n == 2u ? "LOOP 2 BARS " : "LOOP 4 BARS ", sizeof m);
            fmt_int(m + str_len(m), song.g[G_BPM]);
            str_cpy(m + str_len(m), " BPM", sizeof m - str_len(m));
            ui_message(m);
        }
    }
    er_flash = 0;                                       /* (EDIT's erase flash: SLOOP's tiles' only) */
    step_tick();                                        /* STEP's window: FOLLOW, LEN */
    song_tick();                                        /* SONG: an edited reference written, the chain saved */
    op_rows_fix();
    ui.page = (uint8_t)op_cursor_page();
#if FELUCCA_MISSING_WARN
    miss_tick();                                        /* a load used what this build lacks: say so (miss.c) */
#endif
    op_frame_draw();
}

/* ---- panel setup: OCT- + OCT+ held at power-on, or SYSTEM > CALIBRATE (as SLOOP's ui_input.c panel_setup) */
#define SETUP_IDLE_MS 30000u                            /* 30 s without input: the old table stays */
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                            /* wait for the buttons to be let go */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    layers_init();
    ui.force = 1;
    return;
timeout:
    panel = old;
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
