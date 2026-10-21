/* SPDX-License-Identifier: GPL-3.0-only */
/* Which engine a preset is (the user, 2026-10-08: "when I select a preset, I don't see which engine preset it is";
 * 4.3 SOUND). Wherever a sound is browsed or named the engine goes with it, in the engine's colour:
 *   the SOUND row     its picture over the rows (op_draw.c draw_list): the preset before, the one playing and the
 *                     one after, each with its engine's name and colour chip (a user preset: its record's engine)
 *   the mixer         PRESETS on the SOUND row: a toast "Super saw (ANALOG)" framed in the engine's colour; the
 *                     strips' names in their engine's colour (op_draw.c draw_strip)
 *   PRESETS, SHIFT2 + PRESETS (the engine step): a popup list of the sounds over the panel, the one playing lit in the middle,
 *                     scrolling as you turn, ~1.5 s after the last turn (draw_pop; the drum track: its factory kits) */
static uint32_t up_engine_slot(uint32_t k);             /* storage/upreset.c (FELUCCA_UI == 1) */
#if FELUCCA_NB_LIST
static uint32_t nb_kind(const track_t *t);              /* storage/nbank.c */
static void nb_name(uint32_t kind, uint32_t k, char *nm);
#endif

/* list entry n of the selected track's preset list (model.c preset_at): its name -> nm (14), its engine slot
 * (NENGINES: not known) */
static uint32_t pre_entry_of(uint32_t e, uint32_t k, char *nm)
{
    if (e == NENGINES) {
        up_name(k, nm);
        return up_engine_slot(k);
    }
#if FELUCCA_NB_LIST
    if (e == NB_LIST) {                                 /* (the engine's own collection: the track's engine) */
        nb_name(nb_kind(tsel()), k, nm);
        return tsel()->eng_req % NENGINES;
    }
#endif
    str_cpy(nm, preset_name(e, k), 14);
    return e;
}
static uint32_t pre_entry(uint32_t n, char *nm)
{
    uint32_t k, e = preset_at(n, &k);
    return pre_entry_of(e, k, nm);
}
static void pre_engine(uint32_t e, char *b)             /* "ANALOG", "USER" when not known */
{
    str_cpy(b, e < NENGINES ? ENGINES[e]->name : "USER", 10);
}
/* the mixer's PRESETS: what it now plays, a toast framed in its engine's colour; the drum track: "KIT 808 TRAP" in the
 * kit's kind colour, a lane "SNARE: X9 SD" in its source's (the user, 2026-10-12: a lane's PRESETS is its own sound) */
