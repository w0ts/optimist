/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG (docs/UI-OPTIMIST-DESIGN.md 4.5 SONG): scenes, the chain of parts, the settings. Entered by SAVE held alone on PROJECT
 * (op_input.c save_hold -> song_shortcut), SEQ held off STEP or SHIFT2 + SEQ; SAVE held alone on SONG goes back to PROJECT.
 * Three views, SEQ tapped cycles them: SCENES and PARTS are tables drawn and driven by op_songtbl.c (KNOB 1 the cursor, ALGORITHM
 * and KNOB 3 the lane, KNOB 4 the value, KNOB 2 moves a part), SETTINGS the MODE row's four lines. This file keeps the rows,
 * cells and turns underneath, the keys (scenes white, parts black), the holds and the chain edits:
 *   SCENE A .. P  T1 T2 T3 DR: the pattern each track plays in it ("3", "KEEP", "-" none; a plain section "SEC"), and its BARS
 *                 (AUTO or 1..64); the value written once it rests (pat.c pat_scene_ref_set); YES launches it (playing: when
 *                 the scene playing has played its bars, stopped: loads it); REC stores the loop into it; SHIFT + REC clears it
 *   PATTERNS      the hidden row LFO's screen shows (op_layers.c lay_pat_peek): the 4 x 16 session grid, the keys launch patterns
 *   MODE          LOOP / SONG, SONG REC (an action card: YES arms it), the MEM gauge, SAVE (the chain into flash)
 *   PART 1 .. 64  the chain: SCENE, BARS (S its scene's, A its longest pattern, 1..64), INSERT (a copy after it); the part playing white
 * MODE comes before the parts so that it is not 64 rows down. Without PATTERNS a scene row says STORED / EMPTY. */
#if FELUCCA_PATTERNS
static int pat_scene_ref_set(uint32_t s, uint32_t k, uint32_t v);   /* pat.c */
#endif
#if SEC_LOGGED
static void prj_mem(cell_t *c, uint32_t k);            /* op_project.c: the MEM gauge */
#endif
static void song_yes_mode(uint32_t k, int32_t s);
static void op_row_pick(uint32_t r);                    /* op_input.c */
enum { SG_SCENE, SG_PAT, SG_MODE, SG_PART };
#define SG_WIN 8u                       /* the parts window of the keys: eight parts (the user, 2026-10-20: banks of 8) */
static uint8_t sg_page;                 /* ... parts page * 10 + 1 .. + 10 */
static uint32_t key_w(int32_t w);       /* op_keys.c */
static void song_key_hold(void);
static void song_part_cut_ask(uint32_t a);
static void song_pat_hold(void);
static int song_pat_ctx(void);
static void song_keys_released(void);
static uint32_t song_view_of(uint32_t kind);
#define SG_SEC 0xFCu                    /* a scene row's reference: a plain section (no patterns of its own) */
#define SG_CYCLE_KEY 25u                /* the ACCENT lane's key, drums.c: black 11 (SETTINGS and back) */
static uint8_t sg_vrow[3] = {0xFF, 0xFF, 0xFF};         /* each view's last cursor row (the SEQ tap's way back) */
static uint32_t sg_kt0[27];                             /* each key's press (fm1_ms) */
static uint32_t sg_know;                                /* the scene and part keys pressed with SHIFT2: launched as soon as possible */
static uint32_t sg_kfire;                               /* the keys pressed here that fire when let go */
static uint32_t sg_karm;                                /* the keys pressed here and alone so far (a combination or an ask clears it) */
static struct {
    uint8_t dirty;                      /* the chain changed: saved when SONG is left, stopped */
    uint8_t ed_s, ed_k, ed_v;           /* a reference being edited (ed_s 0xFF: none), written once it rests */
    uint32_t ed_ms;
    uint8_t refs[16][NTRK];             /* the scenes' references, read twice a second */
    uint16_t scn, used;                 /* which are scenes, which are stored */
    uint32_t t;
} sg = {.ed_s = 0xFF};

static uint32_t song_nscn(void) { return LAY_NSCN; }
static uint32_t song_base_mode(void) { return song_nscn() + (FELUCCA_PATTERNS ? 1u : 0u); }
static uint32_t song_pat_row_idx(void) { return song_base_mode() - 1u; }   /* the PATTERNS row: LFO's screen (PATTERNS builds) */
static uint32_t song_rows(void) { return song_base_mode() + 1u + arrangement.count; }
static uint32_t song_row_of_part(uint32_t a) { return song_base_mode() + 1u + a; }
static uint32_t song_kind(uint32_t r, uint32_t *a)      /* row r: its kind and its scene / part */
{
    if (r < song_nscn()) {
        *a = r;
        return SG_SCENE;
    }
    *a = r - song_base_mode() - 1u;
    return r < song_base_mode() ? SG_PAT : r == song_base_mode() ? SG_MODE : SG_PART;
}
static int song_on_pat_row(void)
{
    uint32_t a;
    return ui.scr == SCR_SONG && song_kind(ui.row[SCR_SONG], &a) == SG_PAT;
}
static void song_refresh(int now)                       /* the scenes' references and stored marks */
{
    uint32_t s;
    if (!now && sg.t && fm1_ms - sg.t < 500u)
        return;
    sg.t = fm1_ms | 1u;
    sg.scn = sg.used = 0;
    for (s = 0; s < song_nscn(); s++) {
        sg.used |= (uint16_t)((uint32_t)project_used(s) << s);
#if FELUCCA_PATTERNS
        if (((sg.used >> s) & 1u) && pat_scene_refs(s, sg.refs[s]))
            sg.scn |= (uint16_t)(1u << s);
#endif
    }
}
static int song_scene_empty(uint32_t s)                 /* nothing stored, or every track "-" (the user, 2026-10-20) */
{
    if (!((sg.used >> s) & 1u))
        return 1;
#if FELUCCA_PATTERNS
    if ((sg.scn >> s) & 1u) {
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            if (sg.refs[s][k] != PAT_NONE)
                return 0;
        return 1;
    }
#endif
    return 0;
}
static uint32_t song_scenes_mask(void)                  /* the scenes with something in them, as a bit a scene (the keys' LEDs) */
{
    uint32_t i, m = 0;
    song_refresh(0);
    for (i = 0; i < song_nscn() && i < 16u; i++)
        if (!song_scene_empty(i))
            m |= 1u << i;
    return m;
}
static uint32_t song_ref(uint32_t s, uint32_t k)        /* scene s's reference of track k, the edit shown */
{
    if (sg.ed_s == s && sg.ed_k == k)
        return sg.ed_v;
    if ((sg.scn >> s) & 1u)
        return sg.refs[s][k];
    return (sg.used >> s) & 1u ? SG_SEC : PAT_NONE;
}
/* the reference v of track k one step on: none ("-"), KEEP, then the stored slots in order */
static uint32_t song_ref_step(uint32_t k, uint32_t v, int32_t s)
{
#if FELUCCA_PATTERNS
    uint8_t L[PAT_N + 2u];
    uint32_t n = 0, i, at = 0;
    L[n++] = PAT_NONE;
    L[n++] = PAT_KEEP;
    for (i = 0; i < PAT_N; i++)
        if (pat_has(k, i))
            L[n++] = (uint8_t)i;
    for (i = 0; i < n; i++)
        if (L[i] == v)
            at = i;
    return L[clamp((int32_t)at + (s > 0 ? 1 : -1), 0, (int32_t)n - 1)];
#else
    (void)k, (void)s;
    return v;
#endif
}
static void song_ref_flush(void)                        /* the edited reference written now (a rest, another cell, the row or screen left) */
{
    if (sg.ed_s == 0xFF)
        return;
#if FELUCCA_PATTERNS
    {
        char b[8];
        if (!pat_scene_ref_set(sg.ed_s, sg.ed_k, sg.ed_v)) {
            scene_tag(b, sg.ed_s);
            ui_say(b, " CHANGED");
        }
    }
#endif
    sg.ed_s = 0xFF;
    song_refresh(1);
}
/* once a frame: an edited reference written once it rests (or the row or the screen left); the chain saved when
 * SONG is left, stopped; the keys' holds and releases; each view's row remembered */
static void song_tick(void)
{
    uint32_t a;
    if (sg.ed_s != 0xFF && (fm1_ms - sg.ed_ms > 700u || ui.scr != SCR_SONG || ui.row[SCR_SONG] != sg.ed_s))
        song_ref_flush();
    if (sg.dirty && ui.scr != SCR_SONG && !song.playing && !transport_req) {
        sg.dirty = 0;
        arrangement_save();
    }
    if (ui.scr == SCR_SONG && song_kind(ui.row[SCR_SONG], &a) == SG_PART && a >= arrangement.count)
        ui.row[SCR_SONG] = (uint8_t)(song_rows() - 1u);
    if (ui.scr == SCR_SONG && song_kind(ui.row[SCR_SONG], &a) == SG_PART)
        sg_page = (uint8_t)(a / SG_WIN);              /* (SELECT on a part: the window follows) */
    if (ui.scr == SCR_SONG) {
        uint32_t ra;
        sg_vrow[song_view_of(song_kind(ui.row[SCR_SONG], &ra))] = ui.row[SCR_SONG];
    }
    song_keys_released();
    if (ui.scr == SCR_SONG && !song_on_pat_row() && !op_armed())
        song_key_hold();
    else if (song_pat_ctx() && !op_armed())
        song_pat_hold();
}

static void song_name(uint32_t r, char *b)
{
    uint32_t a, kind = song_kind(r, &a);
    if (kind == SG_SCENE) {
        scene_tag(b, a);
#if FELUCCA_PATTERNS
        if (a == (uint32_t)live_sec && pat_scene_dirty(a))
            str_cpy(b + 7, "*", 2);                     /* "SCENE B*": its tracks changed since */
#endif
    } else if (kind == SG_PART) {
        str_cpy(b, "PART ", 12);
        fmt_int(b + 5, (int32_t)a + 1);
    } else {
        str_cpy(b, kind == SG_PAT ? "PATTERNS" : "MODE", 12);
    }
}
/* a row's state colour (op_draw.c draw_row: a mark before its cells): the scene playing green, queued amber; the
 * part playing white */
static uint16_t song_row_col(uint32_t r)
{
    uint32_t a, kind = song_kind(r, &a);
    if (kind == SG_SCENE)
        return live_req == (int8_t)a ? C_WARN : live_sec == (int8_t)a && song.playing && !arrangement_clock.running ? C_OK : 0;
    if (kind == SG_PART)
        return arrangement_clock.running && arrangement_clock.index == a ? C_WHITE : 0;
    return 0;
}
static void song_slot_cell(cell_t *c, uint32_t v)       /* a pattern reference as a cell */
{
    if (v < 16u) {
        fmt_int(c->val, (int32_t)v + 1);
        cell_gauge(c, 1, 0, 15, (int32_t)v);
    } else {
        str_cpy(c->val, v == PAT_KEEP ? "KEEP" : v == SG_SEC ? "SEC" : "-", sizeof c->val);   /* (none: "-", audit #7) */
    }
}
#if FELUCCA_PATTERNS
static void song_pat_cell(uint32_t k, cell_t *c)        /* track k's playing pattern, "3>5" when one is queued */
{
    cell_clear(c);
    c->label = trk_tag(k);
    c->kind = CK_VAL;
    c->col = trk_col(k);
    song_slot_cell(c, pat_cur[k] < PAT_N ? pat_cur[k] : PAT_NONE);
    if (pat_req[k] != PAT_NONE) {
        str_app(c->val, ">", sizeof c->val);
        if (pat_req[k] < PAT_N)
            fmt_app(c->val, pat_req[k] + 1);
        else
            str_app(c->val, "-", sizeof c->val);
    }
}
#endif
static void song_cell(uint32_t r, uint32_t k, cell_t *c)
{
    uint32_t a, kind = song_kind(r, &a);
    cell_clear(c);
    song_refresh(0);
    if (k == 4u) {                                      /* a scene's 5th cell: its BARS, AUTO .. 64, purple (the SCENES table's BARS lane) */
        char b[8];
        if (kind != SG_SCENE)
            return;
        song_scene_bars_text(a % 16u, b);
        c->label = "BARS";
        c->kind = CK_RO;
        c->col = 0xB31Fu;
        str_cpy(c->val, b, sizeof c->val);
        cell_gauge(c, 0, 0, 64, scene_bars[a % 16u]);
        return;
    }
    switch (kind) {
    case SG_SCENE:
#if FELUCCA_PATTERNS
        c->label = trk_tag(k);
        c->kind = (sg.scn >> a) & 1u ? CK_VAL : CK_RO;
        song_slot_cell(c, song_ref(a, k));
        c->col = sg.ed_s == a && sg.ed_k == k ? C_WARN : trk_col(k);   /* (amber: not written yet) */
#else
        if (k)
            return;
        c->label = "SCENE";
        c->kind = CK_RO;
        str_cpy(c->val, (sg.used >> a) & 1u ? "STORED" : "EMPTY", sizeof c->val);
        c->dim = (uint8_t)!((sg.used >> a) & 1u);      /* (an empty scene: EMPTY, dim, audit #7) */
        cell_gauge(c, 1, 0, 1, (int32_t)((sg.used >> a) & 1u));
#endif
        return;
#if FELUCCA_PATTERNS
    case SG_PAT:
        song_pat_cell(k, c);
        return;
#endif
    case SG_MODE:
        if (k == 0u) {
            c->label = "MODE";
            c->kind = CK_VAL;
            str_cpy(c->val, arrangement_enabled ? "SONG" : "LOOP", sizeof c->val);
            cell_gauge(c, 1, 0, 1, arrangement_enabled);
        } else if (k == 1u) {
            cell_act(c, "SONG", "REC", 0, 0);           /* an action card: a turn only picks it, YES arms (asks over
                                                         * a song: the modal is the red one) or stops (the user, 2026-10-13);
                                                         * not destructive itself: never red (style guide 7) */
            c->col = srec_col();
        } else if (k == 2u) {
#if SEC_LOGGED
            prj_mem(c, 0);                              /* (op_project.c: the MEM gauge) */
#endif
        } else {
            cell_act(c, "SONG", "SAVE", 0, 0);
        }
        return;
    case SG_PART: {
        const arr_entry_t *e = &arrangement.entry[a % ARR_STEPS];
        if (k == 0u) {
            c->label = "SCENE";
            c->kind = CK_VAL;
            c->val[0] = (char)('A' + e->scene % 16u);
            if (song_scene_empty(e->scene % 16u)) {         /* (nothing there, or T1..DR all "-": grey and struck through, as a muted name) */
                c->col = C_DIM;
                c->dim = 1;
                c->strike = 1;
            }
            cell_gauge(c, 1, 0, (int32_t)ARR_SCENES - 1, e->scene);
        } else if (k == 1u) {
            c->label = "BARS";
            c->kind = CK_VAL;
            if (e->bars == ARR_BARS_A)
                str_cpy(c->val, "AUTO", sizeof c->val);   /* (A: the scene's longest pattern, whatever its own BARS says) */
            else if (e->bars)
                fmt_int(c->val, e->bars);
            else
                str_cpy(c->val, "SCENE", sizeof c->val);  /* (S: follows its scene's BARS: AUTO, its longest pattern, or the number set on the scene) */
            cell_gauge(c, 0, 0, 65, e->bars == ARR_BARS_A ? 65 : e->bars);
        } else if (k == 2u) {
            cell_act(c, "PART", "INSERT", 0, 0);        /* (DELETE is the part key held, or SHIFT + it: with what follows) */
        }
        return;
    }
    default:
        return;
    }
}
static void song_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    uint32_t a, kind = song_kind(r, &a);
    (void)fine;
    if (s == OP_RESET)
        return;
    switch (kind) {
#if FELUCCA_PATTERNS
    case SG_SCENE:
        if (!((sg.scn >> a) & 1u))
            return;
        if (sg.ed_s != 0xFF && (sg.ed_s != a || sg.ed_k != k))
            song_ref_flush();                           /* (another cell: the edit before written first, not dropped) */
        sg.ed_v = (uint8_t)song_ref_step(k, song_ref(a, k), s);
        sg.ed_s = (uint8_t)a, sg.ed_k = (uint8_t)k, sg.ed_ms = fm1_ms;
        return;
    case SG_PAT:
        if (k < NTRK && song.sel != k)
            track_select(k);                            /* (the knob's track becomes the selected one: the keys launch its patterns) */
        pat_knob(k, s);                                 /* track k's next / previous stored pattern */
        return;
#endif
    case SG_MODE:
        if (k < 1u)
            song_yes_mode(k, s);                        /* (REC, k 1: an action card, a turn only picks it) */
        return;
    case SG_PART: {
        arr_entry_t *e = &arrangement.entry[a % ARR_STEPS];
        if (k > 1u)
            return;
        if (k == 0u)                                    /* (playing too: the chain is RAM, read by the clock at each bar; saved once stopped) */
            e->scene = (uint8_t)clamp(e->scene + (s > 0 ? 1 : -1), 0, (int32_t)ARR_SCENES - 1);
        else {
            int32_t at = clamp((e->bars == ARR_BARS_A ? 65 : (int32_t)e->bars) + s, 0, 65);   /* S, 1..64, A in turn */
            e->bars = at == 65 ? (uint8_t)ARR_BARS_A : (uint8_t)at;
        }
        sg.dirty = 1;
        return;
    }
    default:
        return;
    }
}
/* MODE: LOOP / SONG (k 0, a turn or YES), SONG REC (k 1: YES only, an action card); s: a turn's way, 0 YES (toggles) */
static void song_yes_mode(uint32_t k, int32_t s)
{
    if (k == 1u) {
        if (!s || (s > 0) != (srec != 0u))
            song_rec_toggle();
        return;
    }
    if (srec) {
        ui_message("SONG REC IS ON");
        return;
    }
    arrangement_enabled = (uint8_t)(s ? s > 0 : !arrangement_enabled);
    ui_message(arrangement_enabled ? "SONG MODE" : "LOOP MODE");
}
static void song_part_insert(uint32_t a)                /* a copy of part a after it (playing too: the chain is RAM) */
{
    uint32_t n = arrangement.count;
    if (n >= ARR_STEPS) {
        ui_message("SONG FULL");
        return;
    }
    fm1_irq_off();                                      /* (the clock reads the chain in the audio ISR) */
    memmove(&arrangement.entry[a + 1u], &arrangement.entry[a], (n > a ? n - a : 1u) * sizeof arrangement.entry[0]);   /* (part a twice) */
    arrangement.count = (uint8_t)(n + 1u);
    if (arrangement_clock.running && arrangement_clock.index > a)
        arrangement_clock.index++;                      /* (the part playing moved down with its place) */
    fm1_irq_on();
    ui.row[SCR_SONG]++;                                 /* (the cursor on the new part) */
    sg.dirty = 1;
    ui.force = 1;
}
static int song_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t a, kind = song_kind(r, &a);
    switch (kind) {
    case SG_SCENE:
        scene_launch(a);                                /* playing: the next bar; stopped: loaded */
        song_refresh(1);
        return 1;
#if FELUCCA_PATTERNS
    case SG_PAT: {                                      /* SAVE on the PATTERNS row: the selected track's pattern stored, in its own slot or the first free (asked) */
        uint32_t t = song.sel % NTRK, sl = pat_cur[t] < PAT_N ? (uint32_t)pat_cur[t] : pat_free(t);
        if (sl >= PAT_N)
            ui_message("NO FREE PATTERN");
        else
            pat_store_ask(t, sl);
        return 1;
    }
#endif
    case SG_MODE:
        if (k < 2u)
            song_yes_mode(k, 0);
        else if (k == 3u && !ok)
            op_arm(SCR_SONG, r, k, "SAVE", "SONG", 0);
        else if (k == 3u) {
            sg.dirty = 0;
            arrangement_save();
        }
        return k != 2u;
    case SG_PART:
        if (k >= 3u)
            return 0;                                   /* (the table's cells are lanes 0 and 1; SAVE on a part inserts a copy: INSERT, asked) */
        if (!ok) {                                      /* INSERT asks first (the user, 2026-10-13) */
            char b[10];
            str_cpy(b, "PART ", sizeof b);
            fmt_int(b + 5, (int32_t)a + 1);
            op_arm(SCR_SONG, r, k, "INSERT", b, 0);
            return 1;
        }
        song_part_insert(a);
        return 1;
    default:
        return 0;
    }
}
static uint32_t song_white_of(uint32_t k)               /* key k's white index 0..15, 0xFF a black key */
{
    uint32_t w;
    for (w = 0; w < 16u; w++)
        if (white_key(w) == k)
            return w;
    return 0xFFu;
}
static uint32_t song_pages(void) { return (arrangement.count + SG_WIN - 1u) / SG_WIN; }
static void song_part_jump(uint32_t a, int now)         /* a part key: the song goes on from part a at the next bar, or (now: SHIFT2) at once */
{
    char b[12];
    uint32_t scene = arrangement.entry[a].scene;
    if (!arrangement_clock.running) {
        scene_launch_at(scene, now);                    /* (not playing: the part's scene as a scene key: loaded) */
        song_refresh(1);
        return;
    }
#if SEC_LOGGED
    if (!section_cue(scene)) {                          /* (the scene staged for the ISR; live_req keeps the stage until it is played) */
        ui_say("EMPTY ", "PART");
        return;
    }
#endif
    fm1_irq_off();                                      /* (the clock is the audio ISR's: arr_next takes the jump at the bar, or at once) */
    arrangement_clock.jump_now = (uint8_t)(now != 0);
    arrangement_clock.jump = (uint8_t)(a + 1u);
    fm1_irq_on();
    str_cpy(b, "PART ", sizeof b);
    fmt_int(b + 5, (int32_t)a + 1);
    ui_say(b, now ? " NOW" : " NEXT BAR");
}
/* a part key held, a scene key pressed (the user, 2026-10-20): the scene is that part's, the part made when the chain is
 * shorter (the parts between it and the end too: this scene, S bars). Playing too: the chain is RAM, saved once stopped */
