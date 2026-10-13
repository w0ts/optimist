/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the page organisation (the user's decisions, 2026-10-12; docs/UI-OPTIMIST-DESIGN.md
 * section 17): D1 GLO and SELECT walk one cycle, GLOBAL then SYSTEM, HOME held on the mixer does nothing; D2 SYSTEM's first row CLOCK +
 * LOAD; D6 SAVE on the mixer is PROJECT; D7 EDIT's first row is SOUND; D8 the SCENES layer says "Select: song"; P25 a
 * dark family button does nothing; P28 CALIBRATE > PANEL asks first. Included after ui_optimist_glo.h (glo_find). */
static void pages_nav_tests(void)
{
    cell_t c, c2;
    char nm[12];
    reset_ui();
    song.sel = 0;
    /* D1 (revised again, section 20: "it should switch between the two"): GLO toggles GLOBAL and SYSTEM, each keeping its
     * line; SELECT pages within the screen (pages of 4 lines) and stops at its ends, never crossing to the other */
    {
        uint32_t pg = gl_npg(0), ps = gl_npg(1), p1 = gl_pfirst(0, 1), ng, i = 0;
        char p[12];
        ng = SCREENS[SCR_GLOBAL].rows();
        ui.row[SCR_GLOBAL] = 0;
        ui.row[SCR_SYSTEM] = 0;
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "D1: GLO tapped on the mixer: GLOBAL");
        turn(EN_SELECT, 1);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == p1, "D1: SELECT: GLOBAL's next page (its first line)");
        tap(B_GLO);
        frames(2);
        head_pos(SCR_SYSTEM, 0, p);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == 0u && led_lvl(B_GLO) == 2 && px_in(2, 3, 4, 19, C_HI),
              "D1: GLO on GLOBAL: SYSTEM (CPU), the GLO LED full, the badge SYSTEM");
        check(!strncmp(p, " 1/", 3), "D1: the n/N counts SYSTEM's own pages (1/n)");
        turn(EN_SELECT, 100);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == gl_pfirst(1, ps - 1u), "D1: SELECT right: stops at SYSTEM's last page (no wrap, no GLOBAL)");
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == p1 && led_lvl(B_GLO) == 2, "D1: GLO on SYSTEM: GLOBAL, on the line it was left on");
        turn(EN_SELECT, -3);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "D1: SELECT left: stops on GLOBAL's first page (no wrap)");
        turn(EN_SELECT, 100);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == gl_pfirst(0, pg - 1u), "D1: SELECT right: stops at GLOBAL's last page (no SYSTEM)");
        tap(B_GLO);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == gl_pfirst(1, ps - 1u), "D1: GLO: SYSTEM again, on its line");
        tap(B_ENV);
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL, "D1: GLO from another screen: GLOBAL");
        check(glo_find(SCR_GLOBAL, "SYSTEM") == ng, "D1: GLOBAL has no SYSTEM line (GLO reaches SYSTEM)");
#if FELUCCA_BLE
        op_enter(SCR_SYSTEM);
        line_named(SCR_SYSTEM, "BLE");
        frame();
        tap(B_SAVE);
        i = (uint32_t)dev_listing();
        tap(B_GLO);
        check(i && !dev_listing() && ui.scr == SCR_GLOBAL, "D1: GLO on the BLE devices list (SYSTEM): GLOBAL, the list closed");
        tap(B_HOME);
#endif
    }
    reset_ui();
    press(B_HOME);
    frames(HOLD_FRAMES);
    release(B_HOME);
    frames(2);
    check(ui.scr == SCR_HOME, "D1: HOME held alone on the mixer: nothing (no SYSTEM, no SCOPE)");
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE, "D9: HOME tapped on the mixer: SCOPE, as before");
    tap(B_HOME);
    /* D2 */
    SCREENS[SCR_SYSTEM].name(0, nm);
    SCREENS[SCR_SYSTEM].cell(0, 0, &c);
    SCREENS[SCR_SYSTEM].cell(0, 1, &c2);
#if DUAL_LOAD
    check(!strcmp(nm, "CPU") && c.kind == CK_RO && !strcmp(c.label, "CLOCK") && !strcmp(c.unit, "MHz") &&
          c2.kind == CK_RO && !strcmp(c2.label, "CPU0") && !strcmp(c2.unit, "%"),
          "D2 DUAL: SYSTEM's first row: CPU, CLOCK (MHz) and CPU0 (%), read-outs");
    {   /* KNOB 3: CPU1's load (dual_load.h), OFF when the core is down; YES and a turn do nothing */
        uint32_t q = dual_c1_q8;
        dual_c1_q8 = 128u;
        memset(&c2, 0, sizeof c2);
        SCREENS[SCR_SYSTEM].cell(0, 2, &c2);
        check(c2.kind == CK_RO && !strcmp(c2.label, "CPU1") && !strcmp(c2.val, "50") && !strcmp(c2.unit, "%"),
              "DUAL: SYSTEM's CPU row, KNOB 3: CPU1 50 %, a read-out");
        dual_c1_q8 = DUAL_C1_OFF;
        memset(&c2, 0, sizeof c2);
        SCREENS[SCR_SYSTEM].cell(0, 2, &c2);
        check(c2.kind == CK_RO && !strcmp(c2.label, "CPU1") && !strcmp(c2.val, "OFF"),
              "DUAL: CPU1 down: its cell reads OFF");
        dual_c1_q8 = q;
        memset(&c2, 0, sizeof c2);
        SCREENS[SCR_SYSTEM].cell(0, 3, &c2);
        check(c2.kind == CK_VAL && !strcmp(c2.label, "2ND CORE"), "DUAL: KNOB 4 of the CPU row is 2ND CORE (AUTO / OFF)");
    }
