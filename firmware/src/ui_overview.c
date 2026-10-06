/* SPDX-License-Identifier: GPL-3.0-only */
/* VIEW ALL (GLO > SYSTEM VIEW, the default): a family on PAGEs of 4 x 4 slots, four rows of the four
 * knobs. A row is one of the family's one-page pages (PAGES, params.c: rows of four slots, 0xFF = an
 * empty slot: blank, its knob does nothing), in the family's order; OV_ROWS rows make a PAGE, the
 * family's rows fill PAGE 1, then PAGE 2 ... (ov_grid: the one layout rule, also used by the FM6
 * operator editor's groups). The row the knobs edit is lit (its labels, the knobs' colours, its
 * gauges, a bar in the track colour at the left); the others are dimmed (grey values, no colour).
 * The family button steps down the rows; past the last row of a PAGE the next PAGE shows at once (no
 * scrolling), past the last row of the family PAGE 1 again. The footer says "PAGE n/m".
 *   EDIT   EDIT 1, EDIT 2, VOICE, VOICE 2        PAGE 1/1
 *          (an ANALOG track, ANALOG 2: EDIT 1, EDIT 2, OSC 2, SWARM | FLT 2, VOICE, VOICE 2: 2 PAGEs)
 *   ENV    ENV, ENV DEST + the envelope           two rows over a shorter graph
 *          (an ANALOG track: ENV1, ENV1 DEST, ENV2, ENV2 DEST: four rows, no graph)
 *   LFO    LFO, LFO DEST + the wave               two rows over a shorter graph
 *   ARP    ARP, ARP 2 + the arp's bar             two rows over a shorter graph
 *   SCL    SCL, SCL 2 + the keyboard              two rows over a shorter keyboard
 *   FX     FX, SLICER, DLY, REV/CHO               four rows (the send bars and the slicer steps go)
 *   GLO    GLOBAL, MASTER, SYSTEM, DRUMS          four rows
 * Every cell keeps the one-page column's look (icon, label / value unit / gauge) at the same size:
 * the 8 x 16 font, so every value reads as on a single page. The other families, HOME and the drum
 * track's "DRUM TRACK" pages keep the one-page layout (ui_draw.c). VIEW is a setting (panel.c
 * settings.view, saved with the palette), shown and edited as the global G_VIEW (view_sync). */
#define OV_Y 23                                       /* the first row, under the header rule */
#define OV_RH 44                                      /* a row: label 0..15, value 17..32, gauge 35..39 */
#define OV_CH 41                                      /* the cell's canvas height */
#define OV_ROWS 4u                                    /* rows of a PAGE (x 4 knobs: 16 slots) */
#define OV_MAXROWS 16u                                /* rows of a family (four PAGEs) */

static struct {
    char key[OV_ROWS][4][36];                         /* what each cell shows (drawn when it changes) */
    uint32_t bar_sig, graph_sig, grid_sig;
} ov;

/* the layout rule of VIEW ALL: nrows rows, the lit one lit, OV_ROWS to a PAGE. Returns the rows on the
 * lit row's PAGE; *first = its first row, *act = the lit row on it, *page / *npages: "PAGE n/m" */
static uint32_t ov_grid(uint32_t nrows, uint32_t lit, uint32_t *first, uint32_t *act, uint32_t *page,
                        uint32_t *npages)
{
    uint32_t p = nrows ? (lit < nrows ? lit : nrows - 1u) / OV_ROWS : 0u, f = p * OV_ROWS;
    *first = f;
    *act = lit - f;
    *page = p;
    *npages = (nrows + OV_ROWS - 1u) / OV_ROWS;
    return nrows - f < OV_ROWS ? nrows - f : OV_ROWS;
}

/* "PAGE n/m" into b (at least 12 bytes) */
static void ov_page_label(char *b, uint32_t page, uint32_t npages, int lower)
{
    str_cpy(b, lower ? "page " : "PAGE ", 8);
    fmt_int(b + 5, (int32_t)page + 1);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), (int32_t)(npages ? npages : 1u));
}

