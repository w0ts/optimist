/* SPDX-License-Identifier: GPL-3.0-only */
/* The held performance layers (docs/UI-OPTIMIST-DESIGN.md section 4.9, phase 4): a page button held, its keys and
 * knobs change job and the screen shows their map (op_laydraw.c: 16 tiles for the white keys, the knobs' four
 * cells); tapped, the button still opens its rows (op_input.c). The keys' part runs in the audio ISR as for SLOOP's
 * UI (seq.c layer_now: FX punch-in, EDIT erase, ARP note repeat), the others reach the UI through seq.c lk_q:
 *   FX    the 16 punch-in effects               KNOB  FILTER DUST DUCK (TRK_FILT: the track's FILTER)
 *   ARP   note repeat on the keys               KNOB  RATE
 *   SCL   a key: the key of the song             KNOB  CHORD SCALE KEYS TRANSPOSE
 *   GLO   1-4 mute, 5-8 solo, 9-12 FX on / off (FILLS: 9 a fill held, 10 the next bar, FX on the black keys 1-4),
 *         16 tap tempo                           KNOB  the four levels
 *   EDIT  erase as it plays                      KNOB  SHIFT LENGTH TRANSPOSE; OCT- / OCT+ undo / redo
 *   LFO   the patterns (PATTERNS): ui/sloop's ui_pat.c, copied: white n launches the selected track's pattern n at
 *         its end (OCT- held: the next bar, OCT+ held: now), the black keys 1-4 the track, 5 stop, 6 + n store,
 *         7 + a, b copy, 8 + n clear, 9 duplicate, 10 + n a scene; its cards show each track's pattern. KNOB: none,
 *         LFO held is SHIFT (the user, 2026-10-08): a knob turned edits the screen under it, fine (LEN by one)
 *   SAVE  1-16 scene A-P at the next bar (stopped: loaded; an 8-section build 1-8), REC tapped SONG REC, REC held +
 *         n store the loop into n, HOME held + n clear n, PLAY the song from the start, two taps or more held a
 *         quick chain (QCHAIN)
 * Lock a layer: HOME + its button in either order (the layer held then HOME, or HOME held then the button): it
 * stays with the button let go; any button but PLAY, REC and OCT lets it go (and does only that). The logic of the
 * keys and knobs is SLOOP's (ui/sloop/ui_layers.c, ui_pat.c), copied: nothing in ui/sloop is called. */
#define LAY_SHOW_MS op_hold_ms()        /* a layer shows after this (a click does not flash it: SYSTEM HOLD, op_state.c) */
#define LAY_TAP_MS (op_hold_ms() + OP_TAP_GAP)   /* a page button held longer is no tap (the map was looked at) */
static const uint8_t LAYER_BTN[LY_COUNT] = {NB, B_FX, B_EDIT, B_ARP, NB, B_SCL, B_GLO, B_SAVE, OP_FM6 ? B_ENV : NB,
                                            FIF(FELUCCA_PATTERNS)(B_LFO)};   /* (ENV: on an FM6 track only) */
#if FELUCCA_PATTERNS
static void pat_launch(uint32_t k, uint32_t s, uint32_t when);   /* storage/sections/pat.c, later in the unit */
static int pat_has(uint32_t k, uint32_t s);
static uint32_t pat_users(uint32_t k, uint32_t s, uint32_t but);
static uint32_t pat_free(uint32_t k);
static int pat_store_slot(uint32_t k, uint32_t s);
static int pat_copy(uint32_t k, uint32_t a, uint32_t k2, uint32_t b);
static int pat_write(uint32_t k, uint32_t s, uint32_t n);
static uint32_t pat_changed(void);
static int pat_scene_refs(uint32_t i, uint8_t *r);
static int proj_tmp_busy(void);
#endif
static int song_on_pat_row(void);                      /* op_song.c */
static void song_shortcut(int32_t s);
static void fm6_lay_end(void);                          /* op_fm6draw.c: ENV let go after use */
static void tempo_key(int32_t w);                       /* op_tempo.c */
static void op_act_more(uint32_t a, uint32_t k);        /* op_combos.c */
static void track_select(uint32_t i);                   /* op_input.c */
static void section_store(uint32_t s);                 /* storage: the sections (sections.c, or project.c) */
static void section_load(uint32_t s);
static uint32_t arrangement_ready(void);
#if SEC_LOGGED
static int section_cue(uint32_t s);
static int sec_scene_clear(uint32_t s);                /* sections.c (FELUCCA_UI == 1) */
#endif
#if FELUCCA_QCHAIN
static uint32_t section_bars(uint32_t s);
#endif