static void song_part_assign(uint32_t a, uint32_t scene)
{
    char b[16];
    uint32_t i, n = arrangement.count;
    if (a >= ARR_STEPS)
        return;
    fm1_irq_off();                                      /* (playing too: the clock reads the chain in the audio ISR) */
    for (i = n; i <= a; i++) {
        arrangement.entry[i].scene = (uint8_t)scene;
        arrangement.entry[i].bars = 0;                  /* (a new part: S, its scene's BARS) */
    }
    if (a >= n)
        arrangement.count = (uint8_t)(a + 1u);
    arrangement.entry[a].scene = (uint8_t)scene;
    fm1_irq_on();
    sg.dirty = 1;
    op_row_pick(song_row_of_part(a));
    str_cpy(b, "PART ", sizeof b);
    fmt_int(b + 5, (int32_t)a + 1);
    ui_say(b, a >= n ? " MADE" : " SET");
    ui.force = 1;
}
static uint32_t song_view_of(uint32_t kind) { return kind == SG_PART ? 1u : kind == SG_MODE ? 2u : 0u; }
static void song_view_cycle(void)
{
    static const char *const NAME[3] = {"SCENES", "PARTS", "SETTINGS"};
    uint32_t a, v = song_view_of(song_kind(ui.row[SCR_SONG], &a)), r;
    sg_vrow[v] = ui.row[SCR_SONG];
    v = (v + 1u) % 3u;
    r = sg_vrow[v];
    if (r == 0xFFu || r >= song_rows() || song_view_of(song_kind(r, &a)) != v || song_kind(r, &a) == SG_PAT)
        r = v == 0u ? (live_sec >= 0 && (uint32_t)live_sec < song_nscn() ? (uint32_t)live_sec : 0u)
          : v == 1u ? song_row_of_part(0)
          : song_base_mode();
    op_row_pick(r);
    ui_message(NAME[v]);
}
/* the patterns view (SONG's PATTERNS row, the LFO layer): a white key held alone past HOLD asks to clear that pattern (the
 * scenes' keys: that scene); the key itself launched on its press, as ever */
