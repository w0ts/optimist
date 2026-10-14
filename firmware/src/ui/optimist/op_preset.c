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
static uint32_t pre_entry(uint32_t n, char *nm)
{
    uint32_t k, e = preset_at(n, &k);
    if (e == NENGINES) {
        up_name(k, nm);
        return up_engine_slot(k);
    }
#if FELUCCA_NATIVE_BANKS
    if (e == NB_LIST) {                                 /* (the engine's own collection: the track's engine) */
        nb_name(nb_kind(TSEL), k, nm);
        return TSEL->eng_req % NENGINES;
    }
#endif
    str_cpy(nm, preset_name(e, k), 14);
    return e;
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
    str_cpy(m + str_len(m), on ? ": " : " ", sizeof m - str_len(m));
    str_cpy(m + str_len(m), on ? lane_sound_name(l) : drum_kit_name(), sizeof m - str_len(m));
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
    if (!total)
        return;
    e = pre_entry(cur, nm);
    pre_engine(e, en);
    str_cpy(m, nm, sizeof m);
    str_cpy(m + str_len(m), " (", sizeof m - str_len(m));
    str_cpy(m + str_len(m), en, sizeof m - str_len(m));
    str_cpy(m + str_len(m), ")", sizeof m - str_len(m));
    ui.toast_next = 1;
    ui_message(m);
    ui.toast_next = 0;
    ui.toast_col = e < NENGINES ? ENG_COL[e] : C_HI;
}
static uint32_t pre_sig(void)
{
    uint32_t total, cur = preset_pos(&total);
    return (((0x9E5u * 16777619u ^ cur) * 16777619u ^ total) * 16777619u ^
            (TSEL->eng_req * 64u + (uint32_t)user_of(TSEL) + settings.palette * 4096u)) * 31u +
           (is_drum(TSEL) ? drum_kit_pos() * 977u + (uint32_t)lane_picked() * 7u + lane_sel * 131u + (lane_snd() ? dl.src[lane_selected()] : 0u) : 0u);
}
/* the SOUND row's picture (GRAPH_H rows): the preset before, the one playing (white, its engine big in colour), the
 * one after; a drum track: its kit (graph_lane) */
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
    for (i = 0; i < 3u && total; i++) {
        uint32_t n = (cur + total + i - 1u) % total, e;
        int32_t y = y0 + p * (int32_t)i + 1 + (p - 19) / 2;     /* (2x2: the one playing alone, in the middle) */
        char nm[16], en[10];
        uint16_t ec;
        if ((total < 3u || GRAPH_H < 58) && i != 1u)
            continue;
        e = pre_entry(n, nm);
        pre_engine(e, en);
        ec = e < NENGINES ? ENG_COL[e] : C_GRAY;
        if (i == 1u)
            cv_rect(0, y0 + p, 240, 1, C_LINE), cv_rect(0, y0 + 2 * p - 1, 240, 1, C_LINE);   /* (lined, on black) */
        cv_rect(4, y + 4, 8, 8, ec);                    /* the engine's chip */
        cv_text(18, y, &FONT_S, nm, i == 1u ? C_WHITE : C_DIM);   /* (a preset's name as stored, never re-cased) */
        cv_text(236 - text_w(&FONT_S, en), y, &FONT_S, en, i == 1u ? ec : col_shade(ec, 3u));
    }
}
