/* SPDX-License-Identifier: GPL-3.0-only */
/* The help line (FELUCCA_PARAM_HELP, builder item PARAM_HELP; docs/UI-OPTIMIST-DESIGN.md section 21; the user,
 * 2026-10-13: "we should have a help text on the bottom last line. this info text should be displayed everywhere when
 * i modify a value or in the vertical menus"). One line of small type on the screen's last HL_H rows, in plain words:
 *   a value turned     on any screen (SOUND, the mixer, MASTER, STEP's cards, TEMPO, SONG ...): what the knob changes,
 *                      while it turns and HL_FRAMES (~2 s) after its last detent; another screen lets it go
 *   a vertical menu    GLOBAL and SYSTEM's settings list (the cursor line) and PROJECT (the cursor row's hot cell): always
 *                      there, the list made shorter for it (op_gldraw.c GL_LINE_H, op_draw.c LIST_H); an action that
 *                      asks first says so ("(asks)")
 *   a layer opened     its "Oct- locks it", once a power-on, ~2.5 s (op_laydraw.c lay_hint; the audit's #24)
 * Not under a question (the modal), the REC ring, a layer's map (but that hint) or NAME (its own hints, op_name.c). Its rows are a band of their own (op_graph.c
 * OP_FOOT, the audit's #6): every screen's content ends above it whether the line shows or not, so the line covers
 * nothing; when it goes, the band is cleared to black and nothing else is drawn again.
 * The words: tools/param_help.json, one table with SLOOP's UI and the web editor (tools/gen_param_help.py ->
 * build/gen/felucca_param_help.h, PH_BLOB, a const string in flash: only the lines of the features built; the texts
 * packed, ui/ph_blob.c, unpacked into a line on the stack when drawn). A line is
 * found as (kind op, the row's name, the cell's label), then (op, the screen's name, the label), then by the value's
 * descriptor: the page of PAGES that shows it, as SLOOP's line (an engine's value by the engine). Looked up when what
 * the line is about changes, never every frame; main loop only, nothing in RAM code. Included after op_gldraw.c. */
#if FELUCCA_PARAM_HELP
#include "../ph_blob.c"                  /* ph_find, ph_text (the texts packed) */
#define HL_H OP_FOOT                    /* the line: a rule, then the small face (16 rows): the help band */
#define HL_Y OP_FOOT_Y
#define HL_FRAMES 120u                  /* ~2 s at the UI's ~60 frames a second after a value's last detent */
static struct {
    uint8_t t;                          /* frames the line stays after a turn (0: only a vertical menu shows it) */
    uint8_t scr, row, k;                /* the value turned */
    uint8_t on;                         /* shown now (the band: its own rows, op_graph.c OP_FOOT) */
    uint16_t off;                       /* its line in PH_BLOB, 0: none */
    struct {                            /* what off was found for (kept whole: a hash mixed labels' addresses) */
        const char *label;
        const void *d;
        const char *hlp;
        uint32_t at;                    /* screen, row, cell, engine, track, lane */
    } key;
} hl;

/* a value's line by its descriptor: the first page of PAGES (and this UI's own) showing it that has a line for it */
static uint32_t hl_desc(const param_desc_t *d)
{
    uint32_t i, k, o;
    int16_t *vp;
    for (i = 0; i < OP_NPG; i++) {
        const page_t *pg = i < NPAGES ? &PAGES[i] : op_pg(i);   /* (PAGES' own SOURCE: SRC's line, now on the SOUND row) */
        for (k = 0; k < 4u; k++) {
            if (page_desc(pg, k, &vp) != d)
                continue;
            o = pg->scope == SC_ENGINE ? ph_find(PH_ENG, ENGINES[TSEL->eng_req % NENGINES]->name, d->label)
                                       : ph_find(PH_PAGE, pg->title, d->label);
            if (o)
                return o;
        }
    }
    return 0;
}
/* the line of cell c, row row of screen scr: the Optimist table by the row's name, then by the screen's, then SLOOP's
 * by the descriptor */
