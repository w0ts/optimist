/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel (docs/UI-OPTIMIST-DESIGN.md sections 2, 2.1) and the entry points the core calls (system/main.c).
 *   SELECT       the cursor: the row               ALGORITHM   the walk MASTER T1..DR the lanes, the page's part (op_walk.c)
 *   PRESETS      the sound (DR: the kit; a lane: its own sound)   KNOB 1..4   the cursor row's cells; a turn makes its cell hot
 *   SAVE tapped  YES: enter, toggle, do, confirm    HOME tapped NO: cancel, back; at the root nothing
 *   HOME held + a knob: the cell to its default; + a drum key: pick the lane (on the mixer: mute the sequencer, seq_mute.c); + a button: op_combos.c
 *   SAVE held + a button: save what it owns (op_combos.c); SAVE then HOME: undo, HOME then SAVE: redo
 *   ENV LFO EDIT FX SCL ARP SEQ tapped: their rows of SOUND (again: the family's next, past the last the first); the global FX screen: FX on the mixer's MASTER row, or ALGORITHM left past T1 on a page (SCOPE: its master source)
 *   GLO tapped: GLOBAL; on GLOBAL SYSTEM, on SYSTEM GLOBAL (op_global.c: their settings lists, where SELECT pages, a knob picks and
 *   sets its line, PRESETS does nothing; ALGORITHM is the track walk there too); the mixer's knob sets are SELECT's
 *   FX EDIT ARP SCL GLO LFO SAVE held: the performance layers (op_layers.c); PLAY held: the TEMPO page (op_tempo.c)
 *   On the mixer: SAVE tapped PROJECT (always, D6), HOME held alone nothing (D1); a family button with no rows nothing
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
    if (ui.row[ui.scr] >= n)
        ui.row[ui.scr] = (uint8_t)(n ? n - 1u : 0u);
}
static void op_row_pick(uint32_t r)
{
    step_cards_show();                                  /* (STEP: a page change shows the cards) */
    if (r == ui.row[ui.scr])
        return;
    ui.row[ui.scr] = (uint8_t)r;
    ui.hot = 0;                                         /* (the SOUND row: PRESETS is the preset again) */
    ui.hot_lit = 0;
    op_disarm();                                        /* (the question was about the other row) */
}
static int sys_scr(uint32_t scr) { return scr == SCR_SYSTEM || scr == SCR_GLOBAL; }   /* SYSTEM's settings shown */
/* the pages with no track context (the user, 2026-10-13: "on pages that are global, save/load, glo, system, the keyboard
 * notes keys should not do anything", then "global pages dont play tracks. period."): GLOBAL, SYSTEM (the BLE devices
 * list too), PROJECT, MASTER FX, the mixer's MASTER row, SCOPE and SONG. Their keys play and send nothing, light nothing and start no pick or
 * record. Kept: PROJECT's slot rows pick the slot (op_project.c prj_key); SONG's PATTERNS row launches patterns; TEMPO's
 * keys (PLAY held) are the MIX layer's, they tap the tempo and play nothing. The layers (a held button) are not these
 * pages: their keys stay theirs (ui_input: the lock is LY_PLAY then) */
static int keys_none(void)
{
    return sys_scr(ui.scr) || ui.scr == SCR_PROJECT || ui.scr == SCR_FX || ui.scr == SCR_SCOPE || mx_on_master()
           || (ui.scr == SCR_SONG && !song_on_pat_row());   /* (SONG's PATTERNS row: the keys launch patterns, a layer's) */
}
/* ALGORITHM walks the track only where there is one (the user, 2026-10-14: "it makes no sense to change track when i am
 * on a page that has no track"). The header's classification (HEAD_TRK_SCR / HEAD_TRK_LY, op_draw.c), so the badge and
 * the walk agree, plus the mixer (its rows are the tracks) and SCOPE (no badge, but its source follows ALGORITHM). Inert:
 * SYSTEM (BLE list; GLOBAL and SYSTEM's lists: ALGORITHM scrolls their lines, glo_walk), PROJECT, SONG, MASTER FX (opened by the FX button; reached by the walk it is the walk's MASTER
 * stop and ALGORITHM right leaves it), TEMPO, NAME, an armed question; a held layer follows the layer's
 * own flag (ERASE, STEP, SCALE, OPS walk; FX, ROLL, MIX, SONG, PATTERNS-all do not). The master FX cards in a layer
 * (ui.master) keep the walk: it is how you leave MASTER */