/* ARP (the page's graph, and under the rows in VIEW ALL): one bar of the arp on a C major chord over OCT
 * octaves, as seq.c arp_next plays it. A mark per step of RATE, GATE long, every other step SWG late
 * (swing_units: 100 = half a step, MPC 75 %), the notes up / down / up-down / random by MODE, as
 * played (E C G) with ORD PLAY; a step PROB would skip is dim. OFF: the chord held through the bar.
 * Its height: gr_top .. gr_bot (the page 8 .. 90, VIEW ALL shorter) */
static void graph_arp(const track_t *t, uint16_t c)
{
    static const uint8_t SORTED[3] = {0, 4, 7}, PLAYED[3] = {4, 0, 7};
    const uint8_t *ch = t->p[P_AORDER] ? PLAYED : SORTED;
    uint32_t mode = (uint32_t)t->p[P_AMODE], oct = (uint32_t)clamp(t->p[P_AOCT], 1, 4), len = 3u * oct;
    uint32_t steps = 4u * DIV_DEN[(uint32_t)t->p[P_ARATE] % 6u], i, b;
    int32_t top = gr_top, bot = gr_bot, hi = 12 * ((int32_t)oct - 1) + 7, sw = 236 / (int32_t)steps;
    int32_t gate = sw * t->p[P_AGATE] / 128, late = (int32_t)clamp(t->p[P_ASWING], 0, 100) * sw / 200;
#define ARPY(n) (bot - 2 - (int32_t)(n) * (bot - top - 4) / hi)
    for (b = 0; b < 4u; b++)                          /* the beats */
        cv_rect(2 + (int32_t)b * 59, bot + 1, 1, 3, C_LINE);
    cv_line(0, bot + 1, 239, bot + 1, C_LINE);
    if (!mode) {                                      /* OFF: the chord as played */
        for (i = 0; i < 3u; i++)
            cv_rect(2, ARPY(ch[i]), 234, 2, C_DIM);
        return;
    }
    for (i = 0; i < steps; i++) {
        uint32_t k = i + 1u, j, cyc = len > 1u ? 2u * len - 2u : 1u, h = k * 2654435761u;
        int32_t x = 2 + (int32_t)i * 236 / (int32_t)steps + ((i & 1u) ? late : 0);
        j = mode == 2u ? len - 1u - k % len : mode == 3u ? (k % cyc < len ? k % cyc : cyc - k % cyc) :
            mode == 4u ? (h >> 16) % len : k % len;
        cv_rect(x, ARPY(ch[j % 3u] + 12u * (j / 3u)) - 1, gate > 1 ? gate : 1, 3,
                (int32_t)((h >> 8) % 127u) >= t->p[P_APROB] ? C_DIM : c);
    }
#undef ARPY
}

/* G_VIEW (the page, the editor) and settings.view (flash) follow each other; a change is saved like the
 * menu's settings: at once when stopped, else once the transport stops (project.c) */
static void view_sync(void)
{
    uint32_t v = song.g[G_VIEW] ? 1u : 0u;
    if (v == settings.view)
        return;
    settings.view = v;
    if (song.playing || transport_req)
        settings_later = 1;
    else
        settings_save();
    ui.force = 1;
}

static int ov_family(uint32_t fam)
{
    return fam == FAM_EDIT || fam == FAM_ENV || fam == FAM_LFO || fam == FAM_FX || fam == FAM_GLO ||
           fam == FAM_SCL || (FELUCCA_OV_ARP && fam == FAM_ARP);
}

static int ov_on(void)
{
    const page_t *pg = cur_page();
    view_sync();
    return FELUCCA_OVERVIEW && settings.view && ov_family(pg->fam) && pg->scope != SC_FM6K && !(is_drum(TSEL) && !page_for_drum(pg));
}