static int song_pat_ctx(void) { return (ui.scr == SCR_SONG && song_on_pat_row()) FIF(FELUCCA_PATTERNS)(|| lay_is(LY_PAT)); }
static void song_pat_arm(uint32_t notes)                /* the keys newly down while the patterns' keys are the layer's */
{
    uint32_t k, lfo = 1u << panel.btn[B_LFO];
    for (k = 0; k < 27u; k++) {
        if (!((notes >> k) & 1u))
            continue;
        sg_kt0[k] = fm1_ms;
        sg_kfire &= ~(1u << k);
        if ((fm1_in.notes & ~(1u << k)) || (fm1_in.buttons & ~lfo))
            sg_karm &= ~fm1_in.notes;
        else
            sg_karm |= 1u << k;
    }
}
static void song_pat_hold(void)
{
#if FELUCCA_PATTERNS
    uint32_t k, w;
    for (k = 0; k < 27u; k++) {
        if (!((fm1_in.notes >> k) & 1u) || !((sg_karm >> k) & 1u) || fm1_ms - sg_kt0[k] < op_hold_ms())
            continue;
        sg_karm &= ~(1u << k);
        w = song_white_of(k);
        if (op_armed() || w >= 16u)
            return;
        if (lay.pmod == 10u && w < LAY_NSCN)
            scene_clear_ask(w);
        else if (lay.pmod != 10u && w < PAT_N)
            pat_clear_ask(song.sel % NTRK, w);
    }
#endif
}
/* a scene's BARS (the user, 2026-10-21): AUTO (its longest pattern) .. 64, the SCENES table's BARS lane (K4); the part that
 * follows its scene and a hand launch wait for it. Saved with the song once stopped */