static int algo_has_track(void)
{
    uint32_t l = lay_now();
    if (name_on() || op_armed())
        return 0;
    if (l != LY_PLAY)
        return HEAD_TRK_LY[l % LY_COUNT];
    if (ui.scr == SCR_FX)
        return fx_by_walk;                              /* (MASTER reached by the walk: ALGORITHM right leaves it; opened by FX: no track) */
    return ui.scr == SCR_HOME || ui.scr == SCR_SCOPE || HEAD_TRK_SCR[ui.scr % SCR_N];
}
static void op_enter(uint32_t scr)
{
    fx_by_walk = 0;                                     /* (a screen entered: the master FX screen is the walk's again only via walk_master_in) */
    uint8_t from_master = ui.scr == SCR_HOME && mx_row() == MXR_MASTER;   /* (SCOPE opened on the mixer's MASTER row: its source) */
    if (ui.scr == SCR_SCOPE)
        ui.master = 0;                                  /* (the scope's MASTER source goes with the scope) */
    if (scr != ui.scr && (sys_scr(ui.scr) || (tp.on && sys_scr(tp.prev))))
        sys_leave();                                    /* (SYSTEM or GLOBAL left: their settings saved; the TEMPO page over
                                                         * them: ui.scr is SCR_TEMPO) */
    if (tp.on)                                          /* (a screen entered from the TEMPO page: it is closed) */
        tp.on = 0, clk_nudge = 0;
    ui.scr = (uint8_t)(scr % SCR_N);
    if (scr == SCR_SCOPE)
        ui.master = from_master;
    if (scr == SCR_FX)                                  /* (the master FX opened: right goes to SOUND T1, unless the walk */
        ui.m_scr = SCR_SOUND, ui.m_fam = SND_ALL, ui.m_row = 0;   /* sets where it came from: op_walk.c) */
    step_reset();                                       /* (STEP: the keys are steps again, nothing held) */
    /* (STEP keeps its page: it opens on the one it was left on, the user 2026-10-13: "screen should remember its page") */
    snd_fam = SND_ALL;                                  /* (SOUND entered: every row; a page button narrows it) */
    ui.toast_t = 0;                                     /* (a result belongs to the screen it was done on) */
    prj_op_reset();                                     /* (PROJECT's operation is LOAD again: op_prjflow.c) */
    ui.cards_t = 0;
    cards_snap_use = 0;                                 /* (STEP's held-step snapshot goes with its cards) */
    ui.hot = 0;
    ui.hot_lit = 0;
    op_disarm();
    op_rows_fix();
    ui.force = 1;
}
static void go_home(void)                               /* power-on, a new project: the mixer, MASTER */
{
    op_enter(SCR_HOME);
    ui.lane = 0;                                        /* (the drum track: DR itself, its kit) */
    ui.row[SCR_HOME] = (uint8_t)(MXR_T1 + song.sel);   /* (the mixer opens on the track: MASTER is above it) */
}
/* the PAGES entry the cursor is on (main.c's breadcrumb; miss.c counts on TOOLS) */
static uint32_t op_cursor_page(void)
{
    uint32_t r = ui.row[ui.scr];
    if (ui.scr == SCR_SOUND && snd_page(r))             /* (a lane's LFO, this UI's own page: PAGES' LFO) */
        return lane_lfo_pg(snd_page(r)) ? page_first(FAM_LFO) : lane_fx_pg(snd_page(r)) ? page_first(FAM_FX) : smp_pg(snd_page(r)) ? src_page_ix() : (uint32_t)(snd_page(r) - PAGES);
    if (ui.scr == SCR_FX)
        return fx_ix[r % OP_MAXROWS] == FX_RET_IX ? page_first(FAM_FX) : fx_ix[r % OP_MAXROWS];
    if (ui.scr == SCR_PROJECT && prj_kind(r) == PR_TOOLS)
        return (uint32_t)(tools_page() - PAGES);
    return page_first(FAM_TRK);
}
/* a page button tapped: SOUND with only its family's rows; again: the family's next row, from its last the first */
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
        op_row_pick(ui.row[SCR_SOUND] + 1u < SCR->rows() ? ui.row[SCR_SOUND] + 1u : 0u);   /* (past the last: the first) */
        return;
    }
    if (!snd_has_fam(fam))
        return;                                         /* (none on this track: nothing, as its dark LED says; P25) */
    if (ui.scr != SCR_SOUND)
        op_enter(SCR_SOUND);
    snd_fam = (uint8_t)fam;
    ui.row[SCR_SOUND] = 0xFF;                           /* (so that the pick below is a change) */
    op_row_pick(snd_mem_row(fam));                      /* (the family's page last shown on this track: op_screens.c) */
    ui.force = 1;
}
static void op_jump(uint32_t fam)
{
    op_jump_sound(fam == FAM_ENV && fm6_sel() ? SND_FM6 : fam);   /* (an FM6 track: its operators, op_fm6.c) */
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
        if (scr == ARM_OP)
            op_act(row, k);
        else
            SCREENS[scr % SCR_N].yes(row, k, 1);
        ui.toast_next = 0;
        return;
    }
    if (ui.scr == SCR_STEP && st.held) {                /* a step held: the hot lock's HOLD, else its fill condition */
        held_yes();
        return;
    }
    if (!SCR->yes(ui.row[ui.scr], ui.hot, 0) && ui.scr != SCR_STEP && ui.scr != SCR_PROJECT && !sys_scr(ui.scr))
        op_enter(SCR_PROJECT);                          /* SAVE works on every page but SEQ (where it is "reverse", the user, 2026-10-14): where YES has
                                                         * nothing to do it opens PROJECT, as on the mixer */
}
static void op_no(void)
{
    if (op_armed()) {
        op_disarm();                                    /* cancel */
        return;
    }
#if FELUCCA_BLE
    if (dev_listing()) {                                /* the BLE devices: back to SYSTEM's BLUETOOTH row (op_project.c) */
        dev_close();
        return;
    }
#endif
    if (ui.scr == SCR_STEP && st.held) {                /* a step held, HOME tapped: the step cleared */
        held_clear();
        return;
    }
    if (ui.scr == SCR_HOME)
        op_enter(SCR_SCOPE);                            /* at the root: the mixer and the scope, in turn [P] */
    else
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
static int op_rec(void)                                 /* one record arm, on the selected track; 1: a plain arm /
                                                         * disarm (REC held then clears: rec_held) */
{
    if (ft_owns_press() || song_rec(0))                 /* (SONG: a scene row stores the loop, PATTERNS duplicates) */
        return 0;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return 0;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return 1;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                                    /* playing: record now */
    else
        rec_wait = 1;                                   /* stopped: the first note starts it */
    return 1;
}
/* REC held (op_state.c rh): once a frame. Held RH_ARM_MS after a plain press, the press is undone and the ring fills;
 * RH_CLEAR_MS more, the track is cleared (op_clear_track: undoable, "T2 cleared"); let go before: nothing */
static void rec_held(uint32_t held)
{
    if (!(held & (1u << panel.btn[B_REC]))) {
        if (rh.ring)
            ui.force = 1;                               /* (let go before the end: the ring goes, nothing done) */
        rh.on = rh.ring = 0;
        return;
    }
    if (rh.on && !rh.ring && fm1_ms - rh.t0 >= RH_ARM_MS) {
        song.rec = rh.prev_rec;                         /* a hold: the press is undone */
        rec_wait = rh.prev_wait;
        ui.msg_t = 0;
        rh.ring = 1;
        rh.t1 = fm1_ms;
        rh.trk = song.sel;
    }
    if (rh.ring && fm1_ms - rh.t1 >= RH_CLEAR_MS) {
        ui.toast_next = 1;                              /* (what it says: a toast, as a confirmed clear) */
        op_clear_track(rh.trk);
        ui.toast_next = 0;
        rh.on = rh.ring = 0;                            /* (until REC is up: nothing more) */
    }
}

/* select track i (ALGORITHM, the editor): its sound and pattern from now on */
static void track_select(uint32_t i)
{
    ui.master = 0;                                      /* (a track picked: no longer MASTER) */
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    rec_follow(i);                                      /* recording follows the selected track */
    op_disarm();
    sync_reload = 1;
    op_repaint();                                       /* (in place: no clear, no flash) */
}
/* the drum lane l selected (the pick, the mixer's lane rows, STEP's SELECT): SOUND's rows, STEP's header and grid
 * follow. A change previews the sound once on STEP (the clip editor) while the transport is stopped, never while it
 * plays and never elsewhere: the mixer's ALGORITHM walk is silent (the user, 2026-10-10). key: the pick's own key did
 * it (stopped it sounded as a key does; playing it was kept silent: pick_silent) */
static void lane_pick_from(uint32_t l, int key)
{
    uint32_t was = lane_selected();
    lane_select(l);
    ui.lane = 1;                                        /* (a lane selected, not DR itself: PRESETS its sound) */
    if (!key && ui.scr == SCR_STEP && l != was && l != DRUM_ACC && !song.playing && !transport_req)
        audition_lane(l);                               /* (the ACCENT lane has no sound to preview) */                               /* (seq.c: the next block plays it, as SLOOP's pages do) */
}
static void lane_pick(uint32_t l) { lane_pick_from(l, 0); }
/* the pick's modifier held on the drum track: HOME, on every page (on STEP too, unless a step is held: HOME then clears it;
 * SEQ + a key on STEP clears the step) */
static int pick_mod(uint32_t held)
{
    if (!is_drum(TSEL))
        return 0;
    return ui.scr == SCR_STEP ? step_pick(held) : op_shift(held);
}
/* the pick while playing: its key must not sound over the pattern, so the keys reach the UI only (seq.c LY_STEP:
 * they play nothing) and op_drain drops them as steps; stopped, the key plays: it is the preview */
/* HOME held on the mixer: its keys MUTE the sequencer (seq_mute.c: the 16 drum keys the lanes, the 4 black keys T1 T2 T3 DR)
 * instead of picking the lane (the user, 2026-10-13); the key plays nothing, playing or stopped */
static int mute_mod(uint32_t held)                      /* (SHIFT alone: SAVE + HOME + a key is the scene clear, a layer's keys) */
{
    return ui.scr == SCR_HOME && (held & 0x3FFFu) == (1u << panel.btn[B_OCTDN]);
}
static int pick_silent(uint32_t held) { return mute_mod(held) || (song.playing && pick_mod(held)); }
static void smute_notes(uint32_t notes)
{
    uint32_t id;
    for (id = 0; id < 27u; id++)
        if ((notes >> id) & 1u) {
            int32_t m = smute_key(id);
            if (m >= 0) {
                int on = smute_toggle(m);
                ui_say(m == SMUTE_ACC ? DACC_CODE : m >= 16 ? trk_tag((uint32_t)m - 16u) : LANE_CODE[m], on ? " SEQ MUTE" : " SEQ ON");
            }
        }
}
/* a drum key played while the keys play selects its lane, DR itself included (the user, 2026-10-12: "when i play a note
 * for the drums on the keyboard it should switch to that instrument in all the relevant pages"), as HOME + a key and
 * ALGORITHM do: the mixer's lane row, the drum SOUND / ENV / FX pages, STEP's LANE page, PRESETS by the lane's SOURCE.
 * Not where the keys mean something else (STEP's steps, a layer's tiles, NAME, the FM6 layer: ui_input's other
 * branches), not on MASTER, and not while a value is being turned (a knob in the last 300 ms: the cards stay). On a
 * key down, from the main loop's note edges: nothing in seq.c's key ISR. The lowest key of a chord wins */
#define HIT_KNOB_MS 300u
static void drum_hit_pick(uint32_t notes)
{
    uint32_t k, now = fm1_ticks();
    if (!is_drum(TSEL) || master_sel() || ui.scr == SCR_STEP)
        return;                                         /* (STEP: step_played picks the last hit, the keys playing) */
    for (k = 0; k < 4u; k++)
        if (now - ui.enc_t[EN_K1 + k] < HIT_KNOB_MS * 1000u * FM1_TICKS_PER_US)
            return;
    for (k = 0; k < 27u && !(((notes >> k) & 1u) && lane_key(k)); k++)
        ;                                               /* (the lowest white key, or key 1, the ACCENT lane: the other upper keys play and pick nothing) */
    if (k == 27u || (ui.lane && lane_of(k) == lane_selected()))
        return;
    lane_pick_from(lane_of(k), 1);
    if (ui.scr == SCR_HOME)
        ui.row[SCR_HOME] = (uint8_t)(MXR_LANE0 + lane_selected());   /* (the mixer: its lane row) */
    op_repaint();                                       /* (in place: the cards, the badge, the pictures follow) */
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

/* ---- the octave: OCT- / OCT+ tapped (let go, nothing else used while they were down), with its toast */
static void oct_set(int32_t o)
{
    char b[8];
    song.octave = (int8_t)o;
    fmt_int(b, o);
    ui.toast_next = 1;
    ui_say("OCTAVE: ", b);
    ui.toast_next = 0;
}
static void oct_tap(uint32_t b)                         /* the keyboard octave, STEP's roll view too (op_stepdraw.c roll_range);
                                                         * the drum track: ghost / hard while held, seq.c */
{
    int32_t o = song.octave + (b == B_OCTDN ? -1 : 1);
    if (is_drum(TSEL))
        return;
    oct_set(o < -3 ? -3 : o > 3 ? 3 : o);
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
    if (b < NB && mx_master_dead(b)) {                  /* the mixer's MASTER: no track button (no HOME + it either) */
        op_clean &= ~BIT(b);
        return;
    }
    if (op_combo(b, held))                              /* a locked layer let go, a lock, SAVE / HOME + it */
        return;
    if (ui.scr == SCR_STEP && st.held && b < NB && JUMP_FAM[b] != 0xFF) {   /* a step held + a page button: locks */
        if ((b != B_EDIT || is_drum(TSEL)) && b != B_GLO)   /* (EDIT: the drum lane's SOUND pages, its locks) */
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
        rh.prev_rec = song.rec;                         /* (a hold undoes the press: rec_held) */
        rh.prev_wait = rec_wait;
        rh.t0 = fm1_ms;
        rh.ring = 0;
        rh.on = (uint8_t)op_rec();
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (ui.scr == SCR_STEP && st.held) {            /* a step held: its notes an octave (synths), nothing else */
            if (!is_drum(TSEL))
                held_octave(b == B_OCTDN ? -1 : 1);
            op_clean &= ~BIT(b);
        } else if ((held & (BIT(B_OCTDN) | BIT(B_OCTUP))) == (BIT(B_OCTDN) | BIT(B_OCTUP))) {   /* both OCT: STEP FOLLOW again, else the octave back to 0 */
            if (!is_drum(TSEL))
                oct_set(0);
            if (ui.scr == SCR_STEP)
                st.follow = 1;                          /* (STEP: the fit again, the window follows the playhead) */
            op_clean &= ~(BIT(B_OCTDN) | BIT(B_OCTUP));
        } else if (held & BIT(B_EDIT)) {                       /* EDIT + OCT- / OCT+: undo / redo (SLOOP's) */
            op_undo(b == B_OCTUP);
            op_clean &= ~BIT(b);
        } else if (lay_is(LY_OPS)) {                    /* ENV held on FM6: the page back / on (op_fm6.c) */
            fm6_lay_page(b == B_OCTDN ? -1 : 1);
            lay.used = 1;
            op_clean &= ~(BIT(B_ENV) | BIT(b));
        } else if (tempo_nudging() || (held & BIT(B_PLAY))) {   /* (the TEMPO page, GLOBAL's TEMPO row: the nudge) */
            tp.pend = 0;
            op_clean &= ~BIT(b);
        } else if (lay_is(LY_FX) && b == B_OCTDN) {
#if FELUCCA_PUNCH_LATCH
            punch.keybit = 0;                           /* FX + OCT-: the latched effect off (punch.c) */
            punch.req = -1;
#endif
            op_clean &= ~BIT(b);
        }                                               /* (a plain press does nothing yet: it is SHIFT / SHIFT2 while held; let go with
                                                         * nothing else used it changes the octave, op_tap) */
        break;
    default:
        break;
    }
}
/* the mixer with nothing asked: SAVE tapped there opens PROJECT, always (the user, 2026-10-12, D6: the mixer has no YES;
 * FX on and MUTE are knobs, the SOUND row is EDIT's first). HOME held alone there does nothing (D1: SYSTEM is in GLO's cycle) */
static int mx_quiet(void) { return ui.scr == SCR_HOME && !op_armed(); }
static void op_tap(uint32_t b, uint32_t id)
{
    if (lay_btn_layer(b) != LY_PLAY && fm1_ms - op_t0[id % 14u] > LAY_TAP_MS && !(b == B_SAVE && op_armed()))
        return;                                         /* (held: the layer was looked at, no tap) */
    if (b < NB && mx_master_dead(b))
        return;                                         /* (the mixer's MASTER: a track button does nothing) */
    if (b == B_FX && mx_on_master())
        op_enter(SCR_FX);                               /* (the mixer's MASTER: FX is the master FX) */
    else if (b == B_SAVE)
        mx_quiet() ? op_enter(SCR_PROJECT) : op_yes();     /* the mixer: PROJECT */
    else if (b == B_HOME) {
        if (!(mx_quiet() && fm1_ms - op_t0[id % 14u] >= op_hold_ms()))
            op_no();                                    /* (held alone on the mixer: nothing) */
    } else if (b == B_OCTDN || b == B_OCTUP)
        oct_tap(b);                                     /* (a tap: let go with nothing used meanwhile) */
    else if (b == B_SEQ)
        step_seq_tap(fm1_ms - op_t0[id % 14u] >= op_hold_ms());   /* STEP; on it the next page; long: the keys */
    else if (b == B_GLO)
        glo_tap();                                      /* GLOBAL, again SYSTEM, again GLOBAL (op_global.c) */
    else if (b == B_FX && ui.scr == SCR_FX)
        op_row_pick(ui.row[SCR_FX] + 1u < SCR->rows() ? ui.row[SCR_FX] + 1u : 0u);   /* the global FX: its next page, round */
    else if (b < NB && JUMP_FAM[b] != 0xFF)
        op_jump(JUMP_FAM[b]);
}

/* PRESETS browses the selected track's presets (the drum track: its kits), one a detent, on every screen but STEP,
 * where it pages the window, and GLOBAL and SYSTEM, where it does nothing (the user, 2026-10-10: "presets knobs act as select everywhere, remove it"). Under a question, the REC ring or the NAME screen
 * the turn is drained and changes nothing. 1: a turn was taken */
static int op_presets_turn(void)
{
    int32_t s = panel_enc(EN_PRESET);
    if (s == 0)
        return 0;
    if (sys_scr(ui.scr)) {                              /* GLOBAL, SYSTEM: PRESETS does nothing (the user, 2026-10-13): a knob k is
                                                         * line k, CALIBRATE runs from YES; HOME held: no NO either */
        if (op_shift(fm1_in.buttons))
            op_clean &= ~BIT(B_OCTDN);
    } else if (ui.scr == SCR_STEP && op_shift(fm1_in.buttons)) {   /* STEP: SHIFT + PRESETS does nothing (it changed the engine only
                                                         * because this branch came first; PRESETS alone scrolls the window) */
        op_clean &= ~BIT(B_OCTDN);                      /* (SHIFT let go: no octave) */
    } else if (op_shift(fm1_in.buttons) && !op_armed() && !rh.ring && !name_on() && ui.scr != SCR_FX && !ui.master &&
        !mx_on_master() && !(ui.scr == SCR_STEP && st.held)) {
        if (lane_presets())
            lane_kind_step(s);                          /* (a lane off KIT: its sound's next / previous kind, op_walk.c) */
        else
            op_engine_step(s);                          /* SHIFT + PRESETS: the instruments (DR: its kits) */
        if (!op_armed())
            pre_toast();                                /* (asked "Discard edits?": no toast) */
        op_clean &= ~BIT(B_OCTDN);                      /* (SHIFT let go: no octave) */
    } else if (ui.scr == SCR_STEP) {                    /* STEP: the window's pages, every track (the user, 2026-10-11:
                                                         * "in the step sequencer it scroll pages thru the piano roll"),
                                                         * stopping at the ends; a step held: nothing (the held keys are
                                                         * the window's) */
        if (!op_armed() && !rh.ring && !st.held)
            step_scroll(s > 0 ? 1 : -1);
    } else if (!op_armed() && !rh.ring && !name_on() && ui.scr != SCR_FX && !ui.master && !mx_on_master()) {   /* (MASTER, the mixer's too: nothing) */
        if (lane_presets())
            lane_sound_step(s);                         /* a lane off KIT: its own sound, the kit and the other lanes stay */
        else
            op_preset_step(s);                          /* (DR, a lane on KIT: the kit) */
        if (!op_armed())
            pre_toast();                                /* (asked "Discard edits?": no toast) */
    }
    return 1;
}
static void op_knobs(uint32_t shift)
{
    uint32_t k, row = ui.row[ui.scr], n = SCR->rows(), turned = 0;
    int32_t s;
    cell_t c;
    ui.shift = (fm1_in.buttons & BIT(B_LFO)) != 0u;     /* SHIFT = LFO held (the user): the cells' fine path */
    if ((s = lay_knobs()) != 0) {                       /* a layer at work: its knobs (op_layers.c) */
        if (s == 2)
            op_clean &= ~op_held;                       /* (turned: the layer's button is no tap) */
        return;
    }
    if (step_knobs())                                   /* STEP, a step held: SELECT, ALGORITHM its own */
        turned = 1;
    if (ui.scr == SCR_HOME) {                           /* the mixer: SELECT its knob sets */
        if ((s = panel_enc(EN_SELECT)) != 0) {
            mx_page(s);
            turned = 1;
        }
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {              /* the cursor: a row a detent, stopping at the ends */
        if (glo_cycle())
            glo_step(s);                                /* (GLOBAL, SYSTEM: their pages of 4 lines, op_global.c) */
        else
            op_row_pick((uint32_t)clamp((int32_t)row + s, 0, (int32_t)n - 1));
        turned = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {      /* the walk: MASTER T1..DR the lanes, the page's part (op_walk.c;
                                                         * not in a free take) */
        if (glo_cycle() && lay_now() == LY_PLAY && !name_on())
            glo_walk(s);                                /* (GLOBAL, SYSTEM: the cursor a line a detent, no track here; a layer over them keeps the walk's rules) */
        else
            algo_walk(s, 0);
        turned = 1;
    }
    op_rows_fix();
    row = ui.row[ui.scr];
    if (op_presets_turn())                              /* PRESETS: the selected track's sound, wherever you are */
        turned = 1;
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        turned = 1;
        step_cards_show();                              /* (STEP: a value edited) */
        SCR->cell(row, k, &c);
        if (c.kind == CK_NONE)
            continue;
        ui.hot = (uint8_t)k;                            /* (an action cell: the turn only picks it) */
        gl_pick(row, k);                                /* (GLOBAL, SYSTEM: its line the cursor) */
        ui.hot_lit = 1;
        HL_NOTE(k);                                     /* (the help line: what it changes, op_help.c) */
        if (c.kind == CK_VAL)
            SCR->turn(row, k, shift ? OP_RESET : s, 0);  /* SHIFT held: back to its default */
    }
    if (turned) {
#if FELUCCA_PATTERNS
        if (lay.held == LY_PAT)
            lay.quiet = 1;                              /* (LFO held + a knob: SHIFT, the screen under, no map) */
#endif
        op_clean &= ~op_held;                           /* (a button held while a knob turned: no tap) */
        st.pend &= (uint16_t)~st.held;                  /* (STEP: a step key held while a knob turned: no tap either) */
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
        if ((e >> 8) == LY_STEP && keys_none() && ((e >> 7) & 1u)) {
            if (ui.scr == SCR_PROJECT && !op_shift(held))
                prj_key(e & 0x7Fu, 1);                  /* (PROJECT: the key picks the slot; no track, no other job) */
            continue;                                   /* (a press; its release goes on: a step held before the page changed) */
        }
        if ((e >> 8) == LY_STEP && ((e >> 7) & 1u) && (pick_mod(held) || mute_mod(held)))
            continue;                                   /* (the silent pick's key: no step; its release goes on, as a held step's must) */
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
    if (!op_shift2(held))
        mx_s2_done = 0;                                 /* (SHIFT2 + the volume card's knob: one mute toggle a hold) */
    lay_frame(held);                                    /* the layer held, shown (op_layers.c) */
    tempo_frame(held);                                  /* PLAY held: the TEMPO page, the nudge (op_tempo.c) */
    rec_held(held);                                     /* REC held: the ring, the track cleared */
    op_drain(held | pressed);                           /* STEP's and the layers' keys (seq.c lk_q) */
    keys = lay_now() != LY_PLAY || (held & BIT(B_PLAY)) || song_on_pat_row();   /* (the keys are a layer's) */
    if (notes) {
        lay.chord = 0;
        tp.pend = 0;
    }
    if (notes && keys) {
        lay.used = 1;
        op_clean &= ~held;
    } else if (notes && keys_none()) {                  /* GLOBAL, SYSTEM, PROJECT: no track, the keys do nothing here */
        op_clean &= ~held;
    } else if (notes && ui.scr == SCR_STEP && step_keys(held)) {   /* the keys are steps: what they do came from lk_q */
        op_clean &= ~held;
    } else if (notes && ui.scr == SCR_STEP && !op_shift(held)) {   /* SEQ held (the pick) or the keys play */
        step_played(notes);
        op_clean &= ~held;
    } else if (notes && mute_mod(held)) {               /* HOME held + a key on the mixer: the sequencer mute */
        smute_notes(notes);
        op_clean &= ~BIT(B_OCTDN);
    } else if (notes && op_shift(held)) {               /* SHIFT held + a key: the pick (the key still plays) */
        if (is_drum(TSEL))
            for (id = 0; id < 27u; id++)
                if (((notes >> id) & 1u) && lane_key(id)) {   /* (a white key or key 1, the ACCENT lane: the other upper keys pick nothing) */
                    lane_pick_from(lane_of(id), 1);     /* the selected lane: SOUND's rows, STEP's header */
                    if (ui.scr == SCR_HOME)
                        ui.row[SCR_HOME] = (uint8_t)(MXR_LANE0 + lane_selected());   /* (the mixer: its row) */
                }
        op_clean &= ~BIT(B_OCTDN);
    } else if (notes) {
        op_clean &= ~held;
        drum_hit_pick(notes);                           /* the drum track: the key's lane selected (the keys play) */
    }
    op_knobs(op_shift(held));
    taps = op_clean & ~held & (op_held | pressed);      /* let go with nothing else done meanwhile: a tap */
    op_clean &= held;
    op_released((op_held | pressed) & ~held);           /* the chords, SAVE + REC, PLAY (op_combos.c) */
    op_held = held;
    for (id = 0; id < 14u; id++)
        if ((taps >> id) & 1u && panel_btn_of(id) != B_PLAY)
            op_tap(panel_btn_of(id), id);
    held = fm1_in.buttons;                              /* the ISR's keys: a layer's, STEP's steps, else they play */
    id = name_on() ? LY_STEP : lay.lock != LY_PLAY ? lay.lock : pick_silent(held) ? LY_STEP
       : lay.held != LY_PLAY || (held & BIT(B_PLAY)) ? LY_PLAY
       : step_keys(held) || keys_none() ? LY_STEP : FIF(FELUCCA_PATTERNS)(song_on_pat_row() ? LY_PAT :) LY_PLAY;
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
    if (ui.scr == SCR_SOUND && snd_fam == FAM_EDIT)
        return B_EDIT;                                  /* (EDIT's first row: the SOUND row, D7) */
    return ui.scr == SCR_FX ? B_FX : ui.scr == SCR_STEP ? B_SEQ : sys_scr(ui.scr) ? B_GLO : B_HOME;   /* (GLO: full on
                                                         * GLOBAL and SYSTEM, half lit elsewhere: op_btn_avail) */
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
        uint32_t idx = st.page * 16u + w, on = idx < len && step_shown(t, idx);
        if (song.playing && idx == at)
            on = blink;                                 /* the playhead's key */
        if ((st.held >> w) & 1u)
            on = 1;
        m |= (uint32_t)on << key_of_lane(w);
    }
    return m;
}
/* ---- the buttons' half light (the user, 2026-10-11: "the buttons that are available should be half lit, for instance
 * no LFO on a drum lane"): a button that does something now glows dim (fm1_led_dim: the HAL's glow layer, under the
 * full light of the active button), one that does nothing stays dark. "Does something" is what ui_input would do with
 * a tap: the page buttons need rows of their family on the selected track (op_jump), or a layer of their own held
 * (lay_bits: none on MASTER's ERASE / ARP, the steps' locks on a held step); the NAME screen takes only HOME, SAVE
 * and PLAY. LFO is the exception: its held layer (patterns) does not count, so the drum track's LFO is dark. */
static uint32_t op_fam_mask(void)                       /* a bit per family the selected track has SOUND rows of */
{
    uint32_t i, m = 0;
    for (i = 0; i < OP_NPG; i++)                        /* (a lane's LFO too: LFO half lit on a lane, dark on DR) */
        if (sound_page(op_pg(i)))
            m |= 1u << (op_pg(i)->fam % 32u);
    return m;
}
/* OCT- / OCT+ glow like any button, only where a tap or a hold + something does something (the user, 2026-10-14: "oct +/-
 * should be lit up only when they do something"). Both: the octave where the note keys play on a synth track (a tap on
 * release; a held step's notes; both = octave 0 / FOLLOW), STEP on the drum track (ghost / hard held), the TEMPO nudge.
 * OCT- is SHIFT, so also where a SHIFT combo exists: SOUND (knob reset,
 * PRESETS), STEP, the mixer off MASTER (reset, the lane pick, SHIFT + key), the BLE devices list (FORGET LAST). OCT+ is
 * SHIFT2, so also the mixer off MASTER (the volume card's mute). Dark: the NAME screen, MASTER, GLOBAL (but the tempo
 * line), SYSTEM, PROJECT, SONG, SCOPE and the other pages of no keys */
#if FELUCCA_BLE
static int dev_listing(void);                           /* op_project.c: the BLE devices list is open */
#endif
static uint32_t op_oct_avail(void)
{
    uint32_t both = 1u << B_OCTDN | 1u << B_OCTUP, m = 0;
    int mixer = ui.scr == SCR_HOME && !mx_on_master();
    if ((!keys_none() && !is_drum(TSEL)) || (ui.scr == SCR_STEP && is_drum(TSEL)) || tempo_nudging())
        m = both;
    if (mixer || ui.scr == SCR_SOUND || ui.scr == SCR_STEP)
        m |= 1u << B_OCTDN;
#if FELUCCA_BLE
    if (dev_listing())
        m |= 1u << B_OCTDN;
#endif
    if (mixer)
        m |= 1u << B_OCTUP;
    return m;
}
static uint32_t op_btn_avail(void)                      /* a bit per panel button (B_*) that has a function now */
{
    static const uint8_t PAGE_BTN[] = {B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_ARP};
    static const uint8_t PAGE_FAM[] = {FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_ARP};
    uint32_t m = 1u << B_HOME | 1u << B_SAVE | 1u << B_PLAY, fam, i, l;
    if (name_on())
        return m;
    m |= 1u << B_REC | 1u << B_GLO | 1u << B_SEQ | op_oct_avail();
    fam = op_fam_mask();
    for (i = 0; i < sizeof PAGE_BTN; i++) {
        uint32_t b = PAGE_BTN[i];
        int on = (fam >> PAGE_FAM[i]) & 1u;
        if (b == B_ENV && fm6_sel())
            on = 1;                                     /* (an FM6 track: its operators, held: their pages) */
        if (b == B_FX && ui.scr == SCR_FX)
            on = 1;                                     /* (the global FX: its next page) */
        if (ui.scr == SCR_STEP && st.held && (b != B_EDIT || is_drum(TSEL)))
            on = 1;                                     /* (a step held: the page buttons lock their page) */
        l = b == B_LFO ? LY_PLAY : lay_btn_layer(b);
        if (l != LY_PLAY && ly_bit[l])
            on = 1;
        if (on)
            m |= 1u << b;
    }
    if (master_sel()) {                                 /* MASTER (any page): no track button; FX the master FX */
        for (i = 0; i < NB; i++)
            if (mx_master_dead(i))
                m &= ~(1u << i);
        m |= 1u << B_FX;
    }
    return m;
}
static void keys_lit(uint32_t *f, uint32_t *d);         /* op_keys.c: the keys' lights */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t c, blink = (fm1_ms / 125u) & 1u, armed = op_armed(), avail = op_btn_avail(), kf, kd;
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
    for (c = 0; c < NB; c++)
        led_put(dl, panel.btn[c], (int)((avail >> c) & 1u));   /* (the half light: under the full light below) */
    led_put(nl, panel.btn[op_screen_btn()], 1);
    if (lay.shown != LY_PLAY)                           /* the layer shown: its button (locked: blinking) */
        led_put(nl, panel.btn[LAYER_BTN[lay.shown]], lay.lock == LY_PLAY || ((fm1_ms / 300u) & 1u));
    led_put(nl, panel.btn[B_SAVE], armed && blink);     /* YES is asked for */
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on));
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && blink));
    if (is_drum(TSEL)) {                                /* the drum track: OCT lit while ghost / hard */
        led_put(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0 && ((avail >> B_OCTDN) & 1u));   /* (the octave shown only where it works) */
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0 && ((avail >> B_OCTUP) & 1u));
    }
    keys_lit(&kf, &kd);                                 /* the keys: full, dim, dark in every mode (op_keys.c, D10) */
    for (c = 0; c < 27u; c++) {
        led_put(nl, 14u + c, (int)((kf >> c) & 1u));
        led_put(dl, 14u + c, (int)((kd >> c) & 1u));
    }
