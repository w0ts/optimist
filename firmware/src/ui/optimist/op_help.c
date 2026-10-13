/* SPDX-License-Identifier: GPL-3.0-only */
/* The help line (FELUCCA_PARAM_HELP, builder item PARAM_HELP; docs/UI-OPTIMIST-DESIGN.md section 21; the user,
 * 2026-10-13: "we should have a help text on the bottom last line. this info text should be displayed everywhere when
 * i modify a value or in the vertical menus"). One line of small type on the screen's last HL_H rows, in plain words:
 *   a value turned     on any screen (SOUND, the mixer, MASTER, STEP's cards, TEMPO, SONG ...): what the knob changes,
 *                      while it turns and HL_FRAMES (~2 s) after its last detent; another screen lets it go
 *   a vertical menu    GLOBAL and SYSTEM's settings list (the cursor line) and PROJECT (the cursor row's hot cell): always
 *                      there, the list made shorter for it (op_gldraw.c GL_LINE_H, op_draw.c LIST_H); an action that
 *                      asks first says so ("(asks)")
 * Not under a question (the modal), the REC ring, a layer's map or NAME. While the line shows nothing else is drawn on
 * its rows (gfx.c cv_clip); when it goes, every band is drawn again over itself (op_repaint: in place, no flash).
 * The words: tools/param_help.json, one table with SLOOP's UI and the web editor (tools/gen_param_help.py ->
 * build/gen/felucca_param_help.h, PH_BLOB, a const string in flash: only the lines of the features built; the texts
 * packed, ui/ph_blob.c, unpacked into a line on the stack when drawn). A line is
 * found as (kind op, the row's name, the cell's label), then (op, the screen's name, the label), then by the value's
 * descriptor: the page of PAGES that shows it, as SLOOP's line (an engine's value by the engine). Looked up when what
 * the line is about changes, never every frame; main loop only, nothing in RAM code. Included after op_gldraw.c. */
#if FELUCCA_PARAM_HELP
#include "../ph_blob.c"                  /* ph_find, ph_text (the texts packed) */
#define HL_H 18                         /* the line: a rule, then the small face (16 rows) */
#define HL_Y (240 - HL_H)
#define HL_FRAMES 120u                  /* ~2 s at the UI's ~60 frames a second after a value's last detent */
static struct {
    uint8_t t;                          /* frames the line stays after a turn (0: only a vertical menu shows it) */
    uint8_t scr, row, k;                /* the value turned */
    uint8_t on;                         /* shown now (cv_clip keeps the bands off its rows) */
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
        const page_t *pg = op_pg(i);
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
    if (!o)
        o = ph_find(PH_OP, SCR_NAME[scr % SCR_N], c->label);
    if (!o && c->d)
        o = hl_desc(c->d);
    return o;
}
/* a vertical menu: the line always there (GLOBAL and SYSTEM's settings list, PROJECT's rows) */
static int hl_menu(void) { return (glo_cycle() || ui.scr == SCR_PROJECT) && lay.shown == LY_PLAY && !name_on(); }
static int32_t hl_room(void) { return ui.scr == SCR_PROJECT && hl_menu() ? HL_H : 0; }   /* (op_draw.c LIST_H) */
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
/* before the bands (op_frame_draw, ov: the overlay): shown or not this frame; going, the bands come back over its rows */
static void hl_pre(uint32_t ov)
{
    int vis = (hl_menu() || (hl.t && hl.scr == ui.scr && lay.shown == LY_PLAY && !name_on())) && ov != 1u && ov != 3u;
    if (vis && !hl_menu() && !hl_text())
        vis = 0;                                        /* (a value with no line: nothing; a menu keeps its empty line) */
    if (vis == hl.on)
        return;
    hl.on = (uint8_t)vis;
    cv_clip = vis ? (uint32_t)HL_Y : 240u;
    ui.sig[5] = 0;
    if (!vis)
        op_repaint();                                   /* (every band again, in place: what the line covered) */
}
/* after the bands: the line, when what it says changed */
static void hl_post(void)
{
    char b[PH_LINE];
    uint32_t off, sig;
    if (!hl.on)
        return;
    off = hl_text();
    sig = hu(hu(0x4E1Fu, settings.palette), off);
    if (sig == ui.sig[5])
        return;
    ui.sig[5] = sig;
    cv_clip = 240u;
    cv_begin(240, HL_H, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    if (off)
        cv_text(4, 2, &FONT_S, ph_text(off, b), C_AMB);
    cv_blit(0, HL_Y);
    cv_clip = (uint32_t)HL_Y;
}
#endif
