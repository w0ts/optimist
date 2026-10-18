/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c with FELUCCA_BLE=1: SYSTEM > BLUETOOTH (the radio's ON / OFF, as SLOOP's HOME menu has it), and
 * the settings word's bits 21..22 HOLD, 23 reserved (CARDS until 2026-10-12), 24 BLUETOOTH (SETTINGS_BLE_ON_BIT), 25 KNOB COLORS, which must not collide
 * (storage/settings_word.c), and the BLE devices list (ble_dev_tests: YES on the BLE row opens it; op_project.c).
 * Included after the helpers. The radio itself is the stub in ui_optimist_test.c (midi_ble.c is host-tested apart). */

#if FELUCCA_BLE
static void dev_yes_t(void)                             /* YES on a device row: the card asks first (2026-10-13), YES again does it */
{
    tap(B_SAVE);
    if (op_armed())
        tap(B_SAVE);
}
static uint32_t ble_line(const char *lab)                /* a line of SYSTEM's Bluetooth / Devices pages, the cursor on it; its knob */
{
    uint32_t w;
    reset_ui();
    w = line_named(SCR_SYSTEM, lab);
    ui.force = 1;
    frame();
    return LINE_KNOB(w);
}
static uint32_t ble_sys_row(void) { return ble_line("BLUETOOTH"); }   /* the radio's ON / OFF */
static uint32_t ble_list_row(void) { return ble_line("DEVICES"); }    /* YES: the devices list */
static void ble_word_tests(void)
{
    uint32_t w0, w, all;
    hold_sel = 0;
    ble_on = 0;
    knob_colors = 0;
    bp23_from_word(0);
    check(SETTINGS_BLE_ON_BIT == 24u, "settings word: BLUETOOTH is bit 24 (SETTINGS_BLE_ON_BIT)");
    w0 = bp23_word();
    hold_sel = 2;
    w = bp23_word();
    check((w ^ w0) == (2u << 21), "settings word: HOLD moves bits 21..22 only");
    hold_sel = 0;
    ble_on = 1;
    w = bp23_word();
    check((w ^ w0) == (1u << SETTINGS_BLE_ON_BIT), "settings word: BLUETOOTH moves bit 24 only");
    ble_on = 0;
    knob_colors = 1;
    w = bp23_word();
    check((w ^ w0) == (1u << 25), "settings word: KNOB COLORS moves bit 25 only");
    knob_colors = 0;
    hold_sel = 2;
    ble_on = 1;
    knob_colors = 1;
    all = bp23_word();
    check(((all >> 23) & 1u) == 0u, "settings word: bit 23 (the CARDS of before 2026-10-12) is not written");
    hold_sel = 0;
    ble_on = 0;
    knob_colors = 0;
    bp23_from_word(all);
    check(hold_sel == 2u && ble_on == 1u && knob_colors == 1u,
          "settings word: HOLD, BLUETOOTH and KNOB COLORS all set read back each as itself (21..22, 24, 25)");
    bp23_from_word(all & ~(1u << 25));
    check(hold_sel == 2u && ble_on == 1u && knob_colors == 0u, "... clearing bit 25 leaves the rest");
    bp23_from_word(all & ~(1u << SETTINGS_BLE_ON_BIT));
    check(hold_sel == 2u && ble_on == 0u && knob_colors == 1u, "... clearing bit 24 leaves the rest");
    bp23_from_word(all | (1u << 23));
    check(hold_sel == 2u && ble_on == 1u && knob_colors == 1u && ((bp23_word() >> 23) & 1u) == 1u,
          "... a word with bit 23 set (an old 1x4) leaves the rest, and the bit is kept as read");
    bp23_from_word(all & ~(3u << 21));
    check(hold_sel == 0u && ble_on == 1u && knob_colors == 1u, "... clearing bits 21..22 leaves the rest");
    hold_sel = 0;
    ble_on = 0;
    knob_colors = 0;
    bp23_from_word(0);
}
/* ---- the BLE devices list (SYSTEM > BLUETOOTH, YES on the BLE row): NONE and LAST pinned at the top, the nearby
 * BLE-MIDI devices with signal bars, no wrap; SAVE picks / connects, HOME goes back, SELECT moves, SHIFT + a button asks
 * FORGET LAST (the modal); the states of the SLOOP screen (Connecting, Pairing with the code, Connected, Failed). The
 * rows, the tags and the status line are io/midi/ble_devices.c's (shared; tests/menu_ui.c tests them in SLOOP's menu). */