static uint32_t hl_find(uint32_t scr, uint32_t row, const cell_t *c)
{
    char nm[16];
    uint32_t o;
    if (!c->label || !c->label[0])
        return 0;
    if (c->hlp && (o = ph_find(PH_OP, "INSERTS", c->hlp)) != 0u)
        return o;                                       /* (a limited insert type's own line, FULL's) */
    SCREENS[scr % SCR_N].name(row, nm);
    o = ph_find(PH_OP, nm, c->label);
    if (!o && c->label[0] == 'S' && c->label[1] >= '1' && c->label[1] <= '9' && c->label[2] == ' ')
        o = ph_find(PH_OP, nm, c->label + 3);           /* (MASTER's returns, "S2 CHO": the type's line) */
    if (!o && str_eq(nm, "INSERT"))
        o = ph_find(PH_PAGE, "INSERTS", c->label);      /* (the mixer's INSERT set: any insert type shows its own line, SOUND > INSERTS') */
    if (!o)
        o = ph_find(PH_OP, SCR_NAME[scr % SCR_N], c->label);
    if (!o && c->d)
        o = hl_desc(c->d);
    return o;
}
/* a vertical menu: the line always there (GLOBAL and SYSTEM's settings list, PROJECT's rows) */
static int hl_menu(void) { return (glo_cycle() || ui.scr == SCR_PROJECT) && lay.shown == LY_PLAY && !name_on(); }
/* knob k turned on the cursor row (op_input.c op_knobs): its line for HL_FRAMES */
static void hl_note(uint32_t k)
{
    if (ui.scr == SCR_SYSTEM && !glo_cycle())
        return;                                         /* (the BLE devices list: its status line is at the foot) */
    hl.t = HL_FRAMES;
    hl.scr = ui.scr;
    hl.row = ui.row[ui.scr];
    hl.k = (uint8_t)k;
}
static void hl_tick(void)                               /* once a frame (op_input.c ui_draw) */
{
    if (hl.t)
        hl.t--;
    if (hl.scr != ui.scr)
        hl.t = 0;                                       /* (another screen: it belonged to the one before) */
}
/* the line now, as its offset in PH_BLOB (ph_text unpacks it), 0 none: the cell it is about, found again only when
 * that changes (the screen, row, cell, label, track) */
static uint32_t hl_text(void)
{
    cell_t c;
    uint32_t scr = ui.scr, row = ui.row[ui.scr], k = hl_menu() ? ui.hot : hl.k, at;
    if (glo_cycle())
        gl_cell(scr == SCR_SYSTEM, row, &c);            /* (the settings list: the cursor line) */
    else
        SCREENS[scr % SCR_N].cell(row, k, &c);
    at = scr << 26 | (row & 0xFFu) << 18 | (k & 3u) << 16 | (uint32_t)TSEL->eng_req << 8 | song.sel << 5 | (lane_selected() & 31u);
    if (at != hl.key.at || c.label != hl.key.label || (const void *)c.d != hl.key.d || c.hlp != hl.key.hlp) {
        hl.key.at = at;
        hl.key.label = c.label;
        hl.key.d = c.d;
        hl.key.hlp = c.hlp;
        hl.off = (uint16_t)hl_find(scr, row, &c);
    }
    return hl.off;
}
/* a modulation DEST naming one of the engine's values (LT_E1 .. LT_E8: LFO, LFO 2, ENV2 DEST's slots) says which engine and
 * which value it moves (the user, 2026-10-18: "if in the help bottom line it could say the destination engine and param
 * name"): "ANALOG CUT - Filter cutoff", the engine's own line for that value after the dash while the whole fits the
 * band (else the names alone) -> b (64 bytes); 0: not such a DEST (the table's line stays) */
