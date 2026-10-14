/* The track number in the header's badge (the user, 2026-10-14: "we should always have the track number on the top left
 * with the engine/preset"). op_draw.c head_badge / head_preset:
 *   the badge      "T1 ANALOG", "T3 <engine>", "DR DRUMS"; a picked lane "DR SNARE", the accent lane "DR ACCENT"
 *   the preset     after the page's name, in the track's colour, only where it fits whole; dropped before the name shortens
 *   the n/N        always whole, with or without the preset
 *   no badge       non-track pages unchanged; MASTER keeps its own badge
 * Included after ui_optimist_accent.h. */
static void hn_badge_is(uint32_t trk, const char *eng, const char *what)
{
    char b[24], w[32];
    str_cpy(w, trk_tag(trk), sizeof w);
    str_cpy(w + str_len(w), " ", sizeof w - str_len(w));
    str_cpy(w + str_len(w), eng, sizeof w - str_len(w));
    check(!strcmp(head_badge(b, sizeof b), w), what);
}
/* the header as drawn, preset shown? (the track's colour right of the badge and the title: nothing else there has it) */
static int hn_preset_drawn(void)
{
    char t[32], pos[12], r[16], bpm[8], bt[24];
    int32_t bw, tx;
    ui.sig[0] = 0;
    draw_head();
    bw = text_w(&FONT_S, head_badge(bt, sizeof bt)) + 8;
    tx = bw + 8;
    pos[0] = 0;
    if (lay.shown != LY_PLAY) {
        lay_title(t, sizeof t);
    } else {
        head_title(ui.scr, ui.row[ui.scr], t, sizeof t);
        head_pos(ui.scr, ui.row[ui.scr], pos);
    }
    loop_pos(TSEL, r);
    if (ui.scr == SCR_STEP)
        r[0] = 0;
    fmt_int(bpm, song.g[G_BPM]);
    head_finish(t, pos, head_room(tx, bpm, r));
    return px_in((uint32_t)(tx + text_w(&FONT_S, t) + 8), 4, 8, 16, trk_col(song.sel));
}
static void header_tracknum_tests(void)
{
    uint32_t k, ok = 1, bad = 0, shown = 0, dropped = 0;
    char t[32], pos[12], r[16], bpm[8];
    reset_ui();
    frames(120);                                        /* (the track-name flash over) */
    op_enter(SCR_SOUND);
    for (k = 0; k < 3; k++) {
        song.sel = (uint8_t)k;
        hn_badge_is(k, ENGINES[trk[k].eng_req % NENGINES]->name, "the badge: the track code and the engine (T1, T2, T3)");
        frame();
        ok &= (uint32_t)px_in(2, 3, 20, 19, trk_col(k));
    }
    check(ok, "... drawn in the track's colour");
    song.sel = TRK_DRUM;
    lane_select(0);
    ui.lane = 0;
    hn_badge_is(TRK_DRUM, "DRUMS", "the drum track: DR DRUMS");
#if ACC_UI
    acc_pick();
    press(B_HOME);
    kdown(DACC_KEY);
    kup(DACC_KEY);
    release(B_HOME);
    hn_badge_is(TRK_DRUM, "ACCENT", "the accent lane: DR ACCENT");
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
        str_cpy(w, "DR ", sizeof w);
        str_cpy(w + 3, l, sizeof w - 3u);
        check(lane_picked() && !strcmp(head_badge(b, sizeof b), w) && !strncmp(b, "DR ", 3) && strcmp(l, "DRUMS"),
              "a picked lane: DR and the lane's name");
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
            int32_t tx;
            char bt[24];
            uint32_t pl;
            ui.row[SCR_SOUND] = (uint8_t)row;
            head_title(SCR_SOUND, row, t, sizeof t);
            head_pos(SCR_SOUND, row, pos);
            loop_pos(TSEL, r);
            fmt_int(bpm, song.g[G_BPM]);
            tx = text_w(&FONT_S, head_badge(bt, sizeof bt)) + 16;
            head_finish(t, pos, head_room(tx, bpm, r));
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
