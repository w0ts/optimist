/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the Wi-Fi networks (command 112; FELUCCA_WIFI with FELUCCA_BLE; docs/WIFI-UI-DESIGN.md "From the computer";
 * web/EDITOR_PROTOCOL.md "Wi-Fi"). The web editor shows the saved networks and the connection, adds one (SSID and password
 * typed on the computer), forgets one, sets the mode. INFO tag 0x67 lists opti_wifi_1.
 * Included by felucca.c AFTER console.c (wifi/wifi_conn.c's wc, wl, wc_request .. are defined there); editor.c only lists
 * ed_wifi in its table of commands (ED_WIFI_FN, ed_opti.h declares it).
 *   112 WIFI  0                     -> 0, mode, st, fail, joined, ch, ip (5 x 7 bit, host order), n, n x (slot, len, pack7 SSID),
 *                                      len, pack7 SSID (the network being joined or joined; len 0: none),
 *                                      then MODE AP's block: cap (0: this build has no MODE AP, nothing follows), key (1: a password is
 *                                      kept), status (0 OFF, 1 NO PASSWORD, 2 STARTING, 3 ACTIVE, 4 NO CAL, 5 STOPPED: AP MAN, not started), clients, channel (the one
 *                                      shown: the store's, else 6), len, pack7 name (the stored one, else FM1- and four digits);
 *                                      then peers (the Link peers heard; an older firmware sends no such byte: 0);
 *                                      then group (MODE JANK's GROUP 1..9; 0: this build has no JANK) and heard (the FM-1s of the group
 *                                      heard; an older firmware sends neither: 0, 0; before 2026-11-04 this byte was host)
 *             1, mode               -> 1, rc, mode: MODE (0 OFF, 1 AUTO, 2 MAN, 3 AP AUTO, 4 AP MAN, the two only with cap 1; 7 JANK where
 *                                      group is not 0 (an older editor's 5 JANK JOIN and 6 JANK HOST are JANK too); the store's numbers);
 *                                      saved with the settings
 *             9, group              -> 9, rc, group: MODE JANK's GROUP 1..9 (rc 1: out of range, or no JANK in this build); saved
 *             2, slot               -> 2, rc, slot: that saved network forgotten (its key too)
 *             3, slen, plen, pack7 SSID, password (plen bytes, 32..126; 0 = a saved network of that SSID by its stored key)
 *                                   -> 3, rc: the manager scans, joins the strongest AP of the SSID and saves the network once
 *                                      the 4-way handshake accepted the key (wifi_conn.c); the state says how it goes
 *             4                     -> 4, rc: let go of the joined network
 *             5, slen, plen, pack7 name, password (plen bytes, 32..126, 8..63; slen 0 = the automatic name)
 *                                   -> 5, rc: MODE AP's password (and name): the access point stops, the key is derived (a slice a pass),
 *                                      kept and the access point starts again; the state says how it goes
 *             6, channel            -> 6, rc, channel: MODE AP's channel 1..13 (the access point restarts when it has rested)
 *             7                     -> 7, rc: MODE AP's key cleared (no access point runs without one)
 *             8, on                 -> 8, rc: AP MAN's start (on 1) or stop (0); rc 2 MODE is not AP MAN, 3 no key kept
 * rc: 0 done, 1 arguments (or no MODE AP in this build), 2 MODE OFF (5: MODE is not AP), 3 no password and not a saved network, 4 the radio
 * is not up yet (the mode was just set: ask again in a few seconds). The key bytes (PMK) never leave the device. The password is copied once, handed to wc_request and
 * wiped, here and in the receive buffer; it is not echoed, logged or kept (the manager wipes its own copy once the key is derived). */
#if FELUCCA_WIFI_AP && !defined(WA_CH_AUTO)
#define WA_CH_AUTO 0                                       /* (Optimist's own stack: no CHANNEL AUTO; op_wifi.c the same) */
static int wa_channel_auto(void) { return 0; }
#endif
#ifdef WIFI_ON_FM1B
#define EDWF_JANK 1                                         /* (the trial's Wi-Fi stack: MODE JANK, op 1 value 7, op 9 its GROUP) */
#else
#define EDWF_JANK 0
#endif
#ifndef FELUCCA_WIFI_AP
#define FELUCCA_WIFI_AP 0                                  /* (the builder's configuration always says; a host test may not) */
#endif
enum { ED_WIFI = 112 };
_Static_assert(WN_SLOTS == 4u && WN_SSID_MAX == 32u, "WIFI 0: the state fits one reply");
_Static_assert(WC_OFF == 0 && WC_RADIO == 1 && WC_IDLE == 2 && WC_SCAN == 3 && WC_PMK == 4 && WC_AUTH == 5 && WC_ASSOC == 6 &&
               WC_HS == 7 && WC_UP == 8 && WC_FAIL == 9 && WC_LINK == 10 && WC_PRE == 11, "EDITOR_PROTOCOL: Wi-Fi states");
_Static_assert(WCF_NONE == 0 && WCF_NOCAL == 1 && WCF_NOAP == 2 && WCF_AUTH == 3 && WCF_ASSOC == 4 && WCF_HS == 5 && WCF_KEY == 6,
               "EDITOR_PROTOCOL: Wi-Fi failures");
#include "ed_pack7.h"

static void edwf_ssid(const uint8_t *s, uint32_t n)         /* length, then the SSID as pack7 */
{
    ed_b(n);
    ed_pack7(s, n);
}

#if FELUCCA_WIFI_AP
static void edwf_ap_block(void)
{
    uint8_t nm[WN_SSID_MAX];
    ed_b4(1, wn_st.ap.set, wa_status(), wa_clients());
    ed_b(wa_channel() | (wa_channel_auto() ? 0x40u : 0u));   /* (the channel in use; 0x40: CHANNEL AUTO, 2026-11-03) */
    edwf_ssid(nm, wa_name(nm));
}
/* 5: MODE AP's password; the received frame's password bytes wiped by the caller (edwf_ap_key) */
static uint32_t edwf_ap_key1(const uint8_t *a, uint32_t na)
{
    uint8_t ssid[WN_SSID_MAX + 8u];
    char pass[64];
    uint32_t slen = a[1], plen = a[2], pb = slen + (slen + 6u) / 7u, i, rc;
    if (slen > WN_SSID_MAX || plen < 8u || plen > 63u || na < 3u + pb + plen)
        return 1;
    for (i = 0; i < plen; i++)
        if (a[3u + pb + i] < 32u || a[3u + pb + i] > 126u)
            return 1;
    if (slen && ed_unpack7(a + 3, pb, ssid, sizeof ssid) != slen)
        return 1;
    if (wn_st.mode != WN_MODE_AP && wn_st.mode != WN_MODE_AP_MAN)
        return 2;
    for (i = 0; i < plen; i++)
        pass[i] = (char)a[3u + pb + i];
    pass[plen] = 0;
    rc = wa_set_pass(slen ? ssid : 0, slen, pass) == 0 ? 0u : 1u;
    memset(pass, 0, sizeof pass);
    return rc;
}
static uint32_t edwf_ap_key(const uint8_t *a, uint32_t na)
{
    uint32_t rc = edwf_ap_key1(a, na), slen = na > 2u ? a[1] : 0u, plen = na > 2u ? a[2] : 0u, pb = slen + (slen + 6u) / 7u;
    if (slen <= WN_SSID_MAX && plen <= 63u && na >= 3u + pb + plen) {
        memset((uint8_t *)a + 3u + pb, 0, plen);
        __asm__ volatile("" ::: "memory");
    }
    return rc;
}
#else
static void edwf_ap_block(void) { ed_b(0); }
#endif

static void edwf_state(void)
{
    uint32_t i, n = wn_count(&wn_st);
    int busy = wc.st >= WC_PMK && wc.st != WC_FAIL;          /* (PMK .. PRE, UP: wc.ap is the network in hand) */
    ed_b4(0, wn_st.mode, wc.st, wc.fail);
    ed_b2(wc.joined_slot, wc.ch);
    ed_b32(wc.st == WC_UP ? wl.ip.ip : 0u, 5);
    ed_b(n);
    for (i = 0; i < WN_SLOTS; i++)
        if (wn_st.n[i].used) {
            ed_b(i);
            edwf_ssid(wn_st.n[i].ssid, wn_st.n[i].ssid_len);
        }
    if (busy)
        edwf_ssid(wc.ap.ssid, wc.ap.ssid_len);
    else if (wc_req.on)
        edwf_ssid(wc_req.ssid, wc_req.ssid_len);
    else
        ed_b(0);
    edwf_ap_block();
    ed_b(wl_peers() > 127u ? 127u : wl_peers());          /* the Link peers heard, after the AP block */
#if EDWF_JANK
    ed_b2((uint32_t)plat_wifi_group(), plat_wifi_jank() > 0 ? (uint32_t)plat_wifi_jank() : 0u);   /* MODE JANK's GROUP, the FM-1s heard */
#else
    ed_b2(0, 0);                                          /* (no JANK in this build) */
#endif
}

static void edwf_forget(uint32_t slot)                        /* wc.joined_slot is 1 + the slot: the rest close up */
{
    wn_forget(&wn_st, slot);
    settings_later = 1;
    if (wc.joined_slot == slot + 1u)
        wc.joined_slot = 0;
    else if (wc.joined_slot > slot + 1u)
        wc.joined_slot--;
}

static uint32_t edwf_join1(const uint8_t *a, uint32_t na)
{
    uint8_t ssid[WN_SSID_MAX + 8u];
    char pass[64];
    uint32_t slen = a[1], plen = a[2], pb = slen + (slen + 6u) / 7u, i, rc = 1;
    int r;
    if (slen < 1u || slen > WN_SSID_MAX || (plen && (plen < 8u || plen > 63u)) || na < 3u + pb + plen)
        return 1;
    for (i = 0; i < plen; i++)
        if (a[3u + pb + i] < 32u || a[3u + pb + i] > 126u)
            return 1;
    if (ed_unpack7(a + 3, pb, ssid, sizeof ssid) != slen)
        return 1;
    if (wc.st == WC_OFF || wc.st == WC_RADIO)
        return 4;
    for (i = 0; i < plen; i++)
        pass[i] = (char)a[3u + pb + i];
    pass[plen] = 0;
    r = wc_request(ssid, slen, pass);
    rc = r == 0 ? 0u : r == -1 ? 2u : r == -3 ? 4u : 3u;
    memset(pass, 0, sizeof pass);                              /* wiped: the buffer here */
    return rc;
}
/* the join, then the password's bytes in the received frame wiped on every path (an error return too: a refused request must
 * not leave what was typed in the buffer) */
static uint32_t edwf_join(const uint8_t *a, uint32_t na)
{
    uint32_t rc = edwf_join1(a, na), slen = na > 2u ? a[1] : 0u, plen = na > 2u ? a[2] : 0u, pb = slen + (slen + 6u) / 7u;
    if (slen >= 1u && slen <= WN_SSID_MAX && plen <= 63u && na >= 3u + pb + plen) {
        memset((uint8_t *)a + 3u + pb, 0, plen);
        __asm__ volatile("" ::: "memory");                     /* (the stores stay: nothing reads them after) */
    }
    return rc;
}

/* a Wi-Fi command; 0 = not one: no reply */
static int ed_wifi(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u;
    if (cmd != ED_WIFI || op > 9u || (op >= 5u && op <= 8u && !FELUCCA_WIFI_AP) || (op == 9u && !EDWF_JANK))
        return 0;
    if (op == 0u) {
        edwf_state();
        return 1;
    }
    if ((op == 1u && na < 2u) || (op == 2u && na < 2u) || (op == 3u && na < 3u) || (op == 5u && na < 3u) || (op == 6u && na < 2u) ||
        (op == 8u && na < 2u) || (op == 9u && na < 2u))
        return 0;
    switch (op) {
    case 1: {
        uint32_t m = EDWF_JANK && (a[1] == WN_MODE_JANK || a[1] == WN_MODE_JANK_HOST) ? (uint32_t)WN_MODE_JANK_AUTO : a[1];   /* (one JANK since
                                                         * 2026-11-03: an older editor's JANK JOIN / JANK HOST is it) */
        if (m > (FELUCCA_WIFI_AP ? WN_MODE_AP_MAN : WN_MODE_MAN) && !(m == WN_MODE_JANK_AUTO && EDWF_JANK)) {
            ed_b3(1, 1, wn_st.mode);
            break;
        }
        wn_st.mode = (uint8_t)m;
        if (m == WN_MODE_OFF || m == WN_MODE_AP || m == WN_MODE_AP_MAN)   /* (as the page's MODE, op_wifi.c wf_mode_set) */
            wc_enable(0);
        settings_later = 1;
        ed_b3(1, 0, wn_st.mode);
        break;
    }
#if EDWF_JANK
    case 9:
        if (a[1] >= 1u && a[1] <= 9u) {
            plat_wifi_set_group((int)a[1]);
            settings_later = 1;
            ed_b3(9, 0, a[1]);
        } else {
            ed_b3(9, 1, (uint32_t)plat_wifi_group());
        }
        break;
#endif
    case 2:
        if (a[1] < WN_SLOTS && wn_st.n[a[1]].used)
            edwf_forget(a[1]);
        ed_b3(2, a[1] >= WN_SLOTS, a[1]);          /* (a slot that holds nothing is "done": it is gone) */
        break;
    case 3:
        ed_b2(3, edwf_join(a, na));
        break;
#if FELUCCA_WIFI_AP
    case 5:
        ed_b2(5, edwf_ap_key(a, na));
        break;
    case 6:
        if (a[1] <= 13u && (a[1] || WA_CH_AUTO)) {      /* (0: AUTO, where the stack has it) */
            wa_set_channel(a[1]);
            ed_b3(6, 0, a[1]);
        } else {
            ed_b3(6, 1, wa_channel());
        }
        break;
    case 7:
        wa_forget_key();
        ed_b2(7, 0);
        break;
    case 8: {
        int r = wa_run(a[1] != 0);                       /* AP MAN's start / stop */
        ed_b2(8, r == 0 ? 0u : r == -2 ? 3u : 2u);
        break;
    }
#endif
    default:
        if (wc.st == WC_UP || wc.st == WC_LINK)
            wc_disconnect();
        ed_b2(4, 0);
        break;
    }
    return 1;
}
