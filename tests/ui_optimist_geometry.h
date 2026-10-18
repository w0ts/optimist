/* Geometry (batch B of the 2026-10-14 coherence audit; docs/UI-OPTIMIST-DESIGN.md section 34; the style guide's sections
 * 3, 5, 8, 9):
 *   #6   the help band: with the help line built, the screen's last OP_FOOT rows are the line's alone. No blit but the
 *        line's own (0, OP_FOOT_Y, 240 x OP_FOOT) reaches them in the whole run (every test before this one, the walk
 *        here, the 20000 random frames after: counted in lcd_blit, ui_optimist_test.c), and with the line off they are
 *        black on every screen walked
 *   #12  units in the label row: a 1x4 card (the mixer's) holds its label and its unit with 3 px of air (LEVEL -> LVL
 *        where it cannot), a value never carries a unit after its number on a card ("10ms": 10, ms above)
 *   #16  STEP: the rows between the header and the held step's cards are black (no grid fragments)
 *   #23  a list row: the name in its 9-character column, a character's gap, the cells after it; no two texts closer
 *        than LC_GAP, none past the row's end (the cut cells: tests/ui_optimist_names.h, every value of every list)
 *   #25  FM6's ENV held map: every algorithm's operators in boxes at least 12 rows tall (their number's 10 of ink inside)
 *   #31  a SOUND FX page's needles are its own cards' values, in their order, under the 2x2 cards, labelled
 *   #34  the mixer's ACCENT lane: a ninth row across both columns, as tall as the lanes'
 * Included after ui_optimist_layout.h. */