#if FELUCCA_LIGHTS
    {   /* menu LIGHTS / KEYS: the backlight layer under what is lit; off while a mode lights its keys (D10) */
        uint8_t bl[FM1_NCOL] = {0};
        uint32_t back = kf | kd ? 0u : lights_keys_mask(), k;
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
    for (c = 0; c < FM1_NCOL; c++) {                    /* (written on a change only) */
        if (fm1_led[c] != nl[c])
            fm1_led[c] = nl[c];
        if (fm1_led_dim[c] != dl[c])
            fm1_led_dim[c] = dl[c];
    }
}

/* ---- the frame (main.c, ~60 a second): what the core says, then the screen */
static void ui_draw(void)
{
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.toast_t)
        ui.toast_t--;
    if (ui.cards_t)
        ui.cards_t--;
    HL_TICK();                                          /* (the help line's time after a turn: op_help.c) */
    ed_tick();                                          /* (another sound: unedited; op_edits.c) */
    if (step_held_long())
        step_cards_show();                              /* (a step held, past the hold time: the cards up while it is held;
                                                         * the timer runs once it is let go; a tap: none) */
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
    lay_hint();                                         /* a layer opened: "Home locks it", once (op_laydraw.c) */
    mx_tick();                                          /* the mixer: its cursor follows the track, the hits */
    scope_tick();                                       /* the scope's source for the audio ISR */
    song_tick();                                        /* SONG: an edited reference written, the chain saved */
    op_rows_fix();
    snd_mem_tick();                                     /* SOUND's page, remembered per track and family (op_screens.c) */
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
    draw_text_box(0, 200, 240, &FONT_S, "NO INPUT 30 S: CANCEL", C_GRAY, 1);   /* (the only way out: SETUP_IDLE_MS) */
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