#else
    check(!strcmp(nm, "CPU") && c.kind == CK_RO && !strcmp(c.label, "CLOCK") && !strcmp(c.unit, "MHz") &&
          c2.kind == CK_RO && !strcmp(c2.label, "LOAD") && !strcmp(c2.unit, "%"),
          "D2: SYSTEM's first row: CPU, CLOCK (MHz) and LOAD (%), read-outs");
    memset(&c2, 0, sizeof c2);
    SCREENS[SCR_SYSTEM].cell(0, 2, &c2);
    check(c2.kind != CK_RO, "D2: a single-core build: KNOB 3 of the CPU row is empty (no CPU1)");
#endif
    /* D6 */
    reset_ui();
    turn(EN_K2, 1);
    turn(EN_K2, -1);
    tap(B_SAVE);
    check(ui.scr == SCR_PROJECT, "D6: SAVE on the mixer after a knob turn: PROJECT");
    reset_ui();
    turn(EN_ALGO, -1);
    turn(EN_K1, 1);
    turn(EN_K1, -1);
    tap(B_SAVE);
    check(ui.scr == SCR_PROJECT, "D6: SAVE on the mixer's MASTER after a knob turn: PROJECT");
    reset_ui();
    tap(B_SAVE);
    check(ui.scr == SCR_PROJECT, "D6: SAVE on the mixer, nothing turned: PROJECT");
    /* D7 */
    reset_ui();
    tap(B_EDIT);
    frames(2);
    SCR->cell(0, 0, &c);
    SCR->cell(0, 2, &c2);
    check(ui.scr == SCR_SOUND && ui.row[SCR_SOUND] == 0u && !snd_page(0) && !strcmp(c.label, "PRESET") &&
          !strcmp(c2.label, "INIT") && led_lvl(B_EDIT) == 2, "D7: EDIT tapped: its first row is SOUND (PRESET ENGINE INIT SAVE AS), EDIT full");
    tap(B_EDIT);
    check(snd_page(ui.row[SCR_SOUND]) && snd_page(ui.row[SCR_SOUND])->fam == FAM_EDIT, "D7: EDIT again: the edit pages");
    tap(B_ENV);
    check(snd_page(0) != 0, "D7: the other families have no SOUND row");
    /* D8 */
    reset_ui();
    press(B_SAVE);
    frames(HOLD_FRAMES);
    check(lay.shown == LY_SONG && strstr(lay_sub, "Select: song") && text_w(&FONT_S, lay_sub) <= 232,
          "D8: SAVE held: the SCENES layer says Select: song (and it fits)");
    release(B_SAVE);
    reset_ui();
    /* P25 */
    song.sel = TRK_DRUM;
    frames(2);
    {
        uint32_t lfo = led_lvl(B_LFO);
        tap(B_LFO);
        check(lfo == 0u && ui.scr == SCR_HOME, "P25: the drum track's LFO (dark): tapped, nothing");
    }
    song.sel = 0;
    reset_ui();
    /* P28 */
    op_enter(SCR_SYSTEM);
    line_named(SCR_SYSTEM, "PANEL");                    /* (Calibrate's PANEL line: YES on it) */
    frame();
    tap(B_SAVE);
    check(op_armed() && !strcmp(ui.arm_verb, "CALIBRATE?") && !strcmp(ui.arm_arg, "PANEL") && modal_fits(),
          "P28: CALIBRATE > PANEL asks first (CALIBRATE? PANEL)");
    tap(B_HOME);
    check(!op_armed() && ui.scr == SCR_SYSTEM, "P28: HOME: cancelled, nothing taught, still SYSTEM");
    {
        const panel_t keep = panel;
        tap(B_SAVE);
        tap(B_SAVE);                                    /* (confirmed: the teach, no input: it times out, fm1_wdt_feed) */
        check(!memcmp(&keep, &panel, sizeof panel) && !strcmp(ui.msg, "SETUP CANCELLED"),
              "P28: confirmed: the teach runs; no input for 30 s cancels it, the panel kept");
    }
    reset_ui();
}

