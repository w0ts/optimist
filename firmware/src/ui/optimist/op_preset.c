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
/* the mixer's PRESETS: what it now plays, a toast framed in its engine's colour */
static void pre_toast(void)
{
    char nm[16], m[30], en[10];
    uint32_t total, cur = preset_pos(&total), e;
    if (is_drum(TSEL) || !total)
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
    return ((0x9E5u * 16777619u ^ cur) * 16777619u ^ total) * 16777619u ^
           (TSEL->eng_req * 64u + (uint32_t)user_of(TSEL) + settings.palette * 4096u);
}
/* the SOUND row's picture (GRAPH_H rows): the preset before, the one playing (white, its engine big in colour), the
 * one after; a drum track: its kit (graph_lane) */
static void pre_draw(void)
{
    uint32_t total, cur = preset_pos(&total), i;
    if (is_drum(TSEL)) {
        graph_lane();
        return;
    }
    for (i = 0; i < 3u && total; i++) {
        uint32_t n = (cur + total + i - 1u) % total, e;
        int32_t y = 1 + 19 * (int32_t)i;
        char nm[16], b[16], en[10];
        uint16_t ec;
        if (total < 3u && i != 1u)
            continue;
        e = pre_entry(n, nm);
        pre_engine(e, en);
        ec = e < NENGINES ? ENG_COL[e] : C_GRAY;
        if (i == 1u)
            cv_rect(0, y - 1, 240, 19, OP_SURF);
        cv_rect(4, y + 4, 8, 8, ec);                    /* the engine's chip */
        cv_text(18, y, &FONT_S, op_case(b, nm, sizeof b), i == 1u ? C_WHITE : C_DIM);
        cv_text(236 - text_w(&FONT_S, en), y, &FONT_S, en, i == 1u ? ec : col_shade(ec, 3u));
    }
}
