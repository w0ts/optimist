/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen in words for a screen reader (fm1-reader: charlesvestal.github.io/fm1-reader; fm1-base's device/announce.c
 * keeps the protocol). Only where the build has it (OPT_SPEAK: Optimist on fm1-base, ui/opt_ui.c), and nothing is made
 * while no reader listens (opt_ann_on). What is said:
 *   a page opened (another screen or row), or the reader asking (WHERE): the page's title, then its four cards
 *   a card's value changed (a knob, PRESETS): that card, label and value, once the turn rests (opt_ann_later)
 *   a message (ui_say): its words, at once
 * The words are the screen's own; the reader's words file (words.tsv, its url in the HELLO) says them in full. */
int opt_ann_on(void);                                   /* fm1-base app.c: announce.c's, for Optimist's unit */
int opt_ann_where(void);
void opt_ann_say(const char *t, uint32_t flags);        /* flags: 1 cut off what is being said, 2 a warning */
void opt_ann_later(const char *t);                      /* said once it stays the newest a moment */

#define SPK_MAX 160u
static struct {
    uint32_t scr, row;
    uint8_t ok;
    char v[4][16];                                      /* the cards' values as last said */
} spk;
static void spk_cat(char *d, const char *s)
{
    uint32_t n = str_len(d);
    while (*s && n < SPK_MAX - 1u)
        d[n++] = *s++;
    d[n] = 0;
}
/* a card as words: "CUT 64 HZ", an action its verb; nothing for an empty card */
static void spk_card(char *t, const cell_t *c, char *v)
{
    char l[16];
    const char *u;
    v[0] = 0;
    if (!c->label)
        return;
    if (c->kind == CK_ACT) {
        str_cpy(v, act_verb(c), 16);
        spk_cat(t, v);
        return;
    }
    u = card_unit(c, v, 16);
    spk_cat(t, card_label(c, l, sizeof l));
    spk_cat(t, " ");
    spk_cat(t, v);
    if (u[0]) {
        spk_cat(t, " ");
        spk_cat(t, u);
    }
}
/* every frame the cards are made (draw_cards): what changed since the reader last heard */
static void speak_cards(const cell_t *c)
{
    char t[SPK_MAX], v[16];
    uint32_t k, row = ui.row[ui.scr], page;
    if (!opt_ann_on()) {
        spk.ok = 0;
        return;
    }
    page = opt_ann_where() || !spk.ok || spk.scr != ui.scr || spk.row != row;
    t[0] = 0;
    if (page) {
        head_t h;
        head_make(&h);
        spk_cat(t, h.t);
        for (k = 0; k < 4u; k++) {
            if (!c[k].label)
                continue;
            spk_cat(t, ". ");
            spk_card(t, &c[k], v);
            str_cpy(spk.v[k], v, sizeof spk.v[k]);
        }
        opt_ann_say(t, 1u);
    } else {
        for (k = 0; k < 4u; k++) {
            char m[SPK_MAX];
            m[0] = 0;
            spk_card(m, &c[k], v);
            if (!str_eq(v, spk.v[k])) {
                str_cpy(spk.v[k], v, sizeof spk.v[k]);
                opt_ann_later(m);
            }
        }
    }
    spk.scr = ui.scr;
    spk.row = row;
    spk.ok = 1;
}
static void speak_msg(const char *a, const char *b)     /* ui_say's words (op_state.c) */
{
    char t[SPK_MAX];
    if (!opt_ann_on())
        return;
    t[0] = 0;
    spk_cat(t, a);
    spk_cat(t, b);
    opt_ann_say(t, 1u);
}
