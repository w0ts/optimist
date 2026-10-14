/* Layout and empties (batch D of the 2026-10-14 coherence audit; docs/UI-OPTIMIST-DESIGN.md section 33), every screen walked
 * as tests/ui_optimist_names.h walks it (every engine on a synth track, the drum track and each lane, STEP's pages and a held
 * step's, the mixer's sets, the master FX, PROJECT, SONG, TEMPO, SCOPE, the layers, NAME):
 *   #7   one rule for "nothing": a knob with no parameter is its frame alone (no label, no value, no dash); a parameter
 *        with no value says "-"; a slot with nothing in it says EMPTY, dim. Never a blank value under a label, a label
 *        with no value, a "-" with no label, "--" or "----"
 *   #11  a badge on every screen: the track's "<n> <ENGINE>" on a track page, else the screen's or the layer's name
 *        (MASTER, MIX, PROJECT, SONG, TEMPO, SCOPE, GLOBAL, SYSTEM, NAME, SCENES...); the title never repeats it
 *   #21  no page of one card, but where its family's page is full: TEMPO's REC 2 (QUANT) with REC MODES, SCL 2 (TRANSP)
 *        without CHORD+
 *   #22  a value the build lacks (DUST without FX_DUST) never shows as a card: the mixer, the master FX, the layers
 * Included after ui_optimist_names.h. */