static struct {
    uint8_t lock;                       /* the layer locked open (LY_*), LY_PLAY none */
    uint8_t shown;                      /* the layer whose map is drawn, LY_PLAY none */
    uint8_t held;                       /* the layer whose button is down now (UI view), LY_PLAY none */
    uint8_t used;                       /* the held layer did something: a key, a knob */
    uint8_t quiet;                      /* a combination took its button (SAVE + FX: a save): no map */
    uint8_t hot;                        /* the layer's hot knob (PRESETS acts on it) */
    uint8_t chord;                      /* SAVE then HOME (1: undo) / HOME then SAVE (2: redo) waiting for a release */
    uint8_t rec_pend, rec_used;         /* REC pressed in the SAVE layer: SONG REC at its release, unless it stored */
    uint8_t song_go;                    /* SAVE + PLAY while playing: stopped, the song starts once stopped */
    uint32_t t0;                        /* the held layer's press */
    uint8_t tap_n;                      /* tap tempo: the taps counted, their times */
    uint32_t tap_ms[4];
#if FELUCCA_PATTERNS
    uint8_t pmod;                       /* LFO: the black key held (6 store, 7 copy, 8 clear, 10 scene), 0 none */
    uint8_t pca, pck;                   /* COPY: the first slot and its track (pca 0xFF: none yet) */
    uint32_t pchg_ms, pchg;             /* the patterns changed since their source ("*"), twice a second */
#endif
#if FELUCCA_QCHAIN
    uint8_t chain[CHAIN_MAX], chain_n;  /* the scene keys tapped in this SAVE hold */
#endif
} lay = {.lock = LY_PLAY, .shown = LY_PLAY, .held = LY_PLAY
#if FELUCCA_PATTERNS
         , .pca = 0xFF
#endif
};

static uint32_t lay_now(void) { return lay.lock != LY_PLAY ? lay.lock : lay.held; }   /* the layer at work */
static int lay_is(uint32_t l) { return lay_now() == l; }
static uint32_t lay_btn_layer(uint32_t b)               /* the layer button b holds, LY_PLAY none */
{
    uint32_t l;
    for (l = LY_FX; l < LY_COUNT; l++)
        if (LAYER_BTN[l] == b)
            return l == LY_OPS && !fm6_sel() ? LY_PLAY : l;   /* (ENV elsewhere: a plain button) */
    return LY_PLAY;
}
/* the layers' buttons as the ISR sees them (seq.c ly_bit): none on STEP while a step is held (there a page button
 * is the steps' locks and the keys stay steps); PLAY is the TEMPO page's (its keys tap the tempo: lk_q LY_MIX) */
static void lay_bits(void)
{
    uint32_t l, off = ui.scr == SCR_STEP && st.held, mst = ui.master;
    if (ly_ops_on != (uint8_t)fm6_sel())
        ly_ops_on = (uint8_t)fm6_sel();                 /* (seq.c: ENV is a layer on an FM6 track only) */
    for (l = LY_FX; l < LY_COUNT; l++) {
        uint32_t v = l == LY_STEP ? STEP_LY_BIT : LAYER_BTN[l] < NB && !off ? 1u << panel.btn[LAYER_BTN[l]] : 0u;
        if (mst && (l == LY_ERASE || l == LY_ROLL || l == LY_OPS))
            v = 0;                                      /* (MASTER: no erase, repeat or operator keys on T1's data) */
        if (l == LY_MIX && !off)
            v |= 1u << panel.btn[B_PLAY];
        if (ly_bit[l] != v)
            ly_bit[l] = v;
    }
}
static void lay_unlock(void)
{
    if (lay.lock == LY_PLAY)
        return;
    lay.lock = LY_PLAY;
    ui.force = 1;
}
static void lay_lock(uint32_t l)
{
    lay.lock = (uint8_t)l;
    lay.used = 1;
    lay.shown = (uint8_t)l;
    ui.force = 1;
}

