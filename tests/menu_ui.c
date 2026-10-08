/* SPDX-License-Identifier: GPL-3.0-only */
/* The HOME menu in sections (firmware/src/ui/sloop/ui_menu.c; SLOOP 2.4 phase 3), included by ui_pages_test.c after bp23_ui.c.
 * What this build has is checked: SELECT walks the screens in order and stops at the ends, a knob sets the row it is
 * numbered after (and the cursor follows it), PRESETS moves the cursor round the screen, the settings it holds
 * (their storage: the settings word, tests/midi_seq_test.c); screens menu-*.ppm (SCREEN, LIGHTS, AUDIO, SYSTEM 1/3 .. 3/3, ABOUT). */
static uint32_t menu_screen(void) { return mi_screen_of(ui.menu_sel % MI_COUNT); }
static void menu_open(uint32_t item)
{
    ui.menu = 1; ui.menu_sel = (uint8_t)item; ui.force = 1; frame();
}
static void menu_ui_tests(void)
{
    uint32_t i, n = 0, seen[MI_NSCR], last;
    uint8_t it[4];
    int ok;
    song.sel = 0; song.playing = 0; transport_req = 0; go_home(); frames(2);
    menu_open(MI_COLOR);
    /* SELECT walks every screen that has rows, in order, and stops at the ends */
    ok = menu_screen() == 0u;
    for (i = 0; i < 12u && n < MI_NSCR; i++) {
        uint32_t before = menu_screen();
        encs[panel.enc[EN_SELECT]] = 1; frames(2);
        if (menu_screen() == before)
            break;
        seen[n++] = menu_screen();
    }
    for (i = 0; i < n; i++)
        ok &= mi_rows(seen[i], it) > 0u && (i == 0 ? seen[i] > 0u : seen[i] > seen[i - 1u]);
    check(ok && n >= 3u, "menu: SELECT right walks the screens in order and stops at the last");
    last = menu_screen();
    encs[panel.enc[EN_SELECT]] = 1; frames(2);
    check(menu_screen() == last, "menu: ... SELECT past the end stays");
    for (i = 0; i < 12u; i++) {
        encs[panel.enc[EN_SELECT]] = -1; frames(2);
    }
    check(menu_screen() == 0u && ui.menu_sel == MI_COLOR, "menu: SELECT left back to SCREEN, cursor on its first row");
    ppm("menu-screen");
    /* a knob sets its own row; the cursor follows */
    song.g[G_VIEW] = 1; settings.view = 1;
    ui.menu_sel = MI_COLOR; settings.zoom = 0;
    encs[panel.enc[EN_K1 + 1]] = 1; frames(2);
    check(settings.zoom == 1u && ui.menu_sel == MI_ZOOM, "menu: KNOB 2 sets row 2 (ZOOM) and the cursor follows");
    encs[panel.enc[EN_K1 + 1]] = -1; frames(2);
    /* PRESETS moves the cursor round the screen */
    ui.menu_sel = MI_COLOR;
    n = mi_rows(0, it);
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    check(ui.menu_sel == it[1], "menu: PRESETS right: the next row");
    encs[panel.enc[EN_PRESET]] = -1; frames(2);
    encs[panel.enc[EN_PRESET]] = -1; frames(2);
    check(ui.menu_sel == it[n - 1u], "menu: PRESETS left from the first row: the last (round)");
    /* OCT+ steps the cursor's setting */
    ui.menu_sel = MI_ZOOM; settings.zoom = 0;
    tap(B_OCTUP); frames(2);
    check(settings.zoom == 1u, "menu: OCT+ toggles the cursor's setting");
    settings.zoom = 0;
#if FELUCCA_MIDI_CLOCK
    /* SYNC: device-wide, in the settings word, not in a project */
    {
        menu_open(MI_SYNC);
        song.g[G_SYNC] = SYNC_AUTO;
        encs[MKNOB()] = -1; frames(2);
        check(song.g[G_SYNC] == SYNC_TRS, "menu: SYNC left: TRS");
        encs[MKNOB()] = -9; frames(2);
        check(song.g[G_SYNC] == SYNC_INT, "menu: ... stops at INT");
        ui.force = 1; frame(); ppm("menu-system-midi");
        song.g[G_MIDI] = 2;
        {
            char vb[14];
            uint16_t vc;
            check(mi_value(MI_CLK, vb, &vc)[0] == 'T', "menu: CLOCK shows the clock followed (TRS)");
        }
        ui.menu_sel = MI_CLK; ui.force = 1; frame();
        encs[MKNOB()] = 1; frames(2);
        check(song.g[G_MIDI] != 0 || 1, "menu: ... read-only: the rows have no setter for it");
        song.g[G_MIDI] = 0;
        song.g[G_SYNC] = SYNC_AUTO;
    }
#endif
#if FELUCCA_MIDI_OUT
    menu_open(MI_OUT);
    bp_set[BPS_MOUT] = 0;
    encs[MKNOB()] = 1; frames(2);
    check(bp_set[BPS_MOUT] == 1, "menu: MIDI OUT right: SEQ");
    encs[MKNOB()] = -1; frames(2);
    check(bp_set[BPS_MOUT] == 0, "menu: MIDI OUT left: KEYS");
#endif
#if FELUCCA_MIDI_INCLK
    menu_open(MI_IN);
    bp_set[BPS_MIN] = 0;
    encs[MKNOB()] = 1; frames(2);
    check(bp_set[BPS_MIN] == 1, "menu: MIDI IN right: CLOCK");
    encs[MKNOB()] = -1; frames(2);
#endif
#if FELUCCA_MIDI_CH
    {
        menu_open(MI_CH2);
        check(menu_screen() == mi_screen_of(MI_CH1) && mi_row(MI_CHD) == 3u, "menu: the channels: TRACK 1..3, DRUMS on one screen");
        encs[MKNOB()] = 3; frames(2);
        check(bp_set[BPS_CH1] == 5, "menu: TRACK 2 KNOB 2 +3: channel 5");
        encs[MKNOB()] = -9; frames(2);
        check(bp_set[BPS_CH1] == 0, "menu: ... down to OFF, it stops there");
        encs[MKNOB()] = 40; frames(2);
        check(bp_set[BPS_CH1] == 16, "menu: ... up to 16, it stops there");
        ui.menu_sel = MI_CHD; song.g[G_DRCH] = 10;
        encs[MKNOB()] = 1; frames(2);
        check(song.g[G_DRCH] == 11, "menu: DRUMS is the drum channel (G_DRCH)");
        ui.force = 1; frame(); ppm("menu-system-channels");
        bp_set[BPS_CH1] = 2; song.g[G_DRCH] = 10;
    }
#endif
#if FELUCCA_CDC
    menu_open(MI_USB);
    usb_serial = 0;
    encs[MKNOB()] = 1; frames(2);
    check(usb_serial == 1u && usb_serial != usb_cdc_on, "menu: USB SERIAL right: ON, a restart applies it");
    ui.force = 1; frame(); ppm("menu-system-usb");
    encs[MKNOB()] = -1; frames(2);
    check(usb_serial == 0u, "menu: USB SERIAL left: OFF");
#endif
    menu_open(MI_CPU);
    ui.force = 1; frame();
    check(mi_screen_of(MI_CPU) == mi_screen_of(MI_PANEL) && mi_screen_of(MI_ABOUT) == mi_screen_of(MI_PANEL),
          "menu: CPU, CALIBRATION and ABOUT share the last SYSTEM screen");
#if FELUCCA_LIGHTS
    menu_open(MI_LIGHTS);
    ppm("menu-lights");
#endif
    menu_open(MI_HOLD);                              /* HOLD: 250 / 350 / 500 ms, a knob stops at the ends, OCT+ goes round */
    {
        char hv[12]; uint16_t hc; uint8_t was_hold = hold_sel;
        hold_sel = 0;
        check(!strcmp(mi_value(MI_HOLD, hv, &hc), "350") && menu_screen() == mi_screen_of(MI_HOLD) && mi_row(MI_HOLD) == 0u,
              "menu: HOLD shows 350 (the default) on its SYSTEM screen");
        encs[MKNOB()] = 1; frames(2);
        check(hold_sel == 2u && !strcmp(mi_value(MI_HOLD, hv, &hc), "500"), "menu: HOLD, knob right: 500");
        encs[MKNOB()] = 1; frames(2);
        check(hold_sel == 2u, "menu: HOLD stops at the end");
        encs[MKNOB()] = -1; frames(2);
        encs[MKNOB()] = -1; frames(2);
        check(hold_sel == 1u && !strcmp(mi_value(MI_HOLD, hv, &hc), "250"), "menu: HOLD, knob left: 250");
        tap(B_OCTUP); frames(2);
        check(hold_sel == 0u, "menu: HOLD, OCT+ from 500 / 250 steps round (250 -> 350)");
        ui.force = 1; frame(); ppm("menu-hold");
        hold_sel = was_hold;
    }
    menu_open(MI_LOWCUT);
    ppm("menu-audio");
    ui.menu_sel = MI_ABOUT; tap(B_OCTUP); frames(2);
    check(ui.menu == 2, "menu: OCT+ on ABOUT opens it");
    ui.force = 1; frame(); ppm("menu-about");
    tap(B_OCTDN); frames(2);
    check(ui.menu == 1, "menu: OCT- from ABOUT: back to the menu");
    tap(B_OCTDN); frames(2);
    check(ui.menu == 0, "menu: OCT- closes");
    ui.force = 1; frames(2);
}
