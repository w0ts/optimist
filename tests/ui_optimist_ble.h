/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c with FELUCCA_BLE=1: SYSTEM > BLUETOOTH (the radio's ON / OFF, as SLOOP's HOME menu has it), and
 * the settings word's bits 21..22 HOLD, 23 CARDS, 24 BLUETOOTH (SETTINGS_BLE_ON_BIT), 25 KNOB COLORS, which must not collide
 * (storage/settings_word.c), and the BLE devices list (ble_dev_tests: YES on the BLE row opens it; op_project.c).
 * Included after the helpers. The radio itself is the stub in ui_optimist_test.c (midi_ble.c is host-tested apart). */

#if FELUCCA_BLE
static void ble_sys_row(void)                           /* the BLUETOOTH row of SYSTEM, the cursor on it */
{
    uint32_t w;
    reset_ui();
    op_enter(SCR_SYSTEM);
    for (w = 0; w < SCR->rows(); w++) {
        char nm[12];
        SCR->name(w, nm);
        if (!strcmp(nm, "BLUETOOTH"))
            break;
    }
    ui.row[SCR_SYSTEM] = (uint8_t)w;
    ui.force = 1;
    frame();
}
static void ble_word_tests(void)
{
    uint32_t w0, w, all;
    hold_sel = 0;
    op_cards = CARDS_LINE;
    ble_on = 0;
    knob_colors = 0;
    check(SETTINGS_BLE_ON_BIT == 24u, "settings word: BLUETOOTH is bit 24 (SETTINGS_BLE_ON_BIT)");
    w0 = bp23_word();
    hold_sel = 2;
    w = bp23_word();
    check((w ^ w0) == (2u << 21), "settings word: HOLD moves bits 21..22 only");
    hold_sel = 0;
    op_cards = CARDS_2X2;
    w = bp23_word();
    check((w ^ w0) == (1u << 23), "settings word: CARDS moves bit 23 only");
    op_cards = CARDS_LINE;
    ble_on = 1;
    w = bp23_word();
    check((w ^ w0) == (1u << SETTINGS_BLE_ON_BIT), "settings word: BLUETOOTH moves bit 24 only");
    ble_on = 0;
    knob_colors = 1;
    w = bp23_word();
    check((w ^ w0) == (1u << 25), "settings word: KNOB COLORS moves bit 25 only");
    knob_colors = 0;
    hold_sel = 2;
    op_cards = CARDS_2X2;
    ble_on = 1;
    knob_colors = 1;
    all = bp23_word();
    hold_sel = 0;
    op_cards = CARDS_LINE;
    ble_on = 0;
    knob_colors = 0;
    bp23_from_word(all);
    check(hold_sel == 2u && op_cards == CARDS_2X2 && ble_on == 1u && knob_colors == 1u,
          "settings word: HOLD, CARDS, BLUETOOTH and KNOB COLORS all set read back each as itself (21..22, 23, 24, 25)");
    bp23_from_word(all & ~(1u << 25));
    check(hold_sel == 2u && op_cards == CARDS_2X2 && ble_on == 1u && knob_colors == 0u, "... clearing bit 25 leaves the rest");
    bp23_from_word(all & ~(1u << SETTINGS_BLE_ON_BIT));
    check(hold_sel == 2u && op_cards == CARDS_2X2 && ble_on == 0u && knob_colors == 1u, "... clearing bit 24 leaves the rest");
    bp23_from_word(all & ~(1u << 23));
    check(hold_sel == 2u && op_cards == CARDS_LINE && ble_on == 1u && knob_colors == 1u, "... clearing bit 23 leaves the rest");
    bp23_from_word(all & ~(3u << 21));
    check(hold_sel == 0u && op_cards == CARDS_2X2 && ble_on == 1u && knob_colors == 1u, "... clearing bits 21..22 leaves the rest");
    hold_sel = 0;
    op_cards = CARDS_LINE;
    ble_on = 0;
    knob_colors = 0;
}
/* ---- the BLE devices list (SYSTEM > BLUETOOTH, YES on the BLE row): NONE and LAST pinned at the top, the nearby
 * BLE-MIDI devices with signal bars, no wrap; SAVE picks / connects, HOME goes back, SELECT moves, HOME + a button asks
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
    ble_sys_row();
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
    tap(B_SAVE);
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
    check(px_in(60, (uint32_t)OP_PY + 70u, 130, 36, C_WHITE), "devices: the code is drawn large in the middle of the panel");
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
    /* HOME + a button: FORGET LAST, the modal */
    press(B_HOME);
    press(B_EDIT);
    release(B_EDIT);
    release(B_HOME);
    frames(2);
    check(dev_listing() && ui.arm_scr == SCR_SYSTEM && !strcmp(ui.arm_verb, "FORGET?") && !strcmp(ui.arm_arg, "KeyStep 37") &&
          ble_store_has_last(&ble_store), "devices: HOME + a button asks FORGET LAST? (the modal names it), nothing forgotten yet");
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
        press(B_HOME);
        press(B_ENV);
        release(B_ENV);
        release(B_HOME);
        check(ui.arm_scr == SCR_SYSTEM && ui.arm_ms == t0, "devices: HOME + a button while asked does not ask again (the 3 s run on)");
    }
    tap(B_HOME);
    check(dev_listing() && ui.arm_scr == ARM_NONE && ble_store_has_last(&ble_store), "devices: HOME says no: still the list, LAST kept");
    press(B_HOME);
    press(B_ENV);
    release(B_ENV);
    release(B_HOME);
    tap(B_SAVE);
    frames(2);
    check(!ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_NONE && ui.row[SCR_SYSTEM] == 0u && dev_listing(),
          "devices: SAVE says yes: LAST forgotten, NONE picked, the cursor on NONE");
    dev_screen("opt-ble-devices-forgotten");
    press(B_HOME);
    press(B_EDIT);
    release(B_EDIT);
    release(B_HOME);
    check(ui.arm_scr == ARM_NONE, "devices: HOME + a button with no LAST asks nothing");
    ble_store_set_last(&ble_store, ble_found.e[0].addr, ble_found.e[0].addr_rand, "KeyStep 37", BLE_KIND_MIDI);
    ble_store_select(&ble_store, BLE_SEL_NONE);
    frames(2);
    dev_select(1);
    tap(B_SAVE);
    check(ble_store.sel == BLE_SEL_LAST, "devices: SAVE on LAST chooses it");
    dev_select(-1);
    tap(B_SAVE);
    check(ble_store.sel == BLE_SEL_NONE && ble_store_has_last(&ble_store), "devices: SAVE on NONE chooses it, LAST kept");
    op_cards = CARDS_2X2;
    ui.force = 1;
    frames(3);
    dev_screen("opt-ble-devices-2x2");
    check(px_in(0, (uint32_t)OP_PY, 236, 19, trk_col(song.sel)), "devices: CARDS 2x2: the list under the big values, the cursor bar drawn");
    op_cards = CARDS_LINE;
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
    ble_sys_row();
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
    ble_sys_row();
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
    tap(B_SAVE);
    frames(2);
    check(cenfk.connects == (int)c0 && bdp.tone == BDL_WARN && !strcmp(bdp.st, "Busy: A host is connected") &&
              px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_WARN) &&
              !px_in(0, (uint32_t)OP_PY + OH_BODY - DEV_STATUS_H, 240, DEV_STATUS_H, C_OK),
          "devices: BUSY: A HOST IS CONNECTED is a notice (C_WARN), not green");
    dev_close_fresh();
    /* NONE with BLUETOOTH off: not "stay visible" */
    dev_open_fresh();
    ble_on = 0;
    tap(B_SAVE);
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
    press(B_HOME);
    press(B_EDIT);
    release(B_EDIT);
    release(B_HOME);
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
    ble_sys_row();
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && SCREENS[SCR_SYSTEM].rows() == 1u, "devices (no scanner built in): the list is NONE alone");
    dev_screen("opt-ble-devices-noscan");
    tap(B_HOME);
    ble_on = 0;
    reset_ui();
}
#endif
static void ble_tests(void)
{
    ble_word_tests();
    ble_sys_row();
    check(!ble_on, "SYSTEM BLUETOOTH: OFF by default");
    turn(EN_K1, 1);
    check(ble_on == 1u && ble_sets == 1u, "SYSTEM BLUETOOTH: KNOB 1 right is ON, the radio told once");
    turn(EN_K1, 1);
    check(ble_on == 1u && ble_sets == 1u, "... right again: still ON, the radio not told again");
    turn(EN_K1, -1);
    check(ble_on == 0u && ble_sets == 2u, "... left is OFF");
    tap(B_SAVE);
    check(dev_listing() && ble_on == 0u, "SYSTEM BLUETOOTH: YES opens the devices list (it does not switch the radio)");
    tap(B_HOME);
    check(!dev_listing() && ui.scr == SCR_SYSTEM, "... HOME goes back to SYSTEM");
    ppm("opt-system-ble");
    reset_ui();
    ble_dev_tests();
#if BLE_CENTRAL
    ble_dev_more_tests();
#endif
}
#endif