#define HL_DEST_ON (1u << 16)                   /* hl_dest's result: a DEST found (never 0), the engine and value under it */
static uint32_t hl_dest(char *b)
{
    cell_t c;
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const param_desc_t *d;
    char t[PH_LINE];
    uint32_t o, v, n;
    if (glo_cycle() || hl_menu())
        return 0;
    SCREENS[ui.scr % SCR_N].cell(ui.row[ui.scr], hl.k, &c);
    if (!c.d || c.d->names != N_LT || !c.vp || *c.vp < LT_E1 || *c.vp > LT_E8)
        return 0;
    v = (uint32_t)(*c.vp - LT_E1);
    d = track_desc(TSEL, P_E0 + v);
    str_cpy(b, e->name, 64);
    n = str_len(b);
    b[n++] = ' ';
    str_cpy(b + n, d->label, 64 - n);
    o = ph_find(PH_ENG, e->name, d->label);
    if (o) {
        n = str_len(b);
        str_cpy(b + n, " - ", 64 - n);
        str_cpy(b + n + 3u, ph_text(o, t), 64 - n - 3u);
        if (text_w(&FONT_S, b) > 232)
            b[n] = 0;                                   /* (too wide for the band: the names alone) */
    }
    return HL_DEST_ON | (uint32_t)TSEL->eng_req << 8 | v;
}
/* where an automated value's change comes from (the user, 2026-10-18: "when on a automated parameter to know in the
 * bottom line where autom/plock/mod come from"): the sources acting on cell c's value now, as words into b (64 bytes), each
 * a bit of the result (0: none). A synth part's value: LOCK (a lock in force, auto_base.c lk_param), MOTION (moved by a
 * hold, motion_active), the modulation slots aimed at it (LFO A / B, LFO2 A / B, ENV2 A / B: their DEST its LT_*, or FLT
 * SHP on the engine's cutoff and shape cards; an AMT 0 slot does nothing), ENV1 (an ENV 1 MOD card moving it), MACRO (a
 * macro off home changes it). A drum lane's value: LOCK MOTION (dla), its LFO, LFO 2, ENV2 slots */
static void hl_word(char *b, const char *w)
{
    uint32_t n = str_len(b);
    if (n)
        b[n++] = ' ';
    str_cpy(b + n, w, 64 - n);
}
#if !(FELUCCA_MOTION || FELUCCA_PLOCK)
#define AB_ON(k, id) 0
#endif
static uint32_t hl_src_synth(uint32_t id, char *b)
{
    static const char *const SL[3][2] = {{"LFO-A", "LFO-B"}, {"LFO2-A", "LFO2-B"}, {"ENV2-A", "ENV2-B"}};
    const track_t *t = TSEL;
    uint32_t k = song.sel % NTRK, m = 0, j, s, n;
    int32_t dc = effv_dest(t, 0), ds = effv_dest(t, 1);
#if FELUCCA_PLOCK
    for (j = 0; j < lk_n[k] && j < NLOCK; j++)
        if (lk_param[k][j] == id)
            m |= 1u, hl_word(b, "LOCK");
#endif
    if (!(m & 1u) && id < P_COUNT && AB_ON(k, id))
        m |= 2u, hl_word(b, "MOTION");
    for (j = 0; j < 3u; j++) {                          /* LFO, LFO 2, ENV2's two slots each */
        uint32_t f = j == 0u ? P_L1DA : j == 1u ? P_L2DA : P_COUNT;
#if FELUCCA_ANALOG2
        if (j == 2u)
            f = P_N2DA;
#endif
        for (s = 0; f < P_COUNT && s < 2u; s++) {
            int32_t d = t->p[f + 2u * s];
            uint32_t q = P_COUNT;
            if (!d || !t->p[f + 2u * s + 1u])
                continue;
            if (d == LT_FLT)
                q = dc >= 0 ? (uint32_t)dc : P_COUNT;
            else if (d == LT_SHP)
                q = ds >= 0 ? (uint32_t)ds : P_COUNT;
            else if (!LT_CLASSIC(d))
                (void)lo_dest(t, d, &q);
            if (q == id)
                m |= 4u << j, hl_word(b, SL[j][s]);
        }
    }
    for (n = 0; n < 4u; n++) {                          /* ENV 1 MOD's cards: a move of this EDIT value, FLT SHP on the cards */
        uint32_t c = env1_card(t->eng_req, n), cid = env1_id(t->eng_req, n);
        if (cid >= P_COUNT || !t->p[cid])
            continue;
        if ((E1K_MOVES(c) && id == P_E0 + E1K_ARG(c)) || (E1K_KIND(c) == E1K_FLT && (int32_t)id == dc) ||
            (E1K_KIND(c) == E1K_SHP && (int32_t)id == ds)) {
            m |= 0x20u, hl_word(b, "ENV1");
            break;
        }
    }
#if FELUCCA_MACROS
    if (id < P_COUNT && mac_effective(k, id, t->p[id]) != t->p[id])
        m |= 0x40u, hl_word(b, "MACRO");
#endif
    return m;
}
static uint32_t hl_src(char *b)
{
    cell_t c;
    uint32_t m = 0;
    b[0] = 0;
    if (glo_cycle() || hl_menu() || (ui.scr == SCR_STEP && st.held))
        return 0;
    SCREENS[ui.scr % SCR_N].cell(ui.row[ui.scr], hl.k, &c);
    if (c.kind != CK_VAL || !c.vp)
        return 0;
#if DL_UI
    if (c.vp >= dsv && c.vp < dsv + DE_N) {             /* a drum lane value */
        uint32_t l = lane_selected() % DRUM_LANES, v = (uint32_t)(c.vp - dsv), j, s;
#if DLA_ON
        if ((dla.lm[l] >> v) & 1u)
            m |= 1u, hl_word(b, "LOCK");
        else if ((dla.hm[l] >> v) & 1u)
            m |= 2u, hl_word(b, "MOTION");
#endif
#if DLFO_ON
        for (j = 0; j < 3u; j++)
            for (s = 0; s < LFO_NSLOT; s++) {
                const lfo_slot_t *q = j == 0u ? &dlfo[l].s[s] : j == 1u ? &dlfo2[l].s[s] : &denv[l].s[s];
                static const char *const SL[3][2] = {{"LFO-A", "LFO-B"}, {"LFO2-A", "LFO2-B"}, {"ENV2-A", "ENV2-B"}};
                if (q->dest == v + 1u && q->amt)
                    m |= 4u << j, hl_word(b, SL[j][s]);
            }
#endif
        (void)j, (void)s;
        return m;
    }
#endif
    if (!is_drum(TSEL) && c.vp >= tsel()->p && c.vp < tsel()->p + P_COUNT)
        return hl_src_synth((uint32_t)(c.vp - tsel()->p), b);
    return 0;
}
/* before the bands (op_frame_draw, ov: the overlay): shown or not this frame; going, the band is cleared (nothing else
 * ever draws on it: no band to draw again) */