/* the rows of the lit row's PAGE (indices into PAGES), at most OV_ROWS; *act = the lit row on it; *page,
 * *npages for "PAGE n/m". The family's rows: its pages shown on this track (an ANALOG track's EDIT has
 * ANALOG 2's), the drum track leaving out the pages it has no values on (FX: the sends are the kit's),
 * no empty row; the FM6 operator editor (ENV's last page, SC_FM6K) is no row: it draws its own screen */
static uint32_t ov_rows(uint8_t *idx, uint32_t *act, uint32_t *page, uint32_t *npages)
{
    uint8_t all[OV_MAXROWS];
    uint32_t i, n = 0, a = 0, first, fam = cur_page()->fam, drum = (uint32_t)is_drum(TSEL);
    for (i = 0; i < NPAGES && n < OV_MAXROWS; i++)
        if (PAGES[i].fam == fam && PAGES[i].scope != SC_FM6K && !(drum && !page_for_drum(&PAGES[i])) &&
            page_shown(&PAGES[i])) {
            if (i == ui.page)
                a = n;
            all[n++] = (uint8_t)i;
        }
    n = ov_grid(n, a, &first, act, page, npages);
    for (i = 0; i < n; i++)
        idx[i] = all[first + i];
    return n;
}

static uint32_t ov_pages(uint8_t *idx, uint32_t *act)
{
    uint32_t page, npages;
    return ov_rows(idx, act, &page, &npages);
}

/* the footer's "PAGE n/m" (ui_draw.c draw_foot, VIEW ALL) */
static void ov_foot_label(char *b)
{
    uint8_t idx[OV_ROWS];
    uint32_t act, page, npages;
    ov_rows(idx, &act, &page, &npages);
    ov_page_label(b, page, npages, 0);
}

/* the graph under the rows (ENV, LFO, ARP, SCL), 0 = none */
static uint32_t ov_graph(void)
{
    uint32_t fam = cur_page()->fam;
    return fam == FAM_ENV ? GR_ADSR : fam == FAM_LFO ? GR_LFO : fam == FAM_SCL ? GR_SCALE : fam == FAM_ARP ? GR_ARP :
           fam == FAM_EDIT && is_drum(TSEL) ? GR_DSND : GR_NONE;   /* (the drum track: SOUND, when there is room) */
}

static void ov_frame(void)
{
    uint32_t i, act, n, h, page, npages;
    uint8_t idx[OV_ROWS];
    n = ov_rows(idx, &act, &page, &npages);
    ov.grid_sig = n + page * 8u + cur_page()->fam * 64u + song.sel * 1024u;
    h = n * OV_RH;
    lcd_fill(0, H_HEAD + 1, 240, Y_FOOT - 2 - H_HEAD - 1, C_BLACK);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    lcd_fill(0, Y_FOOT - 2, 240, 1, C_LINE);
    for (i = 1; i < 4u; i++)                          /* the column rules through the rows */
        lcd_fill(i * 60u - 1u, OV_Y + 1, 1, h - 6, C_LINE);
    for (i = 1; i < n; i++)                           /* a short rule between two rows, in each column */
        lcd_fill(0, OV_Y + i * OV_RH - 3u, 240, 1, C_BLACK);
    memset(ov.key, 0, sizeof ov.key);
    ov.bar_sig = ov.graph_sig = 0xFFFFFFFFu;
}

