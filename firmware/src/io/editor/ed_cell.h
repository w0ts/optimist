/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: an Optimist UI card on the wire, the parts SCRN's lines (ed_scrn.c) and SCRS's cells (ed_scrs.c)
 * share, one copy: kind, label, value, unit (ed_cell_text); the colour, 0 the palette's, 1 warn, 2 error, 3 ok, 4 another
 * (ed_cell_col); the bar and the descriptor (ed_cell_bar: gmin, gmax, gv v14, then 1 and fmt, min, max, def, or 0).
 * Included by both (ed_desc.h first). */
#ifndef ED_CELL_H
#define ED_CELL_H
static void ed_cell_text(const cell_t *c)
{
    ed_b(c->kind);
    ed_str(c->label, 14);
    ed_str(c->val, 14);
    ed_str(c->unit, 8);
}
static uint32_t ed_cell_col(const cell_t *c) { return c->col == 0u ? 0u : c->col == C_WARN ? 1u : c->col == C_ERR ? 2u : c->col == C_OK ? 3u : 4u; }
static void ed_cell_bar(const cell_t *c)
{
    ed_v(c->gmin);
    ed_v(c->gmax);
    ed_v(c->gv);
    ed_b(c->d != 0);
    if (c->d)
        ed_fmd(c->d);
}
#endif
