/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > WI-FI and the password entry (docs/WIFI-UI-DESIGN.md, docs/UI-OPTIMIST-DESIGN.md 4.7 and 4.13). Built only with
 * WIFI_UI (optimist.c: FELUCCA_WIFI && FELUCCA_BLE). Unlike the UI's other files this one is included late, by felucca.c
 * after io/console.c: it drives the connection manager (wifi/wifi_conn.c: wc, wc_tab, wc_req, wn_st, wl), whose state is
 * defined after the UI, so everything the UI files call is prototyped in op_project.c and defined here.
 *   MODE      OFF, MAN, AUTO (wn_st.mode, kept in the settings record, the Bluetooth pages' pattern: "it should be very
 *             similar", the user, 2026-10-27). OFF the manager stops; MAN joins when told (SAVE); AUTO also joins the best
 *             saved network heard (wc_poll).
 *   NETWORK   the name under the list's cursor (its knob, K2, moves it: stopping at the ends); joined, the network joined.
 *   SIGNAL    the access point under the cursor ("-68 dBm  CH 1"); joined, the address.
 *   STATE     the manager's, as a word.
 *   the list  while the page is on screen, MODE is MAN or AUTO and nothing is joined or joining, the manager scans
 *             (wc.scan_want) and the page's two middle lines give way to the access points heard, one row each (strongest
 *             first, a network of several access points is several rows): the saved networks first, in C_OK; then the new
 *             WPA2-PSK ones in C_WARN; then those that cannot be joined (open, WPA3 only, TKIP, PMF required) in C_GRAY, not
 *             selectable; then the saved ones not heard (NOT IN RANGE), so they can be forgotten. Text only, a bar at the
 *             left edge for the cursor (op_bt.c bt_pg_paint: the same look).
 *   SAVE      joined or joining: let go. On a row: a saved network joins with its saved key; a new one opens the password
 *             entry and joins with what is typed; a network that cannot be joined says so.
 *   SAVE held the row's saved network forgotten (a new one: NOT REMEMBERED); joined, the network joined is let go and forgotten.
 * MODE AP (FELUCCA_WIFI_AP; the user, 2026-10-27: "maybe we add mode AP"): the FM-1 is the access point (wifi/wifi_apd.c). The page's four
 * lines become NAME (the network's name: FM1- and four digits of the radio's address, or the one set by the web editor), CHANNEL (KNOB 3
 * turns it, 1..13; the AP restarts when the knob has rested), STATE (STARTING, ACTIVE, n CLIENTS, NO CAL). SAVE opens the
 * password entry for the AP's key (8..63 characters, as a network's), SAVE held asks CLEAR KEY (the default password again; with none typed, in AP MAN, SAVE held opens the entry).
 * The password: the one typed, else the default one, a function of the name alone (wifi_ap_default_key: another Optimist that hears
 * the name knows it), which STATE shows as KEY xxxxxxxx every fourth 2 s (KEY TYPED when one typed replaces it).
 * The password entry (pw_*): KNOB 1 the character in its family, KNOB 2 the family, KNOB 3 the cursor; SAVE tapped adds the
 * character, SAVE held joins (DONE), HOME tapped erases, HOME held cancels (the user, 2026-10-27). The text is shown as
 * typed (the panel is small), case-sensitive, 8..63 characters; the buffer (op_pwbuf.h) is wiped when the entry closes and
 * goes nowhere but into wc_request, whose copy the manager wipes. */
#include "op_pwbuf.h"

#define WF_PG_ROWS 4u                                   /* the list's rows on screen (bt_pg_paint's) */
enum { WFC_SAVED, WFC_NEW, WFC_NO, WFC_AWAY };          /* a row: saved and joinable (green), new WPA2-PSK (amber), cannot be joined
                                                         * (grey), saved and not heard (dim) */
#define WF_NO_AP 0xFFu                                  /* (a row with no access point heard: WFC_AWAY) */
#define WF_MAXR (WN_SCAN_MAX + WN_SLOTS)
static struct {
    uint8_t n, cur, first;                              /* the rows, the cursor row, the first row shown */
    uint8_t key_ok, key_ap;                             /* the cursor kept on its network: an access point (its BSSID) or a saved name */
    uint8_t key_bssid[6], key_len;
    uint8_t key_ssid[WN_SSID_MAX];
    uint8_t page;                                       /* the list was shown last pass (the cursor starts at the top again) */
    uint8_t asked;                                      /* a join the user asked for is under way: its failure is said wherever he is */
    uint8_t was_up;                                     /* wc.st was WC_UP last pass (CONNECTED said once) */
    uint8_t fg_len;                                     /* the forget asked (op_arm): the network's name, its row may move meanwhile */
    uint8_t fg_ap;                                      /* .. or MODE AP's key (CLEAR KEY) */
    uint8_t fg_ssid[WN_SSID_MAX];
    struct { uint8_t ap, slot, cls; } r[WF_MAXR];       /* ap: wc_tab's index or WF_NO_AP; slot: the saved network or 0xFF */
} wfs;
static struct {
    struct pw_buf b;
    uint8_t on, ssid_len, first;                        /* open; the network's name; the first character shown */
    uint8_t ap;                                         /* the entry is MODE AP's key, not a network's password */
    uint8_t home, home_used, save, save_used;           /* each button: down, and its hold already acted on */
    uint16_t rev;                                       /* edits made (the drawing's signature: never a hash of the text) */
    uint32_t home_t, save_t;                            /* the buttons' presses (fm1_ms) */
    uint8_t ssid[WN_SSID_MAX];
} pws;