/* ---- the tools of EDIT's knobs, each a step of one undo (the hold's), as ui_layers.c */
static uint32_t lay_sess;
static void lay_undo_mark(track_t *t)
{
    if (!lay_sess)
        lay_sess = (undo_sess += 4u) | 3u;
    undo_mark(t, lay_sess);
}
static void pattern_rotate(track_t *t, int32_t d)       /* every step one later (d > 0) / earlier */
{
    uint32_t len = trk_len(t), i;
    lay_undo_mark(t);
    fm1_irq_off();
    if (is_drum(t)) {
        dstep_t keep;
        if (d > 0) {
            keep = t->dstep[len - 1u];
            for (i = len - 1u; i > 0; i--)
                t->dstep[i] = t->dstep[i - 1u];
            t->dstep[0] = keep;
        } else {
            keep = t->dstep[0];
            for (i = 0; i + 1u < len; i++)
                t->dstep[i] = t->dstep[i + 1u];
            t->dstep[len - 1u] = keep;
        }
    } else {
        step_t keep;
        if (d > 0) {
            keep = t->step[len - 1u];
            for (i = len - 1u; i > 0; i--)
                t->step[i] = t->step[i - 1u];
            t->step[0] = keep;
        } else {
            keep = t->step[0];
            for (i = 0; i + 1u < len; i++)
                t->step[i] = t->step[i + 1u];
            t->step[len - 1u] = keep;
        }
    }
    fm1_irq_on();
    sync_reload = 1;
}
static void pattern_length(track_t *t, int32_t d)       /* x2 (the pattern again after itself), or half */
{
    uint32_t len = trk_len(t), i;
    char b[8];
    lay_undo_mark(t);
    fm1_irq_off();
    if (d > 0 && len * 2u <= NSTEP) {
        for (i = 0; i < len; i++) {
            t->step[len + i] = t->step[i];
            t->dstep[len + i] = t->dstep[i];
        }
        t->p[P_SLEN] = (int16_t)(len * 2u);
    } else if (d < 0 && len >= 2u) {
        t->p[P_SLEN] = (int16_t)(len / 2u);
    }
    fm1_irq_on();
    sync_reload = 1;
    fmt_int(b, t->p[P_SLEN]);
    ui_say("STEPS ", b);
}
static void pattern_transpose(track_t *t, int32_t d)    /* every note a semitone up / down (synth tracks) */
{
    uint32_t i, j;
    if (is_drum(t))
        return;
    lay_undo_mark(t);
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < t->step[i].n && j < 4u; j++)
            t->step[i].note[j] = (uint8_t)clamp(t->step[i].note[j] + (d > 0 ? 1 : -1), 0, 127);
    fm1_irq_on();
    sync_reload = 1;
}