static void hl_pre(uint32_t ov)
{
    int vis = (hl_menu() || lay_hint_t || (hl.t && hl.scr == ui.scr && lay.shown == LY_PLAY && !name_on())) && ov != 1u && ov != 3u && ov != 4u;
    if (vis && !hl_menu() && !lay_hint_t && !hl_text()) {
        char w[64];
        vis = hl_src(w) != 0;                           /* (no line: shown still when the value is automated, its sources) */
    }                                        /* (a value with no line: nothing; a menu keeps its empty line) */
    if (vis == hl.on)
        return;
    hl.on = (uint8_t)vis;
    ui.sig[5] = 0;
    if (!vis) {
        cv_begin(240, HL_H, C_BLACK);
        cv_blit(0, HL_Y);
    }
}
/* after the bands: the line, when what it says changed */
static void hl_post(void)
{
    char b[64], w[64], t[PH_LINE];
    uint32_t off, sig, dst, src;
    if (!hl.on)
        return;
    off = lay_hint_t ? 0xFFFFu : hl_text();             /* (a layer just opened: its hint, op_laydraw.c lay_hint) */
    dst = lay_hint_t ? 0u : hl_dest(b);                 /* (a DEST on an engine value: its engine and name) */
    src = lay_hint_t ? 0u : hl_src(w);                  /* (an automated value: where its change comes from) */
    sig = hu(hu(hu(hu(0x4E1Fu, settings.palette), off), dst), src);
    if (src) {                                          /* the sources first ("LOCK LFO-A: Filter cutoff"), cut to the band */
        uint32_t n = str_len(w);
        if (dst || (off && off != 0xFFFFu)) {
            str_cpy(w + n, ": ", 64 - n);
            str_cpy(w + n + 2u, dst ? b : ph_text(off, t), 64 - n - 2u);
        }
        for (n = str_len(w); n > 1u && text_w(&FONT_S, w) > 232; n--)
            w[n - 1u] = 0;
        str_cpy(b, w, sizeof b);
        dst = 1;                                        /* (drawn as the composed line below) */
    }
    if (sig == ui.sig[5])
        return;
    ui.sig[5] = sig;
    cv_begin(240, HL_H, C_BLACK);
    cv_hline(0, C_LINE);
    if (off == 0xFFFFu)
        cv_texts(4, 2, "Oct- locks it", C_AMB);
    else if (dst)
        cv_texts(4, 2, b, C_AMB);
    else if (off)
        cv_texts(4, 2, ph_text(off, b), C_AMB);
    cv_blit(0, HL_Y);
}
#endif