static void song_scene_bars_text(uint32_t sc, char *b)  /* "AUTO" or "8" */
{
    if (scene_bars[sc % 16u])
        fmt_int(b, scene_bars[sc % 16u]);
    else
        str_cpy(b, "AUTO", 5);
}
static void song_scene_bars_turn(uint32_t sc, int32_t s)
{
    char b[8], m[24];
    sc %= 16u;
    scene_bars[sc] = (uint8_t)clamp((int32_t)scene_bars[sc] + s, 0, 64);
    sg.dirty = 1;
    song_scene_bars_text(sc, b);
    scene_tag(m, sc);                                   /* ("SCENE C") */
    str_app(m, ": ", sizeof m);
    str_app(m, b, sizeof m);
    if (scene_bars[sc])
        str_app(m, " BARS", sizeof m);
    ui_message(m);
}
static void song_part_cut(uint32_t a)                   /* part a and every part after it gone (a confirmed question) */
{
    if (a == 0u || a >= arrangement.count)
        return;
    fm1_irq_off();                                      /* (playing too: a part playing past the end goes on to its bar's end, then the song loops or ends) */
    arrangement.count = (uint8_t)a;
    fm1_irq_on();
    sg.dirty = 1;
    op_row_pick(song_row_of_part(a - 1u));
    ui_message("PARTS CUT");
    ui.force = 1;
}
static void song_part_cut_ask(uint32_t a)               /* part a and what follows: asked (part 1 stays) */
{
    char nm[17];
    if (a == 0u) {
        ui_message("ONE PART LEFT");
        return;
    }
    op_row_pick(song_row_of_part(a));
    str_cpy(nm, "PART ", sizeof nm);
    fmt_int(nm + 5, (int32_t)a + 1);
    str_app(nm, " + AFTER", sizeof nm);
    op_arm(ARM_OP, OA_PART_CUT, a, "DELETE", nm, 1);
}
static void song_key_hold(void)
{
    uint32_t k, w, b, a;
    for (k = 0; k < 27u; k++) {
        if (!((fm1_in.notes >> k) & 1u) || !((sg_karm >> k) & 1u) || fm1_ms - sg_kt0[k] < op_hold_ms())
            continue;
        sg_karm &= ~(1u << k);
        w = song_white_of(k);
        b = lane_of_key(k);
        if (op_armed())
            return;
        if (w < song_nscn()) {
            op_row_pick(w);
            scene_clear_ask(w);                         /* (a scene with nothing in it says so) */
        } else if (b < SG_WIN) {
            a = (uint32_t)sg_page * SG_WIN + b;
            if (a < arrangement.count)
                song_part_cut_ask(a);
        }
    }
}
static void song_key(uint32_t k)                        /* a key pressed: the cursor, a combination's job; a lone tap fires on its release */
{
    uint32_t w = song_white_of(k), b = lane_of_key(k), a;
    if (w < 16u) {
        uint32_t held = fm1_in.notes & ~(1u << k), i;
        if (w >= song_nscn() || song_on_pat_row())
            return;
        if (btn_held(B_SAVE) || btn_held(B_HOME) || btn_held(B_OCTDN)) {   /* SAVE + a scene key stores the loop into it, HOME or SHIFT + clears it (both asked) */
            op_row_pick(w);
            if (btn_held(B_SAVE))
                scene_store(w);
            else
                scene_clear_ask(w);
            song_refresh(1);
            return;
        }
        for (i = 0; i < SG_WIN; i++)
            if ((held >> key_of_lane(i)) & 1u) {        /* a part key down: this scene is that part's */
                song_part_assign((uint32_t)sg_page * SG_WIN + i, w);
                return;
            }
        op_row_pick(w);
    } else if (k == SG_CYCLE_KEY) {                     /* the settings and back: where the cursor was on the scenes */
        if (song_kind(ui.row[SCR_SONG], &a) == SG_MODE)
            op_row_pick(sg_vrow[0] != 0xFFu && sg_vrow[0] < song_rows() ? sg_vrow[0] : 0u);
        else
            op_row_pick(song_base_mode());
    } else if (b < SG_WIN && !song_on_pat_row()) {
        a = (uint32_t)sg_page * SG_WIN + b;
        if (a < arrangement.count) {
            if (btn_held(B_OCTDN))
                song_part_cut_ask(a);                   /* SHIFT + a part key: it and what follows, asked */
            else
                op_row_pick(song_row_of_part(a));
        }
    }
}
/* a lone tap let go (the key not used in a combination, not held past HOLD, SHIFT2 not down at the press): the scene
 * launched (playing: the next bar, stopped: loaded) or the song jumped to the part */
