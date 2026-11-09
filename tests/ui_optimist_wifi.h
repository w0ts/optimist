/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c with FELUCCA_WIFI=1 (and BLE, CDC, BLE_HW_WL82): SYSTEM > WI-FI (ui/optimist/op_wifi.c: MODE,
 * NETWORK, SIGNAL, STATE; the list of networks heard; SAVE joins / lets go, SAVE held forgets; a failure said once) and the
 * password entry (KNOB 1 the character, KNOB 2 the family, KNOB 3 the cursor; SAVE adds, SAVE held joins; HOME erases, HOME
 * held cancels). The manager is the stand-in in wifi_ui_stubs.h. Included after the helpers. */
#ifndef FELUCCA_WIFI_AP
#define FELUCCA_WIFI_AP 0                                /* (this variant has no MODE AP) */
#endif
#if WIFI_UI
static void wf_ap(const char *ssid, uint8_t ch, int8_t rssi, uint8_t sec, uint8_t id)
{
    struct wn_ap a;
    memset(&a, 0, sizeof a);
    a.bssid[0] = 2, a.bssid[5] = id, a.ch = ch, a.rssi = rssi, a.sec = sec;
    a.ssid_len = (uint8_t)strlen(ssid);
    memcpy(a.ssid, ssid, a.ssid_len);
    wn_scan_add(&wc_tab, &a);
}
static void wf_saved(const char *ssid)
{
    uint8_t pmk[32];
    memset(pmk, 7, sizeof pmk);
    wn_put(&wn_st, (const uint8_t *)ssid, (uint32_t)strlen(ssid), pmk);
}
static void wf_fresh(uint32_t mode)                     /* nothing heard, nothing saved, the radio up and idle, the page on screen */
{
    uint32_t i, n;
    char nm[16];
    reset_ui();
    if (pw_on())
        pw_close();
    memset(&wc, 0, sizeof wc);
    memset(&wc_req, 0, sizeof wc_req);
    memset(&wl, 0, sizeof wl);
    memset(&wfs, 0, sizeof wfs);
    memset(&wcfk, 0, sizeof wcfk);
    wn_reset(&wn_st);
    wn_st.mode = (uint8_t)mode;
    wn_scan_clear(&wc_tab);
    wc.st = WC_IDLE;
    op_enter(SCR_SYSTEM);
    n = gl_n(1);
    for (i = 0; i < n; i++) {
        gl_sect(1, i, nm);
        if (!strcmp(nm, "WI-FI"))
            break;
    }
    ui.row[SCR_SYSTEM] = (uint8_t)i;
    ui.hot = 0;
    ui.force = 1;
    frames(2);
}
static const char *wf_val(const char *lab)              /* a line of the WI-FI page: its value as shown */
{
    static cell_t c;
    uint32_t i, f = gl_first(1, ui.row[SCR_SYSTEM]), e = gl_end(1, ui.row[SCR_SYSTEM]);
    for (i = f; i < e; i++) {
        gl_cell(1, i, &c);
        if (c.label && !strcmp(c.label, lab))
            return c.val;
    }
    return "?";
}
static uint32_t wf_knob(const char *lab)                /* the knob of a line of the WI-FI page */
{
    static cell_t c;
    uint32_t i, f = gl_first(1, ui.row[SCR_SYSTEM]), e = gl_end(1, ui.row[SCR_SYSTEM]);
    for (i = f; i < e; i++) {
        gl_cell(1, i, &c);
        if (c.label && !strcmp(c.label, lab))
            return EN_K1 + (i - f);
    }
    return EN_K1;
}
static void wf_hold(uint32_t b, uint32_t ms)            /* a button down this long */
{
    press(b);
    frames(ms / 16u + 2u);
    release(b);
}
static int wf_said(const char *s) { return (ui.msg_t || ui.toast_t) && !strcmp(ui.msg, s); }   /* (in the header, or a toast: what a closed entry says) */
static int wf_zero(const void *p, uint32_t n)           /* all of the n bytes are 0 */
{
    const uint8_t *b = p;
    while (n--)
        if (*b++)
            return 0;
    return 1;
}
static void wf_ui_tests(void)
{
    char nm[48];
    uint32_t i, k;
    check(WIFI_UI == 1, "WI-FI: the page is built (WIFI_UI)");
    /* the page, MODE OFF */
    wf_fresh(WN_MODE_OFF);
    check(!strcmp(wf_val("MODE"), "OFF") && !strcmp(wf_val("STATE"), "OFF") && !strcmp(wf_val("NETWORK"), "NONE") &&
              !strcmp(wf_val("SIGNAL"), "--"),
          "WI-FI OFF: MODE OFF, STATE OFF, NETWORK NONE, SIGNAL --");
    check(!wc.scan_want && !wf_pg_shown(), "... no scan wanted, no list");
    tap(B_SAVE);
    check(wf_said("WI-FI OFF") && !wcfk.requests, "... SAVE: WI-FI OFF, nothing asked of the manager");
    /* MODE: OFF -> MAN -> AUTO, stopping at the ends; kept for the settings record */
    k = wf_knob("MODE");
    settings_later = 0;
    turn(k, 1);
    check(wn_st.mode == WN_MODE_MAN && !strcmp(wf_val("MODE"), "MAN") && settings_later, "MODE right: MAN, saved with the settings");
    turn(k, 1);
    check(wn_st.mode == WN_MODE_AUTO && !strcmp(wf_val("MODE"), "AUTO"), "... right again: AUTO");
    turn(k, 1);
#if FELUCCA_WIFI_AP
    check(wn_st.mode == WN_MODE_AP_MAN && !strcmp(wf_val("MODE"), "AP MAN"), "... right again: AP MAN");
    turn(k, 1);
    check(wn_st.mode == WN_MODE_AP && !strcmp(wf_val("MODE"), "AP AUTO"), "... and AP AUTO");
    turn(k, 1);
    check(wn_st.mode == WN_MODE_AP, "... stops at AP AUTO");
    turn(k, -1);
    turn(k, -1);
    check(wn_st.mode == WN_MODE_AUTO, "... left twice: AUTO");
#else
    check(wn_st.mode == WN_MODE_AUTO, "... stops at AUTO");
#endif
    turn(k, -1);
    turn(k, -1);
    check(wn_st.mode == WN_MODE_OFF && wcfk.enable_off == (FELUCCA_WIFI_AP ? 4 : 1), "... left twice: OFF, the manager told to stop (once; AP told it too)");
    turn(k, 1);
    frames(2);
    check(wc.scan_want && wf_pg_shown(), "MAN, the page on screen, nothing joined: the manager scans, the list is shown");
    /* the list: saved first (green), new WPA2-PSK (amber), the others (grey), then the saved not heard */
    wf_saved("Studio");
    wf_saved("Home");
    wf_ap("Cafe", 6, -60, WN_SEC_PSK, 1);
    wf_ap("Guest", 11, -55, WN_SEC_OPEN, 2);
    wf_ap("Studio", 1, -68, WN_SEC_PSK, 3);
    wf_ap("Fancy", 3, -70, WN_SEC_OTHER, 4);
    wf_ap("Studio", 11, -80, WN_SEC_PSK, 5);
    ui.force = 1;
    frames(2);
    wf_build();
    check(wfs.n == 5, "the list: five access points heard (two of one name, one row: its strongest) and one saved network not heard = five rows");
    {
        static const char *const W[5] = {"Studio", "Cafe", "Guest", "Fancy", "Home"};
        static const uint8_t C[5] = {WFC_SAVED, WFC_NEW, WFC_NO, WFC_NO, WFC_AWAY};
        int ok = 1;
        for (i = 0; i < 5u; i++) {
            wf_row_name(nm, i);
            ok = ok && !strcmp(nm, W[i]) && wfs.r[i].cls == C[i];
        }
        check(ok, "... saved first (Studio once), the new WPA2-PSK one, the open and the other, the saved not heard last");
        check(!strcmp(wf_tag(0), "SAVED") && !strcmp(wf_tag(1), "") && !strcmp(wf_tag(4), "NOT IN RANGE"),
              "... tags: SAVED, none for a new one, NOT IN RANGE");
    }
    check(!strcmp(wf_val("NETWORK"), "Studio") && !strcmp(wf_val("SIGNAL"), "-68 dBm  CH 1"), "NETWORK / SIGNAL: the cursor row, its access point");
    k = wf_knob("NETWORK");
    turn(k, 1);
    check(wfs.cur == 1u && !strcmp(wf_val("SIGNAL"), "-60 dBm  CH 6"), "NETWORK's knob: the next row (Cafe)");
    turn(k, 9);
    check(wfs.cur == 4u && !strcmp(wf_val("NETWORK"), "Home") && !strcmp(wf_val("SIGNAL"), "NOT IN RANGE"), "... stops at the last row (Home, not heard)");
    turn(k, -20);
    check(wfs.cur == 0u, "... and at the first");
    ui.force = 1;
    frames(3);
    ppm("opt-wifi-list");
    /* a new access point heard above the cursor's row: the cursor stays on its network */
    turn(k, 1);
    wf_ap("Loud", 9, -30, WN_SEC_PSK, 6);
    wf_build();
    wf_row_name(nm, wfs.cur);
    check(!strcmp(nm, "Cafe"), "a stronger network heard meanwhile: the cursor stays on Cafe");
    /* SAVE on each kind of row */
    wfs.cur = 0;
    wf_key_set();
    tap(B_SAVE);
    check(wcfk.requests == 1 && wcfk.len == 6 && !memcmp(wcfk.ssid, "Studio", 6) && !wcfk.had_pass && wf_said("CONNECTING Studio"),
          "SAVE on a saved network: asked with its saved key (no password): CONNECTING Studio");
    wf_fresh(WN_MODE_MAN);
    wf_saved("Home");
    wf_ap("Guest", 11, -55, WN_SEC_OPEN, 2);
    frames(2);
    wf_build();
    wfs.cur = 0;
    wf_key_set();
    tap(B_SAVE);
    check(wf_said("NOT SUPPORTED") && !wcfk.requests && !pw_on(), "SAVE on an open network: NOT SUPPORTED, nothing asked");
    wfs.cur = 1;
    wf_key_set();
    tap(B_SAVE);
    check(wf_said("NOT IN RANGE") && !wcfk.requests, "SAVE on a saved network not heard: NOT IN RANGE");
    wf_fresh(WN_MODE_MAN);
    tap(B_SAVE);
    check(wf_said("NONE FOUND"), "SAVE with nothing heard: NONE FOUND");
    /* SAVE held: forget */
    wf_saved("Studio");
    wf_ap("Studio", 1, -68, WN_SEC_PSK, 3);
    wf_ap("Cafe", 6, -60, WN_SEC_PSK, 1);
    frames(2);
    wf_build();
    wfs.cur = 1;
    wf_key_set();
    wf_hold(B_SAVE, 400);
    check(wf_said("NOT REMEMBERED") && wn_find(&wn_st, (const uint8_t *)"Studio", 6) >= 0, "SAVE held on a new network: NOT REMEMBERED");
    wfs.cur = 0;
    wf_key_set();
    settings_later = 0;
    wf_hold(B_SAVE, 400);
    check(op_armed() && !strcmp(ui.arm_q, "FORGET Studio?") && wn_find(&wn_st, (const uint8_t *)"Studio", 6) >= 0 && !settings_later,
          "SAVE held on a saved network: asks FORGET Studio? first, nothing forgotten yet");
    op_no();
    check(!op_armed() && wn_find(&wn_st, (const uint8_t *)"Studio", 6) >= 0 && !settings_later, "... NO: kept");
    wf_hold(B_SAVE, 400);
    tap(B_SAVE);
    check(wf_said("FORGOTTEN Studio") && wn_find(&wn_st, (const uint8_t *)"Studio", 6) < 0 && settings_later,
          "... asked again, SAVE: FORGOTTEN Studio, kept in the settings");
    /* a failure said once; a join completed said once */
    wf_fresh(WN_MODE_MAN);
    wc.fail = WCF_NOAP, wc.fail_new = 1;
    frames(2);
    check(wf_said("NO ACCESS POINT") && !wc.fail_new, "a join failed (NO AP), the page on screen: said once, the flag cleared");
    ui.msg_t = 0;
    frames(2);
    check(!ui.msg_t, "... not said again");
    {
        static const struct { uint8_t f; const char *s; } W[] = {{WCF_AUTH, "AUTH FAILED"}, {WCF_ASSOC, "ASSOC FAILED"}, {WCF_HS, "HANDSHAKE FAILED"},
                                                                {WCF_KEY, "WRONG PASSWORD"}, {WCF_NOCAL, "NO RADIO CAL"}};
        int ok = 1;
        for (i = 0; i < 5u; i++) {
            ui.msg_t = 0;
            wc.fail = W[i].f, wc.fail_new = 1;
            frames(2);
            ok = ok && wf_said(W[i].s);
        }
        check(ok, "... AUTH FAILED, ASSOC FAILED, HANDSHAKE FAILED, WRONG PASSWORD, NO RADIO CAL");
    }
    wf_fresh(WN_MODE_AUTO);
    go_home();
    frames(2);
    ui.msg_t = 0;
    wc.fail = WCF_NOAP, wc.fail_new = 1;
    frames(2);
    check(!ui.msg_t && !wc.fail_new && !wc.scan_want, "AUTO's own try failing with another screen up: silent (the flag cleared), no scan wanted off the page");
    wf_fresh(WN_MODE_MAN);
    wc.st = WC_FAIL, wc.fail = WCF_NOCAL;
    frames(2);
    check(!strcmp(wf_val("STATE"), "NO CAL") && !strcmp(wf_val("MODE"), "NO CAL") && !wc.scan_want && !wf_pg_shown(),
          "NO CAL: STATE and MODE say so, no scan wanted (the radio is not up), no list");
    tap(B_SAVE);
    check(wf_said("NO RADIO CAL"), "... SAVE: NO RADIO CAL");
    /* joined: the network, the address; SAVE lets go; SAVE held lets go and forgets */
    wf_fresh(WN_MODE_MAN);
    wf_saved("Studio");
    memcpy(wc.ap.ssid, "Studio", 6), wc.ap.ssid_len = 6;
    wc.st = WC_UP;
    wl.joined = 1;
    wl.ip.ip = (192u << 24) | (168u << 16) | (100u << 8) | 123u;
    wc.saved_join = 1;                                  /* (joined with the saved key) */
    ui.msg_t = 0;
    frames(2);
    check(wf_said("CONNECTED Studio"), "joined: CONNECTED Studio, said once");
    wfs.was_up = 0;                                     /* (a join with a typed password: the manager saved the network) */
    wc.saved_join = 0;
    ui.msg_t = 0;
    frames(2);
    check(wf_said("SAVED + CONNECTED Studio"), "joined with a typed password: SAVED + CONNECTED Studio");
    check(!strcmp(wf_val("STATE"), FELUCCA_LINK ? "0 LINK PEERS" : "CONNECTED") && !strcmp(wf_val("NETWORK"), "Studio") && !strcmp(wf_val("SIGNAL"), "192.168.100.123") &&
              !wc.scan_want && !wf_pg_shown(),
          "... STATE CONNECTED (with Link: 0 LINK PEERS), NETWORK the name, SIGNAL the address, no scan, no list");
    ui.force = 1;
    frames(3);
    ppm("opt-wifi-joined");
    ui.msg_t = 0;
    frames(3);
    check(!ui.msg_t, "... the message not said again");
    test_peers = 2;
    check(!strcmp(wf_val("STATE"), "2 LINK PEERS"), "... Link peers heard on the network: 2 LINK PEERS");
#if FELUCCA_LINK
    link_mode = LINK_OFF;
    check(!strcmp(wf_val("STATE"), "LINK OFF"), "... GLOBAL > MIDI LINK OFF: LINK OFF (no session, no peer)");
    link_mode = LINK_ONSS;
#endif
    test_peers = 0;
    wl.ip.ip = 0;
    check(!strcmp(wf_val("SIGNAL"), "GETTING IP"), "... no address yet: GETTING IP");
    tap(B_SAVE);
    check(wcfk.disconnects == 1 && wf_said("DISCONNECTED Studio"), "SAVE while joined: let go (DISCONNECTED Studio)");
    wc.st = WC_UP;
    wf_hold(B_SAVE, 400);
    check(op_armed() && wcfk.disconnects == 1 && wn_find(&wn_st, (const uint8_t *)"Studio", 6) >= 0,
          "SAVE held while joined: asks FORGET first, still joined and saved");
    tap(B_SAVE);
    check(wcfk.disconnects == 2 && wf_said("FORGOTTEN Studio") && wn_find(&wn_st, (const uint8_t *)"Studio", 6) < 0,
          "... SAVE: let go and forgotten");
    wc.st = WC_PMK;
    wf_ap("x", 1, -60, WN_SEC_PSK, 9);
    frames(2);
    check(!strcmp(wf_val("STATE"), "CONNECTING") && !wf_pg_shown() && !wc.scan_want, "joining: STATE CONNECTING, no list, no scan");
}
/* ---- the password entry */
static void wf_pw_open(const char *ssid)                /* a new WPA2-PSK network under the cursor, SAVE tapped */
{
    wf_fresh(WN_MODE_MAN);
    wf_ap(ssid, 6, -60, WN_SEC_PSK, 1);
    frames(2);
    tap(B_SAVE);
}
static void wf_pw_type(const char *s)                   /* each character: KNOB 2 the family, KNOB 1 the character, SAVE tapped */
{
    for (; *s; s++) {
        uint32_t f = *s >= 'a' && *s <= 'z' ? 0u : *s >= 'A' && *s <= 'Z' ? 1u : *s >= '0' && *s <= '9' ? 2u : 3u, n = 0, guard = 0;
        const char *fam = PW_FAM[f];
        while (pws.b.fam != f && guard++ < 16u)
            turn(EN_K2, 1);
        while (fam[n] != *s)
            n++;
        while (pws.b.idx[f] != n && guard++ < 200u)
            turn(EN_K1, 1);
        check(pws.b.fam == f && pws.b.idx[f] == n, "the password entry takes the knobs while a character is typed");
        if (guard >= 200u)
            return;
        tap(B_SAVE);
    }
}
static void wf_pw_tests(void)
{
    uint32_t i;
    wf_pw_open("Cafe");
    check(pw_on() && name_on() && wcfk.requests == 0, "SAVE on a new WPA2-PSK network: the password entry opens");
    check(ui.scr == SCR_SYSTEM && !pws.b.len && pws.b.fam == 0 && pw_sel(&pws.b) == 'a', "... empty, family abc, the character a");
    frames(2);
    ppm("opt-wifi-pw-empty");
    /* KNOB 1: the character, round; KNOB 2: the family */
    turn(EN_K1, 3);
    check(pw_sel(&pws.b) == 'd', "KNOB 1: the character (d)");
    turn(EN_K1, -4);
    check(pw_sel(&pws.b) == 'z', "... round the family (a back is z)");
    turn(EN_K2, 1);
    check(pws.b.fam == 1 && pw_sel(&pws.b) == 'A', "KNOB 2: the next family, UPPER CASE (its own character kept: A)");
    turn(EN_K2, 1);
    turn(EN_K2, 1);
    check(pws.b.fam == 3 && pw_sel(&pws.b) == '!', "... digits, then signs");
    turn(EN_K2, 1);
    check(pws.b.fam == 0 && pw_sel(&pws.b) == 'z', "... round to the first, where the character was left (z)");
    /* SAVE adds, HOME erases, KNOB 3 the cursor */
    tap(B_SAVE);
    turn(EN_K1, 1);
    tap(B_SAVE);
    check(pws.b.len == 2 && !strcmp(pws.b.s, "za") && pws.b.cur == 2, "SAVE tapped adds the character at the cursor (z, a)");
    turn(EN_K3, -1);
    turn(EN_K2, 1);
    tap(B_SAVE);
    check(!strcmp(pws.b.s, "zAa") && pws.b.cur == 2, "KNOB 3 moves the cursor; an add goes in before it (A between z and a)");
    turn(EN_K3, -9);
    check(pws.b.cur == 0, "... stops at the start");
    turn(EN_K3, 9);
    check(pws.b.cur == 3, "... and at the end");
    tap(B_HOME);
    check(!strcmp(pws.b.s, "zA") && pws.b.cur == 2 && pw_on(), "HOME tapped erases the character before the cursor");
    tap(B_HOME);
    tap(B_HOME);
    tap(B_HOME);
    check(!pws.b.len && pw_on() && wcfk.requests == 0, "... until empty; HOME tapped on nothing does not leave");
    /* too short */
    wf_pw_type("seven77");
    wf_hold(B_SAVE, 500);
    check(wf_said("TOO SHORT") && pw_on() && !wcfk.requests, "SAVE held under 8 characters: TOO SHORT, the entry stays");
    check(pws.b.len == 7, "... and the release added nothing");
    /* done */
    wf_pw_type("!");
    check(pws.b.len == 8 && !strcmp(pws.b.s, "seven77!"), "typing across the families: lower case, digits, signs (seven77!)");
    frames(2);
    ppm("opt-wifi-pw-typed");
    wf_hold(B_SAVE, 500);
    check(!pw_on() && wcfk.requests == 1 && wcfk.had_pass && !strcmp(wcfk.pass, "seven77!") && wcfk.len == 4 && !memcmp(wcfk.ssid, "Cafe", 4),
          "SAVE held with 8 characters: the manager asked to join Cafe with the typed password");
    check(wf_said("CONNECTING Cafe"), "... CONNECTING Cafe");
    check(wf_zero(&pws, sizeof pws), "... the entry closed and its buffer wiped (all zero)");
    check(!name_on(), "... the screen is the page again");
    /* cancel */
    memset(&wcfk, 0, sizeof wcfk);
    wf_pw_open("Cafe");
    wf_pw_type("abcdefgh");
    wf_hold(B_HOME, 800);
    check(!pw_on() && wcfk.requests == 0 && wf_said("CANCELLED") && wf_zero(&pws, sizeof pws), "HOME held: CANCELLED, nothing asked, the buffer wiped");
    /* full, and 63 characters */
    wf_pw_open("Cafe");
    for (i = 0; i < 70u; i++)
        tap(B_SAVE);
    check(pws.b.len == 63 && wf_said("PASSWORD FULL"), "63 characters at most: PASSWORD FULL");
    frames(2);
    ppm("opt-wifi-pw-long");
    wf_hold(B_HOME, 800);
    /* the keys play nothing, the other knobs do nothing */
    wf_pw_open("Cafe");
    check(ly_lock == LY_STEP || name_on(), "the entry takes the keys (they play nothing)");
    turn(EN_SELECT, 3);
    turn(EN_ALGO, 2);
    check(pw_on() && ui.scr == SCR_SYSTEM && !pws.b.len, "SELECT / ALGORITHM do nothing there");
    wf_hold(B_HOME, 800);
    /* a wrong state: the manager off when the password is done */
    wf_pw_open("Cafe");
    wf_pw_type("abcdefgh");
    wn_st.mode = WN_MODE_OFF;
    wf_hold(B_SAVE, 500);
    check(!pw_on() && wf_said("WI-FI OFF") && wf_zero(&pws, sizeof pws), "the manager off by then: WI-FI OFF, the buffer wiped all the same");
}
#if FELUCCA_WIFI_AP
/* ---- MODE AP: the page's four lines, the password entry for the AP's key, CLEAR KEY, the channel */
static const char *wf_state0(void)                      /* STATE in the first two-second phase (fm1_ms 0: not the password's) */
{
    fm1_ms = 0;
    return wf_val("STATE");
}
static void wf_ap_tests(void)
{
    uint32_t k;
    wf_fresh(WN_MODE_OFF);
    memset(&wafk, 0, sizeof wafk);
    k = wf_knob("MODE");
    turn(k, 1);
    turn(k, 1);
    turn(k, 1);
    turn(k, 1);                                         /* (a detent a mode: OFF, MAN, AUTO, AP MAN, AP AUTO) */
    check(wn_st.mode == WN_MODE_AP && !strcmp(wf_val("MODE"), "AP AUTO") && wcfk.enable_off == 2, "MODE AP AUTO: the station manager told to stop (once more than OFF)");
    check(!strcmp(wf_val("NAME"), "FM1-55B0") && !strcmp(wf_val("CHANNEL"), "6") && !strcmp(wf_state0(), "ACTIVE"),
          "... with no typed key: NAME the automatic one, CHANNEL 6, STATE ACTIVE (the default password)");
    fm1_ms = 3u << WF_PH_SH;                            /* (the fourth phase, 1 s each since 2026-11-03: the password) */
    check(!strcmp(wf_val("KEY"), "abcd2345"), "... one phase in four the line shows the default password alone, under KEY (the line's big font)");
    fm1_ms = 0;
    {   /* K4 (STATE) steps the line's items and holds them 30 s (the user, 2026-11-03: "k4 should scroll thru the status item and pause
         * auto scrolling for 30sec") */
        uint32_t st = wf_knob("STATE");
        turn(st, 1), turn(st, 1), turn(st, 1);          /* (the fourth item: the password) */
        check(!strcmp(wf_val("KEY"), "abcd2345"), "STATE's knob: three steps on, the password shown at once");
        fm1_ms += 20000u;
        check(!strcmp(wf_val("KEY"), "abcd2345"), "... 20 s later: still held (the clock's phase would have moved on)");
        fm1_ms += 11000u;
        check(!strcmp(wf_val("KEY"), "?") || fm1_ms >> WF_PH_SH & 3u, "... 31 s after the turn: the line cycles by itself again");
        fm1_ms = 0;
    }
    frames(2);
    check(!wc.scan_want && !wf_pg_shown(), "... no scan wanted, no list of networks");
    /* the channel: the knob, 1..13 */
    k = wf_knob("CHANNEL");
    settings_later = 0;
    turn(k, 1);
    check(wn_st.ap.ch == 7 && !strcmp(wf_val("CHANNEL"), "7") && settings_later && wafk.chsets == 1, "CHANNEL's knob: 7, kept with the settings");
    turn(k, 20);
    check(wn_st.ap.ch == 13 && !strcmp(wf_val("CHANNEL"), "13"), "... stops at 13");
    turn(k, -40);
    check(wn_st.ap.ch == 1 && !strcmp(wf_val("CHANNEL"), "1"), "... and at 1");
    /* SAVE: the password entry for the AP's key */
    tap(B_SAVE);
    check(pw_on() && pws.ap && pws.ssid_len == 8 && !memcmp(pws.ssid, "FM1-55B0", 8) && name_on(), "SAVE: the password entry opens for the AP (its name in the header)");
    frames(2);
    ppm("opt-wifi-ap-pw");
    wf_pw_type("short");
    wf_hold(B_SAVE, 500);
    check(wf_said("TOO SHORT") && pw_on() && !wafk.pass_calls, "SAVE held under 8 characters: TOO SHORT, the entry stays");
    wf_pw_type("pass123");
    check(pws.b.len == 12 && !strcmp(pws.b.s, "shortpass123"), "typing goes on");
    wf_hold(B_SAVE, 500);
    check(!pw_on() && wafk.pass_calls == 1 && !strcmp(wafk.pass, "shortpass123") && !wafk.pass_named && wf_said("PASSWORD SET"),
          "SAVE held with 8+: the daemon given the password (the automatic name), PASSWORD SET");
    check(wn_st.ap.set == 1 && wn_st.ap.ch == 1 && wf_zero(&pws, sizeof pws), "... the key kept, the channel unchanged, the entry's buffer wiped");
    check(!wcfk.requests, "... and nothing asked of the station manager");
    frames(2);
    check(!strcmp(wf_state0(), "ACTIVE"), "STATE: ACTIVE");
    wafk.clients = 1;
    check(!strcmp(wf_state0(), "1 CLIENT"), "... one client");
    wafk.clients = 3;
    check(!strcmp(wf_state0(), "3 CLIENTS"), "... three clients");
    test_peers = 1;
    fm1_ms = 0;
    check(!strcmp(wf_state0(), "3 CLIENTS"), "... clients and a Link peer heard: the line alternates, the clients first");
    fm1_ms = 1u << WF_PH_SH;
    check(!strcmp(wf_val("STATE"), "1 LINK PEER"), "... then the Link peer (a phase later)");
    fm1_ms = 2u << WF_PH_SH;
    check(!strcmp(wf_val("STATE"), "3 CLIENTS"), "... and the clients again");
    test_peers = 3;
    fm1_ms = 1u << WF_PH_SH;
    check(!strcmp(wf_val("STATE"), "3 LINK PEERS"), "... three peers: 3 LINK PEERS");
    wafk.clients = 0;
    check(!strcmp(wf_state0(), "3 LINK PEERS"), "... no client keyed but peers heard: 3 LINK PEERS");
    wafk.clients = 3;
    test_peers = 0;
    wafk.status = WAS_STARTING;
    check(!strcmp(wf_state0(), "STARTING"), "... STARTING");
    wafk.status = WAS_NOCAL;
    check(!strcmp(wf_state0(), "NO CAL") && !strcmp(wf_val("MODE"), "NO CAL"), "... NO CAL (and MODE says it too)");
    tap(B_SAVE);
    check(wf_said("NO RADIO CAL") && !pw_on(), "SAVE with no radio calibration: NO RADIO CAL, no entry");
    wafk.status = 0;
    wafk.clients = 0;
    /* a custom name (the web editor's) stays; the entry keeps it */
    memcpy(wn_st.ap.ssid, "Studio AP", 9);
    wn_st.ap.ssid_len = 9;
    check(!strcmp(wf_val("NAME"), "Studio AP"), "a name set by the web editor is the NAME shown");
    tap(B_SAVE);
    wf_pw_type("abcdefgh");
    wf_hold(B_SAVE, 500);
    check(wafk.pass_calls == 2 && wafk.pass_named && wn_st.ap.ssid_len == 9, "SAVE held: the key again, under the chosen name");
    /* CLEAR KEY: ask first */
    settings_later = 0;
    wf_hold(B_SAVE, 400);
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR KEY Studio AP?") && wn_st.ap.set == 1 && !wafk.forgets && !settings_later,
          "SAVE held: asks CLEAR KEY Studio AP? first, nothing cleared yet");
    op_no();
    check(!op_armed() && wn_st.ap.set == 1 && !wafk.forgets, "... NO: kept");
    wf_hold(B_SAVE, 400);
    tap(B_SAVE);
    check(wf_said("KEY CLEARED") && wn_st.ap.set == 0 && wafk.forgets == 1 && settings_later, "... asked again, SAVE: KEY CLEARED, kept in the settings");
    check(!strcmp(wf_state0(), "ACTIVE"), "... STATE ACTIVE again (the default password)");
    wf_hold(B_SAVE, 400);
    check(!op_armed() && pw_on() && pws.ap, "SAVE held with no typed key: nothing to clear, the password entry opens");
    wf_hold(B_HOME, 800);
    check(!pw_on(), "(HOME held: closed)");
    /* cancel the entry */
    tap(B_SAVE);
    wf_pw_type("abcdefgh");
    wf_hold(B_HOME, 800);
    check(!pw_on() && wf_said("CANCELLED") && wafk.pass_calls == 2 && wf_zero(&pws, sizeof pws), "HOME held: CANCELLED, the daemon not asked, the buffer wiped");
    /* leaving AP: the station modes are back; the AP lines go */
    k = wf_knob("MODE");
    turn(k, -1);
    check(wn_st.mode == WN_MODE_AP_MAN, "(left from AP AUTO: AP MAN)");
    turn(k, -1);
    check(wn_st.mode == WN_MODE_AUTO, "(and AUTO)");
    check(!strcmp(wf_val("NETWORK"), "NONE") && wf_val("NAME")[0] == '?', "MODE AUTO again: the page's lines are the station's (NETWORK, not NAME)");
    /* AP MAN: off until SAVE; a password types in and starts it; SAVE starts and stops it */
    wf_fresh(WN_MODE_OFF);
    memset(&wafk, 0, sizeof wafk);
    k = wf_knob("MODE");
    turn(k, 1); turn(k, 1); turn(k, 1);
    check(wn_st.mode == WN_MODE_AP_MAN && !strcmp(wf_val("MODE"), "AP MAN"), "AP MAN, no typed key");
    check(!strcmp(wf_state0(), "SAVE: START"), "... STATE SAVE: START (stopped until started)");
    wf_hold(B_SAVE, 400);
    check(pw_on() && pws.ap, "... SAVE held (nothing typed to clear): the password entry");
    wf_pw_type("manual88");
    wf_hold(B_SAVE, 500);
    check(!pw_on() && wafk.pass_calls == 1 && wafk.on, "... a typed password starts the access point in AP MAN");
    frames(2);
    check(!strcmp(wf_state0(), "ACTIVE"), "... ACTIVE");
    tap(B_SAVE);
    check(wf_said("AP STOPPED") && !wafk.on && wafk.runs == 1 && !pw_on(), "SAVE with a key kept, running: AP STOPPED");
    frames(2);
    check(!strcmp(wf_state0(), "SAVE: START"), "... STATE SAVE: START");
    tap(B_SAVE);
    check(wf_said("AP STARTING") && wafk.on && wafk.runs == 2, "SAVE again: AP STARTING");
    wf_hold(B_SAVE, 400);
    check(op_armed() && wn_st.ap.set == 1, "SAVE held: still asks CLEAR KEY first");
    op_no();
}
#endif
#endif