static void pre_toast_drum(void)
{
    char m[30];
    uint32_t l = lane_selected(), on = (uint32_t)lane_presets();
    str_cpy(m, on ? lane_name(l) : "KIT", sizeof m);
    str_app(m, on ? ": " : " ", sizeof m);
    str_app(m, on ? lane_sound_name(l) : drum_kit_name(), sizeof m);
    ui.toast_next = 1;
    ui_message(m);
    ui.toast_next = 0;
    ui.toast_col = on ? op_lane_col(l) : kit_col();
    if (!on && !dl.ukit && drum_kit_built(drum_kit()))
        pop_kind = 2;                                   /* (a factory kit: the popup list of the kits, over the toast) */
}
static void pre_toast_kind(uint32_t kind)               /* kind 1: the PRESETS knob's popup (the whole list), 5: EDIT's PRESET card's (the engine's own sounds) */
{
    char nm[16], m[30], en[10];
    uint32_t total, cur, e;
    if (is_drum(TSEL)) {
        pre_toast_drum();
        return;
    }
    cur = preset_pos(&total);
    if (cur >= total)                                   /* (none, or LIST FAV with the sound not in the list) */
        return;
    e = pre_entry(cur, nm);
    pre_engine(e, en);
    str_cpy(m, nm, sizeof m);
#if FELUCCA_FAVORITES
    {
        uint32_t fk, fe = preset_at(cur, &fk);
        if (fav_entry(fe, fk))
            str_app(m, " *", sizeof m);   /* (a favorite: the star after its name) */
    }
#endif
    str_app(m, " (", sizeof m);
    str_app(m, en, sizeof m);
    str_app(m, ")", sizeof m);
    ui.toast_next = 1;
    ui_message(m);
    ui.toast_next = 0;
    ui.toast_col = e < NENGINES ? ENG_COL[e] : C_HI;
    pop_kind = (uint8_t)kind;                           /* (the popup list of the sounds, the toast's place: draw_pop) */
}
static void pre_toast(void) { pre_toast_kind(1u); }
static void pre_toast_edit(void) { pre_toast_kind(5u); }
#if FELUCCA_FAVORITES
/* a toast over the sound playing: "FAV + NAME" marked, "FAV - NAME" unmarked; the list kept: "FAVORITES ONLY" / "ALL SOUNDS" */
static void pre_toast_fav(const char *what)
{
    char nm[16], m[30];
    if (what) {
        str_cpy(m, what, sizeof m);
    } else {
        snd_name(song.sel, nm);
        str_cpy(m, op_fav_here() ? "FAV + " : "FAV - ", sizeof m);
        str_app(m, nm, sizeof m);
    }
    ui.toast_next = 1;
    ui_message(m);
    ui.toast_next = 0;
    ui.toast_col = C_HI;
}
#endif
/* (pop_kind 3's list, pop_enum_open below) */
static struct {
    const param_desc_t *d;
    const int16_t *vp;
    uint16_t col;
} pen;
static uint32_t pre_sig(void)
{
    if (pop_kind == 4u)
        return hu(0x4E6u, tsel()->eng_req + settings.palette * 64u);
    if (pop_kind == 3u)
        return hu(hu(0x3E1u, (uint32_t)(int32_t)*pen.vp), settings.palette + (uint32_t)(uintptr_t)pen.d);
    uint32_t total, cur = preset_pos(&total);
    return (((0x9E5u * 16777619u ^ cur) * 16777619u ^ total) * 16777619u ^
            (tsel()->eng_req * 64u + (uint32_t)user_of(tsel()) + settings.palette * 4096u)) * 31u +
#if FELUCCA_FAVORITES
           (fav_count() * 131u + fav.user * 2654435761u + (uint32_t)fav_list * 40503u + (uint32_t)op_fav_here() * 7u) +
#endif
           (is_drum(TSEL) ? drum_kit_pos() * 977u + (uint32_t)lane_picked() * 7u + lane_sel * 131u + (lane_snd() ? dl.src[lane_selected()] : 0u) : 0u);
}
/* PRESETS and the engine step: a popup list of the sounds over the panel, the one playing in the middle (framed and lit in
 * its engine's colour), the others scrolling past it as you turn; gone with the toast, ~1.5 s after the last turn (the user,
 * 2026-10-21: "when changing preset, engine, a popup with the scrolling list"). The drum track: its factory kits */
/* a list value of any card (the user, 2026-10-21: "generic, everywhere we select a value from a list": an insert's type, a scale, an
 * arp mode...): pop_kind 3, the descriptor's names min..max, the value in the middle. Opened by pop_enum_open after the knob's turn */
static void pen_name(int32_t v, char *nm)                  /* the list entry v as its card reads it (nm holds 16) */
{
    const char *unit;
    cell_t c;
    param_format(pen.d, v, nm, &unit);
    if (pen.d->names == ins_names || pen.d->names == fxs_names) {
        char b[16];
        str_cpy(nm, nm_card(nm, 0, b, sizeof b), 16);
    }
    memset(&c, 0, sizeof c);
    str_cpy(c.val, nm, sizeof c.val);
    cell_empty_slot(&c);
    str_cpy(nm, c.val, 16);
}
static void pop_enum_open(uint32_t row, uint32_t k)
{
    cell_t c;
    if ((pop_kind != 0u && pop_kind != 3u && ui.toast_t) || op_armed())
        return;                                         /* (a sound's popup, or a question, is up already) */
    SCR->cell(row, k, &c);
    if (c.kind != CK_VAL || !c.d || !c.vp || c.d->fmt != F_ENUM || c.d->max - c.d->min < 2 || c.d->max - c.d->min > 63 || is_go(c.d))
        return;
    pen.d = c.d, pen.vp = c.vp, pen.col = trk_col(song.sel);
    ui.toast_next = 1;
    ui_message(c.d->label);
    ui.toast_next = 0;
    ui.toast_col = pen.col;
    pop_kind = 3;
}
/* the ENGINE step's popup (EDIT's ENGINE card, SHIFT2 + PRESETS; the user, 2026-10-21: "the engine ... should only list the engines"):
 * the engines, in the whole list's order, the track's lit in the middle in its colour */
