/* SPDX-License-Identifier: GPL-3.0-only */
/* Which engine a preset is (the user, 2026-10-08: "when I select a preset, I don't see which engine preset it is";
 * section 11.4). Wherever a sound is browsed or named the engine goes with it, in the engine's colour:
 *   the SOUND row     its picture over the rows (op_draw.c draw_list): the preset before, the one playing and the
 *                     one after, each with its engine's name and colour chip (a user preset: its record's engine)
 *   the mixer         PRESETS on the SOUND row: a toast "Super saw (ANALOG)" framed in the engine's colour; the
 *                     strips' names in their engine's colour (op_draw.c draw_strip) */
static uint32_t up_engine_slot(uint32_t k);             /* storage/upreset.c (FELUCCA_UI == 1) */
#if FELUCCA_NATIVE_BANKS
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
#if FELUCCA_NATIVE_BANKS
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
}
static void pre_toast(void)
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
}
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
static uint32_t pre_sig(void)
{
    uint32_t total, cur = preset_pos(&total);
    return (((0x9E5u * 16777619u ^ cur) * 16777619u ^ total) * 16777619u ^
            (tsel()->eng_req * 64u + (uint32_t)user_of(tsel()) + settings.palette * 4096u)) * 31u +
#if FELUCCA_FAVORITES
           (fav_count() * 131u + fav.user * 2654435761u + (uint32_t)fav_list * 40503u + (uint32_t)op_fav_here() * 7u) +
#endif
           (is_drum(TSEL) ? drum_kit_pos() * 977u + (uint32_t)lane_picked() * 7u + lane_sel * 131u + (lane_snd() ? dl.src[lane_selected()] : 0u) : 0u);
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