/* ---- what the manager is doing */
static int wf_joined(void) { return wc.st == WC_UP || wc.st == WC_LINK; }
static int wf_joining(void) { return wc_req.on || (wc.st >= WC_PMK && wc.st <= WC_HS) || wc.st == WC_PRE; }
static int wf_nocal(void) { return wc.st == WC_FAIL && wc.fail == WCF_NOCAL; }
static int wf_radio_ok(void) { return wc.st != WC_OFF && wc.st != WC_RADIO && !wf_nocal(); }
#ifdef WIFI_ON_FM1B
#define WF_JAM 1                                        /* MODE JAM (the trial's Wi-Fi stack): AUTO that also joins an FM-1 access point */
#else
#define WF_JAM 0
#endif
static const uint8_t WF_W[] = {WN_MODE_OFF, WN_MODE_MAN, WN_MODE_AUTO,   /* the modes in the knob's order, and their words */
#if WF_JAM
                               WN_MODE_JAM,
#endif
#if FELUCCA_WIFI_AP
                               WN_MODE_AP_MAN, WN_MODE_AP,
#endif
};
static const char *const WF_M[] = {"OFF", "MAN", "AUTO",
#if WF_JAM
                                   "JAM",
#endif
#if FELUCCA_WIFI_AP
                                   "AP MAN", "AP AUTO",
#endif
};
#define WF_MODE_LAST ((int32_t)(sizeof WF_W / sizeof WF_W[0]) - 1)
static int wf_is_ap_mode(uint8_t m) { return m == WN_MODE_AP || m == WN_MODE_AP_MAN; }
#if FELUCCA_WIFI_AP
#define WF_AP_ON (wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN)
#define WF_AP_NOCAL (WF_AP_ON && wa_status() == WAS_NOCAL)
#else
#define WF_AP_ON 0
#define WF_AP_NOCAL 0
#endif
static uint32_t wf_mode_idx(void)
{
    uint32_t i;
    for (i = 0; i < sizeof WF_W / sizeof WF_W[0]; i++)
        if (WF_W[i] == wn_st.mode)
            return i;
    return 0u;
}
static int wf_pg_on(void)                               /* the list is what the page shows: a mode on, nothing joined, nothing joining */
{
    return wn_st.mode != WN_MODE_OFF && !WF_AP_ON && !wf_joined() && !wf_joining() && !wf_nocal();
}
static int wf_page(void)                                /* SYSTEM shows the WI-FI page (op_global.c bt_page's way) */
{
    uint32_t e;
    if (ui.scr != SCR_SYSTEM || GL_DEV || ui.row[SCR_SYSTEM] >= gl_n(1))
        return 0;
    e = gl_ent(1, ui.row[SCR_SYSTEM]);
    return str_eq(SYS[(e >> 2) % NSYS].name, "WI-FI");
}
static uint32_t wf_pg_shown(void) { return wf_page() && wf_pg_on() && !pws.on; }

/* a name as the screen draws it: the characters the font has, '?' for the rest (an SSID is 32 bytes of anything) */
static void wf_txt(char *b, const uint8_t *s, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len && i < WN_SSID_MAX; i++)
        b[i] = s[i] >= 32u && s[i] < 127u ? (char)s[i] : '?';
    b[i] = 0;
}
/* the network the manager has: the one joined or being joined (b holds WN_SSID_MAX + 1) */
static void wf_cur_name(char *b, const uint8_t **ssid, uint32_t *len)
{
    const uint8_t *s = wc_req.on ? wc_req.ssid : wc.ap.ssid;
    uint32_t n = wc_req.on ? wc_req.ssid_len : wc.ap.ssid_len;
    wf_txt(b, s, n);
    *ssid = s;
    *len = n;
}