static void pop_engines(uint32_t *list, uint32_t *n, uint32_t *at)
{
    uint32_t i, j, k, e;
    *n = 0, *at = 0;
    for (i = 0; i < NLIST && *n < 32u; i++) {
        e = preset_all_at(i, &k);
        if (e >= NENGINES)
            continue;
        for (j = 0; j < *n && list[j] != e; j++)
            ;
        if (j == *n) {
            if (e == TSEL->eng_req % NENGINES)
                *at = *n;
            list[(*n)++] = e;
        }
    }
}
static void pre_toast_engine(void)
{
    uint32_t e = tsel()->eng_req % NENGINES;
    if (is_drum(TSEL)) {
        pre_toast();
        return;
    }
    ui.toast_next = 1;
    ui_message(ENGINES[e]->name);
    ui.toast_next = 0;
    ui.toast_col = ENG_COL[e];
    pop_kind = 4;
}
static void pop_drum_kits(uint32_t *list, uint32_t *n, uint32_t *at)   /* the kits of this build, the one playing's place */
{
    uint32_t i;
    *n = 0, *at = 0;
    for (i = 0; i < DRUM_KITS && *n < 32u; i++)
        if (drum_kit_built(i)) {
            if (i == drum_kit())
                *at = *n;
            list[(*n)++] = i;
        }
}
static void cv_tall(uint32_t y, uint32_t h, uint16_t bg, void (*fn)(void));   /* op_draw.c */
#define POP_RH 28                                       /* a popup row: the large face (the user, 2026-10-21: "bigger font to be more readable") */
static struct {
    uint32_t kits[32], total, cur, half;
    int32_t h;
    uint16_t fr;
} pop;                                                  /* what pop_paint draws (draw_pop) */
static void pop_paint(void)                             /* the box in its own coordinates (cv_tall: taller than a canvas) */
{
    uint32_t row;
    int32_t off, ry;
    char t[12];
    cv_rect(8, 0, 224, pop.h, pop.fr);                  /* the frame, the engine's colour */
    cv_rect(10, 2, 220, pop.h - 4, OP_SURF);
    str_cpy(t, pop_kind == 2u ? "KIT" : pop_kind == 3u ? pen.d->label : pop_kind == 4u ? "ENGINE" : "SOUND", sizeof t);
    cv_text(16, 6, &FONT_S, t, C_GRAY);
    fmt_int(t, (int32_t)pop.cur + 1);
    str_app(t, "/", sizeof t);
    fmt_app(t, (int32_t)pop.total);
    cv_text(224 - text_w(&FONT_S, t), 6, &FONT_S, t, C_GRAY);
    for (row = 0, off = -(int32_t)pop.half; row < pop.half * 2u + 1u; row++, off++) {
        uint32_t idx = (uint32_t)(((int32_t)pop.cur + off + (int32_t)pop.total * 2) % (int32_t)pop.total), eng;
        char nm[16], en[12];
        uint16_t ec;
        ry = 20 + (int32_t)row * POP_RH;
        if ((int32_t)pop.cur + off < 0 || (int32_t)pop.cur + off >= (int32_t)pop.total)
            continue;                                   /* (a list stops at its ends: nothing past them, not the other end's items; the user, 2026-10-21) */
        if (pop_kind == 4u) {
            str_cpy(nm, ENGINES[pop.kits[idx]]->name, sizeof nm);
            en[0] = 0;
            ec = ENG_COL[pop.kits[idx]];
        } else if (pop_kind == 3u) {
            pen_name(pen.d->min + (int32_t)idx, nm);
            en[0] = 0;
            ec = pop.fr;
        } else if (pop_kind == 2u) {
            str_cpy(nm, DRUM_KIT_NAMES[pop.kits[idx]], sizeof nm);
            str_cpy(en, DRUM_KIT_STYLES[pop.kits[idx]], sizeof en);
            ec = pop.fr;
        } else {
            uint32_t k, e = preset_at(idx, &k);
            eng = pre_entry_of(e, k, nm);
            pre_engine(eng, en);
            ec = eng < NENGINES ? ENG_COL[eng] : C_GRAY;
        }
        {
            int32_t um = 0, fm = 0;                             /* the bullet on the left (the engine's chip) becomes a star for a user preset (the engine's colour) or a favorite (amber) */
            const felucca_font_t *f;
#if FELUCCA_FAVORITES
            if (pop_kind == 1u || pop_kind == 5u) {
                uint32_t fk, fe = preset_at(idx, &fk);
                um = fe == NENGINES;
                fm = fav_entry(fe, fk);
            }
#else
            if (pop_kind == 1u || pop_kind == 5u) {
                uint32_t fk;
                um = preset_at(idx, &fk) == NENGINES;
            }
#endif
            f = text_w(&FONT_L, nm) <= 190 ? &FONT_L : &FONT_S;   /* (the large face where the name fits) */
            if (off == 0 && en[0])                              /* (the one playing: its tag in the header, always) */
                cv_text(16 + text_w(&FONT_S, pop_kind == 2u ? "KIT" : "SOUND") + 12, 6, &FONT_S, en, ec);
            if (text_w(f, nm) + text_w(&FONT_S, en) + 8 > 190)
                en[0] = 0;                                      /* (the tag only where it fits beside the name) */
            if (off == 0)
                cv_rect(12, ry - 1, 216, POP_RH - 2, C_LINE);   /* (the one playing: a lit band) */
            if (um || fm) {
                uint16_t sc = fm ? C_WARN : ec;
                cv_text(14, ry + (POP_RH - (int32_t)FONT_L.h) / 2, &FONT_L, "*", off == 0 ? sc : col_shade(sc, 3u));
            } else {
                cv_rect(16, ry + POP_RH / 2 - 4, 6, 8, ec);     /* the engine's chip */
            }
            cv_text(28, ry + (POP_RH - (int32_t)f->h) / 2, f, nm, off == 0 ? C_WHITE : C_GRAY);
            cv_text(224 - text_w(&FONT_S, en), ry + (POP_RH - (int32_t)FONT_S.h) / 2, &FONT_S, en, off == 0 ? ec : col_shade(ec, 3u));
        }
    }
}
static void draw_pop(void)
{
    if (pop_kind == 2u) {
        pop_drum_kits(pop.kits, &pop.total, &pop.cur);
    } else if (pop_kind == 4u) {
        pop_engines(pop.kits, &pop.total, &pop.cur);
    } else if (pop_kind == 3u) {
        pop.total = (uint32_t)(pen.d->max - pen.d->min + 1);
        pop.cur = (uint32_t)clamp((int32_t)*pen.vp - pen.d->min, 0, (int32_t)pop.total - 1);
    } else {
        pop.cur = preset_pos(&pop.total);
    }
    if (!pop.total || pop.cur >= pop.total)
        return;
    pop.fr = pop_kind == 4u ? ENG_COL[tsel()->eng_req % NENGINES] : ui.toast_col ? ui.toast_col : C_HI;
    pop.half = 2u;                                      /* (five rows whatever the list holds: the ends of a short list are blank, the user, 2026-10-21) */
    pop.h = (int32_t)(pop.half * 2u + 1u) * POP_RH + 24;
    cv_tall((uint32_t)((240 - pop.h) / 2), (uint32_t)pop.h, C_BLACK, pop_paint);
}
/* the SOUND row's picture (GRAPH_H rows): the preset before, the one playing (white, its engine big in colour), the
 * one after; a favorite has a star after its name; LIST FAV with the sound playing not in the list: it stays in the middle,
 * the last favorite before it and the first after; a drum track: its kit (graph_lane) */