static uint32_t sg_kprev;                               /* the keys down in the frame before */
static void song_key_fire(uint32_t k)
{
    uint32_t w = song_white_of(k), b = lane_of_key(k), a;
    if (w < song_nscn()) {
        scene_launch_at(w, (sg_know >> k) & 1u);        /* (as YES on the row; SHIFT2 held at the press: as soon as possible) */
        song_refresh(1);
    } else if (b < SG_WIN) {
        a = (uint32_t)sg_page * SG_WIN + b;
        if (a < arrangement.count)
            song_part_jump(a, (sg_know >> k) & 1u);
    }
}
static void song_keys_released(void)
{
    uint32_t k, rel = sg_kprev & ~fm1_in.notes;
    for (k = 0; k < 27u; k++)
        if (((rel >> k) & 1u) && ((sg_karm >> k) & 1u) && ((sg_kfire >> k) & 1u)) {
            if (ui.scr == SCR_SONG && !song_on_pat_row() && !op_armed())
                song_key_fire(k);
            sg_karm &= ~(1u << k);
        }
    sg_kprev = fm1_in.notes;
}
static void song_keys(uint32_t notes)                   /* the keys newly down, a bit a key */
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if ((notes >> k) & 1u) {
            sg_kt0[k] = fm1_ms;
            if ((fm1_in.notes & ~(1u << k)) || (fm1_in.buttons & ~(1u << panel.btn[B_OCTUP])))
                sg_karm &= ~fm1_in.notes;               /* (another key or a button down, SHIFT2 aside: a combination, no hold) */
            else
                sg_karm |= 1u << k;
            sg_kfire |= 1u << k;
            if (op_shift2(fm1_in.buttons))
                sg_know |= 1u << k;                     /* (SHIFT2 held at the press, a scene or a part key: as soon as possible) */
            else
                sg_know &= ~(1u << k);
            song_key(k);
        }
}
static void song_page_tap(int up)                       /* OCT+ / OCT- on SONG: the parts window by eight */
{
    uint32_t last = song_pages() ? song_pages() - 1u : 0u, a, kind;
    int32_t p = (int32_t)sg_page + (up ? 1 : -1);
    char b[16];
    if (p < 0 || (uint32_t)p > last)
        return;
    sg_page = (uint8_t)p;
    kind = song_kind(ui.row[SCR_SONG], &a);
    if (kind == SG_PART) {                              /* (the cursor on a part: the same place of the new window) */
        a = (uint32_t)p * SG_WIN + a % SG_WIN;
        op_row_pick(song_row_of_part(a < arrangement.count ? a : arrangement.count - 1u));
    }
    str_cpy(b, "PARTS ", sizeof b);
    fmt_int(b + 6, p * (int32_t)SG_WIN + 1);
    ui_message(b);
}
/* the keys' LEDs: the stored scenes dim, the cursor's full; the window's parts dim, the cursor's (or the one playing,
 * blinking) full; the cycle key lit on the PATTERNS row */