/* ---- the list's rows */
static const uint8_t *wf_row_ssid(uint32_t i, uint32_t *len)
{
    if (wfs.r[i].ap != WF_NO_AP) {
        *len = wc_tab.ap[wfs.r[i].ap].ssid_len;
        return wc_tab.ap[wfs.r[i].ap].ssid;
    }
    *len = wn_st.n[wfs.r[i].slot].ssid_len;
    return wn_st.n[wfs.r[i].slot].ssid;
}
static int wf_key_is(uint32_t i)                        /* row i is the one the cursor was kept on */
{
    uint32_t len;
    const uint8_t *s;
    if (wfs.key_ap)
        return wfs.r[i].ap != WF_NO_AP && !memcmp(wc_tab.ap[wfs.r[i].ap].bssid, wfs.key_bssid, 6);
    s = wf_row_ssid(i, &len);
    return wfs.r[i].ap == WF_NO_AP && len == wfs.key_len && !memcmp(s, wfs.key_ssid, len);
}
static void wf_key_set(void)                            /* the cursor row remembered, to be found again when the table moves */
{
    uint32_t len;
    const uint8_t *s;
    if (!wfs.n)
        return;
    s = wf_row_ssid(wfs.cur, &len);
    wfs.key_ap = wfs.r[wfs.cur].ap != WF_NO_AP;
    memset(wfs.key_bssid, 0, 6);
    if (wfs.key_ap)
        memcpy(wfs.key_bssid, wc_tab.ap[wfs.r[wfs.cur].ap].bssid, 6);
    wfs.key_len = (uint8_t)len;
    memcpy(wfs.key_ssid, s, len);
    wfs.key_ok = 1;
}
static void wf_build(void)                              /* the rows from the table and the saved networks; the cursor kept on its network */
{
    uint32_t c, i, n = 0;
    for (c = 0; c < WFC_NO + 1u; c++)
        for (i = 0; i < wc_tab.n; i++) {
            const struct wn_ap *a = &wc_tab.ap[i];
            int sv = wn_find(&wn_st, a->ssid, a->ssid_len);
            uint32_t k = a->sec != WN_SEC_PSK ? WFC_NO : sv >= 0 ? WFC_SAVED : WFC_NEW;
            if (k != c)
                continue;
            if (wn_scan_best_for(&wc_tab, a->ssid, a->ssid_len) != (int)i)
                continue;                               /* a network is one row (the user, 2026-10-28: "mots network appears twice"): a join by
                                                         * name goes to its strongest AP whichever row was picked, so a weaker AP of the same
                                                         * name is not a row of its own */
            wfs.r[n].ap = (uint8_t)i;
            wfs.r[n].slot = (uint8_t)(sv >= 0 ? sv : 0xFF);
            wfs.r[n++].cls = (uint8_t)k;
        }
    for (i = 0; i < WN_SLOTS; i++)                      /* (a saved network no access point of is heard) */
        if (wn_st.n[i].used == 1u && wn_scan_best_for(&wc_tab, wn_st.n[i].ssid, wn_st.n[i].ssid_len) < 0) {
            wfs.r[n].ap = WF_NO_AP;
            wfs.r[n].slot = (uint8_t)i;
            wfs.r[n++].cls = WFC_AWAY;
        }
    wfs.n = (uint8_t)n;
    if (wfs.key_ok)
        for (i = 0; i < n; i++)
            if (wf_key_is(i)) {
                wfs.cur = (uint8_t)i;
                break;
            }
    if (wfs.cur >= n)
        wfs.cur = (uint8_t)(n ? n - 1u : 0u);
    wf_key_set();                                       /* (the row there now, if the old one is gone) */
}
static void wf_row_name(char *b, uint32_t i)
{
    uint32_t len;
    const uint8_t *s = wf_row_ssid(i, &len);
    wf_txt(b, s, len);
}
static uint32_t wf_bars(int32_t rssi) { return rssi >= -60 ? 3u : rssi >= -72 ? 2u : rssi >= -84 ? 1u : 0u; }
static void wf_scroll(void)                             /* the first row shown, so that the cursor is on screen */
{
    if (wfs.cur < wfs.first)
        wfs.first = wfs.cur;
    else if (wfs.cur >= wfs.first + WF_PG_ROWS)
        wfs.first = (uint8_t)(wfs.cur - (WF_PG_ROWS - 1u));
    if (wfs.first + WF_PG_ROWS > wfs.n)
        wfs.first = (uint8_t)(wfs.n > WF_PG_ROWS ? wfs.n - WF_PG_ROWS : 0u);
}
static uint16_t wf_ink(uint32_t cls) { return cls == WFC_SAVED ? C_OK : cls == WFC_NEW ? C_WARN : cls == WFC_NO ? C_GRAY : C_DIM; }
static const char *wf_tag(uint32_t i) { return wfs.r[i].cls == WFC_AWAY ? "NOT IN RANGE" : wfs.r[i].slot != 0xFFu ? "SAVED" : ""; }

static void wf_pg_paint(int32_t y0, int32_t y1, uint16_t col)
{
    uint32_t i;
    int32_t rh = (y1 - y0) / (int32_t)WF_PG_ROWS;
    wf_build();
    wf_scroll();
    for (i = wfs.first; i < wfs.n && i < wfs.first + WF_PG_ROWS; i++) {
        char nm[WN_SSID_MAX + 1u], b[WN_SSID_MAX + 1u], ch[4];
        const char *tag = wf_tag(i);
        int32_t y = y0 + (int32_t)(i - wfs.first) * rh, right = 232, room;
        uint16_t ink = wf_ink(wfs.r[i].cls);
        wf_row_name(nm, i);
        if (i == wfs.cur)
            cv_rect(0, y + 2, 3, rh - 4, col);
        if (wfs.r[i].ap != WF_NO_AP) {                  /* the signal (bars) and the channel, small, to tell two access points of a name apart */
            const struct wn_ap *a = &wc_tab.ap[wfs.r[i].ap];
            ch[0] = 0;
            fmt_int(ch, a->ch);
            dev_bars(right - 16, y + (rh - 16) / 2, wf_bars(a->rssi), 0);
            right -= 22;
            right -= text_ws(ch);
            cv_texts(right, y + (rh - (int32_t)FONT_S.h) / 2, ch, C_DIM);
            right -= 8;
        }
        if (tag[0]) {
            right -= text_ws(tag);
            cv_texts(right, y + (rh - (int32_t)FONT_S.h) / 2, tag, ink);
            right -= 8;
        }
        room = right - 8;
        if (text_w(font_big(), nm) <= room)
            cv_text(8, y + (rh - (int32_t)font_big()->h) / 2, font_big(), nm, ink);
        else                                            /* (a long name: the small face, as many characters as fit) */
            cv_texts(8, y + (rh - (int32_t)FONT_S.h) / 2, cut(b, nm, room > 8 ? (uint32_t)room / 8u : 1u), ink);
    }
    if (wfs.n > WF_PG_ROWS)                             /* where the window is: a thin bar at the right edge */
        scroll_bar(y0, rh * (int32_t)WF_PG_ROWS, wfs.first, (int32_t)WF_PG_ROWS, wfs.n);
}
static uint32_t wf_pg_sig(void)                         /* what the list shows, as one number (op_gldraw.c gl_draw) */
{
    uint32_t i, sig;
    wf_build();
    wf_scroll();
    sig = hu(hu(hu(0x57F1u + wfs.n * 131u, wfs.cur), wfs.first), pws.on);
    for (i = 0; i < wfs.n; i++) {
        char nm[WN_SSID_MAX + 1u];
        wf_row_name(nm, i);
        sig = hu(hs(sig, nm), (uint32_t)wfs.r[i].cls * 64u + (wfs.r[i].ap == WF_NO_AP ? 0u : wf_bars(wc_tab.ap[wfs.r[i].ap].rssi) * 16u + wc_tab.ap[wfs.r[i].ap].ch));
    }
    return sig;
}
static int wf_turn(int32_t s)                           /* NETWORK's knob: the cursor through the list, stopping at the ends -> 1 */
{
    uint32_t c;
    if (!wf_pg_on())
        return 0;
    if (s != OP_RESET) {
        wf_build();
        c = wfs.cur;
        c = s < 0 ? (c > (uint32_t)-s ? c - (uint32_t)-s : 0u) : c + (uint32_t)s;
        wfs.cur = (uint8_t)(c >= wfs.n ? (wfs.n ? wfs.n - 1u : 0u) : c);
        wf_key_set();
    }
    return 1;
}

