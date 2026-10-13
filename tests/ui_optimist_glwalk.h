/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, every line of GLOBAL and SYSTEM walked (fix/settings-lines, 2026-10-13; the user: "many lines
 * in GLOBAL and SYSTEM display YES and nothing can be done with them"). ALGORITHM puts the cursor on each line in turn:
 *   a value     its knob changes a setting and the text drawn (one way, else the other at an end), and back; PRESETS
 *               the same; a value with a descriptor: HOME held + PRESETS its default
 *   a read-out  its knob and PRESETS change no setting
 *   an action   drawn as a word that is not YES (op_gldraw.c gl_val); its knob changes nothing; PRESETS runs it as
 *               YES does (PANEL: the question, cancelled by HOME)
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
    SCREENS[scr].cell(i, i % 4u, c);
    str_cpy(v, gl_val(c), 16);
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
    turn(EN_ALGO, (int32_t)i - (int32_t)ui.row[scr]);   /* (ALGORITHM: the cursor line by line) */
    glw_line(scr, i, &c, v);
    ok = ui.scr == scr && ui.row[scr] == i && c.label && c.label[0] && v[0];
    if (c.kind == CK_VAL) {
        ok &= glw_moves(scr, i, LINE_KNOB(i));
        ok &= glw_moves(scr, i, EN_PRESET);
        if (c.d && c.vp) {                              /* a descriptor's value: HOME held + PRESETS, its default */
            int16_t was = *c.vp;
            press(B_HOME);
            turn(EN_PRESET, 1);
            release(B_HOME);
            ok &= *c.vp == c.d->def && ui.scr == scr;
            *c.vp = was;
        }
    } else if (c.kind == CK_RO) {
        ok &= glw_still(LINE_KNOB(i)) && glw_still(EN_PRESET);
    } else if (c.kind == CK_ACT) {
        ok &= strcmp(v, "YES") != 0 && glw_still(LINE_KNOB(i));
        ui.msg[0] = 0;
        turn(EN_PRESET, 1);
        ok &= op_armed() || ui.msg[0];                  /* (PRESETS: as YES) */
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
    {"KNOB COLORS", "OFF"}, {"LIGHTS", "OFF"}, {"KEYS", "OFF"}, {"LOWCUT", "OFF"}, {"SERIAL", "OFF"}, {"HOLD", "350"},
    {"2ND CORE", "AUTO"}};
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
            turn(EN_ALGO, (int32_t)i - (int32_t)ui.row[scr]);
            glw_line(scr, i, &c, v);
            if (c.kind != CK_VAL || c.d)
                continue;                               /* (a descriptor's line: its default, glw_walk_line) */
            if (!strcmp(c.label, "BLE")) {             /* never reset: a live connection stays */
#if FELUCCA_BLE
                uint8_t was = ble_on;
                ble_on = 1;
                press(B_HOME);
                turn(EN_PRESET, 1);
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
            turn(EN_PRESET, 1);
            release(B_HOME);
            glw_line(scr, i, &c, v);
            if (strcmp(v, f)) {
                printf("  glwalk: %s after HOME + PRESETS %s, factory %s\n", c.label, v, f);
                ok = 0;
            }
            seen++;
        }
    }
    check(ok && seen >= 3u && ble_ok, "HOME + PRESETS: every setting line without a default back to its FACTORY value; BLE never");
    settings.palette = 4;
    palette_set(4);
}
static void glwalk_tests(void)
{
    static int16_t g0[sizeof song.g / sizeof song.g[0]], b0[BPS_COUNT];
    uint32_t s, i, n, ok, acts = 0, pal = settings.palette, low = settings.lowcut;
    int16_t len = TSEL->p[P_SLEN];
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
        check(ok, s ? "SYSTEM, every line: a value's knob and PRESETS change it and its text, a read-out's nothing, an action RUNs"
                    : "GLOBAL, every line: a value's knob and PRESETS change it and its text, a read-out's nothing, an action RUNs");
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
    i = line_named(SCR_SYSTEM, "VERSION");
    glw_line(SCR_SYSTEM, i, &c, v);
    check(c.kind == CK_RO && v[0] && !strstr(v, "OPTIMIST") && strstr(FELUCCA_VERSION, v),
          "About: VERSION a read-out, the version itself (\"OPTIMIST 0.1\": 0.1)");
    memcpy(song.g, g0, sizeof g0);                      /* (an accelerated knob turned back may land near: as before) */
    memcpy(bp_set, b0, sizeof b0);
    settings.palette = pal;
    palette_set(pal);
    settings.lowcut = low;
    TSEL->p[P_SLEN] = len;
    reset_ui();
}
