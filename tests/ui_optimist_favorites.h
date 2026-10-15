/* SPDX-License-Identifier: GPL-3.0-only */
/* Favorites in the Optimist UI (FELUCCA_FAVORITES; docs/UI-OPTIMIST-DESIGN.md section 37), included by ui_optimist_test.c
 * after WK / kdown / kup. The SOUND row (PRESET ENGINE INIT SAVE AS) has no spare knob, so a row of its own follows it on
 * a synth track: FAVS, KNOB 1 FAV, KNOB 2 LIST.
 *   KNOB 1  FAV   ON / OFF for the selected track's sound: right marks, left clears (a set, not a toggle); YES toggles;
 *                 HOME held + the knob clears; saved later (settings_later), never on a detent; the drum track has no
 *                 FAVS row
 *   KNOB 2  LIST  ALL / FAV: PRESETS, the SOUND row's PRESET card and HOME held + PRESETS (the next instrument) then walk
 *                 only the marked sounds; from a sound not marked the first step goes to the first / last marked; none
 *                 marked: "NO FAVORITES", the sound stays; HOME held + the knob: ALL
 * The marks are the store of core/favorites.c, shared with SLOOP's UI (tests/favorites_ui.c checks that side). */
#if FELUCCA_FAVORITES
static uint32_t fv_sum(void)                            /* the picture under the cards (y 124 on, the panel; the hot card's lit state is above) */
{
    uint32_t i, s = 0;
    for (i = 124u * 240u; i < band_y * 240u; i++)       /* (not the help band) */
        s = s * 31u + screen[i];
    return s;
}
static uint32_t fv_here(void)                           /* the selected track's sound as preset_at gives it, one number */
{
    uint32_t u = user_of(TSEL);
    return u < UP_SLOTS ? (uint32_t)NENGINES << 16 | u : (uint32_t)TSEL->eng_req << 16 | (TSEL->preset & 0xFFFFu);
}
static uint32_t fv_at(uint32_t n)                       /* the filtered list's entry n, one number */
{
    uint32_t k, e = preset_at(n, &k);
    return e << 16 | (k & 0xFFFFu);
}
static void fv_open(uint32_t trk)                       /* SOUND > EDIT on track trk, nothing marked, LIST ALL */
{
    reset_ui();
    memset(&fav, 0, sizeof fav);
    fav_list = 0;
    song.sel = (uint8_t)trk;
    track_select(trk);
    tap(B_EDIT);
    frames(2);
}
static void fv_cell(uint32_t r, uint32_t k, cell_t *c) { SCREENS[SCR_SOUND].cell(r, k, c); }
static int fv_val(uint32_t k, const char *want)         /* the FAVS row's card k reads want */
{
    cell_t c;
    fv_cell(1, k, &c);
    return !strcmp(c.val, want);
}
static void fv_home_turn(uint32_t enc, int32_t s)       /* SHIFT (OCT- held, was HOME) + a knob */
{
    fm1_in.buttons |= BT(B_OCTDN);
    turn(enc, s);
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
}
static void favorites_tests(void)
{
    uint32_t i, n, all, total, sum_off, sum_on, mark[3], idx[3] = {9, 41, 77};
    cell_t c;
    char nm[12], title[40];

    fv_open(0);
    n = snd_rows();
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT && ui.row[SCR_SOUND] == 0u, "favorites: EDIT opens SOUND on the SOUND row");
    SCREENS[SCR_SOUND].name(0, nm);
    check(!strcmp(nm, "SOUND"), "... row 1 is still the SOUND row");
    SCREENS[SCR_SOUND].name(1, nm);
    check(!strcmp(nm, "FAVS") && !snd_page(1) && snd_page(2), "... FAVS follows it, then the engine's pages");
    fv_cell(1, 0, &c);
    check(c.label && !strcmp(c.label, "FAV") && c.kind == CK_VAL && c.gk == GK_PILL && fv_val(0, "OFF"), "... KNOB 1 FAV, OFF, a pill");
    fv_cell(1, 1, &c);
    check(c.label && !strcmp(c.label, "LIST") && c.kind == CK_VAL && fv_val(1, "ALL"), "... KNOB 2 LIST, ALL");
    fv_cell(1, 2, &c);
    check(!c.label, "... KNOB 3 and 4 empty");
    fv_cell(0, 2, &c);
    check(c.label && !strcmp(c.label, "INIT") && c.kind == CK_ACT, "... the SOUND row keeps INIT (KNOB 3) and SAVE AS (KNOB 4)");
    fv_cell(0, 3, &c);
    check(c.label && !strcmp(c.label, "SAVE AS"), "... SAVE AS");

    turn(EN_SELECT, 1);
    check(ui.row[SCR_SOUND] == 1u, "SELECT: the FAVS row");
    head_title(SCR_SOUND, 1, title, sizeof title);
    check(!strcmp(title, "Favs"), "... the header names it Favs");
    frame();
    ppm("opt-sound-favorites-off");
    sum_off = fv_sum();

    settings_later = 0;
    turn(EN_K1, 1);
    check(fav_count() == 1u && fv_val(0, "ON"), "KNOB 1 right: FAV ON for the sound");
    check(settings_later == 1u, "... saved later (settings_later), not at once");
    frames(2);
    sum_on = fv_sum();
    check(sum_on != sum_off, "... the picture shows it (the star after the name)");
    ppm("opt-sound-favorites-on");
    turn(EN_K1, 1);
    check(fav_count() == 1u && fv_val(0, "ON"), "KNOB 1 right again: still ON (a set, not a toggle)");
    turn(EN_K1, -1);
    check(fav_count() == 0u && fv_val(0, "OFF"), "KNOB 1 left: OFF");
    frames(2);
    ppm("opt-sound-favorites-off2");
    check(fv_sum() == sum_off, "... the screen as before");
    ui.hot = 0;
    tap(B_SAVE);
    check(fav_count() == 1u, "YES on the FAV card: marks");
    tap(B_SAVE);
    check(fav_count() == 0u, "YES again: clears");
    turn(EN_K1, 1);
    fv_home_turn(EN_K1, 1);
    check(fav_count() == 0u && fv_val(0, "OFF"), "HOME held + KNOB 1: cleared (the grammar's clear)");
    turn(EN_K2, 1);
    check(fav_list == 1u && fv_val(1, "FAV"), "KNOB 2 right: LIST FAV");
    turn(EN_K2, 1);
    check(fav_list == 1u, "... again: stays FAV (a set)");
    fv_home_turn(EN_K2, 1);
    check(fav_list == 0u && fv_val(1, "ALL"), "HOME held + KNOB 2: LIST ALL");

    /* three sounds marked, in the list's order */
    fav_list = 0;
    preset_pos(&all);
    idx[0] = all / 8u;                                  /* (spread over the list, whatever the build holds) */
    idx[1] = all / 2u;
    idx[2] = all * 7u / 8u;
    for (i = 0; i < 3u; i++) {
        uint32_t k;
        while (idx[i] < all && preset_at(idx[i], &k) >= NENGINES)
            idx[i]++;                                   /* (a factory sound: the engines' own collections, NATIVE_BANKS, have no mark) */
        op_preset_go(idx[i]);
        mark[i] = fv_here();
        turn(EN_K1, 1);
    }
    check(fav_count() == 3u, "three sounds marked, by KNOB 1");
    turn(EN_K2, 1);
    preset_pos(&total);
    check(fav_list == 1u && total == 3u, "KNOB 2 right: the list is the three");
    check(fv_at(0) == mark[0] && fv_at(1) == mark[1] && fv_at(2) == mark[2], "... in the list's order");
    ui.force = 1;
    frame();
    ppm("opt-sound-favorites-list");
    turn(EN_PRESET, 1);
    check(fv_here() == mark[0], "PRESETS, one on from the last of the three: the first (wraps)");
    turn(EN_PRESET, 1);
    check(fv_here() == mark[1], "... one on: the second");
    turn(EN_PRESET, -2);
    check(fv_here() == mark[2], "... two back: the last");
    fv_home_turn(EN_PRESET, 1);
    check(fv_here() == mark[0] || fv_here() == mark[1] || fv_here() == mark[2], "HOME held + PRESETS (the next instrument): one of the favorites");
    fav_list = 0;
    op_preset_go(60);
    fav_list = 1;
    turn(EN_PRESET, 1);
    check(fv_here() == mark[0], "from a sound not marked, one step on: the first marked");
    fav_list = 0;
    op_preset_go(60);
    fav_list = 1;
    turn(EN_PRESET, -1);
    check(fv_here() == mark[2], "... one step back: the last marked");
    fav_list = 0;
    op_preset_go(60);
    fav_list = 1;
    frames(2);
    ppm("opt-sound-favorites-outside");
    fv_home_turn(EN_PRESET, 1);
    check(fv_here() == mark[0] || fv_here() == mark[1] || fv_here() == mark[2], "HOME held + PRESETS from a sound not marked: a favorite");
    ui.row[SCR_SOUND] = 0;
    ui.force = 1;
    frames(2);
    ppm("opt-sound-favorites-row0");
    turn(EN_K1, -1);                                    /* (the SOUND row's PRESET card: the favorites too) */
    check(fv_here() == mark[2] || fv_here() == mark[0] || fv_here() == mark[1], "the SOUND row's PRESET card walks the favorites");
    ui.row[SCR_SOUND] = 1;
    turn(EN_K2, -1);
    preset_pos(&total);
    check(fav_list == 0u && total == all, "KNOB 2 left: LIST ALL, the whole list is back");

    /* none marked, LIST FAV */
    memset(&fav, 0, sizeof fav);
    fav_list = 1;
    preset_pos(&total);
    {
        uint32_t here = fv_here();
        ui.msg[0] = 0;
        turn(EN_PRESET, 1);
        check(total == 0u && fv_here() == here, "no favorites, LIST FAV: the sound stays");
        check(!strcmp(ui.msg, "NO FAVORITES"), "... NO FAVORITES says so");
        fv_home_turn(EN_PRESET, 1);
        check(fv_here() == here, "... HOME held + PRESETS too");
        ui.force = 1;
        frames(2);
        ppm("opt-sound-favorites-none");
    }
    fav_list = 0;

    /* a user preset is marked by its slot */
    memset(&fav, 0, sizeof fav);
    trk[0].user = 1;
    check(user_of(TSEL) == 0u, "a user preset loaded");
    ui.row[SCR_SOUND] = 1;
    frame();
    if (up_used(0)) {
        turn(EN_K1, 1);
        check(fav_user_has(0) && !fav_count() && fv_val(0, "ON"), "KNOB 1 marks it by its slot (no factory place used)");
        turn(EN_K2, 1);
        preset_pos(&total);
        check(total == 1u && fv_at(0) == ((uint32_t)NENGINES << 16 | 0u), "LIST FAV: the user preset is the list");
        fav_list = 0;
        turn(EN_K1, -1);
        check(!fav_user_has(0), "KNOB 1 left clears it");
    }
    trk[0].user = 0;
    memset(&fav, 0, sizeof fav);
    fav_list = 0;

    /* the drum track has no FAVS row */
    for (i = 0; i < NTRK && !is_drum(&trk[i]); i++)
        ;
    if (i < NTRK) {
        fv_open(i);
        SCREENS[SCR_SOUND].name(1, nm);
        check(strcmp(nm, "FAVS") != 0 && snd_rows() >= 2u, "the drum track: no FAVS row");
        fv_cell(0, 0, &c);
        check(c.label && strcmp(c.label, "FAV") != 0, "... its SOUND row is the kit's");
    }
    /* an engine's own page family has no such row */
    fv_open(0);
    tap(B_ENV);
    frames(2);
    SCREENS[SCR_SOUND].name(0, nm);
    check(strcmp(nm, "FAVS") != 0 && (snd_fam == FAM_ENV || snd_fam == SND_FM6), "ENV tapped: only the envelopes' rows, no FAVS");
    check(snd_rows() < n, "... (the whole list was longer)");
    memset(&fav, 0, sizeof fav);
    fav_list = 0;
    reset_ui();
}
#else
static void favorites_tests(void) {}
#endif