/* D3: the master FX is MASTER, DELAY REVERB SLOTS TONE COMP LIMIT MACRO (REV TYPE and INS COMP where built); its GLOBAL
 * and DRUMS pages gone, every value they held still reachable: BPM SWING CLICK on GLOBAL, TUNE on LIMIT, the drum
 * level on the mixer's DR row */
static int pg_has_g(uint32_t scr, uint32_t g)            /* a cell of screen scr holds song.g[g] */
{
    uint32_t r, k, n;
    cell_t c;
    ui.scr = (uint8_t)scr;
    n = SCREENS[scr].rows();
    for (r = 0; r < n; r++)
        for (k = 0; k < 4u; k++) {
            SCREENS[scr].cell(r, k, &c);
            if (c.vp == &song.g[g])
                return 1;
        }
    return 0;
}
static void pages_master_tests(void)
{
    static const char *const WANT[] = {"DELAY", "REVERB", "SLOTS", "TONE", "COMP", "LIMIT",
#if FELUCCA_MACROS
                                       "MACRO",
#endif
    };
    uint32_t r, n, i = 0, ok = 1, row = 0;
    char nm[12], h[40];
    cell_t c;
    int16_t tune = song.g[G_TUNE];
    reset_ui();
    turn(EN_ALGO, -1);
    tap(B_FX);
    n = SCR->rows();
    for (r = 0; r < n; r++) {
        SCR->name(r, nm);
        if (!strcmp(nm, "REV TYPE") || !strcmp(nm, "INS COMP"))
            continue;                                   /* (REV_MULTI, the master COMP slot: their own pages) */
        ok &= i < sizeof WANT / sizeof WANT[0] && !strcmp(nm, WANT[i]);
        i++;
    }
    check(ui.scr == SCR_FX && ok && i == sizeof WANT / sizeof WANT[0],
          FELUCCA_MACROS ? "D3: MASTER's 7 pages: DELAY REVERB SLOTS TONE COMP LIMIT MACRO" : "D3: MASTER's pages: DELAY REVERB SLOTS TONE COMP LIMIT");
    head_title(SCR_FX, 0, h, sizeof h);
    check(!strcmp(SCR_NAME[SCR_FX], "MASTER") && !strcmp(h, "MASTER DELAY"), "D3: the screen is MASTER (\"MASTER DELAY\")");
    check(!pg_has_g(SCR_FX, G_BPM) && !pg_has_g(SCR_FX, G_SWING) && !pg_has_g(SCR_FX, G_CLOCK) && !pg_has_g(SCR_FX, G_DRLVL),
          "D3: no GLOBAL or DRUMS page: BPM SWING CLICK and the drum level are not on MASTER");
    check(pg_has_g(SCR_GLOBAL, G_BPM) && pg_has_g(SCR_GLOBAL, G_SWING) && pg_has_g(SCR_GLOBAL, G_CLOCK),
          "D3: BPM, SWING and CLICK are on GLOBAL (TEMPO, REC)");
    ui.scr = SCR_FX;
    row = glo_find(SCR_FX, "LIMIT");
    SCR->cell(row, 2, &c);
    check(row < n && c.vp == &song.g[G_TUNE] && !strcmp(c.label, "TUNE"), "D3: TUNE is on LIMIT (GAIN CEIL TUNE GR)");
    reset_ui();
    turn(EN_ALGO, -1);
    tap(B_FX);
    while (ui.row[SCR_FX] != row)
        tap(B_FX);
    turn(EN_K3, 3);
    check(song.g[G_TUNE] == tune + 3, "D3: MASTER LIMIT, KNOB 3 turns TUNE");
    song.g[G_TUNE] = tune;
    reset_ui();
    song.sel = TRK_DRUM;
    frames(2);
    check(pg_has_g(SCR_HOME, G_DRLVL) || (ui.scr = SCR_HOME, mix_find("VOLUME") && (mix_cell(mx_row(), ui.hot, &c), c.vp == &song.g[G_DRLVL])),
          "D3: the drum level is on the mixer's DR row (VOLUME)");
    mx.set = 0;
    song.sel = 0;
    reset_ui();
}