/* ---- the page's lines */
#if FELUCCA_WIFI_AP
static int wf_ch_turn(int32_t s)                        /* CHANNEL's knob (MODE AP): 1..13, stopping at the ends; SHIFT: the default -> 1 */
{
    int32_t v;
    if (!WF_AP_ON)
        return 0;
    v = s == OP_RESET ? (int32_t)WA_CH_DEFAULT : (int32_t)wa_channel() + s;
    wa_set_channel((uint32_t)(v < 1 ? 1 : v > 13 ? 13 : v));
    return 1;
}
#else
static int wf_ch_turn(int32_t s) { (void)s; return 0; }
#endif
static void wf_mode_cell(cell_t *c)
{
    uint32_t m = wf_mode_idx();
    int bad = (wf_nocal() && m) || WF_AP_NOCAL;
    c->kind = CK_VAL;
    c->label = "MODE";
    c->big = 1;
    cell_val(c, bad ? "NO CAL" : WF_M[m]);
    c->col = bad ? C_WARN : 0;
}
static void wf_mode_set(int32_t s)                      /* a detent a mode, stopping at the ends; YES round (bt_mode_set's) */
{
    uint32_t m = wf_mode_idx(), n = (uint32_t)sys_step((int32_t)m, s, WF_MODE_LAST);
    if (WF_W[n] == wn_st.mode)
        return;
    wn_st.mode = WF_W[n];
    settings_later = 1;
    wfs.asked = 0;
    if (n == 0u || wf_is_ap_mode(WF_W[n])) {
        wc_enable(0);                                   /* (a network joined is told; the radio, once started, stays up) */
        memset(&wc_req, 0, sizeof wc_req);
    }
}
static void wf_net_cell(cell_t *c)
{
    char nm[WN_SSID_MAX + 1u];
    const uint8_t *ssid;
    uint32_t len;
#if FELUCCA_WIFI_AP
    if (WF_AP_ON) {                                     /* MODE AP: the access point's name */
        uint8_t raw[WN_SSID_MAX];
        wf_txt(nm, raw, wa_name(raw));
        c->kind = CK_RO;
        c->label = "NAME";
        c->big = 1;
        c->col = 0;
        cell_val(c, nm);
        return;
    }
#endif
    c->kind = wf_pg_on() ? CK_VAL : CK_RO;              /* (a knob that moves the page's list) */
    c->label = "NETWORK";
    c->big = 1;
    if (wf_joined() || wf_joining()) {
        wf_cur_name(nm, &ssid, &len);
        c->col = wf_joined() ? C_OK : 0;
    } else {
        wf_build();
        nm[0] = 0;
        if (wf_pg_on() && wfs.n)
            wf_row_name(nm, wfs.cur);
        c->col = 0;
    }
    cell_val(c, nm[0] ? nm : "NONE");
}
static void wf_ip_text(char *b, uint32_t ip)            /* a.b.c.d (host order), 15 characters at most */
{
    uint32_t k;
    b[0] = 0;
    for (k = 0; k < 4u; k++) {
        fmt_app(b, (int32_t)((ip >> (24u - 8u * k)) & 0xFFu));
        if (k < 3u)
            str_app(b, ".", 16);
    }
}
static void wf_sig_cell(cell_t *c)
{
    char b[WN_SSID_MAX + 1u];
#if FELUCCA_WIFI_AP
    if (WF_AP_ON) {                                     /* MODE AP: the channel (its knob turns it) */
        c->kind = CK_VAL;
        c->label = "CHANNEL";
        c->big = 1;
        c->col = 0;
        b[0] = 0;
        fmt_int(b, (int32_t)wa_channel());
        cell_val(c, b);
        return;
    }
#endif
    c->kind = CK_RO;
    c->label = "SIGNAL";
    c->big = 1;
    c->wide = 1;                                        /* ("-80 dBm  CH 11", an address: up to 15 characters) */
    b[0] = 0;
    if (wf_joined()) {
        if (wl.ip.ip)
            wf_ip_text(b, wl.ip.ip);
        else
            str_cpy(b, "GETTING IP", sizeof b);
        c->col = wl.ip.ip ? C_OK : 0;
        c->big = 0;                                     /* (15 characters: the small face) */
    } else if (wf_pg_on()) {
        wf_build();
        if (wfs.n && wfs.r[wfs.cur].ap != WF_NO_AP) {
            const struct wn_ap *a = &wc_tab.ap[wfs.r[wfs.cur].ap];
            fmt_int(b, a->rssi);
            str_app(b, " dBm  CH ", sizeof b);
            fmt_app(b, a->ch);
        } else if (wfs.n) {
            str_cpy(b, "NOT IN RANGE", sizeof b);
        }
        c->col = 0;
    }
    str_cpy(c->val, b[0] ? b : "--", sizeof c->val);    /* (cell_val stops at 13: this value has 15) */
}
#if FELUCCA_WIFI_AP
static void wf_ap_state_cell(cell_t *c)
{
    uint32_t st = wa_status(), n = wa_clients();
    char b[20];
    const char *v = "STARTING";
    uint16_t col = 0;
    b[0] = 0;
    if (st == WAS_NOKEY) {
        v = "NO PASSWORD";                              /* (not reached: the default password is always there) */
        col = C_ERR;
    } else if (st == WAS_NOCAL) {
        v = "NO CAL";
        col = C_WARN;
    } else if (st == WAS_STOPPED) {
        v = "SAVE: START";                              /* (AP MAN: off until SAVE) */
    } else if (st == WAS_UP) {
        if (n && (!wl_peers() || ((fm1_ms >> 11) & 1u) == 0u)) {   /* both numbers, the user's way (2026-10-28): the line alternates every 2 s between the clients
                                                         * and the Link peers, in full words */
            fmt_int(b, (int32_t)n);
            str_app(b, n == 1u ? " CLIENT" : " CLIENTS", sizeof b);
            v = b;
        } else if (wl_peers()) {
            fmt_int(b, (int32_t)wl_peers());
            str_app(b, wl_peers() == 1u ? " LINK PEER" : " LINK PEERS", sizeof b);
            v = b;
        } else {
            v = "ACTIVE";
        }
        col = C_OK;
    }
    if (st != WAS_NOCAL && ((fm1_ms >> 11) & 3u) == 3u) {   /* the password, one phase in four (the user reads it to join a phone) */
        char k[16];
        if (wa_default_key(k + 4)) {
            k[0] = 'K', k[1] = 'E', k[2] = 'Y', k[3] = ' ';
            b[0] = 0;
            str_app(b, k, sizeof b);
            v = b;
        } else {
            v = "KEY TYPED";
        }
        col = C_OK;
    }
    c->kind = CK_RO;
    c->label = "STATE";
    c->big = 1;
    cell_val(c, v);
    c->col = col;
}
#endif
static void wf_state_cell(cell_t *c)
{
    static const char *const FAIL[7] = {"FAILED", "NO CAL", "FAILED: NO AP", "FAILED: AUTH", "FAILED: ASSOC", "FAILED: HS",
                                        "WRONG PASSWORD"};   /* (wifi_conn.c WCF_*) */
    const char *v = "OFF";
    uint16_t col = 0;
    char b[20];
    b[0] = 0;
#if FELUCCA_WIFI_AP
    if (WF_AP_ON) {
        wf_ap_state_cell(c);
        return;
    }
#endif
    if (wn_st.mode != WN_MODE_OFF)
        switch (wc.st) {
        case WC_OFF:
        case WC_RADIO:
            v = "STARTING";
            break;
        case WC_IDLE:
            v = "READY";
            break;
        case WC_SCAN:
            v = wc_req.on ? "CONNECTING" : "SCANNING";
            break;
        case WC_UP:
            v = "CONNECTED";
            if (wl_peers()) {                           /* joined, and Link peers heard on that network */
                fmt_int(b, (int32_t)wl_peers());
                str_app(b, wl_peers() == 1u ? " LINK PEER" : " LINK PEERS", sizeof b);
                v = b;
            }
            col = C_OK;
            break;
        case WC_FAIL:
            v = FAIL[wc.fail % 7u];
            col = wc.fail == WCF_NOCAL ? C_WARN : C_ERR;
            break;
        default:                                        /* PMK, PRE, AUTH, ASSOC, HS, LINK */
            v = "CONNECTING";
            break;
        }
#if WF_JAM
    if (wn_st.mode == WN_MODE_JAM) {                    /* MODE JAM: joined to an FM-1 access point (the line alternates with the Link peers), or one refused */
        int j = plat_wifi_jam();
        if (j == 1 && wc.st == WC_UP && ((fm1_ms >> 11) & 1u)) {
            uint32_t i;
            b[0] = 'J', b[1] = 'A', b[2] = 'M', b[3] = ' ';
            for (i = 0; i < wc.ap.ssid_len && i < 8u; i++)
                b[4u + i] = (char)wc.ap.ssid[i];
            b[4u + i] = 0;
            v = b;
            col = C_OK;
        } else if (j == 2 && wc.st != WC_UP && wc.st != WC_SCAN && !wf_joining()) {
            v = "JAM: LOCKED";                          /* (its owner typed a password: not tried again for two minutes) */
            col = C_WARN;
        }
    }
#endif
    c->kind = CK_RO;
    c->label = "STATE";
    c->big = 1;
    cell_val(c, v);
    c->col = col;
}
static void wf_cell(uint32_t it, cell_t *c)
{
    switch (it) {
    case SI_WFMODE:
        wf_mode_cell(c);
        break;
    case SI_WFNET:
        wf_net_cell(c);
        break;
    case SI_WFSIG:
        wf_sig_cell(c);
        break;
    default:
        wf_state_cell(c);
        break;
    }
}

