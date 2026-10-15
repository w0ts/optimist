/* The track number in the header's badge (the user, 2026-10-14: "we should always have the track number on the top left
 * with the engine/preset"). op_draw.c head_badge / head_preset:
 *   the badge      "1 ANALOG", "3 <engine>", "4 DRUMS"; a picked lane "4 SNARE", the accent lane "4 ACCENT"
 *   the preset     after the page's name, in the track's colour, only where it fits whole; dropped before the name shortens
 *   the n/N        always whole, with or without the preset
 *   no badge       non-track pages unchanged; MASTER keeps its own badge
 * Included after ui_optimist_accent.h. */
static void hn_badge_is(uint32_t trk, const char *eng, const char *what)
{
    char b[24], w[32];
    fmt_int(w, (int32_t)trk + 1);
    str_cpy(w + str_len(w), " ", sizeof w - str_len(w));
    str_cpy(w + str_len(w), eng, sizeof w - str_len(w));
    check(!strcmp(head_badge(b, sizeof b), w), what);
}
/* the header as drawn, preset shown? (the track's colour right of the badge and the title: nothing else there has it) */
static int hn_preset_drawn(void)
{
    head_t h;
    ui.sig[0] = 0;
    draw_head();
    head_make(&h);
    return px_in((uint32_t)(h.tx + text_w(&FONT_S, h.t) + 8), 4, 8, 16, trk_col(song.sel));
}
static void header_tracknum_tests(void)
{
    uint32_t k, ok = 1, bad = 0, shown = 0, dropped = 0;
    char t[32], pos[12];
    reset_ui();
    frames(120);                                        /* (the track-name flash over) */
    op_enter(SCR_SOUND);
    for (k = 0; k < 3; k++) {
        song.sel = (uint8_t)k;
        hn_badge_is(k, ENGINES[trk[k].eng_req % NENGINES]->name, "the badge: the track number and the engine (1, 2, 3)");
        frame();
        ok &= (uint32_t)px_in(2, 3, 20, 19, trk_col(k));
    }
    check(ok, "... drawn in the track's colour");
    song.sel = TRK_DRUM;
    lane_select(0);
    ui.lane = 0;
    hn_badge_is(TRK_DRUM, "DRUMS", "the drum track: 4 DRUMS");
#if ACC_UI
    acc_pick();
    press(B_OCTDN);
    kdown(DACC_KEY);
    kup(DACC_KEY);
    release(B_OCTDN);
    hn_badge_is(TRK_DRUM, "ACCENT", "the accent lane: 4 ACCENT");
#endif
    reset_ui();
    frames(120);
    song.sel = TRK_DRUM;
    lane_select(1);
    ui.lane = 1;
    op_enter(SCR_SOUND);
    {
        char b[24], w[32];
        const char *l = head_engine();
        str_cpy(w, "4 ", sizeof w);
        str_cpy(w + 2, l, sizeof w - 2u);
        check(lane_picked() && !strcmp(head_badge(b, sizeof b), w) && !strncmp(b, "4 ", 2) && strcmp(l, "DRUMS"),
              "a picked lane: 4 and the lane's name");
    }
    /* every SOUND row on each synth track: the n/N whole at the title's end, the preset only when it fits, never over the loop position */
    for (k = 0; k < 3; k++) {
        uint32_t row, n;
        song.sel = (uint8_t)k;
        reset_ui();
        frames(120);
        song.sel = (uint8_t)k;
        op_enter(SCR_SOUND);
        n = SCREENS[SCR_SOUND].rows();
        for (row = 0; row < n; row++) {
            head_t h;
            uint32_t pl;
            ui.row[SCR_SOUND] = (uint8_t)row;
            head_make(&h);
            str_cpy(t, h.t, sizeof t);
            str_cpy(pos, h.pos, sizeof pos);
            pl = str_len(pos);
            if (pl && (str_len(t) < pl || strcmp(t + str_len(t) - pl, pos))) {
                printf("  T%u row %u: '%s' lost its n/N '%s'\n", k + 1u, row, t, pos);
                bad++;
            }
            if (hn_preset_drawn())
                shown++;
            else
                dropped++;
        }
    }
    check(!bad, "the n/N is always whole at the end of the title, with the badge's track code");
    printf("  preset shown on %u rows, dropped on %u\n", shown, dropped);
    check(dropped > 0, "the preset is dropped where the title leaves no room");
    /* a short title leaves room: a layer's name ("Scale", "Erase") on a short badge shows the preset after it */
    reset_ui();
    frames(120);
    song.sel = 1;
    op_enter(SCR_SOUND);
    {
        static const uint8_t LYS[] = {LY_ERASE, LY_STEP, LY_SCALE};
        uint32_t any = 0;
        for (k = 0; k < sizeof LYS; k++) {
            lay.shown = LYS[k];
            any |= (uint32_t)hn_preset_drawn();
        }
        lay.shown = LY_PLAY;
        check(any, "a track layer with room (short title, no n/N) draws the preset after the title");
    }
    /* non-track pages: no badge, no preset */
    ui.scr = SCR_PROJECT;
    lay.shown = LY_PLAY;
    frames(120);                                        /* (the track-name flash over) */
    ui.sig[0] = 0;
    draw_head();
    check(!head_has_track() && !px_in(2, 3, 20, 19, trk_col(song.sel)), "a non-track page: no badge");
    ui.scr = SCR_HOME;
    reset_ui();
    song.sel = 0;
    lane_select(0);
    ui.lane = 0;
    reset_ui();
}
