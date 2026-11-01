/* SPDX-License-Identifier: GPL-3.0-only */
/* Favorites in the Optimist UI (FELUCCA_FAVORITES; docs/UI-OPTIMIST-DESIGN.md 13 Favorites), included by ui_optimist_test.c
 * after WK / kdown / kup. Favorites live where presets are picked (the user, 2026-10-14: "we have a preset button, that is
 * enough"): no SOUND / FAVS row on EDIT. SAVE + PRESETS since 2026-10-16 (saved into the favorites; SHIFT2 + PRESETS is
 * the next instrument).
 *   SAVE held + PRESETS            marks the sound playing (right) or clears it (left): a set, not a toggle; a toast
 *                                  "FAV + NAME" / "FAV - NAME"; saved later (settings_later), never on a detent
 *   SHIFT2 + PRESETS               the list's filter (since 2026-10-31; SHIFT2 + SELECT before): FAVORITES browses only the
 *                                  marked sounds, ALL every sound (tests/ui_optimist_prefilter.h: every filter)
 *   PRESETS                        then walks only the marked sounds; from a sound not marked the first step goes to the
 *                                  first / last marked; none marked: "NO FAVORITES", the sound stays
 *   SHIFT + PRESETS                the next instrument whatever the filter (since 2026-10-31; SHIFT2 before)
 *   the toast of a PRESETS turn    puts a star after a favorite's name; PROJECT > USER: SHIFT2 + the ITEM knob marks the slot
 * The drum track has no mark. The marks are the store of core/favorites.c, shared with SLOOP's UI (tests/favorites_ui.c). */