/* ---- GLO: tap tempo (also the TEMPO page's white keys), the FX bypass */
static void tap_tempo(void)
{
    uint32_t now = fm1_ms, i, sum = 0, n;
    if (lay.tap_n && now - lay.tap_ms[(lay.tap_n - 1u) & 3u] > 2000u)
        lay.tap_n = 0;                                  /* a pause: a new count */
    lay.tap_ms[lay.tap_n & 3u] = now;
    lay.tap_n++;
    if (lay.tap_n < 2u)
        return;
    n = lay.tap_n - 1u > 3u ? 3u : lay.tap_n - 1u;
    for (i = 0; i < n; i++)
        sum += lay.tap_ms[(lay.tap_n - 1u - i) & 3u] - lay.tap_ms[(lay.tap_n - 2u - i) & 3u];
    if (sum)
        song.g[G_BPM] = (int16_t)clamp((int32_t)((60000u * n + sum / 2u) / sum), GP[G_BPM].min, GP[G_BPM].max);
}
static void fx_bypass_toggle(uint32_t i)
{
    track_t *t = &trk[i % NTRK];
    t->p[P_FXOFF] = (int16_t)!t->p[P_FXOFF];
    ui_say(trk_tag(i), fx_on(t) ? " FX ON" : " FX OFF");
    if (t == TSEL)
        sync_reload = 1;
}
#if FELUCCA_FILLS
static int32_t black_index(uint32_t k)                  /* black keys 1..4 (F#, G#, A#, C#) -> 0..3 */
{
    static const int8_t B[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
    return k < 12u ? B[k] : -1;
}
#endif
static void glo_key(uint32_t k, int32_t w)
{
    if (w >= 0 && w < 4) {
        trk[w].p[P_MUTE] = (int16_t)!trk[w].p[P_MUTE];
    } else if (w >= 4 && w < 8) {
        song.solo ^= (uint8_t)(1u << (w - 4));
#if FELUCCA_FILLS
    } else if (w == 8) {
        fill_held = 1;                                  /* a fill while held (SLOOP 2.4) */
    } else if (w == 9) {
        fill_arm = (uint8_t)!fill_arm;                  /* the next bar a fill (again: cancelled) */
        ui_message(fill_arm ? "FILL: NEXT BAR" : "FILL BAR OFF");
    } else if (w < 0 && black_index(k) >= 0) {
        fx_bypass_toggle((uint32_t)black_index(k));     /* (the FX bypass: black keys 1..4) */
#else
    } else if (w >= 8 && w < 12) {
        fx_bypass_toggle((uint32_t)w - 8u);
#endif
    } else if (w == 15) {
        tap_tempo();
    }
    (void)k;
}
static void scl_key(uint32_t k)                         /* the key of the song: every synth track's root */
{
    uint32_t i, root = (53u + k) % 12u;
    for (i = 0; i < NPART; i++)
        trk[i].p[P_ROOT] = (int16_t)root;
    ui_say("KEY ", N_NOTE[root]);
}

/* ---- SAVE: the scenes (sections A..P) */
#define LAY_NSCN (FELUCCA_SECTIONS < 16 ? FELUCCA_SECTIONS : 16)
static void scene_tag(char *b, uint32_t s) { str_cpy(b, "SCENE A", 8), b[6] = (char)('A' + s % 16u); }
static void scene_launch(uint32_t s)                    /* playing: at the next bar; stopped: loaded */
{
    char b[2] = {(char)('A' + s % 16u), 0};
    if (arrangement_clock.running) {
        ui_message("SONG PLAYS");
    } else if (!((arrangement_ready() >> s) & 1u)) {
        ui_say("EMPTY ", b);
    } else if (song.playing) {
#if FELUCCA_QCHAIN
        if (lay.chain_n) {                              /* SAVE still held, a second tap or more: the chain grows */
            if (lay.chain_n < CHAIN_MAX)
                lay.chain[lay.chain_n++] = (uint8_t)s;
            return;
        }
        fm1_irq_off();
        chain_n = 0;                                    /* the first tap: as ever, and any chain stops */
        fm1_irq_on();
        if (lay_is(LY_SONG))
            lay.chain[lay.chain_n++] = (uint8_t)s;
#endif
#if SEC_LOGGED
        if (!section_cue(s)) {                          /* (staged now: the ISR plays it on the next bar) */
            ui_say("EMPTY ", b);
            return;
        }
#else
        live_req = (int8_t)s;
#endif
        ui_say("NEXT: ", b);
    } else {
        section_load(s);
        ui_say("LOADED ", b);
    }
}
#if FELUCCA_QCHAIN
/* SAVE let go: two or more scene taps while it was held play as a chain (SLOOP 2.4 chain_release, as ui_layers.c) */
static void chain_release(void)
{
    uint32_t i, n = lay.chain_n < CHAIN_MAX ? lay.chain_n : CHAIN_MAX;
    uint8_t bars[CHAIN_MAX];
    if (n >= 2u && song.playing && !arrangement_clock.running) {
        for (i = 0; i < n; i++)
            bars[i] = (uint8_t)section_bars(lay.chain[i]);
        fm1_irq_off();
        for (i = 0; i < n; i++) {
            chain_sec[i] = lay.chain[i];
            chain_bar[i] = bars[i];
        }
        chain_i = 0;
        chain_n = (uint8_t)n;
        fm1_irq_on();
        ui_message("CHAIN");
    }
    lay.chain_n = 0;
}
#endif
/* the loop stored into scene s (playing: the arena, written when quiet; stopped: at once), as the editor does */
static void scene_store_now(uint32_t s)
{
    char b[8];
    if (song.playing) {
        section_store(s);
        scene_tag(b, s);
        if (project_used(s))
            ui_say("STORED ", b + 6);
    } else {
        project_save(s);
    }
    ui.force = 1;
}
static void scene_store(uint32_t s)                     /* REC + a scene: asked over a used one */
{
    char b[8];
    scene_tag(b, s);
    if (project_used(s))
        op_arm(ARM_OP, OA_SCN_STORE, s, "STORE", b, 1);
    else
        scene_store_now(s);
}
static void scene_clear_ask(uint32_t s)                 /* HOME + a scene */
{
    char b[8];
    scene_tag(b, s);
    if (!project_used(s))
        ui_say("EMPTY ", b + 6);
    else
        op_arm(ARM_OP, OA_SCN_CLR, s, "CLEAR", b, 1);
}
static void scene_clear_now(uint32_t s)
{
#if SEC_LOGGED
    char b[8];
    if (sec_scene_clear(s))
        return;
    scene_tag(b, s);
    ui_say(b, " CLEARED");
    ui.force = 1;
#else
    (void)s;
    ui_message("NOT IN THIS BUILD");                    /* (the four RAM slots: PROJECT keeps them) */
#endif
}
static void song_rec_toggle(void)                       /* SAVE + REC tapped: the order you play becomes the song */
{
    if (srec) {
        fm1_irq_off();
        srec_stop();                                    /* (playing: the order so far is the song) */
        fm1_irq_on();
        ui_message("SONG REC OFF");
    } else if (arrangement_clock.running) {
        ui_message("STOP FIRST");
    } else {
        arrangement_enabled = 0;
        srec = 1;
        ui_message(live_sec < 0 ? "SONG REC: PICK A SCENE" : "SONG REC: NEXT BAR");
    }
}
static void song_play(void)                             /* SAVE + PLAY: the song from its start */
{
    if (!arr_valid(&arrangement, arrangement_ready())) {
        ui_message("EMPTY SECTION: REC");
        return;
    }
    arrangement_enabled = 1;
    if (song.playing || transport_req) {
        transport_req = 2;                              /* (stopped first, started once stopped: lay_frame) */
        lay.song_go = 1;
    } else {
        transport_req = 1;
    }
    ui_message("SONG");
}
static void save_key(uint32_t k, int32_t w)
{
    uint32_t h = fm1_in.buttons;
    (void)k;
    if (w < 0 || w >= LAY_NSCN)
        return;
    if (h & (1u << panel.btn[B_REC])) {                 /* REC held + n: the loop into n */
        lay.rec_used = 1;
        scene_store((uint32_t)w);
    } else if (h & (1u << panel.btn[B_HOME])) {         /* HOME held + n: clear n */
        lay.chord = 0;
        scene_clear_ask((uint32_t)w);
    } else {
        scene_launch((uint32_t)w);
    }
}

/* ---- LFO: the patterns (ui/sloop/ui_pat.c, copied; its "AGAIN" confirm is this UI's modal) */
#if FELUCCA_PATTERNS
static void pat_refresh(void)                           /* the "*" marks, refreshed twice a second */
{
    if (fm1_ms - lay.pchg_ms > 500u) {
        lay.pchg_ms = fm1_ms;
        lay.pchg = proj_tmp_busy() ? lay.pchg : pat_changed();
    }
}
static int pat_scene_dirty(uint32_t s)                  /* scene s's tracks no longer play its patterns as stored */
{
    uint8_t r[NTRK];
    uint32_t k;
    pat_refresh();
    if (!pat_scene_refs(s, r))
        return 0;
    for (k = 0; k < NTRK; k++)
        if (r[k] != PAT_KEEP && (r[k] != pat_cur[k] || ((lay.pchg >> k) & 1u)))
            return 1;
    return 0;
}
static uint32_t pat_black(uint32_t k)                   /* black key k -> 1.. (F#, G#, A#, C#, D#, F#, ..), 0 white */
{
    static const uint8_t B[12] = {0, 1, 0, 2, 0, 3, 0, 0, 4, 0, 5, 0};
    return B[k % 12u] ? (k / 12u) * 5u + B[k % 12u] : 0u;
}
static void pat_name(char *b, uint32_t k, uint32_t s)   /* "T2 5", "DR 16" */
{
    str_cpy(b, trk_tag(k), 4);
    b[2] = ' ';
    fmt_int(b + 3, (int32_t)s + 1);
}
static void pat_store_ask(uint32_t t, uint32_t s)       /* the working copy into slot s: asked over a used one */
{
    char nm[8];
    pat_name(nm, t, s);
    if (pat_has(t, s))
        op_arm(ARM_OP, OA_PAT_STORE, t << 4 | s, "STORE", nm, 1);
    else if (!pat_store_slot(t, s))
        ui_say("STORED ", nm);
}
static void pat_clear_ask(uint32_t t, uint32_t s)
{
    char nm[8];
    pat_name(nm, t, s);
    if (!pat_has(t, s))
        ui_say("EMPTY ", nm);
    else
        op_arm(ARM_OP, OA_PAT_CLR, t << 4 | s, pat_users(t, s, PAT_ALL) ? "CLEAR USED" : "CLEAR", nm, 1);
}
static void pat_launch_key(uint32_t t, uint32_t w)      /* white n: launch (OCT- held: next bar, OCT+ held: now) */
{
    uint32_t when = fm1_in.buttons & dyn_bit[0] ? PW_BAR : fm1_in.buttons & dyn_bit[1] ? PW_NOW : PW_END;
    char nm[8];
    pat_name(nm, t, w);
    pat_launch(t, w, when);
    if (!song.playing)
        ui_say("LOADED ", nm);
    else
        ui_say(nm, when == PW_BAR ? " NEXT BAR" : when == PW_NOW ? " NOW" : " AT END");
}
static void pat_layer_key(uint32_t k, int32_t w)
{
    uint32_t t = song.sel % NTRK, b = pat_black(k), s;
    char nm[8];
    if (b >= 1u && b <= 4u) {
        track_select(b - 1u);
        return;
    }
    if (b == 5u) {
        pat_launch(t, PAT_NONE, PW_END);
        ui_message(song.playing ? "STOP AT END" : "STOPPED");
        return;
    }
    if ((b >= 6u && b <= 8u) || b == 10u) {
        lay.pmod = (uint8_t)b;
        lay.pca = 0xFF;
        return;
    }
    if (b == 9u) {                                      /* DUPLICATE: into the first free slot */
        s = pat_free(t);
        pat_name(nm, t, s);
        if (s >= PAT_N)
            ui_message("NO FREE PATTERN");
        else if (!pat_store_slot(t, s))
            ui_say("DUPLICATE ", nm);
        return;
    }
    if (w < 0 || w >= (int32_t)PAT_N)
        return;
    if (lay.pmod == 10u) {
        if (w < LAY_NSCN)
            scene_launch((uint32_t)w);
    } else if (lay.pmod == 6u) {
        pat_store_ask(t, (uint32_t)w);
    } else if (lay.pmod == 7u) {                        /* COPY a, then b */
        pat_name(nm, t, (uint32_t)w);
        if (lay.pca == 0xFF) {
            lay.pca = (uint8_t)w, lay.pck = (uint8_t)t;
            ui_say("COPY ", nm);
        } else if (pat_has(t, (uint32_t)w)) {
            op_arm(ARM_OP, OA_PAT_COPY, t << 4 | (uint32_t)w, "COPY TO", nm, 1);
        } else if (!pat_copy(lay.pck, lay.pca, t, (uint32_t)w)) {
            lay.pca = 0xFF;
            ui_say("COPIED ", nm);
        }
    } else if (lay.pmod == 8u) {
        pat_clear_ask(t, (uint32_t)w);
    } else {
        pat_launch_key(t, (uint32_t)w);
    }
}
static void pat_layer_up(uint32_t k)
{
    if (pat_black(k) == lay.pmod)
        lay.pmod = 0;
}
static void pat_knob(uint32_t k, int32_t s)             /* track k's next / previous stored pattern, at its end */
{
    uint32_t c = pat_req[k] < PAT_N ? pat_req[k] : pat_cur[k] < PAT_N ? pat_cur[k] : (s > 0 ? PAT_N - 1u : 0u), i;
    for (i = 1; i <= PAT_N; i++) {
        uint32_t n = (c + (s > 0 ? i : PAT_N - i)) % PAT_N;
        if (pat_has(k, n)) {
            pat_launch(k, n, PW_END);
            return;
        }
    }
}
#endif

/* ---- the confirmed actions of this file and SONG's (op_input.c op_yes: ARM_OP) */
static void op_act(uint32_t a, uint32_t k)
{
    switch (a) {
    case OA_SCN_STORE:
        scene_store_now(k);
        break;
    case OA_SCN_CLR:
        scene_clear_now(k);
        break;
#if FELUCCA_PATTERNS
    case OA_PAT_STORE:
    case OA_PAT_CLR:
    case OA_PAT_COPY: {
        char nm[8];
        uint32_t t = k >> 4, s = k & 15u;
        pat_name(nm, t, s);
        if (a == OA_PAT_STORE ? pat_store_slot(t, s) : a == OA_PAT_CLR ? pat_write(t, s, 0) : pat_copy(lay.pck, lay.pca, t, s))
            break;                                      /* (what went wrong is said: MEM FULL ...) */
        if (a == OA_PAT_COPY)
            lay.pca = 0xFF;
        ui_say(a == OA_PAT_STORE ? "STORED " : a == OA_PAT_CLR ? "CLEARED " : "COPIED ", nm);
        break;
    }
#endif
    default:
        op_act_more(a, k);                              /* (op_combos.c: INIT, the extras' clear) */
        break;
    }
}

/* ---- the layer's keys (seq.c lk_q) and knobs */
static void lay_key(uint32_t layer, uint32_t k, uint32_t down)
{
    int32_t w = punch_key(k);
    if (!down) {
#if FELUCCA_PATTERNS
        if (layer == LY_PAT)
            pat_layer_up(k);
#endif
#if FELUCCA_FILLS
        if (layer == LY_MIX && w == 8)
            fill_held = 0;                              /* GLO + key 9 let go: the fill ends */
#endif
        return;
    }
    lay.used = 1;
    lay.chord = 0;                                      /* (SAVE + HOME + a key: a clear, not an undo) */
    switch (layer) {
    case LY_SCALE:
        scl_key(k);
        break;
    case LY_MIX:
        if (fm1_in.buttons & (1u << panel.btn[B_PLAY])) {   /* PLAY held: the TEMPO page, a white key taps */
            tempo_key(w);
            break;
        }
        glo_key(k, w);
        break;
    case LY_OPS:
        fm6_lay_key(k);                                 /* a black key: what to edit (the white keys play) */
        break;
    case LY_SONG:
#if FELUCCA_PATTERNS
        if (song_on_pat_row()) {                        /* SONG's PATTERNS row: SAVE + n stores into slot n */
            if (w >= 0 && w < (int32_t)PAT_N)
                pat_store_ask(song.sel % NTRK, (uint32_t)w);
            break;
        }
#endif
        save_key(k, w);
        break;
#if FELUCCA_PATTERNS
    case LY_PAT:
        if (song_on_pat_row() && (fm1_in.buttons & (1u << panel.btn[B_HOME]))) {   /* HOME + n: clear slot n */
            if (w >= 0 && w < (int32_t)PAT_N)
                pat_clear_ask(song.sel % NTRK, (uint32_t)w);
            break;
        }
        pat_layer_key(k, w);
        break;
#endif
    default:
        break;
    }
}
/* MASTER in a layer (ui.master): the layers about the selected track (FX, ARP, SCL, EDIT, FM6 operators) take it; the
 * others (the levels, the scenes, STEP's, the patterns) have no track page to leave. Its cards are the master FX */
static int lay_master_ok(uint32_t l)
{
    return l == LY_FX || l == LY_ROLL || l == LY_SCALE || l == LY_ERASE || l == LY_OPS;
}
static const uint8_t LAY_MASTER_G[4] = {G_FILT, G_DUST, G_DUCK, G_CTHR};   /* the master FX the cards show: FILTER DUST DUCK COMP */
/* the value of the layer's knob k: its descriptor and value (0: none) */
static const param_desc_t *lay_desc(uint32_t l, uint32_t k, int16_t **vp)
{
    track_t *t = TSEL;
    *vp = 0;
    if (ui.master && lay_master_ok(l)) {
        if (k < 4u) {
            *vp = &song.g[LAY_MASTER_G[k]];
            return &GP[LAY_MASTER_G[k]];
        }
        return 0;
    }
    switch (l) {
    case LY_FX: {
        static const uint8_t G[3] = {G_FILT, G_DUST, G_DUCK};
        if (k < 3u) {
            *vp = &song.g[G[k]];
            return &GP[G[k]];
        }
#if FELUCCA_TRK_FILT
        *vp = &t->p[P_TFLT];
        return &TP[P_TFLT];
#else
        return 0;
#endif
    }
    case LY_ROLL:
        if (k)
            return 0;
        *vp = &song.g[G_ROLL];
        return &GP[G_ROLL];
    case LY_SCALE: {
        static const uint8_t P[4] = {P_CHORD, P_SCALE, P_QUANT, P_TRANS};
        if (is_drum(t) && k != 1u)
            return 0;
        *vp = k == 1u ? &trk[0].p[P_SCALE] : &t->p[P[k]];
        return &TP[P[k]];
    }
    case LY_MIX:
        *vp = k == TRK_DRUM ? &song.g[G_DRLVL] : &trk[k].p[P_LEVEL];
        return k == TRK_DRUM ? &GP[G_DRLVL] : &TP[P_LEVEL];
    default:
        return 0;
    }
}
static void lay_turn(uint32_t k, int32_t s, int fine)
{
    uint32_t l = lay_now(), i;
    int16_t *vp;
    const param_desc_t *d = lay_desc(l, k, &vp);
    track_t *t = TSEL;
    lay.used = 1;
    lay.chord = 0;
    lay.hot = (uint8_t)k;
    if (ui.master && lay_master_ok(l)) {                /* MASTER: the master FX's four values, nothing of the track */
        if (d)
            val_turn(d, vp, k, s, fine);
        return;
    }
    if (l == LY_ERASE) {
        if (k == 0u)
            pattern_rotate(t, s);
        else if (k == 1u)
            pattern_length(t, s);
        else if (k == 2u)
            pattern_transpose(t, s);
        return;
    }
    if (l == LY_OPS) {
        fm6_lay_turn(k, s, fine);                       /* the page's four values (op_fm6.c) */
        return;
    }
    if (!d)
        return;
    val_turn(d, vp, k, s, fine);
    if (l == LY_SCALE && k == 1u)
        for (i = 1; i < NPART; i++)
            trk[i].p[P_SCALE] = trk[0].p[P_SCALE];      /* (the scale is the song's: every synth track) */
#if FELUCCA_TRK_FILT
    if (l == LY_FX && k == 3u && !FXS_ON(FXT_FILT))
        ui_message("FILT: IN NO FX SLOT");              /* (decision D6: a slot type, heard only in a slot) */
#endif
}
/* the knobs while a layer is at work (held or locked): KNOB 1..4 the layer's, PRESETS its hot knob one unit,
 * ALGORITHM the track as ever, SELECT nothing (no rows). 0: no layer, 1: taken, nothing turned, 2: turned */
static int op_presets_turn(void);                       /* op_input.c */
static int lay_knobs(void)
{
    uint32_t k, turned = 0;
    int32_t s;
    if (lay_now() == LY_PLAY) {
        ui.master = 0;                                  /* (the layer let go: the track under it) */
        return 0;
    }
#if FELUCCA_PATTERNS
    if (lay_now() == LY_PAT)
        return 0;                                       /* LFO held = SHIFT: the knobs are the screen's (op_knobs) */
#endif
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0) {
            lay_turn(k, s, 0);
            turned = 1;
        }
    if (op_presets_turn())                              /* (PRESETS: the sound, as everywhere) */
        turned = 1;
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {
        if (ui.master) {
            ui.master = s > 0 ? 0u : 1u;                /* (right: T1 again; left: stays) */
            ui.force = 1;
        } else if (song.sel == 0u && s < 0 && lay_master_ok(lay_now())) {
            ui.master = 1;                              /* (T1, left: MASTER, its FX in the layer's cards) */
            ui.force = 1;
        } else {
            track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
        }
        turned = 1;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {
        turned = 1;
        if (lay_is(LY_SONG))
            song_shortcut(s);                           /* SAVE held + SELECT: the SONG screen (op_song.c) */
        else if (lay_is(LY_OPS))
            fm6_lay_select(s);                          /* ENV held on FM6: its pages (section 2's paging rule) */
    }
    return 1 + (int)turned;
}

/* ---- once a frame (op_input.c): the layer held, shown, its end */
static void lay_frame(uint32_t held)
{
    uint32_t l, h = LY_PLAY, off = ui.scr == SCR_STEP && st.held;
    lay_bits();
    for (l = LY_FX; l < LY_COUNT && !off; l++)
        if (LAYER_BTN[l] < NB && (held & (1u << panel.btn[LAYER_BTN[l]])) && (l != LY_OPS || fm6_sel())) {
            h = l;
            break;
        }
    if (h == LY_SONG && (op_armed() || song_on_pat_row()))
        h = LY_PLAY;                                    /* (SAVE over a question: its YES; on PATTERNS: a slot's store) */
    if (h != lay.held) {
        if (lay.held == LY_SONG) {
#if FELUCCA_QCHAIN
            chain_release();                            /* SAVE let go: the scene taps of the hold */
#endif
        }
        if (h != LY_PLAY) {
            lay.t0 = fm1_ms;
            lay.used = 0;
        } else if (lay_sess) {
            undo_end(lay_sess);                         /* (EDIT's knobs: one undo level for the hold) */
            lay_sess = 0;
        }
        if (lay.held == LY_OPS && h == LY_PLAY && lay.lock == LY_PLAY)
            fm6_lay_end();                              /* ENV let go after use: SOUND's FM6 rows (op_fm6.c) */
        if (h == LY_PLAY)
            lay.quiet = 0;
        lay.held = (uint8_t)h;
    }
    punch.hold = (uint8_t)lay_is(LY_FX);
    l = lay_now();
    if (l == LY_PLAY || (lay.lock == LY_PLAY && (lay.quiet || (!lay.used && fm1_ms - lay.t0 < LAY_SHOW_MS))))
        l = LY_PLAY;
    if (l != lay.shown) {
        lay.shown = (uint8_t)l;
        lay.hot = 0;
        ui.force = 1;
    }
    ui.layer = lay.shown;
    if (lay.song_go && !song.playing && !transport_req) {
        lay.song_go = 0;
        transport_req = 1;                              /* (SAVE + PLAY while playing: the song from its start) */
    }
}