static void dev_screen(const char *name) { ui.force = 1; frames(3); ppm(name); }
#if BLE_CENTRAL
static const char *ble_dev_line_of(void)                 /* the status line (ble_devices.c ble_dev_line), as the list draws it */
{
    static char t[40];
    ble_dev_line(t, sizeof t, 0);
    return t;
}
static uint32_t dev_row_n(void)
{
    int last;
    uint8_t near[BLE_SCAN_N];
    uint32_t nn;
    return ble_dev_rows(&last, near, &nn);
}
static const char *dev_row_tag(uint32_t r)
{
    int last, chosen;
    uint8_t near[BLE_SCAN_N];
    uint32_t nn, bars;
    const char *tag;
    char nm[BLE_NAME_MAX + 1u];
    ble_dev_rows(&last, near, &nn);
    ble_dev_row(r, last, near, nm, &tag, &chosen, &bars);
    return tag;
}
static void dev_select(int32_t s) { turn(EN_SELECT, s); frames(2); }
static void ble_dev_tests(void)
{
    uint32_t c0, sys_row;
    struct ble_dev_store back;
    ble_on = 1;
    ble_link = 0;
    ble_store_reset(&ble_store);
    ble_connect_none();
    ble_list_row();
    sys_row = ui.row[SCR_SYSTEM];
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && ble_devs_open && blefk.want == 1u && ble_scanning(), "devices: YES on the BLE row opens the list, the scan starts");
    check(dev_row_n() == 1u && ui.row[SCR_SYSTEM] == 0u && !strcmp(ble_dev_line_of(), "SCANNING 0 FOUND"),
          "devices: nothing remembered, nothing heard: NONE alone, the cursor on it, SCANNING 0 FOUND");
    dev_screen("opt-ble-devices-empty");
    ble_fake_rep(0, 0x01, 1, 0, 0x0A12);             /* a BLE-MIDI controller: the UUID in ADV_IND, its name in SCAN_RSP */
    ble_fake_rep(4, 0x01, 0, "KeyStep 37", 0x0A12);
    ble_fake_rep(0, 0x02, 0, "Tile", 0x0405);        /* a beacon (no BLE-MIDI UUID): never listed */
    ble_fake_rep(0, 0x03, 1, 0, 0x1020);             /* another FM-1 */
    ble_fake_rep(4, 0x03, 0, "FM-1 A1B2", 0x1020);
    ble_devices_poll();
    frames(2);
    check(dev_row_n() == 3u && SCREENS[SCR_SYSTEM].rows() == 3u, "devices: NONE and the two BLE-MIDI devices (the beacon left out) are SYSTEM's rows now");
    dev_screen("opt-ble-devices");
    dev_select(-1);
    check(ui.row[SCR_SYSTEM] == 0u, "devices: SELECT left on NONE stops there (no wrap)");
    dev_select(1);
    dev_select(1);
    dev_select(1);
    dev_select(1);
    check(ui.row[SCR_SYSTEM] == 2u, "devices: SELECT right stops on the last row (no wrap)");
    dev_select(-1);
    check(ui.row[SCR_SYSTEM] == 1u, "devices: SELECT moves the cursor a row a detent");
    {
        cell_t c;
        SCREENS[SCR_SYSTEM].cell(1, 0, &c);
        check(c.kind == CK_ACT && !strcmp(c.label, "CONNECT"), "devices: the card says what YES does on a nearby row");
        SCREENS[SCR_SYSTEM].cell(1, 1, &c);
        check(c.kind == CK_RO && !strcmp(c.label, "SIGNAL") && c.gv == 3, "devices: ... and its signal (3 of 3 bars for the stronger)");
        SCREENS[SCR_SYSTEM].cell(0, 0, &c);
        check(c.kind == CK_ACT && !strcmp(c.label, "CHOOSE"), "devices: NONE's card: CHOOSE");
    }
    c0 = (uint32_t)cenfk.connects;
    dev_yes_t();
    frames(2);
    check(cenfk.connects == (int)c0 + 1 && !strcmp(dev_status(), "CONNECTING KeyStep 37") && ble_dev_status() == 5u,
          "devices: SAVE on a nearby row connects to it: CONNECTING <name>");
    ble_dev_msg = 0;
    dev_screen("opt-ble-devices-connecting");
    check(!strcmp(bdp.st, "Connecting KeyStep 37") && bdp.tone == BDL_PLAIN,
          "devices: the status line cases its words, not the name: \"Connecting KeyStep 37\" (as advertised)");
    {
        char t[24];
        dev_row_text(t, sizeof t, 1, "KeyStep 37");
        check(!strcmp(t, "KeyStep 37"), "devices: a row shows the name as advertised (\"KeyStep 37\", not \"Keystep 37\")");
        dev_row_text(t, sizeof t, 0, "NONE (VISIBLE)");
        check(!strcmp(t, "None (visible)"), "devices: NONE's row is our words, in sentence case");
    }
    {
        cell_t c;
        SCREENS[SCR_SYSTEM].cell(1, 0, &c);
        check(c.kind == CK_ACT && !strcmp(c.label, "RETRY"), "devices: the row being connected to: the card says RETRY, not CONNECT");
        SCREENS[SCR_SYSTEM].cell(1, 1, &c);
        check(c.kind == CK_NONE, "devices: ... and has no signal gauge (the link is being made)");
    }
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1, cenfk.pairing = 1;
    ble_devices_poll();
    frames(2);
    check(!strcmp(dev_status(), "PAIRING KeyStep 37") && ble_dev_status() == 7u && !strcmp(dev_row_tag(1), "PAIRING"),
          "devices: the pairing: PAIRING <name>, the row tagged PAIRING");
    dev_screen("opt-ble-devices-pairing");
    check(!strcmp(bdp.st, "Pairing KeyStep 37"), "devices: \"Pairing KeyStep 37\" (the name as advertised)");
    cenfk.passkey = 42u;
    ble_devices_poll();
    check(!strcmp(dev_status(), "ENTER THIS CODE ON THE PHONE") && ble_connect_passkey() == 42u,
          "devices: pairing with a code: the prompt and the 6-digit code (000042)");
    dev_screen("opt-ble-devices-passkey");
    check(px_in(60, (uint32_t)OP_PY + OH_BODY / 2u - 6u, 130, 28, C_WHITE), "devices: the code is drawn large in the middle of the panel");
    {
        struct ble_keys k;
        memset(&k, 0, sizeof k);
        k.has = BLE_KEYS_LTK | BLE_KEYS_ID;
        memset(k.ltk, 0x11, 16), memset(k.rand, 0x22, 8), k.ediv = 0x3344;
        memset(k.irk, 0x5A, 16), memset(k.id, 0x66, 6), k.id_rand = 1;
        ble_app_central_keys(&k);
    }
    cenfk.pairing = 0, cenfk.passkey = BLE_NO_PASSKEY, cenfk.st = BLE_CS_READY;
    ble_devices_poll();
    ble_devices_poll();
    ble_store_load(&back, ble_dev_kept);
    check(ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_LAST && back.sel == BLE_SEL_LAST &&
          !strcmp(dev_status(), "CONNECTED KeyStep 37") && ble_dev_status() == 2u,
          "devices: ready: it is LAST and the choice, saved: CONNECTED <name>");
    ble_dev_msg = 0;
    frames(2);
    check(dev_row_n() >= 3u && !strcmp(dev_row_tag(1), "CONNECTED") && !strcmp(dev_row_tag(0), "") && !strcmp(dev_row_tag(2), ""),
          "devices: NONE, then LAST (tagged CONNECTED), then the nearby ones: LAST is pinned under NONE");
    dev_screen("opt-ble-devices-connected");
    check(!strcmp(bdp.st, "Connected KeyStep 37") && bdp.tone == BDL_GOOD &&
              px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_OK),
          "devices: \"Connected KeyStep 37\" (the name as advertised), good: green");
    {
        cell_t c;
        SCREENS[SCR_SYSTEM].cell(1, 0, &c);
        check(c.kind == CK_ACT && !strcmp(c.label, "RECONNECT"), "devices: LAST connected: the card says RECONNECT");
        SCREENS[SCR_SYSTEM].cell(1, 1, &c);
        check(c.kind == CK_NONE, "devices: ... and no \"Signal 0/3\" with an empty gauge (a link up is not scanned for)");
    }
    ui.row[SCR_SYSTEM] = 2;
    frames(2);
    c0 = (uint32_t)cenfk.connects;
    tap(B_SAVE);
    {
        char v[sizeof ui.arm_verb], a[sizeof ui.arm_arg], q[sizeof ui.arm_q], nm[BLE_NAME_MAX + 1u];
        const char *tg;
        int ch, ls;
        uint8_t nr[BLE_SCAN_N];
        uint32_t nn, br;
        (void)ble_dev_rows(&ls, nr, &nn);
        ble_dev_row(2, ls, nr, nm, &tg, &ch, &br);
        modal_words(v, a, q);
        check(op_armed() && !strcmp(ui.arm_verb, "CONNECT?") && cenfk.connects == (int)c0 && ui.arm_raw &&
                  !strcmp(a, nm) && !strcmp(q, "Connect?") == 0 && strstr(q, nm),
              "devices: CONNECT asks, naming the device as advertised, nothing connected yet");
        tap(B_HOME);
        check(!op_armed() && cenfk.connects == (int)c0 && dev_listing(), "devices: HOME says no to CONNECT: nothing connected");
        op_arm(SCR_SYSTEM, 2, 0, "CONNECT", "DEVICE", 0);
        modal_words(v, a, q);
        check(!ui.arm_raw && !strcmp(q, "Connect device?"), "devices: no name known: \"Connect device?\"");
        op_disarm();
    }
    dev_yes_t();
    frames(2);
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1, cenfk.pairing = 1;
    ble_devices_poll();
    cenfk.prompted = 1, cenfk.mitm = 1, cenfk.code = 0x08;
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    frames(2);
    check(cenfk.connects == (int)c0 + 1 && !strcmp(dev_status(), "FAILED: LINK LOST") && ble_dev_status() == 8u,
          "devices: a pick that fails: FAILED: <why>, kept until the user acts");
    cenfk.prompted = cenfk.mitm = 0;
    dev_screen("opt-ble-devices-failed");
    check(bdp.tone == BDL_BAD && px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_ERR) &&
              !px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_AMB),
          "devices: a failure is red (C_ERR), not the palette's grey");
    /* SHIFT + a button: FORGET LAST, the modal */
    press(B_OCTDN);
    press(B_EDIT);
    release(B_EDIT);
    release(B_OCTDN);
    frames(2);
    check(dev_listing() && ui.arm_scr == SCR_SYSTEM && !strcmp(ui.arm_verb, "FORGET?") && !strcmp(ui.arm_arg, "KeyStep 37") &&
          ble_store_has_last(&ble_store), "devices: SHIFT + a button asks FORGET LAST? (the modal names it), nothing forgotten yet");
    dev_screen("opt-ble-devices-forget");
    {
        char v[sizeof ui.arm_verb], a[sizeof ui.arm_arg], q[sizeof ui.arm_q];
        modal_words(v, a, q);
        check(ui.arm_raw && !strcmp(v, "Forget?") && !strcmp(a, "KeyStep 37") && !strcmp(q, "Forget KeyStep 37?"),
              "devices: the FORGET modal names the device as advertised (\"KeyStep 37\")");
    }
    {
        uint32_t t0 = ui.arm_ms;
        fm1_ms += 1000u;
        press(B_OCTDN);
        press(B_ENV);
        release(B_ENV);
        release(B_OCTDN);
        check(ui.arm_scr == SCR_SYSTEM && ui.arm_ms == t0, "devices: SHIFT + a button while asked does not ask again (the 3 s run on)");
    }
    tap(B_HOME);
    check(dev_listing() && ui.arm_scr == ARM_NONE && ble_store_has_last(&ble_store), "devices: HOME says no: still the list, LAST kept");
    press(B_OCTDN);
    press(B_ENV);
    release(B_ENV);
    release(B_OCTDN);
    tap(B_SAVE);
    frames(2);
    check(!ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_NONE && ui.row[SCR_SYSTEM] == 0u && dev_listing(),
          "devices: SAVE says yes: LAST forgotten, NONE picked, the cursor on NONE");
    dev_screen("opt-ble-devices-forgotten");
    press(B_OCTDN);
    press(B_EDIT);
    release(B_EDIT);
    release(B_OCTDN);
    check(ui.arm_scr == ARM_NONE, "devices: SHIFT + a button with no LAST asks nothing");
    ble_store_set_last(&ble_store, ble_found.e[0].addr, ble_found.e[0].addr_rand, "KeyStep 37", BLE_KIND_MIDI);
    ble_store_select(&ble_store, BLE_SEL_NONE);
    frames(2);
    dev_select(1);
    dev_yes_t();
    check(ble_store.sel == BLE_SEL_LAST, "devices: SAVE on LAST chooses it");
    dev_select(-1);
    dev_yes_t();
    check(ble_store.sel == BLE_SEL_NONE && ble_store_has_last(&ble_store), "devices: SAVE on NONE chooses it, LAST kept");
    ui.force = 1;
    frames(3);
    dev_screen("opt-ble-devices-2x2");
    check(px_in(0, (uint32_t)OP_PY, 236, 19, cur_bg(trk_col(song.sel))), "devices: the list under the 2x2 cards, the cursor bar drawn");
    frames(2);
    tap(B_HOME);
    check(!dev_listing() && ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == sys_row && !ble_devs_open && blefk.want == 0u,
          "devices: HOME goes back: the scan stops, SYSTEM on the BLE row again");
    /* leaving SYSTEM by another way closes the list too */
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && ble_devs_open, "devices: opened again");
    op_enter(SCR_HOME);
    check(!dev_listing() && !ble_devs_open && blefk.want == 0u, "devices: SYSTEM left some other way: the list closes, the scan stops");
    /* ... also from the TEMPO page opened over the list (PLAY held): ui.scr is SCR_TEMPO there, not SYSTEM */
    ble_list_row();
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && ble_devs_open, "devices: opened once more");
    tempo_open();
    check(ui.scr == SCR_TEMPO && bdv.open && !dev_listing(), "devices: the TEMPO page over the list: the list is not shown");
    op_enter(SCR_HOME);
    ble_devices_poll();
    frames(2);
    check(!bdv.open && !ble_devs_open && blefk.want == 0u && ui.scr == SCR_HOME && !tp.on,
          "devices: leaving by way of the TEMPO page closes the list and stops the scan (no list back on SYSTEM)");
    ble_connect_none();
    ble_store_reset(&ble_store);
    ble_on = 0;
    cenfk.code = 0;
    reset_ui();
}
/* a device heard: its ADV_IND (the BLE-MIDI UUID) and its SCAN_RSP (the name); a0 tells devices apart */
static void dev_hear(uint8_t a0, const char *name)
{
    ble_fake_rep(0, a0, 1, 0, 0x0A12);
    ble_fake_rep(4, a0, 0, name, 0x0A12);
}
static void dev_open_fresh(void)                          /* a new list, nothing remembered, nothing connected */
{
    ble_on = 1;
    ble_link = 0;
    ble_store_reset(&ble_store);
    ble_connect_none();
    ble_dev_msg = 0;
    ble_list_row();
    tap(B_SAVE);
    frames(2);
}
static void dev_close_fresh(void)
{
    tap(B_HOME);
    ble_connect_none();
    ble_store_reset(&ble_store);
    ble_on = 0;
    ble_link = 0;
    ble_dev_msg = 0;
    cenfk.code = 0;
    reset_ui();
}
/* the review's remaining items: tones, the status line cut, the window, a pick by address, names at their full length */
static void ble_dev_more_tests(void)
{
    uint32_t c0, i, first;
    char t[48];
    cell_t c;
    /* BUSY: a host is connected to the FM-1: a notice (amber), not the green of a good answer */
    dev_open_fresh();
    dev_hear(0x01, "KeyStep 37");
    ble_devices_poll();
    frames(2);
    ble_link = 1;                                         /* (a Mac connected to us: the FM-1 is a peripheral) */
    cenfk.central = 0;
    dev_select(1);
    c0 = (uint32_t)cenfk.connects;
    dev_yes_t();
    frames(2);
    check(cenfk.connects == (int)c0 && bdp.tone == BDL_WARN && !strcmp(bdp.st, "Busy: A host is connected") &&
              px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_WARN) &&
              !px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_OK),
          "devices: BUSY: A HOST IS CONNECTED is a notice (C_WARN), not green");
    dev_close_fresh();
    /* NONE with BLUETOOTH off: not "stay visible" */
    dev_open_fresh();
    ble_on = 0;
    dev_yes_t();
    frames(2);
    check(!strcmp(bdp.st, "None chosen, bluetooth off") && bdp.tone == BDL_WARN && ble_store.sel == BLE_SEL_NONE,
          "devices: NONE while BLUETOOTH is off: says so (no \"stay visible\")");
    dev_close_fresh();
    /* the status line is cut to its room: the words stay, a long name loses its end */
    dev_line_text(t, sizeof t, "CONNECTING (TRY 2/6) Roland Aerophone", 21u);
    check(!strncmp(t, "Connecting (try 2/6) ", 21) && text_w(&FONT_S, t) <= DEV_LINE_W && t[21] == 'R' && str_len(t) < 37u,
          "devices: \"Connecting (try 2/6) \" + a 16-character name is cut to the 232 px (240 px used to overflow)");
    dev_line_text(t, sizeof t, "CONNECTING KeyStep 37", 11u);
    check(!strcmp(t, "Connecting KeyStep 37"), "devices: ... a line that fits is left whole, the name as advertised");
    dev_line_text(t, sizeof t, "ENTER THIS CODE ON THE PHONE", BLE_LINE_NO_NAME);
    check(!strcmp(t, "Enter this code on the phone"), "devices: a line with no name is cased whole");
    /* a name of 16 characters whole in the FORGET modal (arm_arg held 13) */
    dev_open_fresh();
    ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "Roland Aerophone", BLE_KIND_MIDI);
    frames(2);
    press(B_OCTDN);
    press(B_EDIT);
    release(B_EDIT);
    release(B_OCTDN);
    {
        char v[sizeof ui.arm_verb], a[sizeof ui.arm_arg], q[sizeof ui.arm_q];
        modal_words(v, a, q);
        check(ui.arm_scr == SCR_SYSTEM && !strcmp(a, "Roland Aerophone") && !strcmp(q, "Forget Roland Aerophone?"),
              "devices: FORGET shows a 16-character name whole, as advertised");
    }
    dev_close_fresh();
    /* the window keeps its place while the cursor moves inside it; a pick follows the device, not the row */
    dev_open_fresh();
    for (i = 0; i < 8u; i++) {
        char nm[8] = "Dev  ";
        nm[3] = (char)('A' + i);
        dev_hear((uint8_t)(0x10 + i), nm);
        ble_devices_poll();                               /* (the stand-in's queue holds 8 reports) */
    }
    frames(2);
    check(dev_row_n() == 9u && bdp.shown < bdp.n, "devices: nine rows, more than the panel shows");
    for (i = 0; i < 9u; i++)
        dev_select(1);
    frames(2);
    first = bdp.first;
    check(bdp.cur == 8u && first == bdp.n - bdp.shown, "devices: the cursor at the end: the window shows the last rows");
    dev_select(-1);
    frames(2);
    check(bdp.cur == 7u && bdp.first == first && bdv.first == first,
          "devices: one row up from the end: the window stays (the cursor is not pinned to the bottom)");
    dev_select(1);
    dev_select(-9);
    frames(2);
    check(bdp.cur == 0u && bdp.first == 0u, "devices: back to NONE: the window follows to the top");
    dev_close_fresh();
    dev_open_fresh();
    dev_hear(0x01, "Alpha");
    dev_hear(0x03, "Bravo");
    dev_hear(0x05, "Charlie");
    ble_devices_poll();
    frames(2);
    dev_select(2);
    frames(2);
    check(bdp.cur == 2u && ble_found.e[bdp.near[1]].addr[0] == 0x03, "devices: the cursor on Bravo (row 2)");
    c0 = (uint32_t)cenfk.connects;
    press(B_SAVE);                                        /* (SAVE acts on its release: Alpha ages out in between) */
    ble_found.e[bdp.near[0]].used = 0;
    ble_found.gen++;
    release(B_SAVE);
    check(op_armed() && ui.arm_raw && !strcmp(ui.arm_arg, "Bravo") && !strcmp(ui.arm_verb, "CONNECT?"),
          "devices: the question names the highlighted device (\"Bravo\"), the row it was when asked");
    if (op_armed())
        tap(B_SAVE);                                      /* (the card asks first, 2026-10-13) */
    frames(2);
    check(cenfk.connects == (int)c0 + 1 && cenfk.p.addr[0] == 0x03,
          "devices: a device aging out above the cursor: SAVE connects to the highlighted one (Bravo), not the one now in its row");
    dev_close_fresh();
    /* a pick on a device that is gone picks nothing */
    dev_open_fresh();
    dev_hear(0x01, "Alpha");
    dev_hear(0x03, "Bravo");
    ble_devices_poll();
    frames(2);
    dev_select(2);
    frames(2);
    c0 = (uint32_t)cenfk.connects;
    press(B_SAVE);
    ble_found.e[bdp.near[1]].used = 0;
    ble_found.gen++;
    release(B_SAVE);
    if (op_armed())
        tap(B_SAVE);
    frames(2);
    check(cenfk.connects == (int)c0 && !strcmp(bdp.st, "Device gone") && bdp.tone == BDL_WARN,
          "devices: the highlighted device gone before SAVE: nothing is connected, \"Device gone\"");
    (void)c;
    dev_close_fresh();
}
#else
static void ble_dev_tests(void)
{
    ble_on = 1;
    ble_store_reset(&ble_store);
    ble_list_row();
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && SCREENS[SCR_SYSTEM].rows() == 1u, "devices (no scanner built in): the list is NONE alone");
    dev_screen("opt-ble-devices-noscan");
    tap(B_HOME);
    ble_on = 0;
    reset_ui();
}
#endif
/* SYSTEM as the user walks it: GLO tapped twice from HOME, SELECT page by page; each page drawn (opt-ble-sys-<n>) */
static void ble_sys_walk(const char *tag)
{
    uint32_t p, np;
    char nm[64];
    reset_ui();
    go_home();
    frames(2);
    tap(B_GLO);
    tap(B_GLO);
    frames(2);
    check(ui.scr == SCR_SYSTEM, "SYSTEM walk: GLO tapped twice from HOME is SYSTEM");
    turn(EN_SELECT, -20);
    np = gl_npg(1);
    for (p = 0; p < np; p++) {
        snprintf(nm, sizeof nm, "opt-ble-sys-%s-%u", tag, p + 1u);
        ui.force = 1;
        frames(3);
        while (tf.t)                                    /* (the track's name flashed in the header: gone) */
            frame();
        ppm(nm);
        turn(EN_SELECT, 1);
    }
    reset_ui();
}
/* ---- SYSTEM > Bluetooth (BLUETOOTH, STATE, DEVICE, MIDI) and Devices (DEVICES, RECONNECT, LAST DEVICE FORGET): the lines */
static uint32_t blp_line(const char *lab, cell_t *c)    /* the SYSTEM line labelled lab: its index, its cell */
{
    uint32_t i;
    op_enter(SCR_SYSTEM);
    for (i = 0; i < gl_n(1); i++) {
        gl_cell(1, i, c);
        if (c->label && !strcmp(c->label, lab))
            return i;
    }
    return GL_MAXL;
}
static const char *blp_val(const char *lab)
{
    static cell_t c;
    return blp_line(lab, &c) < GL_MAXL ? c.val : "?";
}
static uint16_t blp_col(const char *lab)
{
    cell_t c;
    return blp_line(lab, &c) < GL_MAXL ? c.col : 0xFFFFu;
}
static void blp_shot(const char *lab, const char *name)  /* the page of line lab drawn */
{
    cell_t c;
    line_at(SCR_SYSTEM, blp_line(lab, &c));
    ui.force = 1;
    frames(3);
    while (tf.t)
        frame();
    ppm(name);
}
static void ble_page_tests(void)
{
    cell_t c;
    uint32_t b, i, ok = 1;
    char t[24];
    static const char *const BP[4] = {"BLUETOOTH", "STATE", "DEVICE", "MIDI"};
    reset_ui();
    ble_on = 0, ble_link = 0, ble_up = 1;
    ble_store_reset(&ble_store);
#if BLE_CENTRAL
    ble_connect_none();
#endif
    b = blp_line("BLUETOOTH", &c);
    for (i = 0; i < 4u; i++) {
        gl_cell(1, b + i, &c);
        ok &= c.label && !strcmp(c.label, BP[i]) && gl_pg(1, b + i) == gl_pg(1, b);
    }
    gl_ptitle(1, b, t, sizeof t);
    check(ok && !strcmp(t, "Bluetooth"), "Bluetooth page: BLUETOOTH, STATE, DEVICE, MIDI on one page, titled Bluetooth");
    i = blp_line("DEVICES", &c);
    gl_ptitle(1, i, t, sizeof t);
    check(i == b + 4u && c.kind == CK_ACT && !strcmp(c.val, "SCAN") && gl_pg(1, i) == gl_pg(1, b) + 1u && !strcmp(t, "Devices"),
          "Devices page next: DEVICES (YES: SCAN, the list)");
#if BLE_CENTRAL
    gl_cell(1, i + 1u, &c);
    ok = c.label && !strcmp(c.label, "RECONNECT");
    gl_cell(1, i + 2u, &c);
    check(ok && c.label && !strcmp(c.label, "LAST DEVICE") && c.kind == CK_RO && !strcmp(c.val, "NONE"),
          "... then RECONNECT and LAST DEVICE (NONE while there is none)");
#endif
    check(!strcmp(blp_val("BLUETOOTH"), "OFF") && !strcmp(blp_val("STATE"), "OFF") && !strcmp(blp_val("DEVICE"), "NONE") &&
              !strcmp(blp_val("MIDI"), "--"), "Bluetooth OFF: STATE OFF, DEVICE NONE, MIDI --");
    blp_shot("BLUETOOTH", "opt-ble-page-off");
    ble_on = 1, ble_up = 0;
    check(!strcmp(blp_val("STATE"), "STARTING"), "ON, the radio not started yet: STATE STARTING");
    ble_up = 1;
    check(!strcmp(blp_val("BLUETOOTH"), "ON") && !strcmp(blp_val("STATE"), "ADVERTISING") && blp_col("STATE") == 0u,
          "ON, nothing connected: STATE ADVERTISING (plain)");
    blp_shot("BLUETOOTH", "opt-ble-page-on");
    ble_link = 1;
    check(!strcmp(blp_val("BLUETOOTH"), "ON") && !strcmp(blp_val("STATE"), "CONNECTED") && blp_col("STATE") == C_OK &&
              !strcmp(blp_val("DEVICE"), "HOST"), "a computer connected to the FM-1: STATE CONNECTED (green), DEVICE HOST");
    bmi_w++;
    check(!strcmp(blp_val("MIDI"), "IN") && blp_col("MIDI") == C_OK, "MIDI: a message in lights IN");
    bmo_w++;
    check(!strcmp(blp_val("MIDI"), "IN OUT"), "... one out too: IN OUT");
    blp_shot("BLUETOOTH", "opt-ble-page-host");
    {
        int32_t y = (int32_t)OH_HEAD + gl_y0() + 3 * gl_pitch();    /* (the MIDI line, the 4th) */
        check(px_in(120, (uint32_t)y, 116, (uint32_t)gl_pitch(), C_OK), "... drawn in green, in the large face beside its name");
    }
    fm1_ms += 400u;
    check(!strcmp(blp_val("MIDI"), "--") && blp_col("MIDI") == 0u, "... 0.4 s later, nothing: --");
    ble_link = 0;
#if BLE_CENTRAL
    check(!strcmp(blp_val("RECONNECT"), "OFF"), "RECONNECT: no last device: OFF");
    b = blp_line("RECONNECT", &c);
    line_at(SCR_SYSTEM, b);
    frame();
    ui.msg_t = 0;
    turn(LINE_KNOB(b), 1);
    check(ble_store.sel == BLE_SEL_NONE && !strcmp(blp_val("RECONNECT"), "OFF"), "... right with none: still OFF (a read-out: nothing to choose)");
    ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "KeyStep 37", BLE_KIND_MIDI);
    ble_store_select(&ble_store, BLE_SEL_NONE);
    line_at(SCR_SYSTEM, b);
    frame();
    check(blp_line("LAST DEVICE", &c) < GL_MAXL && c.kind == CK_ACT && c.danger && !strcmp(c.val, "FORGET"),
          "... a last device: LAST DEVICE is the FORGET action (alarm colour)");
    line_at(SCR_SYSTEM, b);
    frame();
    turn(LINE_KNOB(b), 1);
    check(ble_store.sel == BLE_SEL_LAST && !strcmp(blp_val("RECONNECT"), "KeyStep 37"),
          "... with a last device: right is LAST, the line names it as advertised");
    ble_devices_poll();
    check(!strcmp(blp_val("STATE"), "SEARCHING") && !strcmp(blp_val("DEVICE"), "KeyStep 37"),
          "... the FM-1 searches for it: STATE SEARCHING, DEVICE KeyStep 37");
    blp_shot("BLUETOOTH", "opt-ble-page-search");
    line_at(SCR_SYSTEM, b);
    frame();
    turn(LINE_KNOB(b), -1);
    check(ble_store.sel == BLE_SEL_NONE && !strcmp(blp_val("RECONNECT"), "OFF") && ble_store_has_last(&ble_store),
          "... left is NONE (OFF), the device kept");
    tap(B_SAVE);
    check(ble_store.sel == BLE_SEL_LAST, "... YES toggles it (LAST again)");
    blp_shot("RECONNECT", "opt-ble-page-devices");
    i = blp_line("LAST DEVICE", &c);
    line_at(SCR_SYSTEM, i);
    frame();
    tap(B_SAVE);
    {
        char v[sizeof ui.arm_verb], a[sizeof ui.arm_arg], q[sizeof ui.arm_q];
        modal_words(v, a, q);
        check(op_armed() && ble_store_has_last(&ble_store) && !strcmp(q, "Forget KeyStep 37?"),
              "LAST DEVICE: YES asks Forget KeyStep 37?, nothing forgotten yet");
    }
    blp_shot("LAST DEVICE", "opt-ble-page-forget");
    tap(B_HOME);
    check(!op_armed() && ble_store_has_last(&ble_store), "... HOME says no: kept");
    line_at(SCR_SYSTEM, blp_line("LAST DEVICE", &c));
    frame();
    tap(B_SAVE);
    tap(B_SAVE);
    check(!op_armed() && !ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_NONE && !strcmp(blp_val("RECONNECT"), "OFF"),
          "... YES, YES: forgotten, RECONNECT OFF");
    blp_line("LAST DEVICE", &c);
    i = c.kind == CK_RO && !strcmp(c.val, "NONE");
    check(i && blp_line("RECONNECT", &c) < GL_MAXL && c.kind == CK_RO,
          "... none left: LAST DEVICE NONE and RECONNECT OFF are read-outs (nothing to choose)");
    line_at(SCR_SYSTEM, blp_line("LAST DEVICE", &c));
    frame();
    ui.msg_t = 0;
    tap(B_SAVE);
    check(!op_armed() && ui.scr == SCR_SYSTEM && ui.msg_t && strstr(ui.msg, "NO LAST DEVICE"), "... YES with none left: NO LAST DEVICE, nothing asked");
    /* the list: LAST paired is tagged so */
    ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "KeyStep 37", BLE_KIND_MIDI);
    ble_store.dev.info |= BLE_DEV_BONDED;
    ble_list_row();
    tap(B_SAVE);
    frames(3);
    dev_screen("opt-ble-devices-paired");
    tap(B_HOME);
    ble_connect_none();
