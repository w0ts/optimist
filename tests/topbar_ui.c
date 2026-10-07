/* SPDX-License-Identifier: GPL-3.0-only */
/* The top bar (ui_draw.c draw_head), included by tests/ui_pages_test.c:
 *   left    the selected track (T1.. / DR) and its engine's icon (the drum track: the kit's), in the track's colour
 *   centre  transport, SECTION letter (A..P, always shown: green playing, amber in the work, dim only chosen), REC, BPM:
 *           centred, and neither the BPM's digits (2 -> 3) nor the letter move the rest
 *   right   USB, battery
 *   a message and the knob's help line replace the bar, and it comes back
 *   the pages that cover the screen (TRACKS, the drum grid, REC) have the same bar, not a header of their own
 * TOPBAR_SHOTS=prefix in the environment: screenshots DIR/prefix-*.ppm of the scenes the redesign was judged on,
 * then the checks as usual; TOPBAR_SHOTS_ONLY=1 stops after the shots (a build without the new layout). */
#define TB_H 20
#define TB_LEFT_W 36
#define TB_SEC_X0 94                              /* the section letter's slot: x 94..103 */
#define TB_SEC_X1 104
#define TB_CX 80                                  /* the centre group's left edge */
static uint16_t tb_px(int x, int y) { return swap16(screen[y * 240 + x]); }
static uint32_t tb_hash(int x0, int x1)                 /* the bar's columns x0..x1-1, FNV-1a */
{
    uint32_t h = 2166136261u;
    int x, y;
    for (y = 0; y < TB_H; y++)
        for (x = x0; x < x1; x++)
            h = (h ^ tb_px(x, y)) * 16777619u;
    return h;
}
static int tb_has(int x0, int x1, uint16_t c)             /* colour c (full ink) somewhere in the columns */
{
    int x, y;
    for (y = 0; y < TB_H; y++)
        for (x = x0; x < x1; x++)
            if (tb_px(x, y) == c)
                return 1;
    return 0;
}
static void tb_extent(int x0, int x1, int *lo, int *hi)   /* the lit columns' first and last */
{
    int x, y;
    *lo = 999, *hi = -1;
    for (x = x0; x < x1; x++)
        for (y = 0; y < TB_H; y++)
            if (tb_px(x, y)) {
                if (x < *lo) *lo = x;
                if (x > *hi) *hi = x;
            }
}
static const char *tb_pre;
static void tb_shot(const char *name)
{
    char n[64];
    if (!tb_pre)
        return;
    snprintf(n, sizeof n, "%s-%s", tb_pre, name);
    ppm(n);
}
static void tb_scene(uint32_t sel, uint32_t playing, uint32_t rec)
{
    song.sel = sel; song.playing = playing; song.rec = rec; transport_req = 0;
    ui.hot_t = 0; ui.msg_t = 0; go_home(); tap(B_LFO); ui.force = 1; frames(2);
}
static void tb_view(uint32_t v) { song.g[G_VIEW] = (int16_t)v; settings.view = v; ui.force = 1; frames(2); }
static void tb_shots(void)
{
    uint32_t i;
    tb_scene(0, 0, 0); go_home(); frames(2); tb_shot("tracks");
    tb_scene(1, 0, 0); tb_shot("lfo-t2");
    tb_scene(0, 1, 0); tb_shot("lfo-playing");
    tb_scene(0, 1, 1); tb_shot("lfo-recording");
    tb_scene(0, 1, 2); tb_shot("lfo-other-armed");
    tb_scene(2, 0, 0); open_family(FAM_EDIT); ui.force = 1; frames(2); tb_shot("edit-t3");
    tb_scene(0, 0, 0); tap(B_LFO); ui.force = 1; frames(2); tb_shot("lfo");
    song.g[G_BPM] = 99; ui.force = 1; frames(2); tb_shot("lfo-bpm99");
    song.g[G_BPM] = 120; song.octave = 2; ui.force = 1; frames(2); tb_shot("lfo-bpm120-oct");
    song.octave = 0; song.g[G_BPM] = 112;
#if DL_UI
    tb_scene(TRK_DRUM, 0, 0); go_home(); frames(2); tb_shot("drum-home");
    memset(&dl, 0, sizeof dl);
    key(key_of_white(2)); tap(B_EDIT); frames(2); ui.force = 1; frames(2); tb_shot("drum-sound");
#endif
    tb_scene(0, 0, 0); tb_view(1); tb_shot("view-all"); tb_view(0);
    tb_scene(TRK_DRUM, 1, 0); studio_open(SC_DRUM); ui.force = 1; frames(2); tb_shot("drum-grid");
    tb_scene(TRK_DRUM, 0, 0); studio_open(SC_DRUM); ui.force = 1; frames(2); tb_shot("drum-grid-stopped");
    tb_scene(0, 0, 0); go_home(); live_sec = 2; ui.force = 1; frames(2); tb_shot("tracks-section-c");
    tb_scene(0, 1, 0); go_home(); live_sec = 2; ui.force = 1; frames(2); tb_shot("tracks-playing-c");
    live_sec = -1; ui.force = 1; frames(2); tb_shot("tracks-playing-nosection");
    tb_scene(1, 0, 0); go_home(); rec_wait = 1; ui.force = 1; frames(2); tb_shot("rec-ready");
    rec_wait = 0; frames(2);
    tb_scene(0, 1, 0); live_sec = 1; studio_open(SC_SONG); ui.force = 1; frames(2); tb_shot("song"); live_sec = -1;
    tb_scene(0, 0, 0); go_home(); ui_message("SAVED"); frames(2); tb_shot("tracks-message");
    for (i = 0; i < 400u && ui.msg_t; i++) frame();
    tb_scene(0, 0, 0);
    ui_message("SAVED"); frames(2); tb_shot("message");
    ui_say("EMPTY SECTION", "REC"); frames(2); tb_shot("message2");
    for (i = 0; i < 400u && ui.msg_t; i++) frame();
    tb_scene(0, 0, 0);
#if FELUCCA_PARAM_HELP
    tap(B_ENV); frames(2); ph_turn(1, 1); tb_shot("help-env");
#endif
}