static void pre_draw(void)
{
    uint32_t total, cur = preset_pos(&total), i;
    int32_t p = (GRAPH_H - 2) / 3, y0;                  /* the three rows' pitch: 19 up to the picture's height / 3 */
    if (is_drum(TSEL)) {
        graph_lane_of(!lane_picked());                      /* (DR: the kit; a lane: its sound) */
        return;
    }
    p = p < 19 ? 19 : p > 44 ? 44 : p;
    y0 = (GRAPH_H - 3 * p) / 2;
    for (i = 0; i < 3u; i++) {
        uint32_t e, k, eng, all;
        int32_t y = y0 + p * (int32_t)i + 1 + (p - 19) / 2;     /* (2x2: the one playing alone, in the middle) */
        char nm[16], en[10];
        uint16_t ec;
        if (((total < 3u || GRAPH_H < 58) && i != 1u) || (!total && i != 1u))
            continue;
        if (cur < total)
            e = preset_at((cur + total + i - 1u) % total, &k);
        else if (i == 1u)                               /* (LIST FAV: the sound playing is not in the list) */
            e = preset_all_at(preset_all_pos(&all), &k);
        else
            e = preset_at(i ? 0u : total - 1u, &k);
        eng = pre_entry_of(e, k, nm);
        pre_engine(eng, en);
        ec = eng < NENGINES ? ENG_COL[eng] : C_GRAY;
        if (i == 1u)
            cv_rect(0, y0 + p, 240, 1, C_LINE), cv_rect(0, y0 + 2 * p - 1, 240, 1, C_LINE);   /* (lined, on black) */
        cv_rect(4, y + 4, 8, 8, ec);                    /* the engine's chip */
        cv_text(18, y, &FONT_S, nm, i == 1u ? C_WHITE : C_DIM);   /* (a preset's name as stored, never re-cased) */
#if FELUCCA_FAVORITES
        if (fav_entry(e, k))
            cv_text(18 + text_w(&FONT_S, nm) + 4, y, &FONT_S, "*", i == 1u ? C_WHITE : C_DIM);
#endif
        cv_text(236 - text_w(&FONT_S, en), y, &FONT_S, en, i == 1u ? ec : col_shade(ec, 3u));
    }
}