/* one cell: [icon] LABEL / value unit / gauge; lit (the page the knobs edit) or dimmed */
static void ov_cell(uint32_t r, uint32_t c, int32_t y, const char *label, const char *val, const char *unit,
                    uint16_t vc, int32_t ratio, uint32_t icon, int lit)
{
    char l[8], v[8], u[8], key[36];
    int32_t x, gw = 52, mot = col_mot && label[0];   /* (MOTION moves it: ui_draw.c, #63) */
    uint32_t n;
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    fit(l, label, &FONT_S, icon == ICON_NONE ? 54 : 54 - LABEL_X);   /* (no icon: the label from the left) */
    fit(v, val, &FONT_S, is_eng_name(val) ? 56 : 40);
    fit(u, unit, &FONT_S, 54 - text_w(&FONT_S, v) - 3);
    str_cpy(key, l, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), v, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), u, 8);
    n = str_len(key);
    key[n] = (char)('A' + (vc == C_WHITE) + (vc == C_DIM) * 2 + lit * 4 + (c & 3u) * 8);
    key[n + 1] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio / 20));
    key[n + 2] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);
#if FELUCCA_MOTION && FELUCCA_MOTION_MARK
    key[n + 3] = (char)(mot ? 'M' : 0);
    key[n + 4] = 0;
    col_mot = 0;
#else
    key[n + 3] = 0;
#endif
    if (!ui.force && str_eq(key, ov.key[r][c]))
        return;
    str_cpy(ov.key[r][c], key, sizeof ov.key[r][c]);
    cv_begin(55, OV_CH, C_BLACK);
    if (FELUCCA_ICONS && icon != ICON_NONE && l[0])
        cv_icon(0, 1, icon, lit ? TE_COL[c & 3u] : C_DIM);
    cv_text(l[0] && icon != ICON_NONE ? LABEL_X : 0, 0, &FONT_S, l, lit ? C_GRAY : C_DIM);
    if (mot)
        cv_rect(50, 2, 4, 4, lit ? TE_COL[c & 3u] : C_DIM);
    x = cv_text(0, 17, &FONT_S, v, vc);
    cv_text(x + 3, 17, &FONT_S, u, C_DIM);
    if (ratio >= 0) {
        int32_t fx = ratio * gw / 1000;
        cv_rect(0, 37, gw, 1, C_LINE);
        if (lit) {
            cv_rect(0, 36, fx, 3, vc == C_DIM ? C_DIM : TE_DIM[c & 3u]);
            cv_rect(fx, 35, 1, 5, vc == C_DIM ? C_HI : vc);
        } else {
            cv_rect(fx, 35, 1, 5, C_DIM);             /* dimmed: the position only */
        }
    }
    cv_blit(c * 60u + 4u, (uint32_t)y);
}

/* the four columns of page pg in row r (as draw_columns' parameter pages) */
static void ov_row(uint32_t r, const page_t *pg, int lit)
{
    uint32_t c;
    int32_t y = OV_Y + (int32_t)(r * OV_RH);
    char val[12];
    const char *unit;
    int off = fx_page_off(pg);                        /* FX / SLICER while the track plays dry */
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, c, &vp);
        uint16_t vc = lit ? VAL(c) : C_GRAY;
        if (!lit || !(c == ui.hot_col && ui.hot_t))
            vc = off ? C_DIM : vc;
        if (!d || !d->label || d->label[0] == '-') {
            ov_cell(r, c, y, "", "", "", C_HI, -1, ICON_AUTO, lit);
            continue;
        }
        if (pg->scope == SC_GLOBAL && pg->id[c] == G_MIDI) {
            const char *l = midi_status(val, &unit);
            ov_cell(r, c, y, l, val, unit, !lit ? C_GRAY : unit[0] ? C_WHITE : C_HI, -1, ICON_AUTO, lit);
            continue;
        }
        if (pg->scope == SC_GLOBAL && pg->id[c] == G_INFO) {
            cpu_info(val, &unit);
        } else {
            param_format(d, *vp, val, &unit);
        }
#if FELUCCA_MOTION && FELUCCA_MOTION_MARK
        col_mot = (uint8_t)(vp >= TSEL->p && vp < TSEL->p + P_COUNT && motion_drives(song.sel, (uint32_t)(vp - TSEL->p)));
#endif
        ov_cell(r, c, y, d->label, val, unit, vc, d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, enum_rank(d, *vp)),
                param_icon(d, *vp), lit);
    }
}