static void topbar_tests(void)
{
    uint32_t s, base, hc, i;
    int lo, hi;
    tb_pre = getenv("TOPBAR_SHOTS");
    tb_shots();
    if (getenv("TOPBAR_SHOTS_ONLY"))
        return;
    memset(&dl, 0, sizeof dl);
    /* left: the track's colour on its number and on its icon (T1..T3 each its engine's, DR the kit kind's) */
    for (s = 0; s < NTRK; s++) {
        uint16_t c;
        char w[96];
        tb_scene(s, 0, 0);
        c = trk_col(s);
        snprintf(w, sizeof w, "top bar: %s: the number in its colour (left)", s == TRK_DRUM ? "DR" : "T1..T3");
        check(tb_has(0, 20, c), w);
        check(tb_has(20, TB_LEFT_W, c) || !FELUCCA_ICONS, "top bar: ... and the icon right of it in the same colour");
    }
    /* left: another engine, another colour and another icon; the right of the bar is not touched */
    {
        uint32_t e, was;
        tb_scene(0, 0, 0);
        was = tb_hash(0, TB_LEFT_W);
        base = tb_hash(180, 240);
        for (e = 0; e < NENGINES; e++)
            if (eng_built(e) && e != (uint32_t)TSEL->eng_req) {
                set_engine_of(TSEL, e); TSEL->engine = TSEL->eng_req; frames(3);
                if (tb_hash(0, TB_LEFT_W) != was && trk_col(0) == ENG_COL[e] && tb_has(0, TB_LEFT_W, ENG_COL[e]))
                    break;
            }
        check(e < NENGINES, "top bar: another engine on the track: the left changes (its colour, its icon)");
        check(tb_hash(180, 240) == base, "top bar: ... the right (USB, battery) stays");
        set_engine_of(TSEL, trk_def_engine(0)); TSEL->engine = TSEL->eng_req; apply_preset_to(TSEL, trk_def_preset(0));
    }
    /* centre: the transport + REC + BPM group sits in the middle, whatever the state or the digits */
    song.g[G_BPM] = 112;
    for (i = 0; i < 4u; i++) {
        tb_scene(0, i & 1u, i == 2u ? 1u : i == 3u ? 2u : 0u);
        tb_extent(TB_LEFT_W + 20, 180, &lo, &hi);
        check(lo >= 76 && hi <= 164 && (lo + hi) / 2 >= 112 && (lo + hi) / 2 <= 128,
              i == 0 ? "top bar: stopped: transport .. BPM centred on the screen" :
              i == 1 ? "top bar: playing: ... centred" : i == 2 ? "top bar: REC armed on this track: ... centred" :
                       "top bar: REC armed on another track: ... centred");
    }
    tb_scene(0, 1, 1); song.g[G_BPM] = 99;
    base = tb_hash(76, 134);                              /* the symbols (transport, section, REC, icon), up to where the digits start */
    hc = tb_hash(0, 76);
    song.g[G_BPM] = 100; ui.force = 1; frames(2);
    check(tb_hash(76, 134) == base && tb_hash(0, 76) == hc, "top bar: BPM 99 -> 100: the transport, REC, icon and the left do not move");
    tb_extent(TB_LEFT_W + 20, 180, &lo, &hi);
    check((lo + hi) / 2 >= 112 && (lo + hi) / 2 <= 128, "top bar: BPM 100: the group is still centred");
    song.g[G_BPM] = 112;
    /* the section letter: always there, in its own slot, centred in the group, and nothing else moves with it */
    {
        static const int8_t LIVE[] = {0, 1, 2, 3, 8, 12, 15};     /* A, B, C, D, I (narrow), M (wide), P */
        uint32_t st, rest = 0, k, n = 0;
        int ll[3], hh[3];
        song.g[G_BPM] = 112; song.octave = 0;
        for (k = 0; k < sizeof LIVE; k++) {
            if (LIVE[k] >= FELUCCA_SECTIONS)                     /* (a 4-section build: A..D only) */
                continue;
            tb_scene(0, 0, 0); live_sec = LIVE[k]; ui.force = 1; frames(2);
            check(head_sec(&st) == (uint32_t)LIVE[k] && st == 1u && tb_has(TB_SEC_X0, TB_SEC_X1, C_HI),
                  "top bar: the section in the work (stopped): its letter A..P in the slot, in the amber");
            tb_extent(TB_LEFT_W + 20, 180, &ll[0], &hh[0]);
            check(ll[0] >= 76 && hh[0] <= 164 && (ll[0] + hh[0]) / 2 >= 112 && (ll[0] + hh[0]) / 2 <= 128,
                  "top bar: ... the group stays centred whatever the letter");
            if (n++ == 0) { base = tb_hash(0, TB_SEC_X0); hc = tb_hash(TB_SEC_X1, 240); rest = tb_hash(TB_SEC_X0, TB_SEC_X1); }
            else check(tb_hash(0, TB_SEC_X0) == base && tb_hash(TB_SEC_X1, 240) == hc && tb_hash(TB_SEC_X0, TB_SEC_X1) != rest,
                       "top bar: ... another letter changes only its slot (the transport, REC, BPM and the sides stay)");
        }
        tb_scene(0, 1, 0); live_sec = 3; ui.force = 1; frames(2);
        check(head_sec(&st) == 3u && st == 2u && tb_has(TB_SEC_X0, TB_SEC_X1, C_OK) && !tb_has(TB_SEC_X0, TB_SEC_X1, C_HI),
              "top bar: a section plays: its letter in the status green");
        tb_scene(0, 0, 0); live_sec = -1; song.g[G_SLOT] = 3; ui.force = 1; frames(2);
        check(head_sec(&st) == 2u && st == 0u && tb_has(TB_SEC_X0, TB_SEC_X1, C_DIM) && !tb_has(TB_SEC_X0, TB_SEC_X1, C_HI) &&
              !tb_has(TB_SEC_X0, TB_SEC_X1, C_OK), "top bar: no section played or loaded: the letter of the slot chosen (C), dim, never blank");
        tb_extent(TB_LEFT_W + 20, 180, &ll[0], &hh[0]);
        check((ll[0] + hh[0]) / 2 >= 112 && (ll[0] + hh[0]) / 2 <= 128, "top bar: ... still centred");
        song.g[G_BPM] = 99; ui.force = 1; frames(2); tb_extent(TB_LEFT_W + 20, 180, &ll[1], &hh[1]);
        song.g[G_BPM] = 100; ui.force = 1; frames(2); tb_extent(TB_LEFT_W + 20, 180, &ll[2], &hh[2]);
        check(ll[1] == ll[2] && (ll[2] + hh[2]) / 2 >= 112 && (ll[2] + hh[2]) / 2 <= 128,
              "top bar: with the letter, BPM 99 -> 100: the group's left edge does not move, still centred");
        song.g[G_BPM] = 112; song.g[G_SLOT] = 1; live_sec = -1;
        tb_scene(0, 1, 1); live_sec = 5; ui.force = 1; frames(2);
        check(tb_has(TB_SEC_X0, TB_SEC_X1, C_OK) && tb_has(76, 164, C_ERR), "top bar: playing and REC armed: the letter and the red REC disc both shown");
        live_sec = -1;
    }
    /* REC: red for this track, not red for another one (it was white / gray) */
    tb_scene(0, 1, 1);
    check(tb_has(76, 164, C_ERR), "top bar: REC armed on the selected track: red in the centre");
    tb_scene(0, 1, 2);
    check(!tb_has(76, 164, C_ERR), "top bar: REC armed on another track: not red");
    tb_scene(0, 0, 0);
    check(!tb_has(76, 164, C_ERR), "top bar: no REC: no red");
    /* a message wins and the bar comes back */
    base = tb_hash(0, 240);
    ui_message("HELLO"); frames(2);
    check(tb_hash(0, 240) != base && tb_hash(76, 164) != 0 && !tb_has(0, TB_LEFT_W, trk_col(0)),
          "top bar: a message replaces the bar (the track's colour is gone)");
    for (i = 0; i < 400u && ui.msg_t; i++) frame();
    frames(2);
    check(!ui.msg_t && tb_hash(0, 240) == base, "top bar: ... the message ends: the bar is back, as it was");
#if FELUCCA_PARAM_HELP
    tap(B_ENV); frames(2);
    while (cur_page()->scope != SC_TRACK || cur_page()->id[0] != P_ATK) { tap(B_ENV); frames(1); }
    frames(PH_HOT + 4);
    base = tb_hash(0, 240);
    ph_turn(1, 1);
    check(ph_line() && tb_hash(0, 240) != base && !tb_has(0, TB_LEFT_W, trk_col(0)), "top bar: a knob turns: the help line replaces the bar");
    ui_message("SAVED"); ph_turn(1, 1);
    check(ui.msg_t && ph_line() && tb_hash(0, 240) != base, "top bar: ... a message wins over the line");
    for (i = 0; i < 400u && ui.msg_t; i++) frame();
    frames(PH_HOT + 4);
    check(!ph_line() && tb_hash(0, 240) == base, "top bar: ... then the knob's white value goes: the bar is back, as it was");
#endif
    /* every page has the same bar: TRACKS (home), the drum grid, the REC screen draw the top bar (under it a 20 px strip: title, loop position, beats) */
    {
        uint32_t p;
        for (p = 0; p < 4u; p++) {
            const char *nm = p == 0 ? "TRACKS" : p == 1 ? "the drum grid" : p == 2 ? "the REC screen" : "the SONG screen";
            char w[96];
            uint32_t sel = p == 1 ? TRK_DRUM : 1u;
            tb_scene(sel, p != 2u, 0); live_sec = 2;   /* (REC waits for a note only while stopped) */ ui.force = 1; frames(2);
            base = tb_hash(0, 240);                           /* the bar on the LFO page */
            if (p == 0) go_home();
            if (p == 1) studio_open(SC_DRUM);
            if (p == 2) { go_home(); rec_wait = 1; }
            if (p == 3) studio_open(SC_SONG);
            ui.force = 1; frames(2);
            snprintf(w, sizeof w, "top bar: %s: the same bar as on a parameter page (track, transport, section, BPM, USB, battery)", nm);
            check(tb_hash(0, 240) == base, w);
            ui_message("SAVED"); frames(2);
            snprintf(w, sizeof w, "top bar: %s: a message replaces the bar", nm);
            check(tb_hash(0, 240) != base && !tb_has(0, TB_LEFT_W, trk_col(sel)), w);
            for (i = 0; i < 400u && ui.msg_t; i++) frame();
            ui.force = 1; frames(2);
            snprintf(w, sizeof w, "top bar: %s: ... the message ends: the bar is back, as it was", nm);
            check(!ui.msg_t && tb_hash(0, 240) == base, w);
            rec_wait = 0; live_sec = -1;
            tb_scene(0, 0, 0);
        }
    }
    tb_scene(0, 0, 0);
}
