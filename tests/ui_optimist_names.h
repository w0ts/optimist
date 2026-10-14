/* The names table (ui/optimist/op_names.c; the style guide's sections 1-4, batch A of the 2026-10-14 coherence audit):
 * every screen walked, every engine on a synth track, the drum track and each of its lanes, STEP's pages and a held
 * step's, the mixer's sets, the master FX, PROJECT, SONG, TEMPO, SCOPE:
 *   titles   the header's title is never cut: it fits whole with its n/N next to the badge ("2 DIGITAL", "4 ACCENT")
 *   cards    every card label is in capitals and at most 6 characters (an action's object 8)
 *   cells    every list cell is whole (a 4-letter form from the table, never a value cut at 4), and every value of a list
 *            parameter shows whole in its row as the row stands (op_draw.c row_layout: a 5-letter "BLUES", "REUSE", "L1 RT"
 *            reflows the row; batch B)
 *   rows     a list row's name is the header's title (the same words, the same case)
 * NAMES_DUMP=file writes every title, label and cell met (the audit's raw list). Included after ui_optimist_hdrnum.h. */
static FILE *nm_dump;
static uint32_t nm_bad, nm_seen, nm_long, nm_vals;
static void nm_fail(const char *what, const char *a, const char *b)
{
    if (nm_bad < 40u)
        printf("  names: %s '%s' (%s)\n", what, a, b);
    nm_bad++;
}
static const char *nm_where(char *b, uint32_t n)        /* the context: the badge, the screen */
{
    head_t h;
    head_make(&h);
    snprintf(b, n, "%s|%s", h.badge ? h.eng : "-", SCR_NAME[ui.scr % SCR_N]);
    return b;
}
/* the header on the current screen and row: the title whole */
static void nm_head(void)
{
    head_t h;
    char w[40], raw[32];
    ui.msg_t = 0;
    head_make(&h);
    nm_seen++;
    if (ui.scr == SCR_STEP && st.held && ui.cards_t)
        hp_title(raw, sizeof raw);
    else if (ui.scr == SCR_STEP && !ui.cards_t)
        str_cpy(raw, "STEPS", sizeof raw);
    else
        SCR->name(ui.row[ui.scr], raw);
    if (nm_dump)
        fprintf(nm_dump, "T|%s|%s|%s|%d|%d|%s\n", nm_where(w, sizeof w), h.t, h.pos, (int)h.room / 8, (int)h.cut, raw);
    if (h.cut)
        nm_fail("title cut", h.t, nm_where(w, sizeof w));
}
static int nm_upper(const char *s)                      /* no lowercase letter */
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            return 0;
    return 1;
}
static int nm_stored(const cell_t *c)                  /* a stored name: the SOUND row's (#36 of the audit) */
{
    return c->label && (str_eq(c->label, "PRESET") || str_eq(c->label, "KIT") || str_eq(c->label, "LANE") ||
                        str_eq(c->label, "SOUND"));
}
/* the cards and the list cells of row r of the screen shown */
static void nm_cells(uint32_t r)
{
    uint32_t k;
    int rc, list = ui.scr != SCR_STEP && ui.scr != SCR_TEMPO && ui.scr != SCR_SCOPE;   /* (never a list: a picture) */
    cell_t c, row[4];
    rowl_t L;
    char b[32], w[40], nm[16];
    SCR->name(r, nm);
    for (k = 0; k < 4u; k++)
        SCR->cell(r, k, &row[k]);
    row_layout(row, &L, LC_END(236));                   /* (the narrower row: a list that scrolls) */
    for (k = 0; k < 4u; k++) {
        c = row[k];
        if (c.label && c.label[0]) {
            const char *lab = card_label(&c, b, sizeof b);
            uint32_t mx = c.kind == CK_ACT || !strcmp(lab, "MISSING") ? 8u : 6u;   /* (MISSING: PROJECT > TOOLS' read-out,
                                                         * on its 2x2 card only, never the mixer's; the audit's #43) */
            if (nm_dump)
                fprintf(nm_dump, "C|%s|%s|%s|%s|%s|%s|%u\n", nm_where(w, sizeof w), nm, c.label, lab, c.val, c.unit, c.kind);
            if (!nm_upper(lab) || str_len(lab) > mx)
                nm_fail("card label", lab, nm);
        }
        if (list && L.t[k][0]) {
            rc = (L.cut >> k) & 1u ? 2 : 1;
            if (nm_dump)
                fprintf(nm_dump, "L|%s|%s|%s|%s\n", nm_where(w, sizeof w), nm, c.kind == CK_ACT ? (c.val[0] ? c.val : c.label) : c.val, L.t[k]);
            if (rc == 2 && !nm_stored(&c))
                nm_fail("list cell cut", L.t[k], nm);
        }
        if (c.d && (c.d->fmt == F_ENUM || c.d->fmt == F_ONOFF) && c.gk != GK_NONE) {   /* every value of a list: its cell whole */
            int32_t v;
            for (v = c.d->min; v <= c.d->max; v++) {
                char val[16];
                const char *u;
                param_format(c.d, v, val, &u);
                if (nm_dump)
                    fprintf(nm_dump, "E|%s|%s|%s\n", c.label, val, nm_cell(val));
                nm_long += str_len(nm_cell(val)) > 4u;   /* (SLOOP's 5-letter list names: whole, the row reflowed) */
                if (list) {                             /* this value in its row, the others as they stand: whole */
                    cell_t vr[4];
                    rowl_t VL;
                    memcpy(vr, row, sizeof vr);
                    str_cpy(vr[k].val, val, sizeof vr[k].val);
                    row_layout(vr, &VL, LC_END(236));
                    nm_vals++;
                    if ((VL.cut >> k) & 1u)
                        nm_fail("list value cut in its row", val, nm);
                }
            }
        }
    }
}
static void nm_rows(uint32_t scr)                       /* every row of the screen shown: title, cards, cells */
{
    uint32_t r, n = SCREENS[scr].rows();
    for (r = 0; r < n; r++) {
        ui.row[scr] = (uint8_t)r;
        nm_head();
        nm_cells(r);
    }
}
static void nm_sound(void)                              /* SOUND on the selected track, every family */
{
    static const uint8_t FAMS[] = {FAM_EDIT, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_ARP, SND_FM6};
    uint32_t f;
    op_enter(SCR_SOUND);
    nm_rows(SCR_SOUND);                                 /* (every row: SOUND entered) */
    for (f = 0; f < sizeof FAMS; f++) {
        op_enter(SCR_SOUND);
        if (FAMS[f] == SND_FM6 ? !fm6_sel() : !snd_has_fam(FAMS[f]))
            continue;
        if (FAMS[f] == SND_FM6)
            snd_family(SND_FM6);
        else
            op_jump_sound(FAMS[f]);
        nm_rows(SCR_SOUND);
    }
}
static void nm_step(void)                               /* STEP: its pages' cards, the window, a held step's pages */
{
    uint32_t r, n, p;
    op_enter(SCR_STEP);
    ui.cards_t = 0;
    for (st.page = 0; st.page < 4u; st.page++)
        nm_head();                                      /* (the window: "Steps 49-64") */
    st.page = 0;
    n = SCREENS[SCR_STEP].rows();
    for (r = 0; r < n; r++) {
        ui.row[SCR_STEP] = (uint8_t)r;
        ui.cards_t = 60;
        nm_head();
        nm_cells(r);
    }
    st.held = 1;
    ui.cards_t = 60;
    for (p = 0; p < hp_count(); p++) {
        hp_set(p);
        ui.cards_t = 60;
        nm_head();
        {
            uint32_t k;
            cell_t c;
            char b[32];
            for (k = 0; k < 4u; k++) {
                SCREENS[SCR_STEP].cell(ui.row[SCR_STEP], k, &c);
                if (c.label && c.label[0] && (!nm_upper(card_label(&c, b, sizeof b)) || str_len(card_label(&c, b, sizeof b)) > 6u))
                    nm_fail("held step card label", card_label(&c, b, sizeof b), "STEP");
                if (nm_dump && c.label && c.label[0])
                    fprintf(nm_dump, "C|held|STEP|%u|%s|%s|%s|%s\n", p, c.label, card_label(&c, b, sizeof b), c.val, c.unit);
            }
        }
    }
    st.held = 0;
    st.hp = 0;
    ui.cards_t = 0;
}
static void nm_mixer(void)                              /* the mixer: every row, every set */
{
    uint32_t r, s;
    op_enter(SCR_HOME);
    for (r = 0; r < SCREENS[SCR_HOME].rows(); r++) {
        ui.row[SCR_HOME] = (uint8_t)r;
        for (s = 0; s < mx_sets(r); s++) {
            mx.set = (uint8_t)s;
            nm_head();
            nm_cells(r);
        }
    }
    mx.set = 0;
}
static void nm_layers(void)                             /* every layer's four cards over SOUND */
{
    uint32_t l, k;
    cell_t c;
    char b[32];
    op_enter(SCR_SOUND);
    for (l = 0; l < LY_COUNT; l++) {
        lay.shown = (uint8_t)l;
        nm_head();
        for (k = 0; k < 4u; k++) {
            lay_cell(k, &c);
            if (nm_dump && c.label && c.label[0])
                fprintf(nm_dump, "C|layer|%u|%s|%s|%s|%s\n", l, c.label, card_label(&c, b, sizeof b), c.val, c.unit);
            if (c.label && c.label[0] && (!nm_upper(card_label(&c, b, sizeof b)) || str_len(card_label(&c, b, sizeof b)) > 6u))
                nm_fail("layer card label", card_label(&c, b, sizeof b), "layer");
        }
    }
    lay.shown = LY_PLAY;
}
static void nm_settings(void)                           /* GLOBAL and SYSTEM: every line's name in capitals, the header */
{
    uint32_t sys, r, n;
    cell_t c;
    char b[32];
    for (sys = 0; sys < 2u; sys++) {
        op_enter(sys ? SCR_SYSTEM : SCR_GLOBAL);
        n = SCR->rows();
        for (r = 0; r < n; r++) {
            ui.row[ui.scr] = (uint8_t)r;
            nm_head();
            gl_cell(sys, r, &c);
            if (!c.label || !c.label[0])
                continue;
            {
                const char *l = nm_card(c.label, 0, b, sizeof b);
                if (nm_dump)
                    fprintf(nm_dump, "S|%s|%u|%s|%s|%s|%s\n", SCR_NAME[ui.scr], r, c.label, l, c.val, c.unit);
                if (!nm_upper(l) || str_len(l) > 12u)
                    nm_fail("settings line name", l, SCR_NAME[ui.scr]);
            }
        }
    }
}
static void nm_track(uint32_t t)
{
    track_select(t);
    nm_sound();
    nm_step();
}
static void names_tests(void)
{
    uint32_t e, l;
    const char *path = getenv("NAMES_DUMP");
    track_t keep = trk[1];
    int16_t bpm = song.g[G_BPM];
    uint8_t rows[SCR_N];
    nm_dump = path ? fopen(path, "w") : 0;
    memcpy(rows, ui.row, sizeof rows);
    reset_ui();
    song.g[G_BPM] = 120;                                /* (the widest tempo: three digits) */
    frames(120);                                        /* (the track-name flash over) */
    song.sel = 0;
    for (e = 0; e < NENGINES; e++) {                    /* every engine on track 2 */
        if (eng_free(e))
            continue;
        set_engine_of(&trk[1], e);
        ui.lane = 0;
        nm_track(1);
    }
    trk[1] = keep;
    track_select(TRK_DRUM);                             /* the drum track: DR itself, then each lane, the ACCENT lane */
    lane_select(0);
    ui.lane = 0;
    nm_sound();
    nm_step();
    for (l = 0; l < DRUM_UILANES; l++) {
        op_enter(SCR_SOUND);
        lane_pick(l);
        nm_sound();
        nm_step();
    }
    ui.lane = 0;
    lane_select(0);
    track_select(0);
    nm_mixer();
    op_enter(SCR_FX);
    nm_rows(SCR_FX);
    op_enter(SCR_PROJECT);
    nm_rows(SCR_PROJECT);
    op_enter(SCR_SONG);
    nm_rows(SCR_SONG);
    op_enter(SCR_TEMPO);
    nm_rows(SCR_TEMPO);
    op_enter(SCR_SCOPE);
    nm_rows(SCR_SCOPE);
    nm_layers();
    nm_settings();
    if (nm_dump)
        fclose(nm_dump);
    printf("  names: %u headers walked; %u list values longer than 4 letters, %u values laid out in their rows: none cut\n", nm_seen, nm_long, nm_vals);
    check(nm_seen > 200u, "names: every screen, engine, lane and page walked");
    check(!nm_bad, "names: no title cut, every card label in capitals (6 at most), no list cell cut");
    song.g[G_BPM] = bpm;
    memcpy(ui.row, rows, sizeof rows);                  /* (every screen's page as it was: they remember theirs) */
    mx.set = 0;
    reset_ui();
    song.sel = 0;
    track_select(0);
}
