/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, every line of GLOBAL and SYSTEM walked (fix/settings-lines, 2026-10-13; the user: "many lines
 * in GLOBAL and SYSTEM display YES and nothing can be done with them"). ALGORITHM puts the cursor on each line in turn:
 *   a value     its knob changes a setting and the text drawn (one way, else the other at an end), and back; PRESETS
 *               changes nothing (the user, 2026-10-13); a value with a descriptor: HOME held + its knob its default
 *   a read-out  its knob and PRESETS change no setting
 *   an action   drawn as a word that is not YES (op_gldraw.c gl_val); its knob and PRESETS change nothing; YES runs it
 *               (PANEL: the question, cancelled by HOME)
 * Every line has a name and a value drawn. Included after ui_optimist_glo.h. */
static uint32_t glw_mix(uint32_t h, const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t glw_state(void)                        /* every setting a GLOBAL or SYSTEM line can change */
{
    uint32_t h = glw_mix(2166136261u, song.g, sizeof song.g);
    h = glw_mix(h, bp_set, sizeof bp_set);
    h = glw_mix(h, &settings.palette, sizeof settings.palette);
    h = glw_mix(h, &settings.lowcut, sizeof settings.lowcut);
    h = glw_mix(h, &knob_colors, 1);
    h = glw_mix(h, &hold_sel, 1);
    h = glw_mix(h, &TSEL->p[P_SLEN], sizeof TSEL->p[P_SLEN]);
#if FELUCCA_REC_MODES
    h = glw_mix(h, &rec_tempo, 1);
    h = glw_mix(h, &rec_count, 1);
#endif
#if FELUCCA_BRIGHT
    h ^= bright_level() * 0x9E3779B1u;
#endif
#if FELUCCA_LIGHTS
    h = glw_mix(h, &lights_lvl, 1);
    h = glw_mix(h, &lights_keys, 1);
#endif
#if FELUCCA_CDC
    h = glw_mix(h, &usb_serial, 1);
#endif
#if FELUCCA_BLE
    h = glw_mix(h, &ble_on, 1);
#endif
#if FELUCCA_MIDI_CLKOUT
    h = glw_mix(h, &clko_on, 1);
#endif
#if DUAL_LOAD
    h = glw_mix(h, &dual_mode, 1);
#endif
    return h;
}
static void glw_line(uint32_t scr, uint32_t i, cell_t *c, char *v)   /* line i as the list draws it: cell, value text */
{
    SCREENS[scr].cell(i, gl_kn(scr, i), c);
    char lv[14];
    str_cpy(v, gl_text(c, lv), 16);
}
/* a control turned on line i, a detent at a time (up to 4: SWING's 0..100 shows 50..75 %, a detent may not show): one
 * way, else (an end) the other. 1 when a setting and the text drawn changed; then back */
static int glw_moves(uint32_t scr, uint32_t i, uint32_t role)
{
    cell_t c;
    char v0[16], v1[16];
    uint32_t h0 = glw_state(), h1, n = 0;
    int32_t s = 1;
    glw_line(scr, i, &c, v0);
    turn(role, s);
    if (glw_state() == h0) {
        s = -1;
        turn(role, s);
    }
    for (n = 1, glw_line(scr, i, &c, v1); !strcmp(v0, v1) && n < 4u; n++, glw_line(scr, i, &c, v1))
        turn(role, s);
    h1 = glw_state();
    while (n--)
        turn(role, -s);
    return h1 != h0 && strcmp(v0, v1) != 0 && ui.row[scr] == i;   /* (back: an accelerated knob may land near) */
}
static int glw_still(uint32_t role)                     /* a control turned both ways: no setting changed */
{
    uint32_t h0 = glw_state();
    turn(role, 1);
    turn(role, -1);
    return glw_state() == h0;
}
static int glw_walk_line(uint32_t scr, uint32_t i)
{
    cell_t c;
    char v[16];
    int ok;
    line_at(scr, i);
    glw_line(scr, i, &c, v);
    ok = ui.scr == scr && ui.row[scr] == i && c.label && (c.label[0] || c.unit[0]) && (v[0] || c.unit[0] || !strcmp(c.label, "INFO"));   /* (INFO, REPO: text in the unit, or the page itself) */
    if (c.kind == CK_VAL) {
        ok &= glw_moves(scr, i, LINE_KNOB(i));
        ok &= glw_still(EN_PRESET);
        if (c.d && c.vp) {                              /* a descriptor's value: HOME held + its knob, its default */
            int16_t was = *c.vp;
            press(B_HOME);
            turn(LINE_KNOB(i), 1);
            release(B_HOME);
            ok &= *c.vp == c.d->def && ui.scr == scr;
            *c.vp = was;
        }
    } else if (c.kind == CK_RO) {
        ok &= glw_still(LINE_KNOB(i)) && glw_still(EN_PRESET);
    } else if (c.kind == CK_ACT) {
        ok &= strcmp(v, "YES") != 0 && glw_still(LINE_KNOB(i)) && glw_still(EN_PRESET) && !op_armed();
        ui.msg[0] = 0;
        tap(B_SAVE);
        ok &= op_armed() || ui.msg[0];                  /* (YES runs it) */
        if (op_armed())
            tap(B_HOME);                                /* (the question: cancelled) */
        ok &= !op_armed() && ui.scr == scr;
    } else {
        ok = 0;
    }
    if (!ok)
        printf("  glwalk: screen %u line %u %s kind %u value %s\n", scr, i, c.label ? c.label : "-", c.kind, v);
    return ok;
}
/* HOME held + PRESETS on a line with no descriptor: its FACTORY value, what a blank FM-1 / an empty settings record gives
 * (the user, 2026-10-13; panel.c settings_init, bright.c bright_boot, settings_word.c read as 0); BLE is never reset */
static const struct { const char *label, *val; } GLW_FACTORY[] = {
    {"MODE", "FREE"}, {"LENGTH", "1 BAR"}, {"START", "NOTE"}, {"CLK OUT", "OFF"}, {"COLOR", 0}, {"BRIGHT", "8"},
    {"KNOB COLORS", "OFF"}, {"BUTTONS", "OFF"}, {"KEYS", "OFF"}, {"LOWCUT", "OFF"}, {"SERIAL", "OFF"}, {"HOLD", "350"}};
static const char *glw_factory(const char *label)      /* its factory text, 0: none in the table */
{
    uint32_t i;
    for (i = 0; i < sizeof GLW_FACTORY / sizeof GLW_FACTORY[0]; i++)
        if (!strcmp(GLW_FACTORY[i].label, label))
            return GLW_FACTORY[i].val ? GLW_FACTORY[i].val : PALETTES[4].name;   /* (COLOR: MONO, settings_init's 4) */
    return 0;
}
static void glw_factory_tests(void)
{
    uint32_t s, i, n, ok = 1, seen = 0, ble_ok = 1;
    cell_t c;
    char v[16];
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        op_enter(scr);
        n = SCREENS[scr].rows();
        for (i = 0; i < n; i++) {
            const char *f;
            uint32_t tries;
            line_at(scr, i);
            glw_line(scr, i, &c, v);
            if (c.kind != CK_VAL || c.d)
                continue;                               /* (a descriptor's line: its default, glw_walk_line) */
            if (!strcmp(c.label, "BLE")) {             /* never reset: a live connection stays */
#if FELUCCA_BLE
                uint8_t was = ble_on;
                ble_on = 1;
                press(B_HOME);
                turn(LINE_KNOB(i), 1);
                release(B_HOME);
                ble_ok &= ble_on == 1u;
                ble_on = was;
#endif
                continue;
            }
            f = glw_factory(c.label);
            if (!f) {
                printf("  glwalk: %s has no factory value in the test's table\n", c.label);
                ok = 0;
                continue;
            }
            for (tries = 0; tries < 3u && !strcmp(v, f); tries++) {   /* (off the factory value first) */
                turn(LINE_KNOB(i), tries == 1u ? -1 : 1);
                glw_line(scr, i, &c, v);
            }
            press(B_HOME);
            turn(LINE_KNOB(i), 1);
            release(B_HOME);
            glw_line(scr, i, &c, v);
            if (strcmp(v, f)) {
                printf("  glwalk: %s after HOME + knob %s, factory %s\n", c.label, v, f);
                ok = 0;
            }
            seen++;
        }
    }
    check(ok && seen >= 3u && ble_ok, "HOME + a line's knob: every setting line without a default back to its FACTORY value; BLE never");
    settings.palette = 4;
    palette_set(4);
}
#if FELUCCA_MIDI_CLOCK
/* SYNC AUTO in the list (GLOBAL's MIDI page): "Auto: Int" / "Auto: USB" /
 * "Auto: TRS" by the clock followed (G_MIDI), inside the value field next to the label; the cards (param_format) keep
 * "A:INT" ...; a fixed SYNC draws its own name. 2026-10-13: the user wanted "Auto: Int". */
static void glw_sync_long_tests(void)
{
    static const char *const LONG[3] = {"Auto: Int", "Auto: USB", "Auto: TRS"}, *const CARD[3] = {"A:INT", "A:USB", "A:TRS"};
    static const char *const FIX[3] = {"Int", "USB", "TRS"}, *const FIXC[3] = {"INT", "USB", "TRS"};
    static const uint32_t SCRS[1] = {SCR_GLOBAL};
    int16_t sync = song.g[G_SYNC], midi = song.g[G_MIDI];
    uint32_t s, k, ok = 1;
    int32_t fit = 1;
    cell_t c;
    char v[16], card[16];
    const char *u;
    for (s = 0; s < 1u; s++) {
        uint32_t i = line_named(SCRS[s], "CLK IN");
        for (k = 0; k < 3u; k++) {
            song.g[G_SYNC] = SYNC_AUTO;
            song.g[G_MIDI] = (int16_t)k;
            glw_line(SCRS[s], i, &c, v);
            param_format(c.d, SYNC_AUTO, card, &u);
            ok &= !strcmp(v, LONG[k]) && !strcmp(c.val, CARD[k]) && !strcmp(card, CARD[k]);
            fit &= 236 - text_w(&FONT_S, v) >= 24 + text_w(&FONT_S, c.label) + 8;   /* (value right-aligned at 236, label from 24) */
            song.g[G_SYNC] = (int16_t)k;                    /* a fixed clock: INT / USB / TRS as it was */
            glw_line(SCRS[s], i, &c, v);
            param_format(c.d, k, card, &u);
            ok &= !strcmp(v, FIX[k]) && !strcmp(c.val, FIXC[k]) && !strcmp(card, FIXC[k]);
        }
    }
    check(ok, "SYNC AUTO: the GLOBAL list draws Auto: Int / Auto: USB / Auto: TRS, the cards A:INT ..., a fixed SYNC Int / USB / TRS (the cards INT / USB / TRS)");
    check(fit, "SYNC AUTO: the long form fits the list's value field beside the label");
    {   /* GLOBAL's MIDI page: OUT IN SYNC CLK OUT on one page, in this order (3 lines without CLK OUT); SYNC no longer on Tempo */
        static const char *const ML[4] = {"CLK IN", "CLK OUT", "MIDI IN", "MIDI OUT"};
        uint32_t first = line_named(SCR_GLOBAL, FELUCCA_MIDI_CLOCK ? "CLK IN" : FELUCCA_MIDI_CLKOUT ? "CLK OUT" : "MIDI IN"), n = 0, j, pg_ok = 1;
        n = (FELUCCA_MIDI_CLOCK ? 1u : 0u) + (FELUCCA_MIDI_CLKOUT ? 1u : 0u) + (FELUCCA_MIDI_INCLK ? 1u : 0u) + (FELUCCA_MIDI_OUT ? 1u : 0u);
        for (j = 0, k = 0; j < 4u; j++) {
            char nm[12];
            if (!(j == 0u ? FELUCCA_MIDI_CLOCK : j == 1u ? FELUCCA_MIDI_CLKOUT : j == 2u ? FELUCCA_MIDI_INCLK : FELUCCA_MIDI_OUT))
                continue;
            SCREENS[SCR_GLOBAL].cell(first + k, gl_kn(SCR_GLOBAL, first + k), &c);
            SCREENS[SCR_GLOBAL].name(first + k, nm);
            pg_ok &= c.label && !strcmp(c.label, ML[j]) && !strcmp(nm, "MIDI") && gl_first(0, first + k) == gl_first(0, first);
            k++;
        }
        pg_ok &= k == n && gl_end(0, first) - gl_first(0, first) == n && first == gl_first(0, first);
        check(pg_ok && !gl_has(0, TPR_TEMPO, 3), "GLOBAL: the MIDI page is CLK IN, CLK OUT, MIDI IN, MIDI OUT (one page, no CLK read-out); GLOBAL's Tempo has no SYNC");
        tp_cell(TPR_TEMPO, 3, &c);
        check(!FELUCCA_MIDI_CLOCK || (c.kind == CK_VAL && !strcmp(c.label, "SYNC")), "PLAY's TEMPO page still has SYNC (its 4th card)");
    }
    song.g[G_SYNC] = sync;
    song.g[G_MIDI] = midi;
}
#else
static void glw_sync_long_tests(void) {}
#endif
#if FELUCCA_MIDI_OUT || FELUCCA_MIDI_INCLK
/* MIDI OUT / IN in GLOBAL's list: the real options as words, Keys / Seq and Notes / Clock (there is no Both, Other or All:
 * bp_set.c has two options each, the settings word one bit each); the cards' text stays KEYS / SEQ, NOTES / CLOCK */
static void glw_midi_long_tests(void)
{
    static const char *const OUT[2] = {"Keys", "Seq"}, *const IN[2] = {"Notes", "Clock"};
    static const char *const OUTC[2] = {"KEYS", "SEQ"}, *const INC[2] = {"NOTES", "CLOCK"};
    uint32_t k, ok = 1, seen = 0;
    int16_t mo = bp_set[BPS_MOUT], mi = bp_set[BPS_MIN];
    cell_t c;
    char v[16];
    for (k = 0; k < 2u; k++) {
#if FELUCCA_MIDI_OUT
        bp_set[BPS_MOUT] = (int16_t)k;
        glw_line(SCR_GLOBAL, line_named(SCR_GLOBAL, "MIDI OUT"), &c, v);
        ok &= !strcmp(v, OUT[k]) && !strcmp(c.val, OUTC[k]);
        seen++;
#endif
#if FELUCCA_MIDI_INCLK
        bp_set[BPS_MIN] = (int16_t)k;
        glw_line(SCR_GLOBAL, line_named(SCR_GLOBAL, "MIDI IN"), &c, v);
        ok &= !strcmp(v, IN[k]) && !strcmp(c.val, INC[k]);
        seen++;
#endif
    }
    check(ok && seen, "MIDI OUT / IN: the GLOBAL list draws Keys / Seq and Notes / Clock, the cards KEYS / SEQ, NOTES / CLOCK");
    bp_set[BPS_MOUT] = mo;
    bp_set[BPS_MIN] = mi;
}
#else
static void glw_midi_long_tests(void) {}
#endif
/* LIVE: the clock followed changes while a page is open (a USB or TRS clock arrives or stops: seq.c's sync_select sets G_MIDI to
 * the source, here by its presence): with no input the text changes ("Auto: Int" -> "Auto: TRS" -> "Auto: USB"), and the screen is drawn again:
 * GLOBAL's MIDI page, PLAY's TEMPO page (its card and its panel) */
static uint32_t glw_screen_hash(void)
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 240u * 240u; i++)
        h = (h ^ screen[i]) * 16777619u;
    return h;
}
static void glw_follow(uint32_t src)                   /* a clock arrives on src (MSRC_USB / MSRC_TRS), 0: none: the real path, seq.c's sync_select */
{
    uint32_t k;
    for (k = 0; k < 3u; k++)
        sy_pr[k].cnt = 0;
    if (src) {
        sy_pr[src].cnt = SY_STEADY;
        sy_pr[src].last = SYNC_NOW();
    }
}
static void glw_live_tests(void)
{
    static const char *const LONG[3] = {"Auto: Int", "Auto: USB", "Auto: TRS"};
    static const char *const CARD[3] = {"A:INT", "A:USB", "A:TRS"};
    int16_t sync = song.g[G_SYNC], midi = song.g[G_MIDI];
    uint32_t i, ok = 1, hg[3], ht[3], seq[3] = {0, 2, 1};
    cell_t c;
    char v[16];
    song.g[G_SYNC] = SYNC_AUTO;
    reset_ui();
    op_enter(SCR_GLOBAL);
    i = line_named(SCR_GLOBAL, "CLK IN");
    ui.row[SCR_GLOBAL] = (uint8_t)i;
    for (uint32_t n = 0; n < 3u; n++) {
        glw_follow(seq[n]);
        frames(3);                                      /* (no input) */
        glw_line(SCR_GLOBAL, i, &c, v);
        hg[n] = glw_screen_hash();
        ok &= !strcmp(v, LONG[seq[n]]);
    }
    ok &= hg[0] != hg[1] && hg[1] != hg[2] && hg[0] != hg[2];
    check(ok, "SYNC AUTO live: GLOBAL's MIDI page, the followed clock changes (TRS arrives, USB takes over): the line and the screen change with no input");
    glw_follow(0);
    reset_ui();
    op_enter(SCR_TEMPO);
    for (uint32_t n = 0, ok2 = 1; n < 3u; n++) {
        glw_follow(seq[n]);
        frames(3);
        tp_cell(TPR_TEMPO, 3, &c);
        ht[n] = glw_screen_hash();
        ok2 &= !strcmp(c.val, CARD[seq[n]]);
        if (n == 2u)
            check(ok2 && ht[0] != ht[1] && ht[1] != ht[2] && ht[0] != ht[2],
                  "SYNC AUTO live: PLAY's TEMPO page, the card (A:INT / A:TRS / A:USB) and the panel change with no input");
    }
    song.g[G_SYNC] = sync;
    song.g[G_MIDI] = midi;
    glw_follow(0);
    reset_ui();
}
static void glwalk_tests(void)
{
    static int16_t g0[sizeof song.g / sizeof song.g[0]], b0[BPS_COUNT];
    uint32_t s, i, n, ok, acts = 0, pal = settings.palette, low = settings.lowcut;
    __typeof__(TSEL) t0 = TSEL;
    int16_t len = t0->p[P_SLEN];
    cell_t c;
    char v[16];
    memcpy(g0, song.g, sizeof g0);
    memcpy(b0, bp_set, sizeof b0);
    reset_ui();
    song.sel = 0;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        op_enter(scr);
        ui.row[scr] = 0;
        frame();
        n = SCREENS[scr].rows();
        for (i = 0, ok = n > 0u; i < n; i++) {
            glw_line(scr, i, &c, v);
            acts += c.kind == CK_ACT;
            ok &= (uint32_t)glw_walk_line(scr, i);
        }
        check(ok, s ? "SYSTEM, every line: a value's knob changes it and its text (PRESETS nothing), a read-out's nothing, an action RUNs from YES"
                    : "GLOBAL, every line: a value's knob changes it and its text (PRESETS nothing), a read-out's nothing, an action RUNs from YES");
    }
    for (s = 0, ok = 1; s < 2u; s++)                    /* no line draws YES */
        for (i = 0; i < SCREENS[s ? SCR_SYSTEM : SCR_GLOBAL].rows(); i++) {
            glw_line(s ? SCR_SYSTEM : SCR_GLOBAL, i, &c, v);
            ok &= strcmp(v, "YES") != 0;
        }
    i = line_named(SCR_SYSTEM, "PANEL");
    glw_line(SCR_SYSTEM, i, &c, v);
    check(ok && acts == 1u && c.kind == CK_ACT && !strcmp(v, "CALIBRATE"),
          "no GLOBAL / SYSTEM line draws YES; the one action (Calibrate's PANEL) draws CALIBRATE");
    glw_factory_tests();
    glw_sync_long_tests();
    glw_midi_long_tests();
    glw_live_tests();
    i = line_named(SCR_SYSTEM, "VERSION");
    glw_line(SCR_SYSTEM, i, &c, v);
    check(c.kind == CK_RO && v[0] && !strstr(v, "OPTIMIST") && strstr(FELUCCA_VERSION, v),
          "About: VERSION a read-out, the version itself (\"OPTIMIST 0.1\": 0.1)");
    memcpy(song.g, g0, sizeof g0);                      /* (an accelerated knob turned back may land near: as before) */
    memcpy(bp_set, b0, sizeof b0);
    settings.palette = pal;
    palette_set(pal);
    settings.lowcut = low;
    t0->p[P_SLEN] = len;
    reset_ui();
}