static uint32_t lo_bad, lo_cells_seen, lo_heads, lo_pages, lo_one;
static void lo_fail(const char *what, const char *a, const char *b)
{
    if (lo_bad < 40u)
        printf("  layout: %s '%s' (%s)\n", what, a, b);
    lo_bad++;
}
static const char *lo_where(char *b, uint32_t n)
{
    head_t h;
    char r[16] = "";
    head_make(&h);
    if (lay.shown == LY_PLAY && !name_on())
        SCR->name(ui.row[ui.scr], r);
    snprintf(b, n, "%s|%s|%s", h.eng, SCR_NAME[ui.scr % SCR_N], r);
    return b;
}
/* the page's own name is the screen's word (PROJECT's "Project" row, TEMPO's "Tempo"): the title is the row's name, kept */
static int lo_row_is_screen(void)
{
    char r[16], t[16], b[16];
    if (lay.shown != LY_PLAY || name_on())
        return 0;
    SCR->name(ui.row[ui.scr], r);
    nm_title(r, t, sizeof t);
    op_case(b, SCR_NAME[ui.scr % SCR_N], sizeof b);
    return str_eq(t, b) && ui.scr != SCR_HOME;
}
/* the header now: a badge, the title not repeating it (GLOBAL, SYSTEM: the badge alone) */
static void lo_head(void)
{
    head_t h;
    char w[48], b[16];
    ui.msg_t = 0;
    head_make(&h);
    lo_heads++;
    if (!h.badge || !h.eng || !h.eng[0])
        lo_fail("no badge", h.t, lo_where(w, sizeof w));
    else if (!h.t[0] && !gl_scr(ui.scr) && lay.shown == LY_PLAY)
        lo_fail("no title", h.eng, lo_where(w, sizeof w));
    else if (str_len(h.eng) > 3u && strstr(h.t, op_case(b, h.eng, sizeof b)) == h.t &&
             (h.t[str_len(b)] == ' ' || !h.t[str_len(b)]) && !lo_row_is_screen())
        lo_fail("the title repeats the badge", h.t, lo_where(w, sizeof w));
}
/* one cell against the empty rule and the build */
static void lo_cell(const cell_t *c, const char *where)
{
    lo_cells_seen++;
    if (c->label && !c->label[0])
        lo_fail("an empty label (the old \"-\" card)", c->val, where);
    if (c->label && c->label[0] && c->kind != CK_ACT && !c->val[0])
        lo_fail("a label with no value", c->label, where);
    if ((!c->label || !c->label[0]) && c->val[0])
        lo_fail("a value with no label", c->val, where);
    if (!strcmp(c->val, "--") || !strcmp(c->val, "----"))
        lo_fail("\"--\" or \"----\" (EMPTY or \"-\")", c->label ? c->label : "", where);
    if (!strcmp(c->val, "EMPTY") && !c->dim)
        lo_fail("EMPTY not dim", c->label ? c->label : "", where);
    if (!FELUCCA_FX_DUST && c->d == &GP[G_DUST])
        lo_fail("DUST shown, not built", c->label ? c->label : "", where);
}
/* row r of the screen shown: its cells; a page of one card counted (pages: the screens whose rows are pages of cards) */
static int lo_one_ok(void)                              /* the one-card pages kept: a logical group of their own */
{
    char nm[16];
    SCR->name(ui.row[ui.scr], nm);
    return (ui.scr == SCR_TEMPO && FELUCCA_REC_MODES && str_eq(nm, "REC 2")) ||   /* (TEMPO and REC full: QUANT alone) */
           (ui.scr == SCR_SOUND && !FELUCCA_CHORDPLUS && str_eq(nm, "SCL 2"));    /* (SCL full: TRANSP alone, no CHORD+) */
}
static void lo_row(uint32_t r, int pages)
{
    uint32_t k, n = 0;
    cell_t c;
    char w[48];
    lo_where(w, sizeof w);
    for (k = 0; k < 4u; k++) {
        SCR->cell(r, k, &c);
        lo_cell(&c, w);
        n += c.label && c.label[0];
    }
    if (!pages)
        return;
    lo_pages++;
    if (n == 1u && !lo_one_ok()) {
        lo_fail("a page of one card", "", w);
        lo_one++;
    }
}
static void lo_rows(uint32_t scr, int pages)
{
    uint32_t r, n = SCREENS[scr].rows();
    for (r = 0; r < n; r++) {
        ui.row[scr] = (uint8_t)r;
        lo_head();
        lo_row(r, pages);
    }
}
static void lo_sound(void)
{
    static const uint8_t FAMS[] = {FAM_EDIT, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_ARP};
    uint32_t f;
    op_enter(SCR_SOUND);
    lo_rows(SCR_SOUND, 1);
    for (f = 0; f < sizeof FAMS; f++) {
        op_enter(SCR_SOUND);
        if (!snd_has_fam(FAMS[f]))
            continue;
        op_jump_sound(FAMS[f]);
        lo_rows(SCR_SOUND, 1);
    }
}
static void lo_step(void)                               /* STEP's pages, the window, a held step's pages (an empty step, a set one) */
{
    uint32_t r, n, p, set;
    op_enter(SCR_STEP);
    ui.cards_t = 0;
    lo_head();                                          /* (the window) */
    n = SCREENS[SCR_STEP].rows();
    for (r = 0; r < n; r++) {
        ui.row[SCR_STEP] = (uint8_t)r;
        ui.cards_t = 60;
        lo_head();
        lo_row(r, 1);
    }
    for (set = 0; set < 2u; set++) {
        if (is_drum(TSEL) && set)
            dstep_set(&TSEL->dstep[0], lane_selected(), 2u, 0u);
        else if (is_drum(TSEL))
            dstep_clr(&TSEL->dstep[0], lane_selected());
        else {
            TSEL->step[0].time = ST_NOTE;
            TSEL->step[0].n = (uint8_t)set;
        }
        st.page = 0;
        st.held = 1;                                    /* (step 1 held) */
        for (p = 0; p < hp_count(); p++) {
            uint32_t k;
            cell_t c;
            char w[48];
            hp_set(p);
            ui.cards_t = 60;
            lo_head();
            snprintf(w, sizeof w, "held step %s, page %u", set ? "set" : "empty", p);
            for (k = 0; k < 4u; k++) {
                SCREENS[SCR_STEP].cell(ui.row[SCR_STEP], k, &c);
                lo_cell(&c, w);
            }
        }
        st.held = 0;
        st.hp = 0;
    }
    ui.cards_t = 0;
}
static void lo_mixer(void)
{
    uint32_t r, s;
    op_enter(SCR_HOME);
    for (r = 0; r < SCREENS[SCR_HOME].rows(); r++) {
        ui.row[SCR_HOME] = (uint8_t)r;
        for (s = 0; s < mx_sets(r); s++) {
            mx.set = (uint8_t)s;
            lo_head();
            lo_row(r, 0);                               /* (the mixer's sets: its rows' values, not pages) */
        }
    }
    mx.set = 0;
}
static void lo_layers(uint32_t master)
{
    uint32_t l, k;
    cell_t c;
    char w[48];
    op_enter(SCR_SOUND);
    ui.master = (uint8_t)master;
    for (l = 1; l < LY_COUNT; l++) {
        lay.shown = (uint8_t)l;
        lo_head();
        snprintf(w, sizeof w, "layer %u%s", l, master ? " on MASTER" : "");
        for (k = 0; k < 4u; k++) {
            lay_cell(k, &c);
            if (!FELUCCA_FX_DUST && c.d == &GP[G_DUST])
                lo_fail("DUST shown, not built", c.label ? c.label : "", w);
        }
    }
    lay.shown = LY_PLAY;
    ui.master = 0;
}
static void bd_layout_tests(void)
{
    uint32_t e, l;
    track_t keep = trk[1], keep0 = trk[0], keepd = trk[TRK_DRUM];
    uint8_t rows[SCR_N];
    memcpy(rows, ui.row, sizeof rows);
    reset_ui();
    frames(120);                                        /* (the track-name flash over) */
    for (e = 0; e < NENGINES; e++) {                    /* every engine on track 2 */
        if (eng_free(e))
            continue;
        set_engine_of(&trk[1], e);
        ui.lane = 0;
        track_select(1);
        lo_sound();
        lo_step();
    }
    trk[1] = keep;
    track_select(0);
    lo_step();
    trk[0] = keep0;
    track_select(TRK_DRUM);
    lane_select(0);
    ui.lane = 0;
    lo_sound();
    lo_step();
    for (l = 0; l < DRUM_UILANES; l++) {
        op_enter(SCR_SOUND);
        lane_pick(l);
        lo_sound();
        lo_step();
    }
    trk[TRK_DRUM] = keepd;
    ui.lane = 0;
    lane_select(0);
    track_select(0);
    lo_mixer();
    op_enter(SCR_FX);
    lo_rows(SCR_FX, 1);
    op_enter(SCR_PROJECT);
    lo_rows(SCR_PROJECT, 0);                            /* (PROJECT's MEMORY: USED FREE, its own group) */
    op_enter(SCR_SONG);
    lo_rows(SCR_SONG, 0);
    op_enter(SCR_TEMPO);
    lo_rows(SCR_TEMPO, 1);
    op_enter(SCR_SCOPE);
    lo_rows(SCR_SCOPE, 1);
    op_enter(SCR_GLOBAL);
    lo_head();
    op_enter(SCR_SYSTEM);
    lo_head();
    lo_layers(0);
    lo_layers(1);
    printf("  layout: %u headers, %u cells, %u pages walked\n", lo_heads, lo_cells_seen, lo_pages);
    check(lo_heads > 200u && lo_pages > 100u, "layout: every screen, engine, lane, page and layer walked");
    check(!lo_bad, "layout: a badge on every screen, the empty rule (frame / - / EMPTY dim), no one-card page, no DUST unbuilt");
    check(!lo_one, "layout: no page of one card (TEMPO's REC 2 kept where REC is full)");
    {   /* #21: the merges */
        uint32_t i, ftyp = 0, flt2 = 0, filt = 0;
        page_rows(sound_page, snd_ix);
        for (i = 0; i < OP_NPG; i++) {
            const page_t *pg = op_pg(i);
            flt2 |= sound_page(pg) && pg->id[0] == P_A2FTYP;
            filt |= sound_page(pg) && pg->scope == SC_TRACK && str_eq(pg->title, "FILTER");
            ftyp |= pg->id[3] == P_A2FTYP;
        }
        check(!flt2 && !filt && (!FELUCCA_ANALOG2 || ftyp),
              "#21: FLT 2's FTYP on OSC 2's fourth card, no FLT 2 row; no FILTER row (INSERTS' amount card is that value)");
    }
    memcpy(ui.row, rows, sizeof rows);
    reset_ui();
    song.sel = 0;
    track_select(0);
}