#if FELUCCA_FAVORITES
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
static void fv_go_unmarked(void)                        /* the selected track onto a sound of its engine's list that is not marked */
{
    uint32_t j, kk, tt, e;
    pl_filt = PLF_ALL;
    (void)preset_pos(&tt);
    for (j = 0; j < tt; j++) {
        e = preset_at(j, &kk);
        if (!fav_entry(e, kk))
            break;
    }
    op_preset_go(j);
}
static void fv_open(uint32_t trk)                       /* SOUND > EDIT on track trk, nothing marked, LIST ALL */
{
    reset_ui();
    memset(&fav, 0, sizeof fav);
    pl_filt = PLF_ALL;
    song.sel = (uint8_t)trk;
    track_select(trk);
    tap(B_EDIT);
    frames(2);
}
static void fv_shift_turn(uint32_t enc, int32_t s)      /* SHIFT (OCT- held) + a knob */
{
    fm1_in.buttons |= BT(B_OCTDN);
    turn(enc, s);
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
}
static void fv_shift2_turn(uint32_t enc, int32_t s)     /* SHIFT2 (OCT+ held) + a knob */
{
    fm1_in.buttons |= BT(B_OCTUP);
    turn(enc, s);
    fm1_in.buttons &= ~BT(B_OCTUP);
    frame();
}
static void fv_save_turn(uint32_t enc, int32_t s)       /* SAVE held + a knob (the favorite: saved into the favorites) */
{
    fm1_in.buttons |= BT(B_SAVE);
    turn(enc, s);
    fm1_in.buttons &= ~BT(B_SAVE);
    frame();
}
static void fv_filter_to(uint32_t f)                   /* SHIFT2 + PRESETS turned right until the filter is f (at most a cycle) */
{
    uint32_t i;
    for (i = 0; i < PLF_N + 1u && pl_filt != f; i++)
        fv_shift2_turn(EN_PRESET, 1);
}
static void favorites_tests(void)
{
    uint32_t i, all, total, mark[3], idx[3] = {9, 41, 77};
    char nm[12];

    fv_open(0);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT && ui.row[SCR_SOUND] == SND_MAIN_ROW && snd_page(SND_MAIN_ROW) == &main_pg,
          "favorites: EDIT opens on MAIN (2026-10-18), the SOUND row before it, no FAVS row");
    SCREENS[SCR_SOUND].name(0, nm);
    check(strcmp(nm, "SOUND") == 0, "... row 0 is the SOUND row");
    for (i = 0; i < snd_rows(); i++) {
        SCREENS[SCR_SOUND].name(i, nm);
        if (!strcmp(nm, "FAVS"))
            break;
    }
    check(i == snd_rows(), "... no FAVS row");
    settings_later = 0;
    ui.msg[0] = 0;
    fv_save_turn(EN_PRESET, 1);
    check(fav_count() == 1u && op_fav_here(), "SAVE + PRESETS right: the sound is a favorite");
    check(settings_later == 1u, "... saved later (settings_later), not at once");
    check(!strncmp(ui.msg, "FAV + ", 6), "... the toast says it");
    fv_save_turn(EN_PRESET, 1);
    check(fav_count() == 1u, "... right again: still one (a set, not a toggle)");
    fv_save_turn(EN_PRESET, -1);
    check(fav_count() == 0u && !op_fav_here() && !strncmp(ui.msg, "FAV - ", 6), "SAVE + PRESETS left: cleared, \"FAV - NAME\"");

    /* three sounds marked, in the list's order */
    pl_filt = PLF_ALL;
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
        fv_save_turn(EN_PRESET, 1);
    }
    check(fav_count() == 3u, "three sounds marked, by SAVE + PRESETS");
    pre_toast();                                        /* (the popup list: a favorite's bullet is an amber star) */
    frames(3);
    check(op_overlay() == 5u && px_in(14, 40, 30, 190, C_WARN), "the popup marks a favorite with an amber star in place of the bullet");
    ppm("opt-fav-popup");
    ui.msg[0] = 0;
    fv_filter_to(PLF_FAV);
    preset_pos(&total);
    check(fav_list && total == 3u && op_overlay() == 5u, "SHIFT2 + PRESETS to FAVORITES: the list is the three, the popup shows it");
    check(fv_at(0) == mark[0] && fv_at(1) == mark[1] && fv_at(2) == mark[2], "... in the list's order");
    check(ui.row[SCR_SOUND] == SND_MAIN_ROW, "... the cursor row did not move");
    turn(EN_PRESET, 1);
    check(fv_here() == mark[2], "PRESETS, one on from the last of the three: it stays (a list does not wrap)");
    ui.msg[0] = 0;
    pre_toast();
    check(strstr(ui.msg, " *") != 0, "... the toast puts a star after a favorite's name");
    turn(EN_PRESET, -1);
    check(fv_here() == mark[1], "... one back: the second");
    turn(EN_PRESET, -2);
    check(fv_here() == mark[0], "... two back: the first, no lower");
    {
        uint32_t e0 = TSEL->eng_req;
        fv_shift_turn(EN_PRESET, 1);
        uint32_t moved = TSEL->eng_req != e0;
        if (!moved) {
            fv_shift_turn(EN_PRESET, -1);
            moved = TSEL->eng_req != e0;
        }
        check(fav_list && moved, "SHIFT + PRESETS (the next instrument) with FAVORITES: the engines, the filter kept");
    }
    pl_filt = PLF_ALL;
    fv_go_unmarked();
    pl_filt = PLF_FAV;
    turn(EN_PRESET, 1);
    check(fv_here() == mark[0], "from a sound not marked, one step on: the first marked");
    pl_filt = PLF_ALL;
    fv_go_unmarked();
    pl_filt = PLF_FAV;
    turn(EN_PRESET, -1);
    check(fv_here() == mark[2], "... one step back: the last marked");
    pl_filt = PLF_ALL;
    fv_go_unmarked();
    pl_filt = PLF_FAV;
    pl_filt = PLF_FAV;
    fv_filter_to(PLF_ALL);
    preset_pos(&total);
    check(pl_filt == PLF_ALL && total == all, "SHIFT2 + PRESETS round to ALL: every sound, the whole list is back");

    /* none marked, LIST FAV */
    memset(&fav, 0, sizeof fav);
    pl_filt = PLF_FAV;
    preset_pos(&total);
    {
        uint32_t here = fv_here();
        ui.msg[0] = 0;
        turn(EN_PRESET, 1);
        check(total == 0u && fv_here() == here, "no favorites, LIST FAV: the sound stays");
        check(!strcmp(ui.msg, "NO FAVORITES"), "... NO FAVORITES says so");
        fv_shift_turn(EN_PRESET, 1);
        check(TSEL->eng_req != (here >> 16) || NLIST < 2u, "... SHIFT + PRESETS (the next instrument) still moves: the engines are not filtered");
    }
    pl_filt = PLF_ALL;

    /* a user preset is marked by its slot */
    memset(&fav, 0, sizeof fav);
    trk[0].user = 1;
    check(user_of(TSEL) == 0u, "a user preset loaded");
    if (up_used(0)) {
        fv_save_turn(EN_PRESET, 1);
        check(fav_user_has(0) && !fav_count(), "SAVE + PRESETS marks it by its slot (no factory place used)");
        fv_filter_to(PLF_FAV);
        preset_pos(&total);
        check(total == 1u && fv_at(0) == ((uint32_t)NENGINES << 16 | 0u), "LIST FAV: the user preset is the list");
        pl_filt = PLF_ALL;
        fv_save_turn(EN_PRESET, -1);
        check(!fav_user_has(0), "SAVE + PRESETS left clears it");
    }
    trk[0].user = 0;
    memset(&fav, 0, sizeof fav);
    pl_filt = PLF_ALL;

    /* PROJECT > USER: SHIFT2 + the ITEM knob marks the slot */
    reset_ui();
    op_enter(SCR_PROJECT);
    for (i = 0; i < NPRJ && prj_kind(i) != PR_USER; i++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)i;
    ui.user_slot = 0;
    if (up_used(0)) {
        fv_shift2_turn(EN_K3, 1);
        check(fav_user_has(0), "PROJECT > USER: SHIFT2 + the ITEM knob marks the slot");
        fv_shift2_turn(EN_K3, -1);
        check(!fav_user_has(0), "... left clears it");
    }
    memset(&fav, 0, sizeof fav);

    /* the drum track has no mark */
    for (i = 0; i < NTRK && !is_drum(&trk[i]); i++)
        ;
    if (i < NTRK) {
        fv_open(i);
        fv_save_turn(EN_PRESET, 1);
        check(fav_count() == 0u && !fav_user_has(0), "the drum track: SAVE + PRESETS marks nothing");
    }
    memset(&fav, 0, sizeof fav);
    pl_filt = PLF_ALL;
    reset_ui();
}
#else
static void favorites_tests(void) {}
#endif
