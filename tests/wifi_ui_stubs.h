/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi manager's stand-ins for the host UI test (tests/ui_optimist_test.c with FELUCCA_WIFI=1 FELUCCA_BLE=1 FELUCCA_CDC=1
 * BLE_HW_WL82=1): the state SYSTEM > WI-FI reads (firmware/src/wifi/wifi_conn.c wc, wc_req, wc_tab; wifi_link.c wl) and the
 * calls it makes, recorded. The saved networks and the scan table are the real, pure code (wifi_nets.c). The enums are
 * wifi_conn.c's, copied: keep them in step. Included after the UI, before ui/optimist/op_wifi.c. */
#ifndef TEST_WIFI_UI_STUBS_H
#define TEST_WIFI_UI_STUBS_H
#include "../firmware/src/wifi/wifi_nets.c"
enum { WC_OFF, WC_RADIO, WC_IDLE, WC_SCAN, WC_PMK, WC_AUTH, WC_ASSOC, WC_HS, WC_UP, WC_FAIL, WC_LINK, WC_PRE };
enum { WCF_NONE, WCF_NOCAL, WCF_NOAP, WCF_AUTH, WCF_ASSOC, WCF_HS, WCF_KEY };
static struct wn_store wn_st;
static struct {
    uint8_t st, fail, ch, tries, scan_want, saved_join, joined_slot, attempt, fail_new;
    struct wn_ap ap;
} wc;
static struct {
    uint8_t on, sweeps_left, ssid_len;
    uint8_t ssid[WN_SSID_MAX];
    char pass[64];
} wc_req;
static struct wn_scan wc_tab;
static struct { struct { uint32_t ip; } ip; uint8_t joined; } wl;
/* what the page asked of the manager */
static struct { int enable_off, disconnects, requests; uint8_t ssid[WN_SSID_MAX]; uint32_t len; char pass[64]; int had_pass; int rc; } wcfk;
static uint32_t test_peers;
static uint32_t wl_peers(void) { return test_peers; }
static void wc_enable(int on) { if (!on) wcfk.enable_off++; }
static void wc_disconnect(void) { wcfk.disconnects++; wc.st = WC_IDLE; }
static int wc_request(const uint8_t *ssid, uint32_t len, const char *pass)
{
    if (wn_st.mode == WN_MODE_OFF || wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN || len < 1u || len > WN_SSID_MAX)
        return -1;
    if ((!pass || !pass[0]) && wn_find(&wn_st, ssid, len) < 0)
        return -2;
    wcfk.requests++;
    wcfk.len = len;
    memcpy(wcfk.ssid, ssid, len);
    wcfk.had_pass = pass && pass[0];
    memset(wcfk.pass, 0, sizeof wcfk.pass);
    if (pass)
        strncpy(wcfk.pass, pass, 63);
    wc_req.on = 1;                                      /* (as the manager: scanning for the name, wc_poll goes on from there) */
    wc_req.ssid_len = (uint8_t)len;
    memcpy(wc_req.ssid, ssid, len);
    wc.st = WC_SCAN;
    return 0;
}
#if FELUCCA_WIFI_AP
/* MODE AP's daemon (wifi/wifi_apd.c): the state the page reads and the calls it makes, recorded. WAS_* and WA_CH_DEFAULT are the daemon's */
enum { WAS_OFF, WAS_NOKEY, WAS_STARTING, WAS_UP, WAS_NOCAL, WAS_STOPPED };
#define WA_CH_DEFAULT 6u
static struct { uint32_t status, clients; int pass_calls, forgets, chsets, runs, on; uint8_t name[WN_SSID_MAX]; uint32_t name_len; char pass[64]; int pass_named; } wafk;
static uint32_t wa_name(uint8_t *out)
{
    static const uint8_t AUTO[8] = {'F', 'M', '1', '-', '5', '5', 'B', '0'};
    if (wn_st.ap.ssid_len) {
        memcpy(out, wn_st.ap.ssid, wn_st.ap.ssid_len);
        return wn_st.ap.ssid_len;
    }
    memcpy(out, AUTO, 8);
    return 8u;
}
static uint32_t wa_status(void)
{
    if (wn_st.mode != WN_MODE_AP && wn_st.mode != WN_MODE_AP_MAN)
        return WAS_OFF;
    if (wafk.status)
        return wafk.status;
    if (!wn_st.ap.set)
        return WAS_NOKEY;
    return wn_st.mode == WN_MODE_AP_MAN && !wafk.on ? WAS_STOPPED : WAS_UP;
}
static int wa_run(int on)
{
    if (wn_st.mode != WN_MODE_AP_MAN)
        return -1;
    if (on && !wn_st.ap.set)
        return -2;
    wafk.runs++;
    wafk.on = on != 0;
    return 0;
}
static uint32_t wa_clients(void) { return wafk.clients; }
static uint32_t wa_channel(void) { return wn_st.ap.ch ? wn_st.ap.ch : WA_CH_DEFAULT; }
static int wa_set_pass(const uint8_t *ssid, uint32_t len, const char *pass)
{
    uint8_t pmk[32];
    if (strlen(pass) < 8u || strlen(pass) > 63u)
        return -1;
    wafk.pass_calls++;
    if (wn_st.mode == WN_MODE_AP_MAN)
        wafk.on = 1;
    wafk.pass_named = len != 0;
    strncpy(wafk.pass, pass, 63);
    memset(pmk, 9, sizeof pmk);
    wn_ap_put(&wn_st, ssid, len, wn_st.ap.ch, pmk);
    settings_later = 1;
    return 0;
}
static void wa_forget_key(void) { wafk.forgets++; wn_ap_forget(&wn_st); settings_later = 1; }
static void wa_set_channel(uint32_t ch) { if (wn_ap_set_channel(&wn_st, ch) == 0) { wafk.chsets++; settings_later = 1; } }
#endif
#include "../firmware/src/ui/optimist/op_wifi.c"
#endif