/* ---- the actions */
static void wf_say(const char *a, uint32_t len, const uint8_t *ssid)   /* "<a><name>" */
{
    char nm[WN_SSID_MAX + 1u];
    wf_txt(nm, ssid, len);
    ui_say(a, nm);
}
static void wf_let_go(void)                             /* the join under way, or the network joined, ended (a request waiting for its scan too) */
{
    wc_disconnect();
    memset(&wc_req, 0, sizeof wc_req);                  /* (wc_disconnect leaves a request by name armed, and its password) */
    wfs.asked = 0;
}
static void wf_join(const uint8_t *ssid, uint32_t len, const char *pass)   /* the manager asked; says what it answered */
{
    int rc = wc_request(ssid, len, pass);
    if (rc == 0) {
        wfs.asked = 1;
        wf_say("CONNECTING ", len, ssid);
    } else {
        ui_message(rc == -1 ? "WI-FI OFF" : "NO PASSWORD");
    }
}
static void pw_open(const uint8_t *ssid, uint32_t len);
static void wf_save(void)                               /* SAVE tapped on the page, any line */
{
    uint32_t len;
    const uint8_t *ssid;
    char nm[WN_SSID_MAX + 1u];
#if FELUCCA_WIFI_AP
    if (WF_AP_ON) {                                     /* MODE AP: SAVE is the access point's password */
        uint8_t raw[WN_SSID_MAX];
        if (wa_status() == WAS_NOCAL) {
            ui_message("NO RADIO CAL");
            return;
        }
        if (wn_st.mode == WN_MODE_AP_MAN) {                 /* AP MAN: SAVE starts it, or stops it */
            if (wa_status() == WAS_STOPPED) {
                (void)wa_run(1);
                ui_message("AP STARTING");
            } else {
                (void)wa_run(0);
                ui_message("AP STOPPED");
            }
            return;
        }
        len = wa_name(raw);
        pw_open(raw, len);
        pws.ap = 1;
        return;
    }
#endif
    if (wn_st.mode == WN_MODE_OFF) {
        ui_message("WI-FI OFF");
        return;
    }
    if (wf_joined() || wf_joining()) {
        wf_cur_name(nm, &ssid, &len);
        wf_let_go();
        ui_say("DISCONNECTED ", nm);
        return;
    }
    if (!wf_radio_ok()) {
        ui_message(wf_nocal() ? "NO RADIO CAL" : "WI-FI STARTING");
        return;
    }
    wf_build();
    if (!wfs.n) {
        ui_message("NONE FOUND");
        return;
    }
    ssid = wf_row_ssid(wfs.cur, &len);
    switch (wfs.r[wfs.cur].cls) {
    case WFC_SAVED:
        wf_join(ssid, len, 0);                          /* (its saved key: no password) */
        break;
    case WFC_NEW:
        pw_open(ssid, len);
        break;
    case WFC_AWAY:
        ui_message("NOT IN RANGE");
        break;
    default:
        ui_message("NOT SUPPORTED");                    /* (open, WPA3 only, TKIP, PMF required) */
        break;
    }
}
/* SAVE held on the page: ask first, naming the network (the user lost a saved network to a hold, 2026-10-28: "I could not save a
 * network"); SAVE again confirms (op_yes -> sys_yes ok = 1 -> wf_forget_do), anything else cancels */