/* the bar at the left of the lit row (the track colour), in the 4 px inset of column 1 */
static void ov_bar(uint32_t n, uint32_t act)
{
    uint32_t sig = n * 7u + act * 131u + song.sel * 1009u;
    if (!ui.force && sig == ov.bar_sig)
        return;
    ov.bar_sig = sig;
    cv_begin(3, n * OV_RH, C_BLACK);
    cv_rect(0, (int32_t)(act * OV_RH), 2, OV_CH - 2, TE_COL[song.sel & 3u]);
    cv_blit(0, OV_Y);
}

/* ENV / LFO / SCL: the graph in the room under the rows, drawn as on the page with less height */
static void ov_draw_graph(uint32_t n)
{
    const track_t *t = TSEL;
    uint32_t g = ov_graph(), y0 = OV_Y + n * OV_RH - 2u, h = (uint32_t)(Y_FOOT - 3) - y0, sig = 2166136261u, i;
    if (g == GR_NONE || h < 40u)
        return;
    for (i = 0; i < P_COUNT; i++)
        sig = (sig ^ (uint32_t)t->p[i]) * 16777619u;
#if DL_UI
    if (g == GR_DSND)
        sig ^= dsnd_sig();
#endif
    sig ^= g * 31u + song.sel * 7777u;
    if (!ui.force && sig == ov.graph_sig)
        return;
    ov.graph_sig = sig;
    cv_begin(240, h, C_BLACK);
    cv_oy = 0;
#if DL_UI
    if (g == GR_DSND)
        graph_dsnd(2, (int32_t)h - 4, TE_COL[song.sel & 3u]);
    else
#endif
    if (g == GR_ADSR || g == GR_SCALE || g == GR_ARP) {
        gr_top = 6;
        gr_bot = (int32_t)h - 6;
        if (g == GR_ADSR)
            graph_adsr(t, TE_COL[song.sel & 3u]);
        else if (g == GR_ARP && FELUCCA_OV_ARP)
            graph_arp(t, TE_COL[song.sel & 3u]);
        else
            graph_scale(t, TE_COL[song.sel & 3u]);
    } else {
        gr_mid = (int32_t)h / 2;
        gr_amp = (int32_t)h / 2 - 6;
        graph_lfo(t, TE_COL[song.sel & 3u]);
    }
    gr_top = 8, gr_bot = 90, gr_mid = 50, gr_amp = 38;   /* (the page's own sizes back) */
    if (ui.force)
        lcd_dirty_reset();
    cv_blit_dirty(y0, 0);                             /* only the changed rectangle (lcd_dirty.c) */
}

static void ov_draw(void)
{
    uint8_t idx[OV_ROWS];
    uint32_t act, page, npages, n = ov_rows(idx, &act, &page, &npages), r;
    if (n + page * 8u + cur_page()->fam * 64u + song.sel * 1024u != ov.grid_sig) {
        ui.force = 1;                                 /* another PAGE (or family): its frame, at once */
        ov_frame();
    }
    for (r = 0; r < n; r++)
        ov_row(r, &PAGES[idx[r]], r == act);
    ov_bar(n, act);
    ov_draw_graph(n);
}

#if FELUCCA_FM6_KEYS
/* ------------------------------------------------- the FM6 operator editor, VIEW ALL --- */
/* ui_fm6.c's screen with VIEW ALL: under its header the group's pages (an operator's six, PIT's two, GLO's
 * five) as rows of 4 x 4 PAGEs (ov_grid), the page the knobs edit lit; the line at the bottom: the lit
 * row's name and "page n/m", or with ENV held the black keys' map */
#define OVF_Y 42                                      /* the first row, under the 40-row header */
#define OVF_LINE 220                                  /* the bottom line: 220 .. 239 */

