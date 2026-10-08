/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG (docs/UI-OPTIMIST-DESIGN.md section 4.5, phase 4): scenes, patterns, the chain, from the mixer's SONG row.
 *   SCENE A .. P  T1 T2 T3 DR: the pattern each track plays in it ("3", "KEEP", "--"; a plain section "SEC");
 *                 PRESETS edits the hot cell's reference (stopped; written once it rests, pat.c pat_scene_ref_set);
 *                 YES launches it at the next bar (stopped: loads it); REC stores the loop into it (asked over a
 *                 used one); HOME + REC clears it (asked). The playing scene green, a queued one amber, "*": its
 *                 tracks changed since it was stored
 *   PATTERNS      T1 T2 T3 DR: each track's playing pattern, a turn cues its next / previous stored one; the panel
 *                 is the 4 x 16 session grid; while the cursor is here the keys launch the selected track's
 *                 patterns (the LFO layer's keys: seq.c ly_lock LY_PAT), SAVE held + key n stores the working copy
 *                 into slot n, HOME held + key n clears it (asked); REC: the working copy into the first free slot
 *   MODE          LOOP / SONG, SONG REC, the MEM gauge, SAVE (the chain into flash; also when SONG is left)
 *   PART 1 .. 64  the chain: SCENE, BARS, INSERT (a copy after it), DELETE; edited stopped; the part playing white
 * MODE comes before the parts so that it is not 64 rows down. Without PATTERNS a scene row says STORED / EMPTY. */
#if FELUCCA_PATTERNS
static int pat_scene_ref_set(uint32_t s, uint32_t k, uint32_t v);   /* pat.c (FELUCCA_UI == 1) */
#endif
#if SEC_LOGGED
static void prj_mem(cell_t *c, uint32_t k);            /* op_project.c: the MEM gauge */
#endif
static void song_yes_mode(uint32_t k, int32_t s);
enum { SG_SCENE, SG_PAT, SG_MODE, SG_PART };
#define SG_SEC 0xFCu                    /* a scene row's reference: a plain section (no patterns of its own) */
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
static uint32_t song_rows(void) { return song_base_mode() + 1u + arrangement.count; }
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
static uint32_t song_ref(uint32_t s, uint32_t k)        /* scene s's reference of track k, the edit shown */
{
    if (sg.ed_s == s && sg.ed_k == k)
        return sg.ed_v;
    if ((sg.scn >> s) & 1u)
        return sg.refs[s][k];
    return (sg.used >> s) & 1u ? SG_SEC : PAT_NONE;
}
/* the reference v of track k one step on: "--", KEEP, then the stored slots in order */
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
/* once a frame: an edited reference written once it rests (or the row or the screen left); the chain saved when
 * SONG is left, stopped */
static void song_tick(void)
{
    uint32_t a;
    if (sg.ed_s != 0xFF && (fm1_ms - sg.ed_ms > 700u || ui.scr != SCR_SONG || ui.row[SCR_SONG] != sg.ed_s)) {
#if FELUCCA_PATTERNS
        char b[8];
        if (!pat_scene_ref_set(sg.ed_s, sg.ed_k, sg.ed_v)) {
            scene_tag(b, sg.ed_s);
            ui_say(b, " CHANGED");
        }
#endif
        sg.ed_s = 0xFF;
        song_refresh(1);
    }
    if (sg.dirty && ui.scr != SCR_SONG && !song.playing && !transport_req) {
        sg.dirty = 0;
        arrangement_save();
    }
    if (ui.scr == SCR_SONG && song_kind(ui.row[SCR_SONG], &a) == SG_PART && a >= arrangement.count)
        ui.row[SCR_SONG] = (uint8_t)(song_rows() - 1u);
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
        str_cpy(c->val, v == PAT_KEEP ? "KEEP" : v == SG_SEC ? "SEC" : "--", sizeof c->val);
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
        str_cpy(c->val + str_len(c->val), ">", sizeof c->val - str_len(c->val));
        if (pat_req[k] < PAT_N)
            fmt_int(c->val + str_len(c->val), pat_req[k] + 1);
        else
            str_cpy(c->val + str_len(c->val), "-", sizeof c->val - str_len(c->val));
    }
}
#endif
static void song_cell(uint32_t r, uint32_t k, cell_t *c)
{
    uint32_t a, kind = song_kind(r, &a);
    cell_clear(c);
    song_refresh(0);
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
            c->label = "REC";               /* (SONG REC: "SONG REC" is wider than a card) */
            c->kind = CK_VAL;
            str_cpy(c->val, srec == 2u ? "REC" : srec ? "ARMED" : "OFF", sizeof c->val);
            c->col = srec ? C_ERR : 0;
            cell_gauge(c, 1, 0, 2, srec);
        } else if (k == 2u) {
#if SEC_LOGGED
            prj_mem(c, 0);                              /* (op_project.c: the MEM gauge) */
#endif
        } else {
            c->label = "SAVE";
            c->kind = CK_ACT;
        }
        return;
    case SG_PART: {
        const arr_entry_t *e = &arrangement.entry[a % ARR_STEPS];
        if (k == 0u) {
            c->label = "SCENE";
            c->kind = CK_VAL;
            c->val[0] = (char)('A' + e->scene % 16u);
            c->col = (sg.used >> e->scene) & 1u ? 0 : C_DIM;   /* (dim: nothing stored there) */
            cell_gauge(c, 1, 0, (int32_t)ARR_SCENES - 1, e->scene);
        } else if (k == 1u) {
            c->label = "BARS";
            c->kind = CK_VAL;
            fmt_int(c->val, e->bars);
            cell_gauge(c, 0, 0, 64, e->bars);
        } else {
            c->label = k == 2u ? "INSERT" : "DELETE";
            c->kind = CK_ACT;
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
        if (song.playing) {
            ui_message("STOP FIRST");
            return;
        }
        if (sg.ed_s != 0xFF && (sg.ed_s != a || sg.ed_k != k))
            song_tick();                                /* (another cell: the edit before written first) */
        sg.ed_v = (uint8_t)song_ref_step(k, song_ref(a, k), s);
        sg.ed_s = (uint8_t)a, sg.ed_k = (uint8_t)k, sg.ed_ms = fm1_ms;
        return;
    case SG_PAT:
        pat_knob(k, s);                                 /* track k's next / previous stored pattern */
        return;
#endif
    case SG_MODE:
        if (k < 2u)
            song_yes_mode(k, s);
        return;
    case SG_PART: {
        arr_entry_t *e = &arrangement.entry[a % ARR_STEPS];
        if (k > 1u)
            return;
        if (song.playing) {
            ui_message("STOP FIRST");
            return;
        }
        if (k == 0u)
            e->scene = (uint8_t)clamp(e->scene + (s > 0 ? 1 : -1), 0, (int32_t)ARR_SCENES - 1);
        else
            e->bars = (uint8_t)clamp(e->bars + s, 1, 64);
        sg.dirty = 1;
        return;
    }
    default:
        return;
    }
}
/* MODE: LOOP / SONG (k 0), SONG REC (k 1); s: a turn's way, 0 YES (toggles) */
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
static void song_part_edit(uint32_t a, uint32_t ins)    /* INSERT (a copy after part a) or DELETE part a */
{
    uint32_t i, n = arrangement.count;
    if (song.playing) {
        ui_message("STOP FIRST");
        return;
    }
    if (ins) {
        if (n >= ARR_STEPS) {
            ui_message("SONG FULL");
            return;
        }
        for (i = n; i > a + 1u; i--)
            arrangement.entry[i] = arrangement.entry[i - 1u];
        arrangement.entry[a + 1u] = arrangement.entry[a];
        arrangement.count = (uint8_t)(n + 1u);
        ui.row[SCR_SONG]++;                             /* (the cursor on the new part) */
    } else {
        if (n <= 1u) {
            ui_message("ONE PART LEFT");
            return;
        }
        for (i = a; i + 1u < n; i++)
            arrangement.entry[i] = arrangement.entry[i + 1u];
        arrangement.count = (uint8_t)(n - 1u);
    }
    sg.dirty = 1;
    ui.force = 1;
}
static int song_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t a, kind = song_kind(r, &a);
    (void)ok;
    switch (kind) {
    case SG_SCENE:
        scene_launch(a);                                /* playing: the next bar; stopped: loaded */
        song_refresh(1);
        return 1;
    case SG_MODE:
        if (k < 2u)
            song_yes_mode(k, 0);
        else if (k == 3u) {
            sg.dirty = 0;
            arrangement_save();
        }
        return k != 2u;
    case SG_PART:
        if (k < 2u)
            return 0;
        song_part_edit(a, k == 2u);
        return 1;
    default:
        return 0;
    }
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
#if FELUCCA_PATTERNS
    if (kind == SG_PAT && !home) {
        uint32_t t = song.sel % NTRK, s = pat_free(t);
        char nm[8];
        pat_name(nm, t, s);
        if (s >= PAT_N)
            ui_message("NO FREE PATTERN");
        else if (!pat_store_slot(t, s))
            ui_say("DUPLICATE ", nm);
        return 1;
    }
#endif
    return 0;
}
