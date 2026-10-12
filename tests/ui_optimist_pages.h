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
    /* D1 (revised): one cycle, GLOBAL's rows then SYSTEM's, GLO steps it round, SELECT walks it and stops at its ends */
    {
        uint32_t ng = SCREENS[SCR_GLOBAL].rows(), ns = SCREENS[SCR_SYSTEM].rows(), i;
        char p[12];
        ui.row[SCR_GLOBAL] = 0;
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "D1: GLO tapped on the mixer: GLOBAL");
        for (i = 1; i < ng; i++)
            tap(B_GLO);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == ng - 1u, "D1: GLO again: GLOBAL's next rows, to its last");
        tap(B_GLO);
        frames(2);
        head_pos(SCR_SYSTEM, 0, p);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == 0u && led_lvl(B_GLO) == 2 && px_in(2, 3, 4, 19, C_HI),
              "D1: GLO past GLOBAL's last: SYSTEM's first row (CPU), the GLO LED full, the badge SYSTEM");
        check(!strncmp(p, " 1/", 3), "D1: the n/N counts within the screen (SYSTEM 1/n)");
        for (i = 1; i < ns; i++)
            tap(B_GLO);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == ns - 1u, "D1: GLO again: SYSTEM's rows, to its last");
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "D1: GLO past SYSTEM's last: GLOBAL's first (round)");
        turn(EN_SELECT, -3);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "D1: SELECT left on GLOBAL's first: stays (no wrap)");
        turn(EN_SELECT, (int32_t)ng);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == 0u, "D1: SELECT right past GLOBAL's last: SYSTEM's first");
        turn(EN_SELECT, -1);
        check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == ng - 1u, "D1: SELECT left from SYSTEM's first: GLOBAL's last");
        turn(EN_SELECT, 100);
        check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == ns - 1u, "D1: SELECT right: stops at SYSTEM's last (no wrap)");
        tap(B_ENV);
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL, "D1: GLO from another screen: GLOBAL");
        check(glo_find(SCR_GLOBAL, "SYSTEM") == ng, "D1: GLOBAL has no SYSTEM row (the cycle reaches SYSTEM)");
#if FELUCCA_BLE
        op_enter(SCR_SYSTEM);
        ui.row[SCR_SYSTEM] = (uint8_t)glo_find(SCR_SYSTEM, "BLUETOOTH");
        ui.hot = 0;
        frame();
        tap(B_SAVE);
        i = (uint32_t)dev_listing();
        tap(B_GLO);
        check(i && !dev_listing() && ui.scr == SCR_SYSTEM, "D1: GLO on the BLE devices list: back to SYSTEM's BLUETOOTH row");
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
    check(!strcmp(nm, "CPU") && c.kind == CK_RO && !strcmp(c.label, "CLOCK") && !strcmp(c.unit, "MHz") &&
          c2.kind == CK_RO && !strcmp(c2.label, "LOAD") && !strcmp(c2.unit, "%"),
          "D2: SYSTEM's first row: CPU, CLOCK (MHz) and LOAD (%), read-outs");
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
    ui.row[SCR_SYSTEM] = (uint8_t)glo_find(SCR_SYSTEM, "CALIBRATE");
    ui.hot = 0;
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
    keys_are(kw(lane_selected()), kws(0, 15), "D10: the drum track, HOME held: the 16 lanes dim, the selected one full");
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
    tap(B_SAVE);                                        /* SAVE AS: NAME */
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
                                          !(i >= 5u && (!strncmp(t + i - 5u, "OSC", 3) || !strncmp(t + i - 5u, "FLT", 3) ||
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
    op_enter(SCR_GLOBAL);
    check(head_is("Tempo 1/6") || head_is("Tempo 1/5"), "D11: GLOBAL TEMPO: \"Tempo 1/6\" (was \"tempo 1/7\")");
    ui.row[SCR_GLOBAL] = TPR_REC2;
    check(head_is("Rec 3/6") || head_is("Rec 3/5"), "D11: GLOBAL REC 2: \"Rec 3/6\" (was \"rec 2 3/7\")");
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = (uint8_t)glo_find(SCR_SYSTEM, "SCREEN");
    {
        char t[32];
        head_now(t);
        check(!strncmp(t, "Screen 2/", 9), "D11: SYSTEM SCREEN: \"Screen 2/n\" (was \"screen 1/6\")");
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
    check(ok && SCR->rows() == STP_LANE0 + 1u, "rule: SELECT on the drum STEP pages PATTERN / LANE, never ui.lane or the lane");
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