static void ov_fm6_row(uint32_t r, const fm6k_page_t *p, int lit)
{
    uint32_t c;
    int32_t y = OVF_Y + (int32_t)(r * OV_RH);
    char val[10];
    for (c = 0; c < 4u; c++) {
        const fm6k_pd_t *d = &p->p[c];
        int32_t x = fm6k_value(d);
        uint16_t vc = lit ? VAL(c) : C_GRAY;
        if (d->kind == FK_NONE) {                     /* an empty slot: blank, its knob does nothing */
            ov_cell(r, c, y, "", "", "", C_HI, -1, ICON_NONE, lit);
            continue;
        }
        fm6k_format(d, x, val);
        if (d->kind == FK_GO && ui.arm == 0xF0u + d->off)
            vc = C_WHITE;                             /* armed: "again" */
        ov_cell(r, c, y, d->lab, val, "", vc, d->kind == FK_GO ? -1 : d->max ? x * 1000 / d->max : 0, ICON_NONE, lit);
    }
}

static void ov_fm6_frame(uint32_t n)
{
    uint32_t i;
    lcd_fill(0, 40, 240, 200, C_BLACK);
    for (i = 1; i < 4u; i++)                          /* the column rules through the rows */
        lcd_fill(i * 60u - 1u, OVF_Y + 1, 1, n * OV_RH - 6, C_LINE);
    lcd_fill(0, OVF_LINE - 3, 240, 1, C_LINE);
    memset(ov.key, 0, sizeof ov.key);
    ov.bar_sig = ov.graph_sig = 0xFFFFFFFFu;
}

static void ov_fm6_line(const fm6k_page_t *lit, uint32_t page, uint32_t npages)
{
    char pl[12];
    uint32_t sig;
    ov_page_label(pl, page, npages, 1);
    sig = studio_hash(studio_hash(page * 7u + npages * 131u, lit->name), pl);
    if (!ui.force && sig == ov.graph_sig)
        return;
    ov.graph_sig = sig;
    cv_begin(240, 240 - OVF_LINE, C_BLACK);
    cv_text(4, 2, &FONT_S, lit->name, C_WHITE);
    cv_text(236 - text_w(&FONT_S, pl), 2, &FONT_S, pl, npages > 1u ? C_HI : TE_G3);
    cv_blit(0, OVF_LINE);
}

static void ov_fm6_draw(void)
{
    uint32_t ng, first, act, page, npages, n, r, k = fm6k_kind(), sig;
    const fm6k_page_t *g = fm6k_group(&ng);
    n = ov_grid(ng, fm6ui.sub[k] % ng, &first, &act, &page, &npages);
    sig = 0x46360000u + n + page * 8u + fm6ui.target * 64u;
    if (ui.force || sig != ov.grid_sig) {             /* in, another PAGE or group: its frame */
        ov.grid_sig = sig;
        ui.force = 1;
        ov_fm6_frame(n);
    }
    for (r = 0; r < n; r++)
        ov_fm6_row(r, &g[first + r], r == act);
    if (ui.force || act * 131u + n + 0x4636u != ov.bar_sig) {   /* the lit row's bar (the track colour) */
        ov.bar_sig = act * 131u + n + 0x4636u;
        cv_begin(3, n * OV_RH, C_BLACK);
        cv_rect(0, (int32_t)(act * OV_RH), 2, OV_CH - 2, TE_COL[song.sel & 3u]);
        cv_blit(0, OVF_Y);
    }
    if (fm6ui.env_down) {                             /* ENV held: the black keys' map on the line */
        ov.graph_sig = 0xFFFFFFFFu;
        fm6k_draw_info(fm6k_ed(), OVF_LINE, 1);
    } else {
        if (fm6ui.sig[2] != 0xFFFFFFFFu) {            /* (the map was there: the line anew) */
            fm6ui.sig[2] = 0xFFFFFFFFu;
            ov.graph_sig = 0xFFFFFFFFu;
        }
        ov_fm6_line(&g[fm6ui.sub[k] % ng], page, npages);
    }
}
#endif