static void wf_forget(void)
{
    char nm[WN_SSID_MAX + 1u];
    const uint8_t *ssid;
    uint32_t len;
    wfs.fg_ap = 0;
#if FELUCCA_WIFI_AP
    if (WF_AP_ON) {                                     /* MODE AP: the key goes (ask first, as a network's forget) */
        uint8_t raw[WN_SSID_MAX];
        if (!wn_st.ap.set) {                            /* none typed (the default one is in use): SAVE held opens the entry (in AP AUTO SAVE does) */
            pw_open(raw, wa_name(raw));
            pws.ap = 1;
            return;
        }
        wf_txt(nm, raw, wa_name(raw));
        wfs.fg_ap = 1;
        op_arm(SCR_SYSTEM, ui.row[SCR_SYSTEM], ui.hot, "CLEAR KEY", nm, 1);
        return;
    }
#endif
    if (wn_st.mode == WN_MODE_OFF) {
        ui_message("WI-FI OFF");
        return;
    }
    if (wf_joined() || wf_joining()) {                  /* (as the Bluetooth pages: the link ended, and the device forgotten) */
        wf_cur_name(nm, &ssid, &len);
        if (wn_find(&wn_st, ssid, len) < 0) {           /* (a network that was never saved: nothing to ask, the link just ends) */
            wf_let_go();
            ui_say("DISCONNECTED ", nm);
            return;
        }
    } else {
        wf_build();
        if (!wfs.n) {
            ui_message("NONE FOUND");
            return;
        }
        if (wfs.r[wfs.cur].slot == 0xFFu) {
            ui_message("NOT REMEMBERED");
            return;
        }
        ssid = wf_row_ssid(wfs.cur, &len);
        wf_txt(nm, ssid, len);
    }
    wfs.fg_len = (uint8_t)len;
    memcpy(wfs.fg_ssid, ssid, len);
    op_arm(SCR_SYSTEM, ui.row[SCR_SYSTEM], ui.hot, "FORGET", nm, 1);
}
static void wf_forget_do(void)                          /* the question answered YES */
{
    char nm[WN_SSID_MAX + 1u];
    int slot;
#if FELUCCA_WIFI_AP
    if (wfs.fg_ap) {
        wfs.fg_ap = 0;
        wa_forget_key();
        ui_message("KEY CLEARED");
        return;
    }
#endif
    slot = wn_find(&wn_st, wfs.fg_ssid, wfs.fg_len);
    wf_txt(nm, wfs.fg_ssid, wfs.fg_len);
    if (wf_joined() || wf_joining())
        wf_let_go();
    if (slot < 0) {                                     /* (gone meanwhile) */
        ui_message("NOT REMEMBERED");
        return;
    }
    wn_forget(&wn_st, (uint32_t)slot);
    settings_later = 1;
    ui_say("FORGOTTEN ", nm);
}
static int wf_key(uint32_t b, int held)                 /* op_input.c: SAVE on the WI-FI page, any line -> 1 taken */
{
    if (b != B_SAVE || !wf_page() || op_armed())
        return 0;
    if (held)
        wf_forget();
    else
        wf_save();
    return 1;
}

/* a join that failed is said once (the manager sets wc.fail_new): wherever the user is when he asked for it, and on the page
 * for the manager's own tries (AUTO retries every 20 s: said anywhere they would be noise). A join completed is said once */