static uint32_t geo_rows, geo_bad;
static void geo_fail(const char *what, const char *a, const char *b)
{
    if (geo_bad < 30u)
        printf("  geometry: %s '%s' (%s)\n", what, a, b);
    geo_bad++;
}
static int geo_band_black(void)                         /* the help band: black */
{
    uint32_t i;
    for (i = (uint32_t)OP_FOOT_Y * 240u; i < 240u * 240u; i++)
        if (screen[i])
            return 0;
    return 1;
}
/* row r of the list shown: its name's column, its texts apart and inside the row */
static void geo_row(uint32_t r)
{
    cell_t c[4];
    rowl_t L;
    char nm[16], t[16];
    uint32_t k;
    int32_t prev = -1000;
    SCR->name(r, nm);
    nm_title(nm, t, sizeof t);
    for (k = 0; k < 4u; k++)
        SCR->cell(r, k, &c[k]);
    row_layout(c, &L, LC_END(236));
    geo_rows++;
    if (str_len(t) > 9u)
        geo_fail("a row's name past its column", t, SCR_NAME[ui.scr % SCR_N]);
    if (LC_NAME_X + 8 * (int32_t)str_len(t) + 8 > L.x[0])
        geo_fail("a row's name touching its first cell", t, SCR_NAME[ui.scr % SCR_N]);
    for (k = 0; k < 4u; k++) {
        if (!L.t[k][0])
            continue;
        if (L.x[k] < prev + LC_GAP)
            geo_fail("two cells closer than LC_GAP", L.t[k], t);
        prev = L.x[k] + lc_w(&L, k);
        if (prev > LC_END(236))
            geo_fail("a cell past the row's end", L.t[k], t);
    }
}
static void geo_list(uint32_t scr)
{
    uint32_t r, n = SCREENS[scr].rows();
    for (r = 0; r < n; r++)
        geo_row(r);
}
static void geo_lists(void)                             /* #23: every list's rows, every engine on track 2, DR */
{
    static const uint8_t FAMS[] = {FAM_EDIT, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_ARP};
    uint32_t e, f;
    track_t keep = trk[1];
    for (e = 0; e <= NENGINES; e++) {
        if (e < NENGINES && eng_free(e))
            continue;
        if (e < NENGINES) {
            set_engine_of(&trk[1], e);
            track_select(1);
        } else
            track_select(TRK_DRUM);
        op_enter(SCR_SOUND);
        geo_list(SCR_SOUND);
        for (f = 0; f < sizeof FAMS; f++) {
            op_enter(SCR_SOUND);
            if (!snd_has_fam(FAMS[f]))
                continue;
            op_jump_sound(FAMS[f]);
            geo_list(SCR_SOUND);
        }
    }
    trk[1] = keep;
    track_select(0);
    op_enter(SCR_FX);
    geo_list(SCR_FX);
    op_enter(SCR_PROJECT);
    geo_list(SCR_PROJECT);
    op_enter(SCR_SONG);
    geo_list(SCR_SONG);
    check(geo_rows > 100u && !geo_bad, "#23 a list row: the name in its column and a gap, the cells apart and inside the row");
}
/* #12: every mixer card (1x4): label and unit apart; every card (both forms): no unit left on the value */
static void geo_units(void)
{
    uint32_t r, s, k, n = 0, bad = 0, split = 0;
    cell_t c;
    char b[16], v[16], nb[12];
    op_enter(SCR_HOME);
    for (r = 0; r < SCREENS[SCR_HOME].rows(); r++) {
        ui.row[SCR_HOME] = (uint8_t)r;
        for (s = 0; s < mx_sets(r); s++) {
            mx.set = (uint8_t)s;
            for (k = 0; k < 4u; k++) {
                const char *u, *lab;
                int32_t uw, rw;
                SCR->cell(r, k, &c);
                if (!c.label || !c.label[0] || c.kind == CK_ACT)
                    continue;
                u = card_unit(&c, v, sizeof v);
                lab = card_label_n(&c, u, b, sizeof b, nb);
                uw = u[0] ? text_w(&FONT_S, u) : 0;
                rw = (c.dr ? text_w(&FONT_S, "DR") + 4 : 0) + (c.mark ? 10 : 0);
                n += u[0] != 0;
                if (u[0] && 3 + text_w(&FONT_S, lab) + 4 > CARD_UX(0, uw, rw)) {
                    if (bad++ < 10u)
                        printf("  geometry: mixer card '%s' '%s' and '%s' collide\n", lab, v, u);
                }
            }
        }
    }
    mx.set = 0;
    ui.row[SCR_HOME] = MXR_T1;
    check(n > 10u && !bad, "#12 the mixer's 1x4 cards: every unit in the label row beside its label (LEVEL as LVL there)");
    op_enter(SCR_FX);                                   /* the master FX: the compressor's ATK "10ms" */
    for (r = 0; r < SCREENS[SCR_FX].rows(); r++)
        for (k = 0; k < 4u; k++) {
            const char *u;
            SCREENS[SCR_FX].cell(r, k, &c);
            u = card_unit(&c, v, sizeof v);
            if (str_eq(c.label ? c.label : "", "ATK") && str_eq(u, "ms") && !strstr(v, "ms"))
                split++;
        }
    check(split > 0u, "#12 a value carrying its unit (ATK 10ms): the number on the card, ms in the label row");
    ui.row[SCR_FX] = 0;
    op_enter(SCR_HOME);                                 /* drawn: LVL and dB in the LEVEL card's label row */
    ui.force = 1;
    frames(2);
    check(px_in((uint32_t)CARD_X(1) + CARD_W - 18u, OY_CARD + 4u, 16, 11, C_DIM) &&
              !px_in((uint32_t)CARD_X(1) + 3u, OY_CARD + 18u, CARD_W - 6u, 16, C_DIM),
          "... drawn: the mixer's LEVEL card, its dB in the label row, none after the value");
}
#if FELUCCA_PARAM_HELP
/* #6: every screen with the help line off: the band black */
static void geo_band_walk(void)
{
    static const uint8_t SCR_WALK[] = {SCR_HOME, SCR_SOUND, SCR_FX, SCR_STEP, SCR_SONG, SCR_SCOPE};
    uint32_t i, r, n, ok = 1;
    for (i = 0; i < sizeof SCR_WALK; i++) {
        op_enter(SCR_WALK[i]);
        n = SCR->rows();
        for (r = 0; r < n && r < 24u; r++) {
            ui.row[ui.scr] = (uint8_t)r;
            hl.t = 0;
            ui.force = 1;
            frames(2);
            if (!geo_band_black()) {
                ok = 0;
                printf("  geometry: %s row %u drew in the help band\n", SCR_NAME[ui.scr], r);
                break;
            }
        }
    }
    op_enter(SCR_HOME);
    for (i = 1; i < LY_COUNT; i++) {                    /* every layer's map */
        lay.shown = (uint8_t)i;
        ui.force = 1;
        frames(2);
        if (!geo_band_black()) {
            ok = 0;
            printf("  geometry: layer %u drew in the help band\n", i);
        }
    }
    lay.shown = LY_PLAY;
    reset_ui();
    check(ok, "#6 every screen and layer, the help line off: the help band black");
}
#endif
/* #16: a step held on STEP, its cards up: the rows over them black */
static void geo_step_strip(void)
{
    uint32_t y, ok = 1;
    reset_ui();
    track_select(0);
    op_enter(SCR_STEP);
    trk[0].step[0].time = ST_NOTE;
    trk[0].step[0].n = 1;
    trk[0].step[0].note[0] = 60;
    ui.force = 1;
    frames(2);
    fm1_in.notes |= 1u << 0;                            /* step 1 held: its cards over the grid */
    frames(HOLD_FRAMES + 2u);
    for (y = SP_Y; y < OY_CARD; y++)
        ok &= (uint32_t)!px_in(0, y, 240, 1, C_LINE) && !px_in(0, y, 240, 1, C_WHITE) && !px_in(0, y, 240, 1, C_DIM);
    check(ui.cards_t > 0u && ok, "#16 STEP, a step held: the rows between the header and its cards black (no grid fragments)");
    fm1_in.notes = 0;
    frames(2);
    reset_ui();
}
/* #31: SOUND FX's SENDS: the needles are the cards' values, in the cards' order, under the 2x2 cards */
static void geo_fx_needles(void)
{
    uint32_t r, n, k, ok = 0;
    const page_t *gp = 0;
    reset_ui();
    track_select(0);
    op_enter(SCR_SOUND);
    op_jump_sound(FAM_FX);
    n = snd_rows();
    for (r = 0; r < n; r++)
        if (snd_page(r) && snd_page(r)->graph == GR_FX) {
            ui.row[SCR_SOUND] = (uint8_t)r;
            gp = snd_page(r);
            break;
        }
    if (gp) {
        cell_t c;
        int16_t *vp[4] = {0, 0, 0, 0};
        for (k = 0; k < 4u; k++) {
            page_desc(gp, k, &vp[k]);
            page_cell(gp, k, &c);
            if (vp[k] && c.label)
                *vp[k] = (int16_t)(k == 0u ? c.d->max : c.d->min);   /* (KNOB 1 full, the others none) */
        }
        ui.force = 1;
        frames(2);
        ok = snd_graph_page(r) == gp && px_in((uint32_t)FXN_X(0u), OP_PY + 4u, 1, 3, trk_col(0)) &&
             !px_in((uint32_t)FXN_X(1u), OP_PY + 4u, 1, (uint32_t)(GRAPH_H - 28), trk_col(0));
        for (k = 0; k < 4u; k++) {                      /* (each needle named as its card) */
            char b[16];
            page_cell(gp, k, &c);
            if (c.label && c.gk != GK_NONE)
                ok &= (uint32_t)px_in((uint32_t)(FXN_X(k) - text_w(&FONT_S, card_label(&c, b, sizeof b)) / 2), OP_PY + (uint32_t)GRAPH_H - 16u,
                                      (uint32_t)text_w(&FONT_S, b), 14, C_GRAY);
        }
    }
    check(gp && ok, "#31 SOUND FX: the needles are the page's own values (KNOB 1 full, KNOB 2 none), under the 2x2 cards, named");
    reset_ui();
}
/* #34: the mixer's ACCENT lane the last row of the lanes' column (2026-10-18: one column) */
static void geo_accent_row(void)
{
#if ACC_UI
    mrect_t a, l;
    mx_rect(MXR_LN - 1u, &a);
    mx_rect(MXR_LN - 2u, &l);
    check(mx_is_acc(MXR_LN - 1u) && a.x == 0 && a.w == 240 && a.y == l.y + l.h && a.y + a.h == OP_PH && a.h >= l.h - 1 && a.h <= l.h + 1 &&
              a.sx + 16 * a.sp <= 240 && a.sx == l.sx && a.sp == l.sp && l.h >= 13,
          "#34 the mixer's ACCENT lane: the last row, as tall as a lane's (13 px at least), its steps under the lanes'");
#endif
}
/* #25: FM6's ENV held map: the operators' boxes at least 12 rows tall, every algorithm */
static void geo_fm6(void)
{
#if OP_FM6
    uint32_t a, op, worst = 99;
    int16_t vv[512];
    int32_t h = (OP_FOOT_Y - OY_PANEL_2X2) - F6_KEY_H - 1 - F6_ALG_Y;   /* (the layer's panel: the cards 2x2) */
    for (a = 0; a < 32u; a++) {
        uint8_t mods[6], car, fb;
        f6_node_t nd[6];
        int16_t rows = 0, cols = 0, rh;
        int32_t bh;
        memset(vv, 0, sizeof vv);
        vv[FV_ALG] = (int16_t)a;
        f6_graph(vv, mods, &car, &fb);
        for (op = 0; op < 6u; op++)
            nd[op].w = nd[op].x2 = nd[op].row = -1;
        for (op = 0; op < 6u; op++)
            if ((car >> op) & 1u) {
                int16_t top;
                f6_width(mods, nd, op, 0);
                top = f6_place(mods, nd, op, cols, 0, 0);
                cols = (int16_t)(cols + nd[op].w);
                rows = top + 1 > rows ? (int16_t)(top + 1) : rows;
            }
        rh = (int16_t)(rows ? ((h - 6) / rows > 24 ? 24 : (h - 6) / rows) : 24);
        bh = 7 * 2 > rh - 2 ? (rh - 2) / 2 : 7;
        worst = (uint32_t)(2 * bh) < worst ? (uint32_t)(2 * bh) : worst;
    }
    check(FV_ALG < 512 && worst >= F6_INK + 2u, "#25 FM6's ENV held map: every algorithm's boxes at least 12 rows tall, the number inside");
#endif
}
static void geometry_tests(void)
{
    geo_lists();
    geo_units();
#if FELUCCA_PARAM_HELP
    geo_band_walk();
#endif
    geo_step_strip();
    geo_fx_needles();
    geo_accent_row();
    geo_fm6();
    reset_ui();
}
