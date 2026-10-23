/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c with FELUCCA_BLE=1: SYSTEM > Bluetooth (op_project.c, op_bt.c: MODE, DEVICE, ACTIVITY, STATE; SAVE
 * and SAVE held on any line; the devices list), and the settings word's bits 21..22 HOLD, 23 reserved (CARDS until
 * 2026-10-12), 24 BLUETOOTH (SETTINGS_BLE_ON_BIT), 25 KNOB COLORS, which must not collide (storage/settings_word.c).
 * Included after the helpers. The radio is the stand-in in ble_ui_stubs.h (midi_ble.c is host-tested apart). */

#if FELUCCA_BLE
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
/* ---- SYSTEM > Bluetooth, one pattern for MIDI and AUDIO (op_bt.c; the user, 2026-10-19): MODE OFF / MIDI MAN / MIDI AUTO
 * (/ AUDIO MAN / AUDIO AUTO with BT_AUDIO), DEVICE the last device, ACTIVITY, STATE. SAVE on any line: connected or
 * connecting lets go, else the last device is connected to, none: the devices list (the scan). On the list SAVE connects
 * to the row. SAVE held forgets the last device. HOME goes home. AUTO connects to the last device at boot and when
 * chosen; a link lost is made again in neither. The radio is the stand-in in ble_ui_stubs.h. */
