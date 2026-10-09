/* SPDX-License-Identifier: GPL-3.0-only */
/* GLO > MACRO on the screens (FELUCCA_MACROS: macro.c mac_effective*, ui_draw.c mac_card, ui_studio.c mac_mark),
 * included by tests/ui_pages_test.c. The user's project: MOTION +35, ENERGY -38, DST 100 % % (plays as 40 %), LFO FLT 0 %
 * (plays +17 %): wherever a macro moves a parameter the page shows what plays (the notice colour, an M, a tick on the
 * gauge where it plays); the value the knob is turning shows as authored; at home nothing differs from a build
 * without the macros; the authored value is never touched.
 * MACRO_SHOTS=prefix in the environment: DIR/prefix-*.ppm of the scenes. */
#if FELUCCA_MACROS
#define MS_X0 4
#define MS_X1 58                                          /* column 0's card (x 4..58) */
static const char *ms_pre;
static void ms_shot(const char *name)
{
    char n[64];
    if (!ms_pre)
        return;
    snprintf(n, sizeof n, "%s-%s", ms_pre, name);
    ppm(n);
}
static void ms_pos(int32_t c, int32_t m, int32_t s, int32_t e)
{
    TDRUM->p[MAC_ID[0]] = (int16_t)c;
    TDRUM->p[MAC_ID[1]] = (int16_t)m;
    TDRUM->p[MAC_ID[2]] = (int16_t)s;
    TDRUM->p[MAC_ID[3]] = (int16_t)e;
}
static uint32_t ms_warn(int x0, int x1, int y0, int y1)   /* pixels in the notice colour in a box */
{
    uint32_t n = 0;
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += swap16(screen[y * 240 + x]) == C_WARN;
    return n;
}
static void ms_page(uint32_t scope, uint32_t id0)         /* the page whose first knob is id0 */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].scope == scope && PAGES[i].id[0] == id0) {
            ui.page = (uint8_t)i;
            break;
        }
    ui.hot_t = 0;
    ui.force = 1;
    frames(2);
}
static int ms_key(uint32_t c, const char *prefix)         /* column c's cache key starts with prefix */
{
    return !strncmp(ui.col[c], prefix, strlen(prefix));
}
static void macro_show_tests(void)
{
    int16_t dst0, flt0;
    ms_pre = getenv("MACRO_SHOTS");
    song.sel = 0; song.playing = 0; song.rec = 0; transport_req = 0; ui.msg_t = 0; ui.menu = 0; settings.view = 0;
    song.g[G_VIEW] = 0;
    fxs_set(FXS_DEF);                                     /* (the FX slots as a new project's: the fuzz turned them) */
    trk[0].p[P_DIST] = 127;
    trk[0].p[P_LD_FLT] = 0;
    dst0 = trk[0].p[P_DIST];
    flt0 = trk[0].p[P_LD_FLT];
    go_home();
    ms_pos(0, 0, 0, 0);
    open_family(FAM_FX); ms_page(SC_TRACK, P_DIST);
    check(ms_key(0, "DST|100|") && !ms_warn(MS_X0, MS_X1, 26, 70), "macro show: at home: DST 100 %, nothing in the notice colour");
    ms_pos(0, 35, 0, -38);
    ms_page(SC_TRACK, P_DIST);
    ms_shot("fx");
    check(ms_key(0, "DST|40|"), "macro show: ENERGY -38: the DST card shows 40 (what plays)");
    check(ms_warn(MS_X0, MS_X1, 26, 44) > 8 && ms_warn(MS_X0, MS_X1, 44, 62) > 20 && ms_warn(MS_X0, MS_X1, 62, 70) > 2,
          "macro show: ... with an M (top), the value and a tick on the gauge in the notice colour");
    check(trk[0].p[P_DIST] == dst0, "macro show: ... the authored value is untouched (100)");
    ui.hot_col = 0; ui.hot_t = 30; ui.force = 1; frame();
    check(ms_key(0, "DST|100|") && ms_warn(MS_X0, MS_X1, 62, 70) > 2,
          "macro show: its knob just turned: the authored 100 in the text, the tick still where it plays");
    ui.hot_t = 0;
    ms_page(SC_TRACK, P_LD_PIT);
    ms_shot("lfo");
    check(ms_key(1, "FLT|+26|") && trk[0].p[P_LD_FLT] == flt0, "macro show: MOTION +35: LFO FLT authored 0 shows +26 % (raw +17: its macro amount)");
    check(ms_warn(MS_X0 + 60, MS_X1 + 60, 26, 44) > 8, "macro show: ... marked with the M");
    ms_page(SC_TRACK, P_ATK);
    check(!ms_warn(MS_X0, MS_X1 + 180, 26, 70), "macro show: a page no macro moves: nothing in the notice colour");
    /* VIEW ALL: the same cells */
    ms_page(SC_TRACK, P_DIST);
    song.g[G_VIEW] = 1; settings.view = 1; ui.force = 1; frames(3);
    ms_shot("view-all");
    {
        uint32_t r, c, n = 0;
        for (r = 0; r < OV_ROWS; r++)
            for (c = 0; c < 4u; c++)
                n += !strncmp(ov.key[r][c], "DST|40|", 7);
        check(n == 1u, "macro show: VIEW ALL: the DST cell shows 40 as well");
    }
    song.g[G_VIEW] = 0; settings.view = 0; ui.force = 1; frames(2);
    /* ENERGY on the drums' level, SPACE on the part's pan, in the dial strips */
    ms_pos(0, 0, 40, -38);
    song.sel = TRK_DRUM; go_home(); ui.force = 1; frames(2);
    ms_shot("tracks-drum");
    check(ms_warn(0, 240, 184, 224) > 6, "macro show: TRACKS on the drum track: its level dial marked (ENERGY)");
    song.sel = 0; go_home(); ui.force = 1; frames(2);
    ms_shot("tracks");
    ms_pos(0, 0, 0, 0);
    song.sel = 0; go_home(); ui.force = 1; frames(2);
    check(!ms_warn(0, 240, 184, 224), "macro show: ... at home: no mark");
    {   /* a drum sound's REV under SPACE: SOUND 3 */
        uint32_t i, ok = 0;
        ms_pos(0, 0, 40, 0);
        song.sel = TRK_DRUM; go_home(); frames(1);
        key(key_of_white(2)); tap(B_EDIT); frames(2);
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].scope == SC_DSND && PAGES[i].id[0] == 16u) {
                ui.page = (uint8_t)i;
                ok = 1;
            }
        ui.force = 1; frames(2);
        ms_shot("drum-rev");
        /* (SOUND 3 follows the FX slots, FX slots phase 5: REV is the 4th card, x 184..238, in the default layout) */
        check(ok && fxs_lane_id(3) == 16u && ms_warn(MS_X0 + 180, MS_X1 + 180, 26, 70) > 8,
              "macro show: a drum sound's REV (SOUND 3, the REV slot's card) marked under SPACE");
        ms_pos(0, 0, 0, 0);
        ui.force = 1; frames(2);
        check(!ms_warn(MS_X0 + 180, MS_X1 + 180, 26, 70), "macro show: ... at home: nothing");
    }
    ms_pos(0, 0, 0, 0);
    song.sel = 0; go_home(); ui.force = 1; frames(2);
    {   /* the editor's view: cmd 65 MACRO, track 1 and the drum track, at the user's positions and at home */
        uint8_t t1 = 0, t4 = TRK_DRUM;
        uint32_t i, n, dst = 0, lfo = 0, drev = 0, glob = 0, p;
        ms_pos(0, 35, 0, -38);
        trk[0].p[P_DIST] = 127;
        ed_begin(65);
        check(ed_macro(65, &t1, 1) && ed_out[0] == 0, "macro show: editor cmd 65 answers for track 1");
        check((ed_out[1] | ed_out[2] << 7) - 8192 == 0 && (ed_out[3] | ed_out[4] << 7) - 8192 == 35 &&
              (ed_out[5] | ed_out[6] << 7) - 8192 == 0 && (ed_out[7] | ed_out[8] << 7) - 8192 == -38,
              "macro show: ... with the four positions (COLOR 0, MOTION 35, SPACE 0, ENERGY -38)");
        n = ed_out[9];
        for (i = 0, p = 10; i < n; i++) {
            uint32_t sc = ed_out[p], id = ed_out[p + 1];
            int32_t v = (ed_out[p + 2] | ed_out[p + 3] << 7) - 8192;
            if (sc == 0 && id == P_DIST) dst = v == 51;
            if (sc == 0 && id == P_LD_FLT) lfo = v == 17;
            glob += sc == 1;
            drev += sc == 2;
            p += sc == 2 ? 6u : 4u;
        }
        check(dst && lfo && glob >= 1u && !drev, "macro show: ... DST 127 plays 51, LFO FLT 0 plays 17, ENERGY's global (drum level), no REV move");
        check(ed_n == p, "macro show: ... the reply holds exactly its entries");
        ms_pos(0, 0, 40, 0);
        ed_begin(65);
        (void)ed_macro(65, &t4, 1);
        check(ed_out[0] == t4 && ed_out[9] >= 1u, "macro show: the drum track: no track values, the globals and the REV send under SPACE");
        ms_pos(0, 0, 0, 0);
        ed_begin(65);
        (void)ed_macro(65, &t1, 1);
        check(ed_out[9] == 0u, "macro show: at home: no entries");
        ed_begin(65);
        t4 = NTRK;
        check(!ed_macro(65, &t4, 1) && !ed_macro(66, &t1, 1), "macro show: another track or command: not answered");
    }
}
#else
static void macro_show_tests(void) {}
#endif
