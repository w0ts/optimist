/* SPDX-License-Identifier: GPL-3.0-only */
/* The one-line parameter help (FELUCCA_PARAM_HELP, builder item PARAM_HELP): while a knob changes a value, the top
 * bar says in plain words what it is ("Filter cutoff"), until the white value of the knob goes back (ui.hot_t,
 * PH_HOT frames, ~1 s after the last detent). The lines: tools/param_help.json, one table for the device and the web editor's
 * tooltips; tools/gen_param_help.py makes build/gen/felucca_param_help.h (PH_BLOB, in flash; only the lines of the
 * features built; the texts packed, ui/ph_blob.c). Nothing in RAM but the line's offset (ui.help, in the padding at
 * the end of ui) and no RAM code: a knob looks its line up once (ph_find, a walk over the table), the draw unpacks it
 * from flash into a line on its stack (ph_text). Included by ui.c. A build without it: ph_line() is 0, PH_PICK /
 * PH_SET do nothing. */
static uint32_t str_hash(uint32_t h, const char *s);   /* ui_draw.c (the screens' signatures of the line) */
/* frames a parameter knob's value stays white after a detent (ui.hot_t), and with it the help line: ~1 s at the UI's
 * ~60 frames a second (main.c: 15 ms a frame at least); 40 (~0.65 s) without the help line, as before */
#define PH_HOT (FELUCCA_PARAM_HELP ? 64u : 40u)
#if FELUCCA_PARAM_HELP
/* ph_find: the line of a label in a context of a kind (PH_PAGE: a page title or live screen, PH_ENG: an engine's
 * name, PH_FM6: an FM6 editor page), as its offset in PH_BLOB, 0 = none; ph_text: that line unpacked */
#include "../ph_blob.c"

/* KNOB k of a live screen (TRACKS, DRUM GRID, DRUM KIT, SONG ...: the table's "slots") */
static void ph_slot(const char *ctx, uint32_t k)
{
    char l[2] = {(char)('1' + (k & 3u)), 0};
    ui.help = (uint16_t)ph_find(PH_PAGE, ctx, l);
}
#define PH_SET(kind, ctx, label) (ui.help = (uint16_t)ph_find((kind), (ctx), (label)))
#define PH_SLOT(ctx, k) ph_slot((ctx), (k))
#define PH_CLEAR() (ui.help = 0)

/* KNOB k of page pg (ui_input.c): an engine's EDIT value by the engine and its label (a mode's own label too), any
 * other value by the page's title and its label, a page without descriptors (STEP, TRACKS, USER ...) by the knob */
static void ph_pick(const page_t *pg, uint32_t k)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, k, &vp);
    if (!d || !d->label)
        PH_SLOT(pg->title, k);
    else if (pg->scope == SC_ENGINE)
        PH_SET(PH_ENG, ENGINES[TSEL->eng_req % NENGINES]->name, d->label);
    else
        PH_SET(PH_PAGE, pg->title, d->label);
}
#define PH_PICK(pg, k) ph_pick((pg), (k))
/* the line to show now, as its offset in PH_BLOB (ph_text unpacks it), 0 = none (the knob's white value is gone, or
 * no line) */
static uint32_t ph_line(void) { return ui.hot_t ? ui.help : 0u; }
#else
#define PH_SET(kind, ctx, label) ((void)0)
#define PH_SLOT(ctx, k) ((void)0)
#define PH_CLEAR() ((void)0)
#define PH_PICK(pg, k) ((void)0)
#define ph_line() 0u
#define PH_LINE 1
#define ph_text(off, buf) ((void)(off), (const char *)(buf))
#endif