static void dev_screen(const char *name) { ui.force = 1; frames(3); ppm(name); }
static uint32_t bt_line(const char *lab)                /* the cursor on the Bluetooth page's line lab; its knob */
{
    uint32_t w = line_named(SCR_SYSTEM, lab);
    ui.force = 1;
    frame();
    return LINE_KNOB(w);
}
static const char *bt_val(const char *lab)              /* line lab's value as the page shows it */
{
    static cell_t c;
    uint32_t i;
    op_enter(SCR_SYSTEM);
    for (i = 0; i < gl_n(1); i++) {
        gl_cell(1, i, &c);
        if (c.label && !strcmp(c.label, lab))
            return c.val;
    }
    return "?";
}
static void save_held(void)
{
    press(B_SAVE);
    frames(HOLD_FRAMES);
    release(B_SAVE);
}
static void blp_shot_t(const char *name)               /* the Bluetooth page drawn */
{
    bt_line("MODE");
    ui.force = 1;
    frames(3);
    while (tf.t)
        frame();
    ppm(name);
}
static int said(const char *s) { return ui.msg_t && !strcmp(ui.msg, s); }
static void bt_fresh(uint32_t mode)                     /* nothing remembered, nothing connected, MODE mode */
{
    reset_ui();
    bt.boot = 1;                                        /* (the saved mode applied: not in these tests) */
    ble_link = 0, ble_up = 1;
#if BLE_CENTRAL
    cen_gone(BLE_CF_NONE);
    ble_connect_none();
#endif
    ble_store_reset(&ble_store);
    ble_dev_msg = 0;
#if FELUCCA_BT_AUDIO
    bt_enter(BT_AUD, 0);                                /* (BT AUDIO off, nothing remembered) */
    memset(&bta, 0, sizeof bta);
    btafk.st = BTA_S_NONE, btafk.lost = 0, btafk.out_bt = 1;
#endif
    bt_enter(BT_MIDI, mode);
    frames(2);
}
static void ble_mode_tests(void)
{
    uint32_t k;
    static const char *const M[3] = {"OFF", "MAN", "AUTO"};
    bt_fresh(0);
    check(!ble_on && !strcmp(bt_val("MODE"), "OFF") && !strcmp(bt_val("STATE"), "OFF") && !strcmp(bt_val("DEVICE"), "NONE"),
          "Bluetooth OFF: MODE OFF, STATE OFF, DEVICE NONE");
    blp_shot_t("opt-ble-page-off");
    k = bt_line("MODE");
    turn(k, 1);
    check(ble_on && bt_mode(BT_MIDI) == 1u && !strcmp(bt_val("MODE"), M[1]), "MODE right: MIDI MAN, the radio on");
    check(!strcmp(bt_val("STATE"), "ADVERTISING"), "... nothing connected: STATE ADVERTISING (MIDI is visible)");
    blp_shot_t("opt-ble-page-on");
    k = bt_line("MODE");
    turn(k, 1);
    check(ble_on && bt_mode(BT_MIDI) == 2u && !strcmp(bt_val("MODE"), M[2]), "... right again: MIDI AUTO");
    k = bt_line("MODE");
    turn(k, 1);
    check(bt_mode(BT_MIDI) == 2u && bt_mode(BT_AUD) == 0u, "... right again: MIDI AUTO stays (the end), BT AUDIO not touched");
    for (k = 0; k < 5u; k++)
        turn(bt_line("MODE"), -1);                      /* (a detent a mode) */
    check(!ble_on && bt_mode(BT_MIDI) == 0u, "... left to the end: OFF, the radio off");
    ble_link = 1;
    bt_enter(BT_MIDI, 1);
    check(!strcmp(bt_val("STATE"), "CONNECTED") && !strcmp(bt_val("DEVICE"), "HOST"),
          "a computer connected to the FM-1: STATE CONNECTED, DEVICE HOST");
    bmi_w++;
    check(!strcmp(bt_val("ACTIVITY"), "IN"), "ACTIVITY: a message in lights IN");
    blp_shot_t("opt-ble-page-host");
    bt_line("STATE");
    k = ble_sets;
    tap(B_SAVE);
    check(ble_sets == k + 2u && ble_on && said("DISCONNECTED "),
          "SAVE on STATE, a host connected: let go (the radio off and on again: it advertises), DISCONNECTED");
    ble_link = 0;
    bt_fresh(0);
}
#if BLE_CENTRAL
static uint32_t dev_row_n(void) { return bt_rows(BT_MIDI); }
static const char *dev_row_tag(uint32_t r)
{
    const char *tag;
    int32_t bars;
    char nm[BLE_NAME_MAX + 1u];
    bt_row(BT_MIDI, r, nm, &tag, &bars);
    return tag;
}
static const char *bt_val_row(uint32_t r)              /* the list's row r: its name */
{
    static char nm[BLE_NAME_MAX + 1u];
    const char *tag;
    int32_t bars;
    bt_row(BT_MIDI, r, nm, &tag, &bars);
    return nm;
}
static void dev_select(int32_t s) { turn(EN_SELECT, s); frames(2); }
static void dev_hear(uint8_t a0, const char *name)      /* a device heard: its ADV_IND (the BLE-MIDI UUID), its SCAN_RSP (the name) */
{
    ble_fake_rep(0, a0, 1, 0, 0x0A12);
    ble_fake_rep(4, a0, 0, name, 0x0A12);
}
static void cen_up(void)                                /* the stand-in's link is made and ready */
{
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cenfk.st = BLE_CS_READY;
    ble_devices_poll();
    ble_devices_poll();
    frames(2);
}
/* SAVE: the same on every line; no last device: the list */
static void ble_save_tests(void)
{
    static const char *const LINES[4] = {"MODE", "DEVICE", "ACTIVITY", "STATE"};
    uint32_t i, c0;
    for (i = 0; i < 4u; i++) {
        bt_fresh(1);
        bt_line(LINES[i]);
        tap(B_SAVE);
        frames(2);
        check(dev_listing() && ble_devs_open && blefk.want == 1u, "SAVE, no last device: the devices list opens, the scan starts (any line)");
        tap(B_HOME);
    }
    check(!dev_listing() && ui.scr == SCR_SYSTEM && !ble_devs_open, "the list: HOME goes back to the page, the scan stops");
    /* the list: the devices heard, no NONE row; SAVE connects to the row, back to the page */
    bt_fresh(1);
    bt_line("MODE");
    tap(B_SAVE);
    frames(2);
    check(dev_row_n() == 0u && !strcmp(bdp.st, "Scanning 0 found"), "the list: nothing heard: no rows, \"Scanning 0 found\"");
    dev_screen("opt-ble-devices-empty");
    dev_hear(0x01, "KeyStep 37");
    ble_fake_rep(0, 0x02, 0, "Tile", 0x0405);           /* a beacon (no BLE-MIDI UUID): never listed */
    dev_hear(0x03, "Roland Aerophone");
    ble_devices_poll();
    frames(2);
    check(dev_row_n() == 2u && SCREENS[SCR_SYSTEM].rows() == 2u, "the list: the two BLE-MIDI devices (the beacon left out) are the rows");
    dev_screen("opt-ble-devices");
    dev_select(-1);
    check(ui.row[SCR_SYSTEM] == 0u, "the list: SELECT stops at the top (no wrap)");
    dev_select(5);
    check(ui.row[SCR_SYSTEM] == 1u, "the list: SELECT stops at the bottom (no wrap)");
    dev_select(-1);
    c0 = (uint32_t)cenfk.connects;
    tap(B_SAVE);
    frames(2);
    check(cenfk.connects == (int)c0 + 1 && !op_armed() && !dev_listing() && ui.scr == SCR_SYSTEM && said("CONNECTING KeyStep 37"),
          "the list: SAVE connects to the row at once (nothing asked), back to the page: CONNECTING KeyStep 37");
    check(!strcmp(bt_val("DEVICE"), "KeyStep 37") && !strcmp(bt_val("STATE"), "CONNECTING"),
          "... the page: DEVICE KeyStep 37, STATE CONNECTING");
    cen_up();
    check(ble_store_has_last(&ble_store) && !strcmp(bt_val("STATE"), "CONNECTED") && !strcmp(bt_val("DEVICE"), "KeyStep 37") && !bt_wants(BT_MIDI),
          "... up: it is the last device, STATE CONNECTED");
    blp_shot_t("opt-ble-page-connected");
    check(ble_store.sel == BLE_SEL_NONE, "MAN: once up, no search for it is armed (a link lost stays lost)");
    /* connected: SAVE lets go, on any line */
    bt_line("ACTIVITY");
    c0 = (uint32_t)cenfk.cancels;
    tap(B_SAVE);
    check(cenfk.cancels == (int)c0 + 1 && said("DISCONNECTED KeyStep 37") && ble_store_has_last(&ble_store),
          "connected: SAVE lets go (DISCONNECTED KeyStep 37), the device kept");
    cen_gone(BLE_CF_NONE);
    c0 = (uint32_t)cenfk.connects;
    for (i = 0; i < 5u; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
        frames(1);
    }
    check(ble_seeking() == 0 && cenfk.connects == (int)c0 && !bt_wants(BT_MIDI), "... and it is not searched for again by itself");
    /* a last device, not connected: SAVE connects to it; again (connecting): lets go */
    c0 = (uint32_t)cenfk.connects;
    bt_line("DEVICE");
    tap(B_SAVE);
    check(bt_wants(BT_MIDI) && ble_store.sel == BLE_SEL_LAST && said("CONNECTING KeyStep 37"), "the last device, not connected: SAVE connects to it");
    for (i = 0; i < 3u && ble_seeking() != 2; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    check(ble_seeking() != 0 && !strcmp(bt_val("STATE"), "SEARCHING"), "... STATE SEARCHING");
    tap(B_SAVE);
    check(!bt_wants(BT_MIDI) && ble_store.sel == BLE_SEL_NONE && said("DISCONNECTED KeyStep 37"), "... SAVE while it connects: let go");
    /* not found in BT_TRY_MS: given up, said */
    tap(B_SAVE);
    fm1_ms += BT_TRY_MS + 16u;
    frames(1);
    check(!bt_wants(BT_MIDI) && ble_store.sel == BLE_SEL_NONE && said("NO ANSWER KeyStep 37"), "a connect not up in 30 s: NO ANSWER KeyStep 37, given up");
    /* SAVE held: forget, on any line; HOME: home */
    bt_line("MODE");
    save_held();
    check(!ble_store_has_last(&ble_store) && said("FORGOTTEN KeyStep 37") && ui.scr == SCR_SYSTEM && !dev_listing(),
          "SAVE held: FORGOTTEN KeyStep 37 (on MODE too), nothing else");
    save_held();
    check(said("NO LAST DEVICE") && !dev_listing(), "SAVE held, nothing remembered: NO LAST DEVICE");
    bt_line("STATE");
    tap(B_HOME);
    check(ui.scr != SCR_SYSTEM, "HOME on the Bluetooth page goes home, as anywhere");
    (void)c0;
    bt_fresh(0);
}
/* AUTO: the last device connected to when chosen (and at boot, from the record); MAN never by itself */
static void ble_auto_tests(void)
{
    uint8_t rec[BT_REC_SIZE];
    uint32_t c0;
    bt_fresh(1);
    ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "KeyStep 37", BLE_KIND_MIDI);
    bt_enter(BT_MIDI, 1);
    frames(2);
    check(!bt_wants(BT_MIDI) && ble_store.sel == BLE_SEL_NONE, "MIDI MAN with a last device: nothing connects by itself");
    bt_enter(BT_MIDI, 2);
    check(bt_wants(BT_MIDI) && ble_store.sel == BLE_SEL_LAST, "MIDI AUTO chosen: the last device is connected to");
    c0 = (uint32_t)cenfk.connects;
    fm1_ms += 1000u;
    ble_devices_poll();
    ble_devices_poll();                                 /* (the search armed, then its first try) */
    check(cenfk.connects == (int)c0 + 1, "... the stack initiates to it");
    cen_up();
    check(!bt_wants(BT_MIDI) && !strcmp(bt_val("STATE"), "CONNECTED"), "... up: CONNECTED");
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    frames(2);
    fm1_ms += 3000u;
    ble_devices_poll();
    frames(2);
    check(ble_seeking() == 0 && !bt_wants(BT_MIDI), "AUTO: a link lost is not made again by itself");
    /* the record: the mode back at boot, AUTO connects */
    bt_rec_out(rec);
    bt_enter(BT_MIDI, 1);
    bt.rec = 0;
    bt_rec_in(rec);
    check(bt.rec == (BT_REC_OK | 2u), "the record: MODE kept (MIDI AUTO, AUDIO OFF)");
    bt.boot = 0;
    bt_mode_put(BT_MIDI, 1);
    fm1_ms += 2000u;
    frames(1);
    check(bt.boot && bt_mode(BT_MIDI) == 2u && bt_wants(BT_MIDI) && ble_store.sel == BLE_SEL_LAST, "boot: MIDI AUTO from the record, the last device connected to");
    rec[0] ^= 0xFFu;
    bt.rec = 0;
    bt_rec_in(rec);
    check(bt.rec == 0u, "the record without its mark: not read");
    bt_fresh(0);
}
/* the list's rows follow the device, not the place; a device gone is said */
static void ble_list_tests(void)
{
    uint32_t c0, i, first;
    char t[48];
    bt_fresh(1);
    bt_line("MODE");
    tap(B_SAVE);
    for (i = 0; i < 8u; i++) {
        char nm[8] = "Dev  ";
        nm[3] = (char)('A' + i);
        dev_hear((uint8_t)(0x10 + i), nm);
        ble_devices_poll();                             /* (the stand-in's queue holds 8 reports) */
    }
    frames(2);
    check(dev_row_n() == 8u && bdp.shown < bdp.n, "the list: eight rows, more than the panel shows");
    dev_select(9);
    frames(2);
    first = bdp.first;
    check(bdp.cur == 7u && first == bdp.n - bdp.shown, "the list: the cursor at the end: the window shows the last rows");
    dev_select(-1);
    frames(2);
    check(bdp.cur == 6u && bdp.first == first, "the list: one row up from the end: the window stays");
    tap(B_HOME);
    bt_fresh(1);
    bt_line("MODE");
    tap(B_SAVE);
    dev_hear(0x01, "Alpha");
    dev_hear(0x03, "Bravo");
    dev_hear(0x05, "Charlie");
    ble_devices_poll();
    frames(2);
    dev_select(1);
    frames(2);
    check(bdp.cur == 1u && !strcmp(bt_val_row(1), "Bravo"), "the list: the cursor on Bravo");
    c0 = (uint32_t)cenfk.connects;
    press(B_SAVE);                                      /* (SAVE acts on its release: Alpha ages out in between) */
    for (i = 0; i < BLE_SCAN_N; i++)
        if (ble_found.e[i].addr[0] == 0x01)
            ble_found.e[i].used = 0;
    ble_found.gen++;
    release(B_SAVE);
    frames(2);
    check(cenfk.connects == (int)c0 + 1 && cenfk.p.addr[0] == 0x03 && said("CONNECTING Bravo"),
          "the list: a device aging out above the cursor: SAVE connects to the highlighted one (Bravo)");
    bt_fresh(1);
    bt_line("MODE");
    tap(B_SAVE);
    dev_hear(0x01, "Alpha");
    dev_hear(0x03, "Bravo");
    ble_devices_poll();
    frames(2);
    dev_select(1);
    frames(2);
    c0 = (uint32_t)cenfk.connects;
    press(B_SAVE);
    for (i = 0; i < BLE_SCAN_N; i++)                    /* (Bravo: gone) */
        if (ble_found.e[i].addr[0] == 0x03)
            ble_found.e[i].used = 0;
    ble_found.gen++;
    release(B_SAVE);
    check(cenfk.connects == (int)c0 && said("DEVICE GONE"), "the list: the highlighted device gone before SAVE: nothing connected, DEVICE GONE");
    tap(B_HOME);
    /* the status line cut to its room: the words stay, a long name loses its end */
    dev_line_text(t, sizeof t, "CONNECTING (TRY 2/6) Roland Aerophone", 21u);
    check(!strncmp(t, "Connecting (try 2/6) ", 21) && text_w(&FONT_S, t) <= DEV_LINE_W && t[21] == 'R',
          "the list's status line: \"Connecting (try 2/6) \" + a 16-character name is cut to its 232 px");
    /* DEVICE: a long name whole, in the small face */
    ble_connect_none();
    ble_dev_msg = 0;
    ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "Roland Aerophone", BLE_KIND_MIDI);
    {
        cell_t c;
        ble_peer_cell(&c);
        check(!strcmp(c.val, "Roland Aeroph") && !c.big, "DEVICE: a 16-character name: its first 13 characters, in the small face");
        ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "KeyStep", BLE_KIND_MIDI);
        ble_peer_cell(&c);
        check(!strcmp(c.val, "KeyStep") && c.big, "DEVICE: a short name in the large face");
    }
    bt_fresh(0);
}
/* the search for the last device with the list closed (AUTO, the user on HOME): a passkey pairing it starts shows its code
 * over any screen (the user, 2026-10-18: the Mac asked for a PIN, nothing showed on the FM-1) */