static void song_keys_led(uint32_t *f, uint32_t *d, uint32_t blink)
{
    uint32_t i, a, kind = song_kind(ui.row[SCR_SONG], &a);
    song_refresh(0);
    uint32_t *lit = d;                                  /* (the keys that fire dim, the cursor's full) */
    for (i = 0; i < song_nscn() && i < 16u; i++)
        if (!song_scene_empty(i))
            *lit |= key_w((int32_t)i);
    if (kind == SG_SCENE && a < 16u)
        *f |= key_w((int32_t)a);
    for (i = 0; i < SG_WIN; i++) {
        uint32_t part = (uint32_t)sg_page * SG_WIN + i;
        if (part >= arrangement.count)
            break;
        *lit |= 1u << key_of_lane(i);
        if ((kind == SG_PART && a == part) || (blink && arrangement_clock.running && arrangement_clock.index == part))
            *f |= 1u << key_of_lane(i);
    }
    *d |= 1u << SG_CYCLE_KEY;
    if (song_on_pat_row())
        *f |= 1u << SG_CYCLE_KEY;
}
/* REC on SONG (op_input.c): a scene row stores the loop into it, the PATTERNS row duplicates; 1 taken */
static int song_rec(uint32_t home)
{
    uint32_t a, kind;
    if (ui.scr != SCR_SONG)
        return 0;
    kind = song_kind(ui.row[SCR_SONG], &a);
    if (kind == SG_SCENE) {
        if (home)
            scene_clear_ask(a);
        else
            scene_store(a);
        song_refresh(1);
        return 1;
    }
    return 0;                                           /* (the PATTERNS row: the LFO layer's keys, black 9 duplicates) */
}
/* SAVE held on PROJECT (op_input.c save_hold, the user 2026-10-16): the SONG screen, its cursor on the scene playing
 * (none: scene A); on SONG already, SELECT moves its cursor (s) */
static void song_shortcut(int32_t s)
{
    if (ui.scr == SCR_SONG) {
        op_row_pick((uint32_t)clamp((int32_t)ui.row[SCR_SONG] + s, 0, (int32_t)song_rows() - 1));
        return;
    }
    op_enter(SCR_SONG);
    ui.row[SCR_SONG] = (uint8_t)(live_sec >= 0 && (uint32_t)live_sec < song_nscn() ? (uint32_t)live_sec : 0u);
    song_refresh(1);
}
