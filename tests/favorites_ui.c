/* SPDX-License-Identifier: GPL-3.0-only */
/* Favorites in SLOOP's UI (FELUCCA_FAVORITES; Felucca 1.6's gesture), included by tests/ui_pages_test.c. On SAVE > PRESETS
 * (the browser, GR_BROWSE), whose KNOB 3 and 4 were empty:
 *   KNOB 3  FAV  ON / OFF for the selected track's sound: a turn right marks, left clears (a set, not a toggle), the user
 *                preset a sound came from by its slot; saved later (settings_later), never on a detent; the drum track and
 *                the engines' own collections (FM6 / CZ banks) have no mark
 *   KNOB 4  LIST ALL / FAV: the PRESETS knob, KNOB 1 and the TRACKS PRESETS knob walk only the marked sounds (wrapping);
 *                from a sound not marked the first step goes to the first / last marked; none marked: a message, the
 *                sound stays; ALL again and the whole list is back
 * The list is not kept over a power cycle (it starts at ALL). */
static uint32_t fav_screen_sum(void)
{
    uint32_t i, s = 0;
    for (i = 0; i < 240u * 240u; i++)
        s = s * 31u + screen[i];
    return s;
}
static void fav_open_presets(void)
{
    uint32_t i;
    song.sel = 0;
    go_home();
    frame();
    for (i = 0; i < NPAGES && PAGES[i].graph != GR_BROWSE; i++)
        ;
    ui.page = (uint8_t)i;
    ui.fam_last[PAGES[i].fam] = ui.page;
    page_entered();
    frame();
}
static void fav_knob(uint32_t enc, int32_t s) { encs[panel.enc[enc]] = s; frames(2); }
/* the entry (engine slot, preset) at filtered list index n, as one number */
static uint32_t fav_at(uint32_t n)
{
    uint32_t k, e = preset_at(n, &k);
    return e << 16 | (k & 0xFFFFu);
}
static uint32_t fav_here(void)                         /* the selected track's sound as preset_at gives it */
{
    uint32_t u = user_of(TSEL);
    return u < UP_SLOTS ? (uint32_t)NENGINES << 16 | u : (uint32_t)TSEL->eng_req << 16 | (TSEL->preset & 0xFFFFu);
}

static void favorites_ui_tests(void)
{
    uint32_t all, total, i, sum_off, sum_on, mark[3], idx[3] = {9, 41, 77};
    track_t *t = &trk[0];
    memset(&fav, 0, sizeof fav);
    fav_list = 0;
    t->user = 0;
    fav_open_presets();
    preset_pos(&all);
    check(cur_page()->graph == GR_BROWSE && all > 80u, "favorites: SAVE > PRESETS is up, the whole list long");
    check(!preset_favorite() && fav_list == 0u, "favorites: nothing marked, LIST ALL");
    frame();
    sum_off = fav_screen_sum();

    settings_later = 0;
    fav_knob(EN_K3, 1);
    check(preset_favorite() && fav_count() == 1u, "KNOB 3 right: FAV ON for the sound");
    check(settings_later == 1u, "... saved later (the flag the main loop saves on when quiet), not at once");
    sum_on = fav_screen_sum();
    check(sum_on != sum_off, "... the screen shows it (FAV ON)");
    fav_knob(EN_K3, 1);
    check(preset_favorite() && fav_count() == 1u, "KNOB 3 right again: still ON (a set, not a toggle)");
    fav_knob(EN_K3, -1);
    check(!preset_favorite() && fav_count() == 0u, "KNOB 3 left: FAV OFF");
    check(fav_screen_sum() == sum_off, "... the screen as before");

    for (i = 0; i < 3u; i++) {                         /* three sounds marked, by the knob */
        preset_go(idx[i]);
        mark[i] = fav_here();
        fav_knob(EN_K3, 1);
    }
    check(fav_count() == 3u && preset_favorite(), "three sounds marked");
    fav_knob(EN_K4, 1);
    preset_pos(&total);
    check(fav_list == 1u && total == 3u, "KNOB 4 right: LIST FAV, the list is the three");
    ui.force = 1; frame(); ppm("page-presets-fav");
    check(fav_at(0) == mark[0] && fav_at(1) == mark[1] && fav_at(2) == mark[2],
          "... in the order of the list (as PRESETS has it)");
    fav_knob(EN_K4, 1);
    check(fav_list == 1u, "KNOB 4 again: stays at FAV (no wrap, no category here)");
    fav_knob(EN_PRESET, 1);
    check(fav_here() == mark[0], "the PRESETS knob, one step on from the last of the three: the first (wraps)");
    fav_knob(EN_PRESET, 1);
    check(fav_here() == mark[1], "... a step on: the second");
    fav_knob(EN_PRESET, -2);
    check(fav_here() == mark[2], "... two steps back: the last (wraps back)");

    fav_list = 0;
    preset_go(60);                                    /* a sound not marked */
    fav_list = 1;
    fav_knob(EN_PRESET, 1);
    check(fav_here() == mark[0], "from a sound not marked, one step on: the first marked");
    fav_list = 0;
    preset_go(60);
    fav_list = 1;
    fav_knob(EN_PRESET, -1);
    check(fav_here() == mark[2], "... one step back: the last marked");
    check(fav_here() == mark[2] && preset_favorite(), "... and it shows FAV ON");

    fav_open_presets();                                /* KNOB 1: the same list (the page's own) */
    fav_knob(EN_K1, 1);
    check(fav_here() == mark[0], "PRESETS page KNOB 1 walks the marked sounds too");

    fav_knob(EN_K4, -1);
    preset_pos(&total);
    check(fav_list == 0u && total == all, "KNOB 4 left: LIST ALL, the whole list is back");
    ui.force = 1; frame(); ppm("page-presets-all");

    fav_list = 1;                                      /* nothing marked */
    memset(&fav, 0, sizeof fav);
    preset_pos(&total);
    {
        uint32_t here = fav_here();
        ui.msg[0] = 0;
        fav_knob(EN_PRESET, 1);
        check(total == 0u && fav_here() == here, "no favorites, LIST FAV: the list is empty, the sound stays");
        check(!strcmp(ui.msg, "NO FAVORITES"), "... NO FAVORITES says so");
    }
    fav_list = 0;

    /* a user preset (slot 0 of the two this test has) */
    t->user = 1;
    check(user_of(TSEL) == 0u && !preset_favorite(), "a user preset loaded: not marked");
    fav_knob(EN_K3, 1);
    check(preset_favorite() && fav_user_has(0) && !fav_count(), "KNOB 3 marks it by its slot (no factory place used)");
    fav_knob(EN_K4, 1);
    preset_pos(&total);
    check(total == 1u && fav_at(0) == ((uint32_t)NENGINES << 16 | 0u), "LIST FAV: the user preset is the list");
    fav_list = 0;
    fav_knob(EN_K3, -1);
    check(!fav_user_has(0), "KNOB 3 left clears it");
    t->user = 0;

    /* the drum track has no mark */
    for (i = 0; i < NTRK && !is_drum(&trk[i]); i++)
        ;
    if (i < NTRK) {
        song.sel = (uint8_t)i;
        ui.force = 1;
        frame();
        fav_knob(EN_K3, 1);
        check(fav_count() == 0u && !fav_user_has(0), "the drum track: KNOB 3 marks nothing");
        song.sel = 0;
    }
    memset(&fav, 0, sizeof fav);
    fav_list = 0;
    go_home();
    frames(2);
}
