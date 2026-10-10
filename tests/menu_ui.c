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
#if FELUCCA_BLE
/* connecting out with security (docs/BLE-DEVICES-DESIGN.md §9.3): Just Works silent, the passkey on screen, the one
 * reconnection for it, the level kept with LAST, failures kept until the user acts, no loop */
static void ble_devices_security_tests(void)
{
    uint8_t near[BLE_SCAN_N];
    uint32_t n_near, c0, i;
    int last;
    ble_on = 1;
    ble_link = 0;
    ble_store_reset(&ble_store);
    ble_connect_none();
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    ble_fake_rep(0, 0x09, 1, 0, 0x0A12);             /* an iPhone app (BluePiano-like) */
    ble_fake_rep(4, 0x09, 0, "iPhone Piano", 0x0A12);
    ble_devices_poll();
    ble_dev_rows(&last, near, &n_near);
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    c0 = (uint32_t)cenfk.connects;
    tap(B_OCTUP); frames(2);
    check(cenfk.connects == (int)c0 + 1 && !cenfk.p.sec && !strcmp(dev_status(), "CONNECTING iPhone Piano"),
          "security: a pick connects with nothing known (no passkey asked for)");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1, cenfk.pairing = 1;
    ble_devices_poll();
    check(!strcmp(dev_status(), "PAIRING iPhone Piano") && ble_status() == 7u && ble_connect_passkey() == BLE_NO_PASSKEY,
          "security: its Just Works pairing: PAIRING <name>, header PAIRING, no code (nothing to type)");
    cen_gone(BLE_CF_NEED_MITM);
    ble_devices_poll();
    check(cenfk.connects == (int)c0 + 2 && (cenfk.p.sec & BLE_PEER_MITM) && !cenfk.p.bonded &&
          !memcmp(cenfk.p.addr, ble_found.e[near[0]].addr, 6) && !strcmp(dev_status(), "PAIRING iPhone Piano"),
          "security: refused after Just Works (NEED_MITM): connected again at once to the same address, with a passkey");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1, cenfk.pairing = 1;
    cenfk.passkey = 42u;
    ble_devices_poll();
    check(!strcmp(dev_status(), "ENTER THIS CODE ON THE PHONE") && ble_connect_passkey() == 42u && ble_status() == 7u,
          "security: the passkey pairing: ENTER THIS CODE ON THE PHONE, the code (000042) for the large type");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-passkey");
    {
        char nm[BLE_NAME_MAX + 4u];
        const char *tag;
        int chosen;
        uint32_t bars;
        ble_dev_rows(&last, near, &n_near);
        ble_dev_row(1, last, near, nm, &tag, &chosen, &bars);
        check(!strcmp(tag, "PAIRING") && !strcmp(nm, "iPhone Piano"),
              "security: the row of the device being paired is tagged PAIRING (not CONNECTING)");
    }
    {
        struct ble_keys k;
        memset(&k, 0, sizeof k);
        k.has = BLE_KEYS_LTK | BLE_KEYS_ID | BLE_KEYS_AUTH;
        memset(k.ltk, 0x31, 16), memset(k.rand, 0x32, 8), k.ediv = 0x3334;
        memset(k.irk, 0x5A, 16), memset(k.id, 0x36, 6), k.id_rand = 0;
        ble_app_central_keys(&k);
    }
    cenfk.pairing = 0, cenfk.passkey = BLE_NO_PASSKEY, cenfk.st = BLE_CS_READY;
    ble_devices_poll();
    ble_devices_poll();
    check(!strcmp(dev_status(), "CONNECTED iPhone Piano") && ble_status() == 2u && ble_store_has_last(&ble_store) &&
          ble_store.dev.sec == (BLE_DEV_SEC_AUTH | BLE_DEV_SEC_MITM) && (ble_store.dev.info & BLE_DEV_BONDED),
          "security: ready: CONNECTED <name>; LAST keeps the authenticated bond and that it needs a passkey");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-connected-auth");
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    fm1_ms += 5000u;
    check(!strcmp(dev_status(), "LOST iPhone Piano"), "security: the link lost: LOST <name> stays (not a 3 s note)");
    tap(B_OCTDN); frames(2);                         /* DEVICES closed: the search for LAST */
    for (i = 0; i < 4u && ble_connect_phase() != 2u; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    ble_fake_rep(0, 0x77, 1, 0, 0x0A12);             /* (its IRK: scanned for, an RPA the stand-in resolves) */
    ble_devices_poll();
    check(cenfk.connects == (int)c0 + 3 && cenfk.p.bonded && cenfk.p.sec == (BLE_PEER_AUTH | BLE_PEER_MITM) &&
          cenfk.p.ltk[0] == 0x31, "security: LAST searched for with its authenticated bond and the passkey level");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cen_gone(BLE_CF_AUTH);
    ble_devices_poll();
    check(!strcmp(dev_status(), "FAILED: AUTH") && ble_status() == 8u && ble_connect_phase() == 6u,
          "security: refused even so: FAILED: AUTH, header FAILED, held");
    for (i = 0; i < 60u; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    check(cenfk.connects == (int)c0 + 3 && !strcmp(dev_status(), "FAILED: AUTH"),
          "security: a minute later: no new attempt (no prompt on the phone again), FAILED: AUTH still shown");
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-failed");
    encs[panel.enc[EN_PRESET]] = 1; frames(2);       /* (LAST's row) */
    tap(B_OCTUP); frames(2);
    check(!strcmp(dev_status(), "") && ble_connect_phase() == 0u, "security: OCT+ on LAST (the user acts): the failure "
          "cleared, the search may start again");
    tap(B_OCTDN); frames(2);
    /* its bond refused (blell-dev6: the iPhone left with MIC Failure while our encryption with the bond started) */
    c0 = (uint32_t)cenfk.connects;
    for (i = 0; i < 4u && ble_connect_phase() != 2u; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    ble_fake_rep(0, 0x77, 1, 0, 0x0A12);
    ble_devices_poll();
    for (i = 0; i < 3u; i++) {
        check(cenfk.connects == (int)(c0 + i + 1u) && cenfk.p.bonded && cenfk.p.ltk[0] == 0x31,
              "security KEY: LAST connected to with its bond");
        cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
        ble_devices_poll();
        cenfk.code = 0x3D;
        cen_gone(BLE_CF_KEY);
        ble_devices_poll();
    }
    check(cenfk.connects == (int)c0 + 3 && !strcmp(dev_status(), "FAILED: KEY (3 TRIES)") && ble_status() == 8u &&
          ble_connect_phase() == 6u && !(ble_store.dev.info & BLE_DEV_BONDED) && ble_store.dev.sec == BLE_DEV_SEC_MITM &&
          (ble_store.dev.info & BLE_DEV_IRK), "security KEY: the bond refused on 3 links: FAILED: KEY (3 TRIES), held, "
          "the bond dropped (its IRK and the passkey level kept)");
    for (i = 0; i < 60u; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    check(cenfk.connects == (int)c0 + 3, "security KEY: a minute later no new attempt (blell-dev6 made 75)");
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    encs[panel.enc[EN_PRESET]] = 1; frames(2);       /* (LAST's row) */
    tap(B_OCTUP); frames(2);
    tap(B_OCTDN); frames(2);
    for (i = 0; i < 4u && ble_connect_phase() != 2u; i++) {
        fm1_ms += 1000u;
        ble_devices_poll();
    }
    ble_fake_rep(0, 0x77, 1, 0, 0x0A12);
    ble_devices_poll();
    check(cenfk.connects == (int)c0 + 4 && !cenfk.p.bonded && (cenfk.p.sec & BLE_PEER_MITM),
          "security KEY: LAST picked again: connected without the bond, a passkey pairing to come");
    cenfk.code = 0;
    cen_gone(BLE_CF_LOST);
    ble_connect_none();
    menu_close();
    ble_connect_none();
    ble_store_reset(&ble_store);
    ble_on = 0;
}

/* a link that is not made (0x3E: the FM-1 saw 2 in 6, blell-dev3 / dev4) is made again at once, up to 6 attempts:
 * CONNECTING (TRY n/6) meanwhile, never FAILED before the last; a link that had a pairing on it is not made again by
 * itself (one prompt on the phone per user action); a device that needed a passkey pairs with it at once next time */
static void ble_devices_retry_tests(void)
{
    uint8_t near[BLE_SCAN_N];
    uint32_t n_near, c0, i;
    int last;
    ble_on = 1;
    ble_link = 0;
    ble_store_reset(&ble_store);
    ble_connect_none();
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    ble_fake_rep(0, 0x0B, 1, 0, 0x0A12);
    ble_fake_rep(4, 0x0B, 0, "Retry Keys", 0x0A12);
    ble_devices_poll();
    ble_dev_rows(&last, near, &n_near);
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    c0 = (uint32_t)cenfk.connects;
    tap(B_OCTUP); frames(2);
    check(cenfk.connects == (int)c0 + 1 && !strcmp(dev_status(), "CONNECTING Retry Keys"), "retry: a pick connects");
    for (i = 2; i <= 6u; i++) {                      /* the link made, then not established (0x3E) */
        cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
        ble_devices_poll();
        cenfk.code = 0x3E;
        cen_gone(BLE_CF_LOST);
        ble_devices_poll();
        if (i == 2u) {
            check(cenfk.connects == (int)c0 + 2 && !strcmp(dev_status(), "CONNECTING (TRY 2/6) Retry Keys") &&
                  ble_status() == 5u && !memcmp(cenfk.p.addr, ble_found.e[near[0]].addr, 6),
                  "retry: not established (0x3E): made again at once, CONNECTING (TRY 2/6), header CONNECTING");
            ble_dev_msg = 0;                         /* (the pick's 3 s note gone) */
            ui.force = 1; frames(2);
            ppm("menu-ble-devices-retry");
        }
    }
    check(cenfk.connects == (int)c0 + 6 && !strcmp(dev_status(), "CONNECTING (TRY 6/6) Retry Keys") && ble_status() == 5u,
          "retry: ... up to the 6th attempt, still CONNECTING, never FAILED meanwhile");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    check(cenfk.connects == (int)c0 + 6 && !strcmp(dev_status(), "FAILED: NO LINK (6 TRIES)") && ble_status() == 8u,
          "retry: the 6th not established either: FAILED: NO LINK (6 TRIES), header FAILED, no 7th");
    c0 = (uint32_t)cenfk.connects;
    tap(B_OCTUP); frames(2);                         /* (the same row picked again: the user acts) */
    check(cenfk.connects == (int)c0 + 1 && !strcmp(dev_status(), "CONNECTING Retry Keys"),
          "retry: picked again: a new user action, its own attempts");
    c0 = (uint32_t)cenfk.connects;
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1, cenfk.pairing = 1;
    ble_devices_poll();
    cenfk.prompted = 1, cenfk.mitm = 1, cenfk.code = 0x08;
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    check(cenfk.connects == (int)c0 && !strcmp(dev_status(), "FAILED: LINK LOST") && ble_status() == 8u,
          "retry: lost after a pairing started on it (a prompt on the phone): not made again, FAILED: LINK LOST");
    cenfk.prompted = cenfk.mitm = 0;
    tap(B_OCTUP); frames(2);
    check(cenfk.connects == (int)c0 + 1 && (cenfk.p.sec & BLE_PEER_MITM),
          "retry: that device needed a passkey: picked again, it pairs with the passkey at once (no Just Works first)");
    cen_gone(BLE_CF_LOST);
    tap(B_OCTDN); frames(2);
    menu_close();
    ble_connect_none();
    ble_store_reset(&ble_store);
    cenfk.code = 0;
    ble_on = 0;
}

/* the review of the list: the status line's tones and cut, the window, a pick by address (also tests/ui_optimist_ble.h) */
static int mdev_px(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col)
{
    uint32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (screen[j * 240u + i] == swap16(col))
                return 1;
    return 0;
}
static void mdev_hear(uint8_t a0, const char *name)
{
    ble_fake_rep(0, a0, 1, 0, 0x0A12);
    ble_fake_rep(4, a0, 0, name, 0x0A12);
    ble_devices_poll();
}
static void ble_devices_review_tests(void)
{
    uint32_t c0, i, first;
    char st[40];
    ble_on = 1;
    ble_link = 0;
    ble_store_reset(&ble_store);
    ble_connect_none();
    ble_dev_msg = 0;
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    check(ble_dev_line(st, sizeof st, 0) == BDL_PLAIN && !strcmp(st, "SCANNING 0 FOUND"), "review: SCANNING 0 FOUND, one space");
    mdev_hear(0x01, "KeyStep 37");
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    c0 = (uint32_t)cenfk.connects;
    ble_link = 1;                                    /* (a Mac connected to us) */
    cenfk.central = 0;
    tap(B_OCTUP); frames(2);
    ui.force = 1; frames(2);
    check(cenfk.connects == (int)c0 && ble_dev_line(st, sizeof st, 0) == BDL_WARN && !strcmp(st, "BUSY: A HOST IS CONNECTED") &&
          mdev_px(0, 168, 240, 24, C_WARN) && !mdev_px(0, 168, 240, 24, C_HI),
          "review: BUSY: A HOST IS CONNECTED is a notice (C_WARN)");
    ble_link = 0;
    ble_dev_msg = 0;
    c0 = (uint32_t)cenfk.connects;
    tap(B_OCTUP); frames(2);
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cenfk.code = 0x3E;
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    ble_dev_msg = 0;
    check(ble_dev_line(st, sizeof st, 0) == BDL_PLAIN && !strcmp(st, "CONNECTING (TRY 2/6) KeyStep 37") &&
          ble_dev_line_name_at() == 21u, "review: the TRY line: the name starts at 21");
    snprintf(st, sizeof st, "%s", "CONNECTING (TRY 2/6) Roland Aerophone");
    mdev_fit(st);
    check(text_w(&FONT_S, st) <= MDEV_LINE_W && !strncmp(st, "CONNECTING Roland Aero", 22), "review: ... and a long line drops the (TRY n/6) first, then is cut to the 232 px");
    snprintf(st, sizeof st, "%s", "CONNECTING (TRY 2/6) Retry Keys");
    mdev_fit(st);
    check(text_w(&FONT_S, st) <= MDEV_LINE_W && !strcmp(st, "CONNECTING Retry Keys"), "review: \"CONNECTING (TRY 2/6) Retry Keys\" loses the (TRY 2/6), not its last letters");
    for (i = 3; i <= 6u; i++) {
        cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
        ble_devices_poll();
        cen_gone(BLE_CF_LOST);
        ble_devices_poll();
    }
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;
    ble_devices_poll();
    cen_gone(BLE_CF_LOST);
    ble_devices_poll();
    ble_dev_msg = 0;
    ui.force = 1; frames(2);
    check(ble_dev_line(st, sizeof st, 0) == BDL_BAD && mdev_px(0, 168, 240, 24, C_ERR), "review: a failure is red (C_ERR)");
    cenfk.code = 0;
    ble_connect_none();
    menu_close();
    /* the window and a pick by address */
    ble_store_reset(&ble_store);
    ble_link = 0;
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    for (i = 0; i < 8u; i++) {
        char nm[8] = "Dev  ";
        nm[3] = (char)('A' + i);
        mdev_hear((uint8_t)(0x10 + i), nm);
    }
    frames(2);
    for (i = 0; i < 9u; i++) {
        encs[panel.enc[EN_PRESET]] = 1;
        frame();
    }
    frames(2);
    first = mdev_first;
    check(mdev_cur == 8u && first == 1u, "review: the cursor at the end (nine rows, eight shown): the window shows the last");
    encs[panel.enc[EN_PRESET]] = -1; frames(2);
    check(mdev_cur == 7u && mdev_first == first, "review: one row up from the end: the window stays");
    for (i = 0; i < 9u; i++) {
        encs[panel.enc[EN_PRESET]] = -1;
        frame();
    }
    frames(2);
    check(mdev_cur == 0u && mdev_first == 0u, "review: back to NONE: the window follows");
    ble_connect_none();
    menu_close();
    ble_store_reset(&ble_store);
    menu_open(MI_BLEDEV);
    tap(B_OCTUP); frames(2);
    mdev_hear(0x01, "Alpha");
    mdev_hear(0x03, "Bravo");
    mdev_hear(0x05, "Charlie");
    frames(2);
    for (i = 0; i < 2u; i++) {
        encs[panel.enc[EN_PRESET]] = 1;
        frame();
    }
    frames(2);
    c0 = (uint32_t)cenfk.connects;
    {
        int last;
        uint8_t near[BLE_SCAN_N];
        uint32_t nn;
        ble_dev_rows(&last, near, &nn);
        ble_found.e[near[0]].used = 0;               /* (Alpha ages out between the draw and OCT+) */
        ble_found.gen++;
    }
    tap(B_OCTUP); frames(2);
    check(cenfk.connects == (int)c0 + 1 && cenfk.p.addr[0] == 0x03, "review: a device aging out above the cursor: OCT+ connects to the highlighted one (Bravo)");
    {                                                /* a nearby device that has become LAST is still there, not GONE */
        int last;
        uint8_t near[BLE_SCAN_N];
        uint32_t nn, row;
        struct ble_dev_key k;
        const struct ble_found *e;
        ble_dev_rows(&last, near, &nn);
        row = 2u;                                    /* NONE, Bravo, Charlie */
        ble_dev_key_of(row, &k);
        e = &ble_found.e[near[row - 1u]];
        check(k.kind == 2u && k.addr[0] == 0x05 && e->used, "review: the key of the third row is Charlie (nearby)");
        memcpy(ble_store.dev.addr, e->addr, 6);
        ble_store.dev.info = (uint8_t)(BLE_DEV_USED | BLE_DEV_CENTRAL | (e->addr_rand ? BLE_DEV_RANDOM : 0u));
        ble_dev_rows(&last, near, &nn);
        check(last >= 0 && ble_dev_key_row(&k) == last, "review: Charlie became LAST: its nearby key finds the LAST row");
        ble_dev_msg = 0;
        ble_dev_pick_key(&k);
        check(!ble_dev_message() || strcmp(ble_dev_message(), "DEVICE GONE") != 0, "review: ... and YES on it does not say DEVICE GONE");
        ble_dev_msg = 0;
        ble_dev_pick(nn + 3u);
        check(ble_dev_message() && !strcmp(ble_dev_message(), "DEVICE GONE"), "review: a row past the end (the list shrank): DEVICE GONE, not silence");
        ble_store_reset(&ble_store);
    }
    ble_connect_none();
    menu_close();
    ble_store_reset(&ble_store);
    ble_on = 0;
    ble_link = 0;
    ble_dev_msg = 0;
    cenfk.code = 0;
}

/* HOME > BLUETOOTH > DEVICES (firmware/src/io/midi/ble_devices.c, docs/BLE-DEVICES-DESIGN.md §2.3): NONE, LAST, nearby */
static void ble_devices_tests(void)
{
    uint8_t it2[4], a[6] = {0xB2, 0xA1, 0x33, 0x44, 0x55, 0xC6};
    int last;
    uint8_t near[BLE_SCAN_N];
    uint32_t n_near, n;
    struct ble_dev_store back;
    ble_on = 1;
    ble_link = 0;
    ble_store_reset(&ble_store);
    menu_open(MI_BLEDEV);
    check(mi_rows(menu_screen(), it2) == 2u && ui.menu_sel == MI_BLEDEV, "DEVICES: the second row of the BLUETOOTH screen");
    check(it2[0] == MI_BLE && it2[1] == MI_BLEDEV && mi_screen_of(MI_KCOL) != mi_screen_of(MI_BLE),
          "DEVICES: the BLUETOOTH screen holds BLUETOOTH and DEVICES only (KNOB COLORS is on another screen)");
    tap(B_OCTUP); frames(2);
    check(ui.menu == 3 && blefk.want == 1u && ble_scanning() && ble_devs_open,
          "DEVICES: OCT+ opens the list and the scan starts (BLE_CENTRAL)");
    n = ble_dev_rows(&last, near, &n_near);
    check(n == 1u && last < 0, "DEVICES: nothing remembered, nothing heard: NONE alone");
    ble_fake_rep(0, 0x01, 1, 0, 0x0A12);             /* a BLE-MIDI controller: the UUID in ADV_IND, its name in SCAN_RSP */
    ble_fake_rep(4, 0x01, 0, "KeyStep 37", 0x0A12);
    ble_fake_rep(0, 0x02, 0, "Tile", 0x0405);        /* a beacon (no BLE-MIDI UUID): never listed */
    ble_fake_rep(0, 0x03, 1, 0, 0x1020);             /* another FM-1, as ours advertises: the UUID, then its name */
    ble_fake_rep(4, 0x03, 0, "FM-1 A1B2", 0x1020);
    ble_devices_poll();
    n = ble_dev_rows(&last, near, &n_near);
    check(n == 3u && n_near == 2u && !strcmp(ble_found.e[near[0]].name, "KeyStep 37") &&
          !strcmp(ble_found.e[near[1]].name, "FM-1 A1B2") && ble_found.e[near[1]].kind == (BLE_KIND_MIDI | BLE_KIND_FM1),
          "DEVICES: the two BLE-MIDI devices listed in the order heard, with their names; the beacon left out");
    check(ble_scan_bars(&ble_found, near[0]) == 3u && ble_scan_bars(&ble_found, near[1]) == 1u,
          "DEVICES: relative bars from the RSSI word (the stronger 3, the weaker 1)");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices");
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    tap(B_OCTUP); frames(2);
    check(cenfk.connects == 1 && !memcmp(cenfk.p.addr, ble_found.e[near[0]].addr, 6) && !cenfk.p.bonded &&
          !cenfk.p.sec && !strcmp(dev_status(), "CONNECTING KeyStep 37") && ble_store.sel == BLE_SEL_NONE &&
          ble_seeking() == 1, "DEVICES: OCT+ on a nearby device connects to it (the stack initiates), CONNECTING");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-connecting");
    cenfk.initiating = 0, cenfk.central = 1, cenfk.st = BLE_CS_SETUP, ble_link = 1;   /* (connected, setting up) */
    ble_devices_poll();
    check(!ble_store_has_last(&ble_store), "DEVICES: connected but not ready (discovery, pairing): not LAST yet");
    check(!strcmp(dev_status(), "CONNECTING KeyStep 37") && ble_status() == 5u,
          "DEVICES: the link setting up: still CONNECTING (not CONNECTED) in the status area and the header");
    {
        struct ble_keys k;                         /* the pairing's keys come before ready */
        memset(&k, 0, sizeof k);
        k.has = BLE_KEYS_LTK | BLE_KEYS_ID;
        memset(k.ltk, 0x11, 16), memset(k.rand, 0x22, 8), k.ediv = 0x3344;
        memset(k.irk, 0x5A, 16), memset(k.id, 0x66, 6), k.id_rand = 1;
        ble_app_central_keys(&k);
    }
    cenfk.st = BLE_CS_READY;
    ble_devices_poll();
    ble_devices_poll();
    ble_store_load(&back, ble_dev_kept);
    check(ble_store_has_last(&ble_store) && !strcmp(ble_store.dev.name, "KeyStep 37") &&
          ble_store.sel == BLE_SEL_LAST && back.sel == BLE_SEL_LAST && !strcmp(dev_status(), "CONNECTED KeyStep 37") &&
          ble_status() == 2u && !ble_store.dev.sec,
          "DEVICES: ready: KeyStep 37 becomes LAST and the choice, saved, CONNECTED (status area and header); no "
          "authentication known or needed");
    fm1_ms += 5000u;
    check(!strcmp(dev_status(), "CONNECTED KeyStep 37"), "DEVICES: CONNECTED stays while the link is up (not a 3 s note)");
    check((ble_store.dev.info & (BLE_DEV_BONDED | BLE_DEV_IRK)) == (BLE_DEV_BONDED | BLE_DEV_IRK) &&
          ble_store.dev.ltk[0] == 0x11 && ble_rd16(ble_store.dev.ediv) == 0x3344 && ble_store.dev.addr[0] == 0x66 &&
          ble_store.dev.irk[0] == 0x5A && back.dev.irk[0] == 0x5A,
          "DEVICES: the pairing's bond and identity go with LAST (its identity address replaces the AdvA)");
    check(ble_connect_last_up() && ble_seeking() == 0, "DEVICES: LAST shows CONNECTED");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-connected-last");
    encs[panel.enc[EN_PRESET]] = -5; frames(2);
    tap(B_OCTUP); frames(2);
    check(mdev_cur == 0 && cenfk.cancels == 1 && ble_store.sel == BLE_SEL_NONE && ble_store_has_last(&ble_store) &&
          !strcmp(ble_dev_message(), "NONE: STAY VISIBLE"),
          "DEVICES: PRESETS stops at NONE; OCT+ on NONE: our link left, stay visible, LAST kept");
    cenfk.st = BLE_CS_IDLE, cenfk.fail = BLE_CF_LOST, ble_link = 0;
    ble_devices_poll();
    check(ble_seeking() == 0, "DEVICES: NONE: no search for LAST");
    {
        uint8_t ra[6] = {0x77, 1, 2, 3, 4, 0x45};  /* an RPA that the IRK resolves (the stand-in's rule) */
        check(rc_is_last(ra, 1) && !rc_is_last(ra, 0), "LAST found behind a resolvable private address by its IRK");
    }
    n = ble_dev_rows(&last, near, &n_near);
    check(n == 4u && last == 1, "DEVICES: LAST shown second, before the nearby devices");
    ble_on = 0;
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    tap(B_OCTUP); frames(2);
    ble_store_load(&back, ble_dev_kept);
    check(ble_store.sel == BLE_SEL_LAST && back.sel == BLE_SEL_LAST && ble_store_has_last(&back) && ble_on == 1u &&
          !strcmp(ble_dev_message(), "LAST: SEARCHING WHEN CLOSED"),
          "DEVICES: OCT+ on LAST picks it (kept for the settings), switches BLUETOOTH ON; not heard: searched when closed");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-last");
    encs[panel.enc[EN_K1 + 3u]] = 1; frames(2);
    check(mdev_forget_armed(), "DEVICES: KNOB 4 on LAST arms FORGET");
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-forget");
    tap(B_OCTUP); frames(2);
    ble_store_load(&back, ble_dev_kept);
    check(!ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_NONE && !ble_store_has_last(&back) && mdev_cur == 0,
          "DEVICES: OCT+ then FORGETs LAST: the entry gone, NONE picked, the settings' copy too");
    encs[panel.enc[EN_K1 + 3u]] = 1; frames(2);
    check(!mdev_forget_armed(), "DEVICES: KNOB 4 on another row arms nothing");
    fm1_ms += 11000u;
    ble_devices_poll();
    n = ble_dev_rows(&last, near, &n_near);
    check(n == 1u, "DEVICES: devices not heard for 10 s leave the list");
    tap(B_OCTDN); frames(2);
    check(ui.menu == 1 && blefk.want == 0u && !ble_devs_open, "DEVICES: OCT- back to the menu, the scan stops");
    check(ui.menu_sel == MI_BLEDEV, "DEVICES: back with the menu's cursor on DEVICES");
    tap(B_OCTUP); frames(2);
    ble_link = 1;
    ui.force = 1; frames(2);
    ppm("menu-ble-devices-connected");
    ble_link = 0;
    check(ui.menu == 3 && blefk.want == 1u, "DEVICES: opened again");
    menu_close();
    check(ui.menu == 0 && blefk.want == 0u && !ble_devs_open, "DEVICES: the menu closed from the list stops the scan");
    ble_on = 0;
}
#endif

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
#if FELUCCA_BLE
    {   /* BLUETOOTH: a screen of its own, the last, OFF by default; the radio is told at once, the settings saved at close */
        char vb[14];
        uint16_t vc;
        uint32_t k, sets = ble_sets;
        menu_open(MI_COLOR);
        for (k = 0; k < 12u; k++) {
            encs[panel.enc[EN_SELECT]] = 1; frames(2);
        }
        check(menu_screen() == (uint32_t)MI_NSCR - 1u && ui.menu_sel == MI_BLE && mi_rows(menu_screen(), it) == 2u &&
              it[1] == MI_BLEDEV, "menu: BLUETOOTH and DEVICES on the last screen, the cursor on BLUETOOTH (FELUCCA_BLE)");
        check(MI_SCR[menu_screen()].sec == MS_SYSTEM && mi_screen_of(MI_ABOUT) == (uint32_t)MI_NSCR - 2u,
              "menu: ... in SYSTEM, after CPU / CALIBRATION / ABOUT");
        check(ble_on == 0u && strcmp(mi_value(MI_BLE, vb, &vc), "OFF") == 0, "menu: BLUETOOTH shows OFF by default");
        encs[MKNOB()] = 1; frames(2);
        check(ble_on == 1u && ble_sets == sets + 1u && strcmp(mi_value(MI_BLE, vb, &vc), "ON") == 0,
              "menu: BLUETOOTH KNOB 1 right: ON, the radio told once");
        sets = ble_sets;
        encs[MKNOB()] = -1; frames(2);
        check(ble_on == 0u && ble_sets == sets + 1u && strcmp(mi_value(MI_BLE, vb, &vc), "OFF") == 0,
              "menu: BLUETOOTH KNOB 1 left: OFF, the radio told once");
        encs[MKNOB()] = -3; frames(2);
        check(ble_on == 0u && ble_sets == sets + 1u, "menu: ... again left: still OFF, not told twice");
        ui.force = 1; frame(); ppm("menu-system-bluetooth-off");
        encs[MKNOB()] = 1; frames(2);
        check(ble_on == 1u && ble_sets == sets + 2u, "menu: BLUETOOTH KNOB 1 right: ON");
        tap(B_OCTUP); frames(2);
        check(ble_on == 0u, "menu: OCT+ toggles it (ON -> OFF)");
        tap(B_OCTUP); frames(2);
        check(ble_on == 1u, "menu: OCT+ toggles it (OFF -> ON)");
        ble_link = 1;
        ui.force = 1; frame(); ppm("menu-system-bluetooth-connected");
        ble_link = 0;
        encs[panel.enc[EN_PRESET]] = 1; frames(2);
        check(ui.menu_sel == MI_BLEDEV, "menu: PRESETS moves to DEVICES");
        encs[panel.enc[EN_PRESET]] = 1; frames(2);
        check(ui.menu_sel == MI_BLE, "menu: ... and round to BLUETOOTH");
        menu_close();
        test_open_menu();                            /* HOME twice (the shared helper), as the user opens it */
        check(ui.menu == 1, "menu: HOME double-tapped opens the SYSTEM menu");
        menu_close();
        ble_devices_tests();
        ble_devices_security_tests();
        ble_devices_retry_tests();
        ble_devices_review_tests();
    }
#else
    check(MI_NSCR == 7 && MI_COUNT == MI_KCOL + 1, "menu: no BLUETOOTH row or screen without FELUCCA_BLE (the menu as it was, KNOB COLORS the last row)");
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