static void wf_poll(void)
{
    static const char *const WHY[7] = {"JOIN FAILED", "NO RADIO CAL", "NO ACCESS POINT", "AUTH FAILED", "ASSOC FAILED",
                                       "HANDSHAKE FAILED", "WRONG PASSWORD"};   /* (wifi_conn.c WCF_*) */
    int shown = wf_page() && !pws.on, want = shown && wf_pg_on(), up = wc.st == WC_UP;
    if (want && !wfs.page)
        wfs.cur = wfs.first = wfs.key_ok = 0;           /* (the page opens at the top, as bt_pg_sync) */
    wfs.page = (uint8_t)want;
    wc.scan_want = (uint8_t)want;                       /* (the manager scans while the list is on screen and nothing is joined) */
    if (wc.fail_new) {
        wc.fail_new = 0;
        if (wfs.asked || shown)
            ui_message(WHY[wc.fail % 7u]);
        wfs.asked = 0;
    }
    if (up && !wfs.was_up) {
        wf_say(wc.saved_join ? "CONNECTED " : "SAVED + CONNECTED ", wc.ap.ssid_len, wc.ap.ssid);   /* (a typed password: the manager saved the
                                                         * network when the handshake passed; a saved key was already) */
        wfs.asked = 0;
    }
    wfs.was_up = (uint8_t)up;
}

/* ================= the password entry ================= */
static int pw_on(void) { return pws.on; }
static void pw_close(void)
{
    pw_wipe(&pws.b);                                    /* (the text is gone from here; wc_request has its own copy, the manager wipes it) */
    memset(&pws, 0, sizeof pws);
    ui.force = 1;
}
static void pw_open(const uint8_t *ssid, uint32_t len)
{
    uint32_t l;
    memset(&pws, 0, sizeof pws);
    pws.ssid_len = (uint8_t)len;
    memcpy(pws.ssid, ssid, len);
    pws.on = 1;
    for (l = LY_FX; l < LY_COUNT; l++)                  /* (no layer takes the keys, as NAME: they play nothing) */
        if (l != LY_STEP)
            ly_bit[l] = 0;
    op_disarm();
    ui.toast_t = 0;
    ui.force = 1;
}
static void pw_title(char *t, uint32_t n)               /* "NAME " and the network (op_draw.c head_other shows the network) */
{
    char s[WN_SSID_MAX + 1u];
    wf_txt(s, pws.ssid, pws.ssid_len);
    str_cpy(t, "NAME ", n);
    str_cpy(t + 5, s, n - 5u);
}
static uint32_t pw_leds(uint32_t *d)
{
    *d = 0;                                             /* (the keys do nothing here) */
    return 0;
}
static void pw_done(void)                               /* SAVE held: join with what is typed */
{
    uint8_t ssid[WN_SSID_MAX];
    uint32_t len = pws.ssid_len;
    int rc;
    if (!pw_valid(&pws.b)) {
        ui_message("TOO SHORT");                        /* (WPA2: 8 characters at least; the entry stays open) */
        return;
    }
    memcpy(ssid, pws.ssid, len);
#if FELUCCA_WIFI_AP
    if (pws.ap) {                                       /* MODE AP's key: the name stays as it is (the automatic one, or the one the web editor set) */
        rc = wa_set_pass(wn_st.ap.ssid_len ? wn_st.ap.ssid : 0, wn_st.ap.ssid_len, pws.b.s);
        pw_close();
        ui.toast_next = 1;
        ui_message(rc == 0 ? "PASSWORD SET" : "TOO SHORT");
        ui.toast_next = 0;
        return;
    }
#endif
    rc = wc_request(ssid, len, pws.b.s);
    pw_close();
    ui.toast_next = 1;                                  /* (what it says: a toast, as NAME's) */
    if (rc == 0) {
        wfs.asked = 1;
        wf_say("CONNECTING ", len, ssid);
    } else {
        ui_message("WI-FI OFF");
    }
    ui.toast_next = 0;
}
/* a button's hold or tap, timed by the button's own stamps (op_state.c op_btn_ms): the ms it has been down, held or let go this pass */
static uint32_t pw_down(uint32_t held, uint32_t b, uint32_t t0)
{
    return (held & (1u << panel.btn[b])) ? fm1_ms - t0 : op_btn_span(panel.btn[b], t0);
}
static void pw_frame(uint32_t pressed, uint32_t held)  /* one UI frame (op_name.c name_frame hands it here) */
{
    uint32_t k, sb = 1u << panel.btn[B_SAVE], hb = 1u << panel.btn[B_HOME];
    int32_t s;
    while (lk_r != lk_w)
        lk_r++;                                         /* (the keys do nothing here: the queue is emptied) */
    for (k = 0; k < 3u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0) {
            if (k == 0u)
                pw_pick(&pws.b, s);                     /* KNOB 1: the character */
            else if (k == 1u)
                pw_family(&pws.b, s);                   /* KNOB 2: the family */
            else
                pw_move(&pws.b, s);                     /* KNOB 3: the cursor */
            pws.rev++;
        }
    (void)panel_enc(EN_K4);
    (void)panel_enc(EN_PRESET);                         /* (nothing else turns anything here: no stray detents left) */
    (void)panel_enc(EN_SELECT);
    (void)panel_enc(EN_ALGO);
    if (pressed & sb) {
        pws.save = 1;
        pws.save_used = 0;
        pws.save_t = op_btn_ms(panel.btn[B_SAVE], 0);
    }
    if (pressed & hb) {
        pws.home = 1;
        pws.home_used = 0;
        pws.home_t = op_btn_ms(panel.btn[B_HOME], 0);
    }
    if (pws.save && !pws.save_used && pw_down(held, B_SAVE, pws.save_t) >= op_hold_ms()) {
        pws.save_used = 1;                              /* SAVE held: DONE (at once, not at its release) */
        pw_done();
        if (!pws.on)
            return;
    }
    if (pws.home && !pws.home_used && pw_down(held, B_HOME, pws.home_t) >= NM_HOLD_MS) {
        pw_close();                                     /* HOME held: CANCEL, nothing joined */
        ui.toast_next = 1;
        ui_message("CANCELLED");
        ui.toast_next = 0;
        return;
    }
    if (pws.save && !(held & sb)) {                     /* SAVE let go before the hold: the character added */
        pws.save = 0;
        if (!pws.save_used) {
            if (pw_add(&pws.b))
                pws.rev++;
            else
                ui_message("PASSWORD FULL");
        }
    }
    if (pws.home && !(held & hb)) {                     /* HOME let go before the hold: the character before the cursor erased */
        pws.home = 0;
        if (!pws.home_used && pw_erase(&pws.b))
            pws.rev++;
    }
    if (pressed & (1u << panel.btn[B_PLAY]))
        op_play();                                      /* (PLAY stops and starts the transport as everywhere) */
}