static void ble_bg_passkey_tests(void)
{
    static const uint8_t MAC[6] = {0x36, 0x36, 0x36, 0x36, 0x36, 0x36}, LTK[16] = {0x31}, RND[8] = {0x32};
    uint32_t i, c0;
    bt_fresh(1);
    ble_store_set_last(&ble_store, MAC, 0, "MacBook", BLE_KIND_MIDI);
    ble_store_set_bond(&ble_store, LTK, RND, 0x3334, 1);
    ble_store_set_mitm(&ble_store);
    bt_enter(BT_MIDI, 2);
    go_home();
    ui.force = 1;
    frames(2);
    c0 = (uint32_t)cenfk.connects;
    for (i = 0; i < 4u && ble_connect_phase() != 3u; i++) {   /* (RC_TRY: initiating to LAST) */
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    check(cenfk.connects == (int)c0 + 1 && cenfk.p.bonded && (cenfk.p.sec & BLE_PEER_MITM) && !dev_listing() &&
              ui.scr == SCR_HOME, "background passkey: on HOME: the last device (a Mac, bonded, needs a passkey) searched for");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cenfk.pairing = 1, cenfk.passkey = 123456u;         /* (its bond refused: a pairing afresh, the passkey ours to show) */
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
    bt_fresh(0);
}
#endif
/* the settings record across the two pages (op_bt.c bt_rec_out / bt_rec_in): both modes kept; a record from before the
 * split (one MODE in r[4]: OFF, MIDI MAN, MIDI AUTO, AUDIO MAN, AUDIO AUTO) read as that side's mode, the other side OFF;
 * r[4] still written, as an older firmware reads it */
static void bt_rec_tests(void)
{
    static const uint8_t OLD_M[5] = {0, 1, 2, 0, 0}, OLD_A[5] = {0, 0, 0, 1, 2};
    uint8_t rec[BT_REC_SIZE];
    uint32_t x, mk = BT_REC_MARK, want;
    char what[160];
    bt_fresh(0);
    for (x = 0; x < 5u; x++) {
        memset(rec, 0, sizeof rec);
        memcpy(rec, &mk, 4);
        rec[4] = (uint8_t)x;
        bt.rec = 0;
        bt_rec_in(rec);
        want = x >= 3u && !BT_AUDIO_BUILT ? 0u : BT_REC_OK | OLD_M[x] | (uint32_t)OLD_A[x] << 2;
        snprintf(what, sizeof what, "the record from before the two pages, MODE %u: read as MIDI %u, AUDIO %u%s", x,
                 OLD_M[x], OLD_A[x], want ? "" : " (no BT_AUDIO built: as no record)");
        check(bt.rec == want, what);
    }
    rec[4] = 7u;
    bt.rec = 0;
    bt_rec_in(rec);
    check(bt.rec == 0u, "the record from before the two pages with a MODE past AUDIO AUTO: not read");
    bt_mode_put(BT_MIDI, BT_MAN);
    bt_mode_put(BT_AUD, BT_AUDIO_BUILT ? BT_AUTO : BT_OFF);
    bt_rec_out(rec);
    check(rec[4] == (BT_AUDIO_BUILT ? 4u : 1u) && rec[BT_REC_MODES] == (BT_REC_OK | bt.mode),
          "the record written: both modes (byte 29), and byte 4 as an older firmware reads it (AUDIO's when AUDIO is on)");
    bt.rec = 0;
    bt_rec_in(rec);
    check(bt.rec == (BT_REC_OK | BT_MAN | (BT_AUDIO_BUILT ? BT_AUTO : 0u) << 2), "... read back: both modes");
    bt.mode = 0;
    ble_on = 1;                                         /* (the settings word: BLUETOOTH on) */
    bt.boot = 0;
    fm1_ms += 2000u;
    frames(1);
    check(bt.boot && bt_mode(BT_MIDI) == BT_MAN && bt_mode(BT_AUD) == (BT_AUDIO_BUILT ? BT_AUTO : BT_OFF) && ble_on,
          "boot: both modes from the record, each side on (MIDI MAN, AUDIO AUTO)");
    memset(rec, 0, sizeof rec);
    memcpy(rec, &mk, 4);
    rec[4] = 3u;                                        /* (an old AUDIO MAN record: MIDI was switched off by it) */
    bt.rec = 0;
    bt_rec_in(rec);
    bt.mode = 0;
    ble_on = 0;
    bt.boot = 0;
    fm1_ms += 2000u;
    frames(1);
    check(bt.boot && bt_mode(BT_MIDI) == BT_OFF && bt_mode(BT_AUD) == (BT_AUDIO_BUILT ? BT_MAN : BT_OFF) && !ble_on,
          "boot from an old AUDIO MAN record: AUDIO MAN, MIDI OFF (as before the split)");
    bt_fresh(0);
}
#if FELUCCA_BT_AUDIO
/* ---- the two pages at once (the user, 2026-10-23): BT MIDI and BT AUDIO, each its MODE, its connect, its list */
static int bta_find_line(const char *lab, cell_t *c)    /* BT AUDIO's line lab (its index; -1: none) */
{
    uint32_t i;
    if (ui.scr != SCR_SYSTEM)
        op_enter(SCR_SYSTEM);
    for (i = 0; i < gl_n(1); i++) {
        if (strcmp(SYS[(gl_ent(1, i) >> 2) % NSYS].name, "BT AUDIO"))
            continue;
        gl_cell(1, i, c);
        if (c->label && !strcmp(c->label, lab))
            return (int)i;
    }
    return -1;
}
static uint32_t bta_line(const char *lab)               /* the cursor on BT AUDIO's line lab; its knob */
{
    cell_t c;
    int i = bta_find_line(lab, &c);
    if (i < 0)
        return EN_K1;
    line_at(SCR_SYSTEM, (uint32_t)i);
    ui.force = 1;
    frame();
    return LINE_KNOB((uint32_t)i);
}
static const char *bta_val(const char *lab)
{
    static cell_t c;
    return bta_find_line(lab, &c) >= 0 ? c.val : "?";
}
static void bt_two_pages_tests(void)
{
    uint32_t u, midi = 0, aud = 0, s0, p0, h0;
    for (u = 0; u < NSYS_SETUP; u++) {
        midi += !strcmp(SYS[u].name, "BT MIDI");
        aud += !strcmp(SYS[u].name, "BT AUDIO");
    }
    check(midi == 1u && aud == 1u, "Bluetooth: two SYSTEM pages, BT MIDI and BT AUDIO (BLE and BT_AUDIO built)");
    bt_fresh(1);
    check(bta_find_line("MODE", &(cell_t){0}) >= 0 && !strcmp(bta_val("MODE"), "OFF") && !strcmp(bta_val("FORMAT"), "SBC 44.1k") &&
              !strcmp(bta_val("DEVICE"), "NONE") && !strcmp(bta_val("STATE"), "OFF"),
          "BT AUDIO: MODE OFF, DEVICE NONE, FORMAT SBC 44.1k, STATE OFF");
    s0 = btafk.starts;
    turn(bta_line("MODE"), 1);
    check(bt_mode(BT_AUD) == BT_MAN && btafk.starts == s0 + 1u && !strcmp(bta_val("MODE"), "MAN"), "BT AUDIO MODE right: MAN, the audio side started");
    check(ble_on && bt_mode(BT_MIDI) == BT_MAN && !strcmp(bt_val("MODE"), "MAN") && !strcmp(bt_val("STATE"), "ADVERTISING"),
          "... BT MIDI stays on beside it (MAN, ADVERTISING): both at once");
    s0 = btafk.stops;
    turn(bt_line("MODE"), -1);
    check(!ble_on && bt_mode(BT_MIDI) == BT_OFF && bt_mode(BT_AUD) == BT_MAN && bta.mode != BTA_OFF && btafk.stops == s0,
          "BT MIDI MODE left: MIDI OFF, the radio's MIDI off; BT AUDIO left as it was (MAN, not stopped)");
    turn(bt_line("MODE"), 1);
    check(ble_on && bt_mode(BT_MIDI) == BT_MAN && bt_mode(BT_AUD) == BT_MAN, "BT MIDI back to MAN: AUDIO still MAN");
    /* SAVE on BT AUDIO, nothing remembered: AUDIO's list (the inquiry), not MIDI's */
    bta_line("FORMAT");
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && dev_side() == BT_AUD && btafk.scan_on && !blefk.want,
          "BT AUDIO: SAVE, no last device: AUDIO's devices list, the inquiry on (BLE's scan not started)");
    bta_fake_dev(0, 0x11, "AirPods");
    bta_fake_dev(1, 0x22, "Speaker");
    frames(2);
    if (strcmp(bdp.st, "Save connects, hold forgets"))
        printf("  the AUDIO list's status line: \"%s\"\n", bdp.st);
    {
        char nm[BLE_NAME_MAX + 1u];
        const char *tag;
        int32_t bars;
        bt_row(dev_side(), 1, nm, &tag, &bars);
        check(!strcmp(nm, "Speaker") && !tag[0] && bars < 0, "... row 2: Speaker, no tag, no bars (the inquiry gives none)");
    }
    check(dev_rows() == 2u && !strcmp(bdp.st, "Save connects, hold forgets"),
          "... the devices the inquiry heard are its rows (the AUDIO list's line under them)");
    dev_screen("opt-bta-devices");
    p0 = btafk.pages;
    tap(B_SAVE);
    frames(2);
    check(btafk.picks == 1u && btafk.pages == p0 + 1u && !dev_listing() && !btafk.scan_on && said("CONNECTING AirPods") &&
              bt_wants(BT_AUD) && !bt_wants(BT_MIDI), "... SAVE on AirPods: paged, the list closed, AUDIO connecting, MIDI not");
    check(!strcmp(bta_val("STATE"), "CONNECTING") && !strcmp(bta_val("DEVICE"), "AirPods") && !strcmp(bt_val("STATE"), "ADVERTISING"),
          "... BT AUDIO: STATE CONNECTING, DEVICE AirPods; BT MIDI's STATE its own (ADVERTISING)");
    /* MIDI's SAVE meanwhile: MIDI's list, AUDIO's connect left */
    bt_line("MODE");
    tap(B_SAVE);
    frames(2);
    check(dev_listing() && dev_side() == BT_MIDI && blefk.want && bt_wants(BT_AUD),
          "BT MIDI: SAVE while AUDIO connects: MIDI's own list (BLE's scan), AUDIO's connect kept");
    tap(B_HOME);
    check(!dev_listing() && !blefk.want && bt_wants(BT_AUD), "... HOME: MIDI's scan stops, AUDIO's connect kept");
    /* each side's own timer: AUDIO given up after 30 s, a MIDI connect started later goes on */
    fm1_ms += 20000u;
    bt_want(BT_MIDI, 1);
    fm1_ms += BT_TRY_MS - 20000u + 32u;
    frames(1);
    check(!bt_wants(BT_AUD) && bt_wants(BT_MIDI) && said("NO ANSWER AirPods"),
          "the timers are a side's own: AUDIO's connect given up at 30 s (NO ANSWER AirPods), MIDI's started 20 s later goes on");
    bt_stop(BT_MIDI);
    /* the link up: CONNECTED; SAVE lets go of AUDIO's only (a host on MIDI stays) */
    ble_link = 1;
    bta_line("STATE");
    tap(B_SAVE);                                        /* (the last device: connected to) */
    btafk.st = BTA_S_STREAMING;
    frames(2);
    check(!bt_wants(BT_AUD) && !strcmp(bta_val("STATE"), "CONNECTED") && !strcmp(bt_val("STATE"), "CONNECTED") &&
              !strcmp(bt_val("DEVICE"), "HOST"), "AirPods streaming and a host on BLE MIDI: both pages CONNECTED");
    bta_line("MODE");
    ui.force = 1;
    frames(3);
    while (tf.t)
        frame();
    ppm("opt-bta-page");
    s0 = ble_sets, h0 = btafk.hangups;
    bta_line("DEVICE");
    tap(B_SAVE);
    check(btafk.hangups == h0 + 1u && ble_sets == s0 && ble_link && said("DISCONNECTED AirPods"),
          "BT AUDIO: SAVE while streaming: AUDIO let go (DISCONNECTED AirPods), the MIDI host not touched");
    btafk.st = BTA_S_FAILED;
    /* SAVE held: forgets that page's last device only */
    bta_line("MODE");
    save_held();
    check(!bta.have_last && said("FORGOTTEN AirPods") && ui.scr == SCR_SYSTEM && !dev_listing(),
          "BT AUDIO: SAVE held forgets AirPods (that page's last device)");
#if BLE_CENTRAL
    ble_store_set_last(&ble_store, (const uint8_t[6]){1, 2, 3, 4, 5, 6}, 1, "KeyStep 37", BLE_KIND_MIDI);
    bta_fake_dev(0, 0x11, "AirPods");
    bta_pick(0);
    bta_line("MODE");
    save_held();
    check(!bta.have_last && ble_store_has_last(&ble_store), "... with a MIDI last device too: MIDI's kept");
    bt_line("MODE");
    save_held();
    check(!ble_store_has_last(&ble_store) && said("FORGOTTEN KeyStep 37"), "BT MIDI: SAVE held forgets MIDI's (KeyStep 37)");
#endif
    ble_link = 0;
    s0 = btafk.stops;
    turn(bta_line("MODE"), -1);
    check(bt_mode(BT_AUD) == BT_OFF && btafk.stops == s0 + 1u && ble_on && bt_mode(BT_MIDI) == BT_MAN,
          "BT AUDIO MODE left: OFF, the audio side stopped; BT MIDI still on");
    bta_line("MODE");
    tap(B_SAVE);
    check(said("BT AUDIO OFF") && !dev_listing(), "BT AUDIO OFF: SAVE says so");
    bt_fresh(0);
}
#endif
static void ble_tests(void)
{
    ble_sys_walk("off");
    ble_word_tests();
    ble_mode_tests();
#if BLE_CENTRAL
    ble_save_tests();
    ble_auto_tests();
    ble_list_tests();
    ble_bg_passkey_tests();
#endif
    bt_rec_tests();
#if FELUCCA_BT_AUDIO
    bt_two_pages_tests();
#endif
    ble_on = 0, ble_link = 0, ble_up = 1;
    bmi_w = bmo_w = 0;
    ble_dev_msg = 0;
    reset_ui();
}
#endif
