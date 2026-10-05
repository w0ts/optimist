/* SPDX-License-Identifier: GPL-3.0-only */
/* VIEW ALL (GLO > SYSTEM VIEW, the default): the pages of a family at once, one row of four columns
 * per page, in the family's order. The page the knobs edit is lit (its labels, the knobs' colours,
 * its gauges, a bar in the track colour at the left); the others are dimmed (grey values, no colour).
 * The family buttons step through the pages as before: the light moves down the rows.
 *   EDIT   EDIT 1, EDIT 2, VOICE, VOICE 2        four rows
 *   ENV    ENV, ENV DEST + the envelope           two rows over a shorter graph
 *   LFO    LFO, LFO DEST + the wave               two rows over a shorter graph
 *   FX     FX, SLICER, DLY, REV/CHO               four rows (the send bars and the slicer steps go)
 *   GLO    GLOBAL, MASTER, SYSTEM, DRUMS          four rows
 * Every cell keeps the one-page column's look (icon, label / value unit / gauge) at the same size:
 * the 8 x 16 font, so every value reads as on a single page. The other families, HOME and the drum
 * track's "DRUM TRACK" pages keep the one-page layout (ui_draw.c). VIEW is a setting (panel.c
 * settings.view, saved with the palette), shown and edited as the global G_VIEW (view_sync). */
#define OV_Y 23                                       /* the first row, under the header rule */
#define OV_RH 44                                      /* a row: label 0..15, value 17..32, gauge 35..39 */
#define OV_ROWS 4
#define OV_CH 41                                      /* the cell's canvas height */

static struct {
    char key[OV_ROWS][4][36];                         /* what each cell shows (drawn when it changes) */
    uint32_t bar_sig, graph_sig;
} ov;

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
    return fam == FAM_EDIT || fam == FAM_ENV || fam == FAM_LFO || fam == FAM_FX || fam == FAM_GLO;
}

static int ov_on(void)
{
    const page_t *pg = cur_page();
    view_sync();
    return settings.view && !ui.home && ov_family(pg->fam) && !(is_drum(TSEL) && !page_for_drum(pg));
}

/* the family's pages (indices into PAGES), at most OV_ROWS; *act = the row of the current page. The drum
 * track leaves out the pages it has no values on (FX: the sends are the kit's), no empty row */
static uint32_t ov_pages(uint8_t *idx, uint32_t *act)
{
    uint32_t i, n = 0, fam = cur_page()->fam, drum = (uint32_t)is_drum(TSEL);
    *act = 0;
    for (i = 0; i < NPAGES && n < OV_ROWS; i++)
        if (PAGES[i].fam == fam && !(drum && !page_for_drum(&PAGES[i]))) {
            if (i == ui.page)
                *act = n;
            idx[n++] = (uint8_t)i;
        }
    return n;
}

/* the graph under the rows (ENV, LFO), 0 = none */
static uint32_t ov_graph(void)
{
    uint32_t fam = cur_page()->fam;
    return fam == FAM_ENV ? GR_ADSR : fam == FAM_LFO ? GR_LFO : GR_NONE;
}

static void ov_frame(void)
{
    uint32_t i, act, n, h;
    uint8_t idx[OV_ROWS];
    n = ov_pages(idx, &act);
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
    int32_t x, gw = 52;
    uint32_t n;
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    fit(l, label, &FONT_S, 54 - LABEL_X);
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
    key[n + 3] = 0;
    if (!ui.force && str_eq(key, ov.key[r][c]))
        return;
    str_cpy(ov.key[r][c], key, sizeof ov.key[r][c]);
    cv_begin(55, OV_CH, C_BLACK);
    if (FELUCCA_ICONS && icon != ICON_NONE && l[0])
        cv_icon(0, 1, icon, lit ? TE_COL[c & 3u] : C_DIM);
    cv_text(l[0] ? LABEL_X : 0, 0, &FONT_S, l, lit ? C_GRAY : C_DIM);
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
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            ov_cell(r, c, y, "USB", val, "", lit ? C_HI : C_GRAY, -1, ICON_AUTO, lit);   /* (the label says USB) */
            continue;
        }
        if (pg->scope == SC_GLOBAL && pg->id[c] == G_INFO) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        ov_cell(r, c, y, d->label, val, unit, vc, d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
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

/* ENV / LFO: the graph in the room under the rows, drawn as on the page with less height */
static void ov_draw_graph(uint32_t n)
{
    const track_t *t = TSEL;
    uint32_t g = ov_graph(), y0 = OV_Y + n * OV_RH - 2u, h = (uint32_t)(Y_FOOT - 3) - y0, sig = 2166136261u, i;
    if (g == GR_NONE)
        return;
    for (i = 0; i < P_COUNT; i++)
        sig = (sig ^ (uint32_t)t->p[i]) * 16777619u;
    sig ^= g * 31u + song.sel * 7777u;
    if (!ui.force && sig == ov.graph_sig)
        return;
    ov.graph_sig = sig;
    cv_begin(240, h, C_BLACK);
    cv_oy = 0;
    if (g == GR_ADSR) {
        gr_top = 6;
        gr_bot = (int32_t)h - 6;
        graph_adsr(t, TE_COL[song.sel & 3u]);
    } else {
        gr_mid = (int32_t)h / 2;
        gr_amp = (int32_t)h / 2 - 6;
        graph_lfo(t, TE_COL[song.sel & 3u]);
    }
    gr_top = 8, gr_bot = 90, gr_mid = 50, gr_amp = 38;   /* (the page's own sizes back) */
    cv_blit(0, y0);
}

static void ov_draw(void)
{
    uint8_t idx[OV_ROWS];
    uint32_t act, n = ov_pages(idx, &act), r;
    for (r = 0; r < n; r++)
        ov_row(r, &PAGES[idx[r]], r == act);
    ov_bar(n, act);
    ov_draw_graph(n);
}