/* ---- drawing: the header names the network (op_draw.c head_other), the cards' band is the text as typed, the panel the
 * families and their characters, the foot the gestures and the count (the NAME screen's bands: op_name.c) */
#define PW_CELLS 14u                                    /* the characters in view: 16 px each, the large face */
#define PW_CW 16
static uint32_t pw_sig(void)
{
    pws.first = (uint8_t)pw_window(&pws.b, pws.first, PW_CELLS);   /* (the window follows the cursor before the signature is taken) */
    return hu(hu(hu(hu(hu(0x9A57u + pws.b.len, pws.b.cur), pws.b.fam * 64u + pws.b.idx[pws.b.fam % PW_NFAM]), pws.rev),
                 settings.palette + 256u * song.sel), pws.first);
}
static void pw_draw_field(void)
{
    uint32_t i;
    uint16_t tc = trk_col(song.sel);
    cv_begin(240, OH_CARD, C_BLACK);
    for (i = 0; i < PW_CELLS; i++) {
        int32_t x = 8 + PW_CW * (int32_t)i;
        uint32_t j = pws.first + i;
        char c[2] = {0, 0};
        if (j < pws.b.len)
            c[0] = pws.b.s[j];
        if (c[0] == ' ')
            cv_frame(x + 3, 20, 10, 6, C_DIM);          /* a space: a small box (an underscore is a bar on the baseline) */
        else if (c[0])
            cv_text(x, 6, font_big(), c, C_WHITE);
        else
            cv_rect(x + 2, 41, 12, 1, C_LINE);          /* (an empty cell) */
    }
    if (pws.first)
        cv_texts(0, 14, "<", C_DIM);
    if (pws.first + PW_CELLS < pws.b.len)
        cv_texts(232, 14, ">", C_DIM);
    cv_rect(7 + PW_CW * (int32_t)(pws.b.cur - pws.first), 4, 2, 36, tc);   /* the cursor: before the character an add would push right */
    cv_blit(0, OY_CARD);
}
static void pw_draw_panel(void)
{
    uint32_t i, f = pws.b.fam % PW_NFAM, n = pw_fam_n(f);
    uint16_t tc = trk_col(song.sel);
    cv_begin(240, OH_PANEL, C_BLACK);
    for (i = 0; i < PW_NFAM; i++) {                     /* the families: the tabs, the one chosen lit */
        int32_t x = 60 * (int32_t)i + 30 - text_ws(PW_FAM_TAG[i]) / 2;
        cv_texts(x, 2, PW_FAM_TAG[i], i == f ? tc : C_GRAY);
        if (i == f)
            cv_rect(60 * (int32_t)i + 6, 20, 48, 2, tc);
    }
    for (i = 0; i < n; i++) {                           /* its characters, 13 a row; the one KNOB 1 is on framed */
        int32_t x = 9 + 17 * (int32_t)(i % 13u), y = 25 + 21 * (int32_t)(i / 13u);
        char c[2] = {PW_FAM[f][i], 0};
        uint32_t on = i == pws.b.idx[f];
        if (on)
            cv_frame(x, y, 17, 20, tc);
        if (c[0] == ' ')
            cv_frame(x + 4, y + 7, 9, 6, on ? C_WHITE : C_GRAY);   /* (the space: a small box, as in the text) */
        else
            cv_texts(x + 5, y + 2, c, on ? C_WHITE : C_GRAY);
    }
    cv_texts(6, 92, "Hold Save: join", C_GRAY);
    cv_texts(6, 107, "Hold Home: cancel", C_GRAY);
    cv_blit(0, OY_PANEL);
}
static void pw_draw_foot(void)
{
    char n[8];
    cv_begin(240, 18, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    cv_texts(4, 2, "Save add  Home erase", C_AMB);
    fmt_int(n, pws.b.len);
    str_app(n, "/63", sizeof n);
    cv_texts(236 - text_ws(n), 2, n, pws.b.len < PW_MIN ? C_WARN : C_OK);   /* (amber until the 8 WPA2 needs) */
    cv_blit(0, NM_FOOT_Y);
}
static void pw_draw(void)                               /* the cards' band, the panel, the hints (op_name.c name_draw hands it here) */
{
    uint32_t sig = pw_sig();
    if (sig == ui.sig[1] && sig == ui.sig[2])
        return;
    ui.sig[1] = ui.sig[2] = sig;
    pw_draw_field();
    pw_draw_panel();
    pw_draw_foot();
}