/* D10: the keys full / dim / dark in every mode, as the FM6 layer's test (ui_optimist_test.c fm6_tests) */
static uint32_t key_led(uint32_t k)                     /* 2 full, 1 dim, 0 dark: key k's LED */
{
    uint8_t q = led_pos[14u + k];
    if (q == 0xFF)
        return 9;
    return (fm1_led[q >> 3] >> (q & 7u)) & 1u ? 2u : (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u ? 1u : 0u;
}
static int keys_are(uint32_t full, uint32_t dim, const char *what)   /* every key: full, dim or dark as asked */
{
    uint32_t k, ok = 1;
    for (k = 0; k < 27u; k++) {
        uint32_t want = (full >> k) & 1u ? 2u : (dim >> k) & 1u ? 1u : 0u, lv = key_led(k);
        if (lv != want) {
            printf("  %s: key %u is %u, wants %u\n", what, k, lv, want);
            ok = 0;
        }
    }
    check(ok, what);
    return (int)ok;
}
static uint32_t kw(uint32_t i) { return 1u << key_of_lane(i); }   /* white key i (0..15) */
static uint32_t kws(uint32_t a, uint32_t b)             /* white keys a..b */
{
    uint32_t m = 0;
    for (; a <= b; a++)
        m |= kw(a);
    return m;
}
static void hold_layer(uint32_t b) { press(b); frames(HOLD_FRAMES); }
static void let_go(uint32_t b) { release(b); frames(2); reset_ui(); }
/* the pages with no track context (GLOBAL, SYSTEM, PROJECT): the keys play nothing, send nothing, light nothing, pick
 * no lane; PROJECT's slot rows keep the slot pick; the layers and a key held over a page change stay sound */
static int key_dead(uint32_t k) { return kb_kind[k] == KS_UI || kb_kind[k] == KS_NONE; }
static void keys_global_tests(void)
{
    uint32_t pass, r, tools = 0, sel0;
    for (pass = 0; pass < 2u; pass++) {
        song.sel = pass ? TRK_DRUM : 0;
        reset_ui();
        frames(2);
        tap(B_GLO);
        frames(2);
        check(ui.scr == SCR_GLOBAL, "keys, no-track pages: GLO: GLOBAL");
        kdown(WK(3));
        check(key_dead(WK(3)) && kb_kind[WK(3)] != KS_NOTE && kb_kind[WK(3)] != KS_DRUM, "GLOBAL: a white key plays nothing");
        keys_are(0, 0, "GLOBAL: the keys are dark");
        kup(WK(3));
        kdown(1);
        check(kb_kind[1] != KS_NOTE && kb_kind[1] != KS_DRUM, "GLOBAL: a black key plays nothing");
        kup(1);
        sel0 = lane_selected();
        press(B_HOME);
        frames(HOLD_FRAMES);
        kdown(WK(5));
        check(lane_selected() == sel0 && kb_kind[WK(5)] != KS_NOTE && kb_kind[WK(5)] != KS_DRUM, "GLOBAL: HOME + a key: no lane pick, no sound");
        keys_are(0, 0, "GLOBAL: HOME held: the keys stay dark");
        kup(WK(5));
        release(B_HOME);
        tap(B_GLO);
        frames(2);
        check(ui.scr == SCR_SYSTEM, "keys, no-track pages: GLO again: SYSTEM");
        kdown(WK(2));
        check(kb_kind[WK(2)] != KS_NOTE && kb_kind[WK(2)] != KS_DRUM, "SYSTEM: a key plays nothing");
        keys_are(0, 0, "SYSTEM: the keys are dark");
        kup(WK(2));
        reset_ui();
        op_enter(SCR_PROJECT);
        for (r = 0; r < NPRJ; r++)
            if (prj_kind(r) == PR_TOOLS)
                tools = r;
        ui.row[SCR_PROJECT] = (uint8_t)tools;
        frames(2);
        kdown(WK(2));
        check(kb_kind[WK(2)] != KS_NOTE && kb_kind[WK(2)] != KS_DRUM, "PROJECT, TOOLS row: a key plays nothing");
        keys_are(0, 0, "PROJECT, TOOLS row: dark");
        kup(WK(2));
        ui.row[SCR_PROJECT] = 0;
        ui.force = 1;
        frames(2);
        kdown(WK(2));
        frames(2);
        check(kb_kind[WK(2)] != KS_NOTE && kb_kind[WK(2)] != KS_DRUM && song.g[G_SLOT] == 3, "PROJECT, slot row: the key still picks slot 3");
        kup(WK(2));
        press(B_HOME);
        frames(HOLD_FRAMES);
        sel0 = lane_selected();
        kdown(WK(1));
        check(lane_selected() == sel0 && song.g[G_SLOT] == 3, "PROJECT, HOME held + a key: no lane pick, no slot");
        kup(WK(1));
        release(B_HOME);
        reset_ui();
        /* MASTER FX, SCOPE, SONG (not its PATTERNS row), TEMPO: the same */
        {
            static const uint8_t SCRS[3] = {SCR_FX, SCR_SCOPE, SCR_SONG};
            uint32_t i;
            for (i = 0; i < 3u; i++) {
                reset_ui();
                op_enter(SCRS[i]);
                ui.row[SCRS[i]] = 0;
                frames(2);
                if (SCRS[i] == SCR_SONG && song_on_pat_row())
                    continue;
                sel0 = lane_selected();
                kdown(WK(3));
                check(ui.scr == SCRS[i] && kb_kind[WK(3)] != KS_NOTE && kb_kind[WK(3)] != KS_DRUM, "MASTER FX / SCOPE / SONG: a key plays nothing");
                keys_are(0, 0, "MASTER FX / SCOPE / SONG: the keys are dark");
                kup(WK(3));
                kdown(1);
                check(kb_kind[1] != KS_NOTE && kb_kind[1] != KS_DRUM, "MASTER FX / SCOPE / SONG: a black key plays nothing");
                kup(1);
                press(B_HOME);
                frames(HOLD_FRAMES);
                kdown(WK(5));
                check(lane_selected() == sel0 && kb_kind[WK(5)] != KS_NOTE && kb_kind[WK(5)] != KS_DRUM, "MASTER FX / SCOPE / SONG: HOME + a key: no lane pick");
                kup(WK(5));
                release(B_HOME);
            }
            reset_ui();
            press(B_PLAY);
            frames(HOLD_FRAMES);
            kdown(WK(2));
            check(ui.scr == SCR_TEMPO && kb_kind[WK(2)] != KS_NOTE && kb_kind[WK(2)] != KS_DRUM, "TEMPO (PLAY held): a key plays nothing (it taps the tempo)");
            kup(WK(2));
            release(B_PLAY);
            reset_ui();
        }
        /* a layer from a no-track page: SAVE held on GLOBAL shows the scenes, the key is the layer's */
        tap(B_GLO);
        frames(2);
        hold_layer(B_SAVE);
        kdown(WK(1));
        check(kb_kind[WK(1)] == KS_UI && lay.shown == LY_SONG, "GLOBAL, SAVE held + a key: the scene (the layer's key)");
        kup(WK(1));
        let_go(B_SAVE);
        hold_layer(B_FX);
        kdown(WK(2));
        check(kb_kind[WK(2)] == KS_FX, "GLOBAL, FX held + a key: the punch-in");
        kup(WK(2));
        let_go(B_FX);
        /* a key held over a page change: its note-off, and a UI key's release leaves no step held */
        reset_ui();
        kdown(WK(4));
        check(kb_kind[WK(4)] == KS_NOTE || kb_kind[WK(4)] == KS_DRUM, "the mixer: a key plays");
        tap(B_GLO);
        frames(2);
        kup(WK(4));
        check(kb_kind[WK(4)] == KS_NONE, "a key down on the mixer, GLOBAL shown, released: note off");
        kdown(WK(4));
        tap(B_SEQ);
        frames(2);
        kup(WK(4));
        check(kb_kind[WK(4)] == KS_NONE && st.held == 0u, "a key down on GLOBAL, STEP shown, released: no step held");
        reset_ui();
    }
}
static void pages_keys_tests(void)
{
    uint32_t k, root = 0;
    int16_t fxoff[4];
    reset_ui();
    song.sel = 0;
    frames(2);
    keys_are(0, 0, "D10: the mixer, a synth track: the keys only play, all dark");
    song.sel = TRK_DRUM;
    frames(2);
    keys_are(0, 0, "D10: the mixer, the drum track: the keys play, all dark (the lane key only where keys pick lanes)");
    press(B_HOME);
    frames(HOLD_FRAMES);
    keys_are(0, kws(0, 15) | 1u << 1 | 1u << 3 | 1u << 5 | 1u << 8,
             "D10: the mixer, HOME held: the sequencer mutes, the 16 lanes and the 4 black keys dim, none muted");
    release(B_HOME);
    tap(B_EDIT);
    press(B_HOME);
    frames(HOLD_FRAMES);
    keys_are(kw(lane_selected()), kws(0, 15), "D10: the drum track's SOUND, HOME held: the 16 lanes dim, the selected one full");
    release(B_HOME);
    reset_ui();
    song.sel = 0;
    frames(2);
    hold_layer(B_FX);
    keys_are(0, kws(0, 15), "D10: FX layer: the 16 effects dim");
    punch.req = 3;
    frames(2);
    keys_are(kw(3), kws(0, 15), "D10: FX layer: the latched effect full");
    punch.req = -1;
#if FELUCCA_LIGHTS
    {   /* LIGHTS / KEYS ALL: the backlight off while a mode lights its keys */
        uint8_t kl = lights_keys, ll = lights_lvl, q = led_pos[14u];
        uint32_t on_lay, on_mix;
        lights_keys = 3;
        lights_lvl = LIGHTS_LOW;
        frames(2);
        on_lay = (fm1_led_bg[q >> 3] >> (q & 7u)) & 1u;
        let_go(B_FX);
        frames(2);
        on_mix = (fm1_led_bg[q >> 3] >> (q & 7u)) & 1u;
        check(!on_lay && on_mix, "D10: the KEYS backlight is off while a layer lights its keys, on again on the mixer");
        lights_keys = kl;
        lights_lvl = ll;
    }
#else
    let_go(B_FX);
#endif
    for (k = 0; k < 4u; k++)
        fxoff[k] = trk[k].p[P_FXOFF], trk[k].p[P_FXOFF] = 0;
    trk[1].p[P_MUTE] = 1;
    song.solo = 1u << 2;
    hold_layer(B_GLO);
#if FELUCCA_FILLS
    keys_are(kw(1) | kw(6), kws(0, 9) | kw(15) | 1u << 1 | 1u << 3 | 1u << 5 | 1u << 8,
             "D10: GLO layer: mute solo fill bar tap and the black 1-4 dim, T2 muted and T3 soloed full");
    trk[0].p[P_FXOFF] = 1;
    frames(2);
    check(key_led(1) == 2u && key_led(3) == 1u, "D10: GLO layer: T1's FX off, its black key full");
#else
    keys_are(kw(1) | kw(6), kws(0, 11) | kw(15), "D10: GLO layer: mute solo FX tap dim, T2 muted and T3 soloed full");
    trk[0].p[P_FXOFF] = 1;
    frames(2);
    check(key_led(key_of_lane(8)) == 2u, "D10: GLO layer: T1's FX off, its key full");
#endif
    for (k = 0; k < 4u; k++)
        trk[k].p[P_FXOFF] = fxoff[k];
    trk[1].p[P_MUTE] = 0;
    song.solo = 0;
    let_go(B_GLO);
    hold_layer(B_SCL);
    for (k = 0; k < 27u; k++)
        root |= (uint32_t)((53u + k) % 12u == (uint32_t)trk[0].p[P_ROOT] % 12u) << k;
    keys_are(root, (1u << 27) - 1u, "D10: SCL layer: every key dim, the song's root full");
    let_go(B_SCL);
    live_sec = 0;
    hold_layer(B_SAVE);
    keys_are(arrangement_clock.running ? 0u : kw(0), kws(0, LAY_NSCN - 1u), "D10: SAVE layer: the scenes dim, the playing one full");
    let_go(B_SAVE);
    live_sec = -1;
    hold_layer(B_EDIT);
    keys_are(0, 0, "D10: EDIT layer (ERASE): a play surface, dark");
    let_go(B_EDIT);
    hold_layer(B_ARP);
    keys_are(0, 0, "D10: ARP layer (REPEAT): a play surface, dark");
    let_go(B_ARP);
#if FELUCCA_PATTERNS
    hold_layer(B_LFO);
    {
        uint32_t blk = 0, kk;
        for (kk = 0; kk < 27u; kk++)
            blk |= (uint32_t)(pat_black(kk) >= 1u && pat_black(kk) <= 10u) << kk;
        check(key_led(1) == 2u && key_led(3) == 1u && key_led(0) >= 1u && key_led(25) == 0u && (blk & 1u << 22),
              "D10: LFO layer: the patterns and the 10 black keys dim, the selected track's key full");
    }
    let_go(B_LFO);
#endif
    press(B_PLAY);
    frames(HOLD_FRAMES + 30u);
    keys_are(0, kws(0, 15), "D10: TEMPO (PLAY held): the white keys tap, dim");
    release(B_PLAY);
    frames(2);
    transport_req = 0;
    song.playing = 0;
    reset_ui();
    {   /* STEP: set steps full, empty steps inside LEN dim, past LEN dark */
        track_t keep = *TSEL;
        track_defaults_steps(TSEL);
        TSEL->p[P_SLEN] = 8;
        tap(B_SEQ);
        frames(2);
        keys_are(0, kws(0, 7), "D10: STEP: an empty pattern of 8: steps 1-8 dim, 9-16 dark");
        *TSEL = keep;
        tap(B_HOME);
    }
    reset_ui();
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    turn(EN_K4, 1);
    tap(B_SAVE);                                        /* SAVE AS: asks, then NAME */
    tap(B_SAVE);
    frames(2);
    {
        uint32_t d = 0, kk;
        for (kk = 0; kk < 27u; kk++)
            d |= (uint32_t)(punch_key(kk) >= 0 || nm_black(kk) != NMK_NONE) << kk;
        keys_are(0, d, "D10: NAME: every key with a job dim");
        key(key_of_lane(2));
        keys_are(kw(2), d, "D10: NAME: the key cycling its letters full");
    }
    name_close();
    reset_ui();
}

/* D11: the headers as drawn: cut, then cased (no lower-case start); full names; a continuation's digit never next to n/N */
static void head_now(char *t)                           /* the header's title now, as draw_head makes it */
{
    char r[16], bpm[8], pos[12];
    const char *eng = head_engine();
    if (ui.scr == SCR_FX || ui.master || (lay.shown == LY_PLAY && mx_on_master()))
        eng = "MASTER";
    else if ((ui.scr == SCR_GLOBAL || ui.scr == SCR_SYSTEM) && lay.shown == LY_PLAY)
        eng = SCR_NAME[ui.scr];
    head_title(ui.scr, ui.row[ui.scr], t, 32);
    head_pos(ui.scr, ui.row[ui.scr], pos);
    loop_pos(TSEL, r);
    if (ui.scr == SCR_STEP)
        r[0] = 0;
    fmt_int(bpm, song.g[G_BPM]);
    head_finish(t, pos, head_room(text_w(&FONT_S, eng) + 8, bpm, r));
}
static uint32_t head_bad;
static void head_ok(const char *where)                  /* the header now: a capital first, no "n n/N" */
{
    char t[32];
    uint32_t i, n;
    head_now(t);
    n = str_len(t);
    for (i = 0; i + 2u < n && t[i + 2u] != '/'; i++)
        ;
    if (!(t[0] >= 'A' && t[0] <= 'Z') || (i >= 2u && i + 2u < n && t[i - 1u] >= '0' && t[i - 1u] <= '9' && t[i - 2u] == ' ' &&
                                          !(i >= 5u && (!strncmp(t + i - 5u, "OSC", 3) || !strncmp(t + i - 5u, "FLT", 3) || !strncmp(t + i - 5u, "LFO", 3) || !strncmp(t + i - 5u, "Lfo", 3) ||
                                                        !strncmp(t + i - 5u, "Osc", 3) || !strncmp(t + i - 5u, "Flt", 3) ||
                                                        !strncmp(t + i - 6u, "Part", 4))))) {
        printf("  header %s: '%s'\n", where, t);
        head_bad++;
    }
    if (getenv("HEADS"))
        printf("  head %-10s '%s'\n", where, t);
}
static void heads_of(uint32_t scr, const char *where)   /* every row of screen scr */
{
    uint32_t r, n;
    ui.scr = (uint8_t)scr;
    n = SCREENS[scr].rows();
    for (r = 0; r < n; r++) {
        ui.row[scr] = (uint8_t)r;
        head_ok(where);
    }
    ui.row[scr] = 0;
}
static void heads_fam(uint32_t b, const char *where)    /* a family's rows, by its button */
{
    uint32_t r, n;
    go_home();
    frame();
    tap(b);
    if (ui.scr != SCR_SOUND)
        return;
    n = SCR->rows();
    for (r = 0; r < n; r++) {
        ui.row[SCR_SOUND] = (uint8_t)r;
        head_ok(where);
    }
}
static int head_is(const char *want)
{
    char t[32];
    head_now(t);
    return !strcmp(t, want);
}
static void pages_head_tests(void)
{
    static const uint8_t FAMB[] = {B_ENV, B_LFO, B_FX, B_EDIT, B_ARP, B_SCL};
    uint32_t i, tr;
    reset_ui();
    head_bad = 0;
    for (tr = 0; tr < NTRK; tr++) {
        song.sel = (uint8_t)tr;
        for (i = 0; i < sizeof FAMB; i++)
            heads_fam(FAMB[i], "SOUND");
    }
    song.sel = 0;
    heads_of(SCR_GLOBAL, "GLOBAL");
    heads_of(SCR_SYSTEM, "SYSTEM");
    heads_of(SCR_PROJECT, "PROJECT");
    heads_of(SCR_FX, "MASTER");
    heads_of(SCR_SONG, "SONG");
    heads_of(SCR_SCOPE, "SCOPE");
    ui.scr = SCR_HOME;
    ui.row[SCR_HOME] = MXR_MASTER;
    head_ok("mixer M");
    reset_ui();
    check(head_bad == 0u, "D11: every header of SOUND (every track, family), GLOBAL, SYSTEM, PROJECT, MASTER, SONG, SCOPE: "
                          "a capital first, no digit before n/N");
    {   /* GLOBAL and SYSTEM: the cursor line's section, the page n/N (section 20) */
        char t[32], w[32];
        uint32_t l;
        op_enter(SCR_GLOBAL);
        ui.row[SCR_GLOBAL] = 0;
        head_now(t);
        check(!strncmp(t, "Tempo 1/", 8), "D11: GLOBAL's first line: \"Tempo 1/N\" (the page of 4 lines)");
        l = line_named(SCR_GLOBAL, "QUANT");
        head_now(t);
        snprintf(w, sizeof w, "Rec %u/", gl_pg(ui.scr == SCR_SYSTEM, l) + 1u);
        check(!strncmp(t, w, strlen(w)), "D11: GLOBAL's QUANT line (REC 2): \"Rec n/N\", its page");
        l = line_named(SCR_SYSTEM, "COLOR");
        head_now(t);
        snprintf(w, sizeof w, "Screen %u/", gl_pg(ui.scr == SCR_SYSTEM, l) + 1u);
        check(!strncmp(t, w, strlen(w)), "D11: SYSTEM's COLOR line: \"Screen n/N\", its page");
    }
    reset_ui();
    turn(EN_ALGO, -1);
    tap(B_FX);
    check(head_is("Delay 1/7") || head_is("Delay 1/6") || head_is("Delay 1/8") || head_is("Delay 1/9"),
          "D11: MASTER DELAY: \"Delay 1/7\" (was \"master dl 1/9\")");
    ui.row[SCR_FX] = (uint8_t)glo_find(SCR_FX, "TONE");
    {
        char t[32];
        head_now(t);
        check(!strncmp(t, "Tone ", 5), "D11: MASTER TONE: \"Tone n/N\" (was \"FX master 5/9\")");
    }
    reset_ui();
    tap(B_EDIT);
    tap(B_EDIT);
    {
        char t[32];
        head_now(t);
        check(!strncmp(t, "Edit 2/", 7), "D11: EDIT 1: \"Edit 2/N\" (was \"edit 1 1/7\")");
    }
    reset_ui();
}

/* SELECT is the page of the four knobs, ALGORITHM the track or the lane (the user, 2026-10-12: "in step seq: select should
 * always select the page for the 4 knobs, and algo always the track/drum sub track"), on every page but the mixer */
static void pages_select_tests(void)
{
    uint32_t i, ok = 1;
    cell_t c;
    reset_ui();
    song.sel = TRK_DRUM;
    ui.lane = 1;
    lane_select(3);
    frame();
    tap(B_SEQ);
    for (i = 0; i < 6u; i++) {
        turn(EN_SELECT, i & 1u ? -1 : 1);
        ok &= lane_selected() == 3u && ui.lane == 1u;
    }
    check(ok && SCR->rows() == STP_LANE0 + 1u + STP_TL, "rule: SELECT on the drum STEP pages PATTERN / LANE, never ui.lane or the lane");
    ui.row[SCR_STEP] = (uint8_t)STP_LANE0;
    turn(EN_ALGO, 1);
    check(lane_selected() == 4u && ui.lane == 1u && ui.row[SCR_STEP] == STP_LANE0, "rule: ALGORITHM on the drum STEP: the next lane");
    turn(EN_ALGO, -5);
    check(lane_selected() == 4u && ui.lane == 0u, "rule: ALGORITHM back past the first lane: DR itself (ui.lane 0)");
    tap(B_HOME);
    ui.lane = 1;
    lane_select(3);
    tap(B_EDIT);                                        /* the drum SOUND row: KIT, LANE (a read-out) */
    SCR->cell(0, 1, &c);
    turn(EN_K2, 2);
    check(c.kind == CK_RO && !strcmp(c.label, "LANE") && lane_selected() == 3u, "rule: the drum SOUND row's LANE is a read-out; KNOB 2 leaves the lane");
    ok = 1;
    for (i = 0; i < 5u; i++) {
        turn(EN_SELECT, 1);
        ok &= lane_selected() == 3u;
    }
    check(ok, "rule: SELECT on the drum SOUND pages: the pages, never the lane");
    song.sel = 0;
    lane_select(0);
    ui.lane = 0;
    reset_ui();
}

/* a drum key played selects its lane on every page where the keys play (the user, 2026-10-12: "when i play a note for
 * the drums on the keyboard it should switch to that instrument in all the relevant pages"); DR itself included */
static void pages_hit_tests(void)
{
    uint32_t snare = lane_of_key(WK(2)), kick = lane_of_key(WK(0));
    reset_ui();
    song.sel = TRK_DRUM;
    ui.lane = 0;
    lane_select(kick);
    frames(30);                                         /* (no knob in the last 300 ms) */
    ui.row[SCR_HOME] = MXR_DR;
    frame();
    key(WK(2));
    check(lane_selected() == snare && ui.lane == 1u && mx_row() == MXR_LANE0 + snare,
          "drum key: on the mixer with DR itself selected, the key's lane selected, the cursor on its row");
    tap(B_EDIT);
    tap(B_EDIT);
    key(WK(0));
    check(ui.scr == SCR_SOUND && lane_selected() == kick && ui.lane == 1u, "drum key: on the drum SOUND pages, the lane follows");
    tap(B_FX);
    key(WK(2));
    check(lane_selected() == snare, "drum key: on the drum FX page too");
    ui.hot = 0;
    turn(EN_K1, 1);
    turn(EN_K1, -1);
    key(WK(0));
    check(lane_selected() == snare, "drum key: a knob turned in the last 300 ms: the lane stays (the cards do not move)");
    frames(30);
    key(WK(0));
    check(lane_selected() == kick, "... later: the lane follows again");
    tap(B_HOME);
    press(B_FX);
    frames(HOLD_FRAMES);
    key(WK(2));
    release(B_FX);
    frames(2);
    check(lane_selected() == kick, "drum key: the FX layer held, the key is an effect: no lane change");
    reset_ui();
    song.sel = TRK_DRUM;
    tap(B_SEQ);
    frames(30);
    key(WK(2));
    check(lane_selected() == kick, "drum key: STEP, the keys steps: no lane change");
    tap(B_HOME);
    song.sel = 0;
    lane_select(0);
    ui.lane = 0;
    track_defaults_steps(&trk[TRK_DRUM]);
    reset_ui();
}