#endif
    ble_store_reset(&ble_store);
    ble_on = 0, ble_link = 0, ble_up = 1;
    bmi_w = bmo_w = 0;
    ble_dev_msg = 0;
    reset_ui();
}
#if BLE_CENTRAL
/* the search for LAST with the devices list closed (the user on HOME, the mixer): a passkey pairing it starts (a Mac or
 * phone that lost the bond: ble_central.c pairs afresh at once, with the passkey LAST is known to need) shows its code
 * over the panel, whatever screen is up, until the pairing ends. The user's report of 2026-10-18: the Mac asked for a
 * PIN, nothing showed on the FM-1 (the code was drawn only in the devices list, and the search runs only while that
 * list is closed) */
static void ble_bg_passkey_tests(void)
{
    static const uint8_t MAC[6] = {0x36, 0x36, 0x36, 0x36, 0x36, 0x36}, LTK[16] = {0x31}, RND[8] = {0x32};
    uint32_t i, c0;
    reset_ui();
    ble_on = 1, ble_link = 0, ble_up = 1;
    ble_store_reset(&ble_store);
    ble_connect_none();
    ble_store_set_last(&ble_store, MAC, 0, "MacBook", BLE_KIND_MIDI);
    ble_store_set_bond(&ble_store, LTK, RND, 0x3334, 1);
    ble_store_set_mitm(&ble_store);
    ble_store_select(&ble_store, BLE_SEL_LAST);
    ui.force = 1;
    frames(2);
    c0 = (uint32_t)cenfk.connects;
    for (i = 0; i < 4u && ble_connect_phase() != 3u; i++) {   /* (RC_TRY: initiating to LAST) */
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    check(cenfk.connects == (int)c0 + 1 && cenfk.p.bonded && (cenfk.p.sec & BLE_PEER_MITM) && !dev_listing() &&
              ui.scr == SCR_HOME, "background passkey: on HOME, the list closed: LAST (a Mac, bonded, needs a passkey) searched for");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cenfk.pairing = 1, cenfk.passkey = 123456u;      /* (its bond refused: a pairing afresh, the passkey ours to show) */
    ble_devices_poll();
    frames(2);
    check(ble_connect_passkey() == 123456u && op_overlay() == 4u && px_in(40, (uint32_t)OP_PY + MODAL_H / 2u - 10u, 160, 24, C_WHITE),
          "background passkey: the code over the panel on HOME, large, while the Mac's user types it");
    ppm("opt-ble-background-passkey");
    cenfk.pairing = 0, cenfk.passkey = BLE_NO_PASSKEY, cenfk.st = BLE_CS_READY;
    ble_devices_poll();
    ble_devices_poll();
    frames(2);
    check(op_overlay() != 4u && !strcmp(dev_status(), "CONNECTED MacBook"), "background passkey: typed: the code goes, CONNECTED MacBook");
    cen_gone(BLE_CF_LOST);
    ble_connect_none();
    ble_store_reset(&ble_store);
    ble_on = 0, ble_link = 0;
    ble_dev_msg = 0;
    reset_ui();
}
#endif
static void ble_tests(void)
{
    uint32_t kb;
    ble_sys_walk("off");
    ble_word_tests();
    kb = ble_sys_row();
    ble_sets = 0;                                       /* (the radio told: counted from here; a fuzz run before may have turned it) */
    check(!ble_on, "SYSTEM BLUETOOTH: OFF by default");
    turn(kb, 1);
    check(ble_on == 1u && ble_sets == 1u, "SYSTEM BLUETOOTH: the BLE line's knob right is ON, the radio told once");
    turn(kb, 1);
    check(ble_on == 1u && ble_sets == 1u, "... right again: still ON, the radio not told again");
    turn(kb, -1);
    check(ble_on == 0u && ble_sets == 2u, "... left is OFF");
    tap(B_SAVE);
    check(!dev_listing() && ble_on == 1u && ble_sets == 3u, "SYSTEM BLUETOOTH: YES toggles it, as every on / off line (ON)");
    tap(B_SAVE);
    check(!dev_listing() && ble_on == 0u && ble_sets == 4u, "... YES again: OFF");
    ble_list_row();
    tap(B_SAVE);
    check(dev_listing() && ble_on == 0u, "SYSTEM Devices: YES on DEVICES opens the devices list (it does not switch the radio)");
    tap(B_HOME);
    check(!dev_listing() && ui.scr == SCR_SYSTEM, "... HOME goes back to SYSTEM");
    ppm("opt-system-ble");
    reset_ui();
    ble_page_tests();
    ble_dev_tests();
#if BLE_CENTRAL
    ble_dev_more_tests();
    ble_bg_passkey_tests();
#endif
}
#endif
