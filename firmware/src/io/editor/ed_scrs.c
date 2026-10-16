/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the Optimist UI's screens as rows of cards (ui/optimist/op_screens.c screen_t; included by editor.c; web/EDITOR_PROTOCOL.md
 * "v13: the device's screens"; builds with the Optimist UI only: FELUCCA_UI == 1). SCRN hands GLOBAL and SYSTEM out as lines; this does it for
 * every other screen the device draws as rows of four cards: SOUND (any track's pages, a drum lane's), MASTER, PROJECT (slots, snapshots, user
 * presets, TOOLS), STEPS, SONG, TEMPO. The device builds each card in its own screen's functions (rows, name, cell, turn, yes: the table
 * SCREENS) and the editor draws what they say, turns and asks through the same ones, so it shows the same pages in the same order with the same
 * names, units, ranges and questions, and cannot drift.
 *   99 SCRS  op scr trk lane fam [row k mode v14] -> op scr row ...
 *       scr   0 MIX (one row a knob set of each mixer row: the device pages them with SELECT), 1 SOUND, 2 MASTER, 3 PROJECT, 5 STEPS, 6 SONG, 7 TEMPO, 8 SCOPE (the INFO tag 0x5C mask says which are built)
 *       trk   the track the screen is about (0..3; SOUND, STEPS, SCOPE; SCOPE's fam 1: its MASTER source), lane 0..16 a drum lane picked (the drum track: its lane pages; 127 none,
 *             DR itself), fam the page family of SOUND (127 every row, as the device opens it; 126: an FM6 track's operator rows)
 *       op 0  -                          -> 0, scr, trk, lane, fam, rows n, the screen's name
 *       op 1  row                        -> 1, scr, row, ROW
 *       op 2  row, k, mode, v14          turn card k: mode 0 steps (a knob, with acceleration), 1 fine steps, 2 the default -> 2, scr, row, ROW
 *       op 3  row, k, ok                 YES: ok 0 may ask (the card's `ask` is 1: the device's question is up), ok 1 does it -> 3, scr, row, rc, ROW
 *       STEPS: fam is the step held (0..63; 127 none): its card pages are the rows (STEP, EXTRA, the lock pages; op_step.c hp_*), lane the lane picked on the drum track
 *       ROW: the row's family (127 none), its flags (bit 0: YES on any card runs the row's operation on its item: a store's row), its name (as the SOUND screen titles it), 4 x CELL, the armed question ("" none)
 *       CELL: kind (CK_*: 0 none, 1 value, 2 read-out, 3 action, 4 enter), label, value text, unit, state, danger, gauge form (GK_*), gmin v14,
 *             gmax v14, gv v14, by-descriptor 0/1 then fmt, min v14, max v14, def v14, the value v14, flags (bit 0 dim, 1 the drum track's own value
 *             shown on a lane, 2 struck through, 3..4 the step event mark, 5 the card asks), the action's object text
 * The context is set for the one call and put back: the device's own cursor, track and page are not moved. A bad screen, track or row: no reply. */
enum { ED_SCRS = 99, SCRS_FAM_FM6 = 126 };
#if FELUCCA_UI == 1 && FELUCCA_ED_SCRS
#define SCRS_MASK ((1u << SCR_HOME) | (1u << SCR_SOUND) | (1u << SCR_FX) | (1u << SCR_PROJECT) | (1u << SCR_STEP) | (1u << SCR_SONG) | (1u << SCR_TEMPO) | (1u << SCR_SCOPE))
static struct {                                         /* what a call changed on the device's UI, to put back */
    uint8_t scr, sel, lane, lsel, fam, hot, master, row, rs, mset, held, spage, hp, lpg, cards;
    uint16_t sheld;
} scrs_was;
static int scrs_in(uint32_t s, uint32_t t, uint32_t l, uint32_t f)
{
    if (s >= SCR_N || !((SCRS_MASK >> s) & 1u) || t >= NTRK)
        return 0;
    scrs_was.scr = ui.scr, scrs_was.sel = (uint8_t)song.sel, scrs_was.lane = ui.lane, scrs_was.lsel = lane_sel;
    scrs_was.mset = mx.set, scrs_was.fam = snd_fam, scrs_was.hot = ui.hot, scrs_was.master = ui.master, scrs_was.rs = ui.row[s];
    ui.scr = (uint8_t)s;
    song.sel = (uint8_t)t;
    ui.lane = l < 17u && is_drum(&trk[t]);
    if (ui.lane)
        lane_sel = (uint8_t)l;
    if (s == SCR_STEP) {                                /* (STEPS: fam is the step held, 0..63: its cards' pages are the rows; 127 none: PATTERN, TOOLS, EUCLID) */
        scrs_was.sheld = st.held, scrs_was.spage = st.page, scrs_was.hp = st.hp, scrs_was.lpg = st.lock_pg, scrs_was.cards = ui.cards_t;
        if (f < 64u && f >= trk_len(&trk[t])) {
            ui.scr = scrs_was.scr, song.sel = scrs_was.sel, ui.lane = scrs_was.lane, lane_sel = scrs_was.lsel;
            return 0;
        }
        st.held = f < 64u ? (uint16_t)(1u << (f & 15u)) : 0u;
        st.page = (uint8_t)(f < 64u ? f >> 4 : st.page);
        if (f < 64u)
            st.hp = 0, st.lock_pg = LOCK_NONE;
    }
    snd_fam = (uint8_t)(f >= 64u && s == SCR_STEP ? SND_ALL : f == 127u ? SND_ALL : f == SCRS_FAM_FM6 ? SND_FM6 : f);   /* (a data byte is 7 bits: the device's 0xFF and 0xFE are 127 and 126) */
    ui.master = s == SCR_FX || (s == SCR_SCOPE && f == 1u);   /* (SCOPE: fam 1 is its MASTER source, else the track) */
    return 1;
}
static void scrs_out(uint32_t s)
{
    ui.row[s] = scrs_was.rs;
    ui.scr = scrs_was.scr, song.sel = scrs_was.sel, ui.lane = scrs_was.lane, lane_sel = scrs_was.lsel;
    snd_fam = scrs_was.fam, ui.hot = scrs_was.hot, ui.master = scrs_was.master, mx.set = scrs_was.mset;
    if (s == SCR_STEP)
        st.held = scrs_was.sheld, st.page = scrs_was.spage, st.hp = scrs_was.hp, st.lock_pg = scrs_was.lpg, ui.cards_t = scrs_was.cards;
}
/* the mixer's rows are the tracks, the lanes, MASTER; each has knob sets (SELECT pages them on the device). SCRS lists a row a set: v -> the mixer row, mx.set the set */
static uint32_t scrs_n(uint32_t s)
{
    uint32_t n = 0, r;
    if (s == SCR_STEP && st.held)                       /* (a step held: its card pages, STEP, EXTRA, then the p-lock pages: op_step.c hp_*) */
        return hp_count();
    if (s != SCR_HOME)
        return SCREENS[s].rows() < 127u ? SCREENS[s].rows() : 127u;
    for (r = 0; r < MXR_N && n < 127u; r++)
        n += mx_sets(r);
    return n < 127u ? n : 127u;
}
static uint32_t scrs_map(uint32_t s, uint32_t v)
{
    uint32_t r;
    if (s == SCR_STEP && st.held) {                     /* (hp_set without its card overlay on the device's screen) */
        uint8_t ix[OP_MAXROWS];
        uint32_t n = hp_locks(ix);
        v = v >= HP_BASE + n ? HP_BASE + n - 1u : v;
        st.hp = (uint8_t)v;
        st.lock_pg = v >= HP_BASE ? ix[v - HP_BASE] : LOCK_NONE;
        return v;
    }
    if (s != SCR_HOME)
        return v;
    for (r = 0; r < MXR_N; r++) {
        if (v < mx_sets(r)) {
            mx.set = (uint8_t)v;
            return r;
        }
        v -= mx_sets(r);
    }
    return 0;
}
static void scrs_cell(uint32_t s, uint32_t r, uint32_t k)
{
    cell_t c;
    uint32_t armed = op_armed() && ui.arm_scr == s && ui.arm_row == r && ui.arm_k == k;
    memset(&c, 0, sizeof c);
    SCREENS[s].cell(r, k, &c);
    ed_b(c.kind);
    ed_str(c.label, 14);
    ed_str(c.val, 14);
    ed_str(c.unit, 8);
    ed_b(c.col == 0u ? 0u : c.col == C_WARN ? 1u : c.col == C_ERR ? 2u : c.col == C_OK ? 3u : 4u);
    ed_b(c.danger);
    ed_b(c.gk);
    ed_v(c.gmin);
    ed_v(c.gmax);
    ed_v(c.gv);
    ed_b(c.d != 0);
    if (c.d) {
        ed_b(c.d->fmt);
        ed_v(c.d->min);
        ed_v(c.d->max);
        ed_v(c.d->def);
        ed_v(c.vp ? *c.vp : c.d->def);
    }
    ed_b((c.dim ? 1u : 0u) | (c.dr ? 2u : 0u) | (c.strike ? 4u : 0u) | ((c.mark & 3u) << 3) | (armed ? 32u : 0u));
    ed_str(c.obj, 14);
}
static void scrs_row(uint32_t s, uint32_t r)
{
    char nm[16];
    uint32_t k;
    nm[0] = 0;
    ui.row[s] = (uint8_t)r;
    if (s == SCR_SOUND) {
        snd_title_row(r, nm);
    } else if (s == SCR_STEP && st.held) {
        hp_title(nm, 12);
    } else if (s == SCR_HOME) {                         /* (the row's code and its set: "T1 MIX", "M RETURNS", "SNARE SENDS") */
        char set[16];
        SCREENS[s].name(r, set);
        str_cpy(nm, mx_code(r), 5);
        str_cpy(nm + str_len(nm), " ", 2);
        str_cpy(nm + str_len(nm), set, (uint32_t)sizeof nm - str_len(nm));
    } else {
        SCREENS[s].name(r, nm);
    }
    ed_b(s == SCR_SOUND && snd_fam != SND_FM6 && snd_page(r) ? snd_page(r)->fam : 127u);
    ed_b(s == SCR_PROJECT && prj_slots_row(prj_kind(r)));   /* bit 0: YES on any card runs the row's operation on its ITEM (a store: PROJECT, SNAPSHOT, USER) */
    ed_str(nm, 12);
    for (k = 0; k < 4u; k++)
        scrs_cell(s, r, k);
    ed_str(op_armed() && ui.arm_scr == s && ui.arm_row == r ? ui.arm_q : "", 24);
}
/* 99 SCRS: 1 = handled (replied), 0 = not this command or bad arguments (no reply) */
static int ed_scrs(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, s, r, vr, k, rc = 0;
    if (cmd != ED_SCRS || na < 5u || op > 3u || !scrs_in(s = a[1], a[2], a[3], a[4]))
        return 0;
    r = na > 5u ? a[5] : 0u;
    if (op == 0u) {
        char nm[16];
        str_cpy(nm, SCR_NAME[s], sizeof nm);
        ed_b(0), ed_b(s), ed_b(a[2]), ed_b(a[3]), ed_b(a[4]), ed_b(scrs_n(s));
        ed_str(nm, 12);
        scrs_out(s);
        return 1;
    }
    if (na < 6u || r >= scrs_n(s) || (op == 2u && na < 10u) || (op == 3u && na < 8u)) {
        scrs_out(s);
        return 0;
    }
    k = na > 6u ? (a[6] & 3u) : 0u;
    vr = r;
    r = scrs_map(s, vr);
    ui.row[s] = (uint8_t)r;
    ui.hot = (uint8_t)k;
    if (op == 2u) {
        cell_t c;
        uint32_t mode = a[7];
        memset(&c, 0, sizeof c);
        SCREENS[s].cell(r, k, &c);
        if (c.kind == CK_VAL)
            SCREENS[s].turn(r, k, mode == 2u ? OP_RESET : ed_rv(a + 8), mode != 0u);
    } else if (op == 3u) {
        rc = (uint32_t)SCREENS[s].yes(r, k, a[7] != 0u) != 0u;
    }
    if (op >= 2u)                                       /* (a read draws nothing: ui.force would clear the panel to black) */
        ui.force = 1;
    ed_b(op), ed_b(s), ed_b(vr);
    if (op == 3u)
        ed_b(rc);
    scrs_row(s, r);
    scrs_out(s);
    return 1;
}
/* INFO tag 0x5C: version 1, the screens built (a mask, 2 x 7 bits: bit n = SCR_n) */
static void ed_scrs_info(void)
{
    ed_b(0x5C);
    ed_b(3);
    ed_b(1);
    ed_b(SCRS_MASK & 127u);
    ed_b((SCRS_MASK >> 7) & 127u);
}
#else
static int ed_scrs(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_scrs_info(void) {}
#endif
