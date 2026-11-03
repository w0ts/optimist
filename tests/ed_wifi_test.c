/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor's Wi-Fi command (firmware/src/io/editor/ed_wifi.c, cmd 112) on the host: the real saved-network store
 * (wifi_nets.c) and the real command file over a stand-in manager (wc, wl, wc_request .. as wifi_conn.c declares them).
 * Checks: the state reply (mode, state, failure, IP, the saved SSIDs with any byte in them, never a key byte), MODE
 * (saved flag, OFF stops the manager), FORGET (the joined slot follows), JOIN (the SSID and password reach wc_request intact,
 * every refusal, the password wiped from the frame and nowhere in a reply), DISCONNECT, and an unknown op getting no reply.
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/wifi/wifi_nets.c"

enum { WC_OFF, WC_RADIO, WC_IDLE, WC_SCAN, WC_PMK, WC_AUTH, WC_ASSOC, WC_HS, WC_UP, WC_FAIL, WC_LINK, WC_PRE };
enum { WCF_NONE, WCF_NOCAL, WCF_NOAP, WCF_AUTH, WCF_ASSOC, WCF_HS, WCF_KEY };
static struct { uint8_t st, fail, ch, joined_slot; struct wn_ap ap; } wc;
static struct { uint8_t on, ssid_len; uint8_t ssid[WN_SSID_MAX]; } wc_req;
static struct { struct { uint32_t ip; } ip; } wl;
static struct wn_store wn_st;
static uint8_t settings_later;
static int enabled = 1, left;
static uint8_t seen_ssid[40], seen_pass[80];
static uint32_t seen_len;
static int req_rc;
static void wc_enable(int on) { enabled = on; }
static void wc_disconnect(void) { left++; }
static int wc_request(const uint8_t *ssid, uint32_t len, const char *pass)
{
    seen_len = len;
    memcpy(seen_ssid, ssid, len);
    strcpy((char *)seen_pass, pass);
    return req_rc;
}

#if FELUCCA_WIFI_AP
/* MODE AP's daemon (wifi/wifi_apd.c): the state the command reads and the calls it makes, recorded */
enum { WAS_OFF, WAS_NOKEY, WAS_STARTING, WAS_UP, WAS_NOCAL };
static uint32_t wa_stat, wa_cl;
static int wa_pass_calls, wa_forgets, wa_chsets, wa_rc;
static char wa_seen_pass[80];
static uint8_t wa_seen_ssid[40];
static uint32_t wa_seen_len;
static uint32_t wa_status(void) { return wa_stat; }
static uint32_t wa_clients(void) { return wa_cl; }
static uint32_t wa_channel(void) { return wn_st.ap.ch ? wn_st.ap.ch : 6u; }
static uint32_t wa_name(uint8_t *out)
{
    if (wn_st.ap.ssid_len) {
        memcpy(out, wn_st.ap.ssid, wn_st.ap.ssid_len);
        return wn_st.ap.ssid_len;
    }
    memcpy(out, "FM1-55B0", 8);
    return 8u;
}
static int wa_set_pass(const uint8_t *ssid, uint32_t len, const char *pass)
{
    wa_pass_calls++;
    wa_seen_len = len;
    if (len)
        memcpy(wa_seen_ssid, ssid, len);
    strcpy(wa_seen_pass, pass);
    return wa_rc;
}
static int wa_run_calls, wa_run_last, wa_run_rc;
static int wa_run(int on) { wa_run_calls++; wa_run_last = on; return wa_run_rc; }
static void wa_forget_key(void) { wa_forgets++; wn_ap_forget(&wn_st); }
static void wa_set_channel(uint32_t ch) { wa_chsets++; wn_ap_set_channel(&wn_st, ch); }
#define APB 17u                                            /* the AP block of a state reply: cap, key, status, clients, channel, name (8 + 2 + length) */
#else
#define APB 2u                                             /* a build without MODE AP: cap 0 */
#endif
static uint32_t test_peers = 2;
#define JAMB 2u                                            /* the state's end: MODE JAM's group and hosting (0, 0 without JAM) */
#ifdef WIFI_ON_FM1B                                        /* (the trial's stack: MODE JAM and its GROUP, op 9) */
static int test_group = 3, test_host;
static int plat_wifi_group(void) { return test_group; }
static void plat_wifi_set_group(int g) { test_group = g; }
static int plat_wifi_jam_host(void) { return test_host; }
#endif
static uint32_t wl_peers(void) { return test_peers; }
static uint8_t ed_out[640];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
#include "../firmware/src/io/editor/ed_wifi.c"

static int fails, checks;
static void ok(int c, const char *what)
{
    checks++;
    if (!c) {
        fails++;
        printf("FAIL %s\n", what);
    }
}
static int cmd(const uint8_t *a, uint32_t na) { ed_n = 0; return ed_wifi(ED_WIFI, a, na); }
static void reset(void)
{
    memset(&wc, 0, sizeof wc);
    memset(&wc_req, 0, sizeof wc_req);
    memset(&wl, 0, sizeof wl);
    wn_reset(&wn_st);
    settings_later = 0;
    enabled = 1;
    left = 0;
    req_rc = 0;
    seen_len = 0;
    wc.st = WC_IDLE;
}
static void save(const uint8_t *ssid, uint32_t n, uint8_t key)
{
    uint8_t pmk[32];
    memset(pmk, key, 32);
    wn_put(&wn_st, ssid, n, pmk);
}
/* a JOIN request: op 3, slen, plen, pack7 SSID, password */
static uint8_t req_op = 3;                                  /* 3 JOIN; 5 MODE AP's password: the same frame */
static uint32_t join_req(uint8_t *r, const uint8_t *ssid, uint32_t slen, const char *pass)
{
    uint32_t n = 0, i, k, j, m;
    r[n++] = req_op;
    r[n++] = (uint8_t)slen;
    r[n++] = (uint8_t)strlen(pass);
    for (i = 0; i < slen; i += 7) {
        k = slen - i > 7 ? 7 : slen - i;
        for (m = j = 0; j < k; j++)
            m |= (uint32_t)(ssid[i + j] >> 7) << j;
        r[n++] = (uint8_t)m;
        for (j = 0; j < k; j++)
            r[n++] = ssid[i + j] & 0x7Fu;
    }
    memcpy(r + n, pass, strlen(pass));
    return n + (uint32_t)strlen(pass);
}

int main(void)
{
    uint8_t r[160], odd[10] = {'c', 'a', 'f', 0xC3, 0xA9, ' ', 0x01, 'x', 0xFF, '!'};
    uint32_t n, i, p;
    uint8_t q[4];

    /* the state: no networks */
    reset();
    q[0] = 0;
    ok(cmd(q, 1) == 1 && ed_out[0] == 0, "STATE answers");
    ok(ed_n == 4 + 2 + 5 + 1 + 1 + APB + JAMB, "STATE with nothing saved: header, ip, n, no current");
    ok(ed_out[1] == WN_MODE_AUTO || ed_out[1] == wn_st.mode, "STATE: mode");
    /* two saved networks, one with high and control bytes, the second joined */
    save((const uint8_t *)"HOME", 4, 0x5A);
    save(odd, 10, 0xA5);                                   /* now slot 0 = odd, 1 = HOME */
    wc.st = WC_UP;
    wc.joined_slot = 1;
    wc.ch = 6;
    wl.ip.ip = 0xC0A80117u;                                /* 192.168.1.23 */
    wc.ap.ssid_len = 10;
    memcpy(wc.ap.ssid, odd, 10);
    cmd(q, 1);
    ok(ed_out[2] == WC_UP && ed_out[3] == WCF_NONE && ed_out[4] == 1 && ed_out[5] == 6, "STATE: up, no failure, slot, channel");
    ok(ed_r32(ed_out + 6, 5) == 0xC0A80117u, "STATE: the IPv4 address (host order, 5 x 7 bit)");
    ok(ed_out[11] == 2, "STATE: two saved");
    ok(ed_out[12] == 0 && ed_out[13] == 10, "STATE: slot 0, 10 bytes");
    {
        uint8_t back[16];
        uint32_t pb = 10 + 2;
        ok(ed_unpack7(ed_out + 14, pb, back, 16) == 10 && !memcmp(back, odd, 10), "STATE: an SSID with any byte survives");
        p = 14 + pb;
        ok(ed_out[p] == 1 && ed_out[p + 1] == 4 && ed_unpack7(ed_out + p + 2, 5, back, 16) == 4 && !memcmp(back, "HOME", 4),
           "STATE: the second row");
        p += 2 + 5;
        ok(ed_out[p] == 10 && ed_n == p + 1 + 12 + APB + JAMB, "STATE: the current network, then the end");
    }
    for (i = 0; i < ed_n; i++)
        if (ed_out[i] == 0x5A || ed_out[i] == 0x25)
            break;                                         /* (a key byte 0x5A / 0xA5 & 0x7F = 0x25 would show here) */
    ok(i == ed_n, "STATE carries no key byte");
    wc.st = WC_FAIL;
    wc.fail = WCF_KEY;
    wl.ip.ip = 0xC0A80117u;
    cmd(q, 1);
    ok(ed_out[2] == WC_FAIL && ed_out[3] == WCF_KEY && ed_r32(ed_out + 6, 5) == 0, "STATE: failed, wrong key, no address");
    wc.st = WC_SCAN;
    wc_req.on = 1;
    wc_req.ssid_len = 3;
    memcpy(wc_req.ssid, "ABC", 3);
    cmd(q, 1);
    p = ed_n - JAMB - APB - 1 - 4;
    ok(ed_out[p] == 3, "STATE: the SSID being looked for during a request");

    /* MODE */
    reset();
    q[0] = 1; q[1] = WN_MODE_MAN;
    ok(cmd(q, 2) == 1 && ed_out[0] == 1 && ed_out[1] == 0 && ed_out[2] == WN_MODE_MAN && wn_st.mode == WN_MODE_MAN, "MODE MAN");
    ok(settings_later == 1 && enabled == 1, "MODE: saved, the manager left on");
    q[1] = WN_MODE_OFF;
    cmd(q, 2);
    ok(wn_st.mode == WN_MODE_OFF && enabled == 0, "MODE OFF stops the manager");
#ifndef WIFI_ON_FM1B
    q[1] = 6;
    cmd(q, 2);
    ok(ed_out[1] == 1 && wn_st.mode == WN_MODE_OFF, "MODE 6 refused");
    q[0] = 0;
    cmd(q, 1);
    ok(ed_out[ed_n - 2] == 0 && ed_out[ed_n - 1] == 0, "STATE ends with group 0, host 0: no JAM in this build");
    q[0] = 9; q[1] = 2;
    ok(cmd(q, 2) == 0, "GROUP (op 9): not an op without JAM");
    q[0] = 1;
#else
    q[1] = WN_MODE_JAM_AUTO;
    ok(cmd(q, 2) == 1 && ed_out[1] == 0 && ed_out[2] == WN_MODE_JAM_AUTO && wn_st.mode == WN_MODE_JAM_AUTO, "MODE JAM (7): accepted");
    q[1] = WN_MODE_JAM;
    ok(cmd(q, 2) == 1 && ed_out[1] == 0 && wn_st.mode == WN_MODE_JAM_AUTO, "an older editor's JAM JOIN (5) is JAM (7)");
    q[1] = WN_MODE_JAM_HOST;
    ok(cmd(q, 2) == 1 && ed_out[1] == 0 && wn_st.mode == WN_MODE_JAM_AUTO, "an older editor's JAM HOST (6) is JAM (7)");
    q[0] = 0;
    test_host = 1;
    cmd(q, 1);
    ok(ed_out[1] == WN_MODE_JAM_AUTO && ed_out[ed_n - 2] == 3 && ed_out[ed_n - 1] == 1, "STATE ends with the group (3) and hosting (1)");
    test_host = 0;
    q[0] = 9; q[1] = 7;
    ok(cmd(q, 2) == 1 && ed_out[0] == 9 && ed_out[1] == 0 && ed_out[2] == 7 && test_group == 7 && settings_later, "GROUP 7: set, saved");
    q[1] = 10;
    ok(cmd(q, 2) == 1 && ed_out[1] == 1 && ed_out[2] == 7 && test_group == 7, "GROUP 10: refused, the group kept");
    q[1] = 0;
    ok(cmd(q, 2) == 1 && ed_out[1] == 1 && test_group == 7, "GROUP 0: refused");
    q[0] = 1; q[1] = WN_MODE_OFF;
    cmd(q, 2);
#endif
#if !FELUCCA_WIFI_AP
    q[1] = 3;
    cmd(q, 2);
    ok(ed_out[1] == 1 && wn_st.mode == WN_MODE_OFF, "MODE 3 (AP) refused by a build without MODE AP");
#endif
    ok(cmd(q, 1) == 0 || ed_n == 0, "MODE without its argument: no reply");

    /* FORGET: the joined slot follows */
    reset();
    save((const uint8_t *)"A", 1, 1);
    save((const uint8_t *)"B", 1, 2);
    save((const uint8_t *)"C", 1, 3);                       /* order: C B A */
    wc.joined_slot = 3;                                    /* A */
    q[0] = 2; q[1] = 0;
    cmd(q, 2);
    ok(ed_out[1] == 0 && wn_count(&wn_st) == 2 && wc.joined_slot == 2 && settings_later, "FORGET slot 0: A is now slot 1 (joined_slot 2)");
    q[1] = 1;
    cmd(q, 2);
    ok(wc.joined_slot == 0 && wn_find(&wn_st, (const uint8_t *)"A", 1) < 0, "FORGET the joined one");
    q[1] = 9;
    cmd(q, 2);
    ok(ed_out[1] == 1, "FORGET a slot past the end refused");

    /* JOIN */
    reset();
    n = join_req(r, odd, 10, "p@ss W0rd!~");
    ok(cmd(r, n) == 1 && ed_out[0] == 3 && ed_out[1] == 0, "JOIN accepted");
    ok(seen_len == 10 && !memcmp(seen_ssid, odd, 10) && !strcmp((char *)seen_pass, "p@ss W0rd!~"), "JOIN: SSID and password intact");
    for (i = 0; i < n; i++)
        ok(r[i] < 0x80, "7-bit");
    ok(r[3 + 12] == 0 && r[n - 1] == 0, "JOIN: the password is wiped from the frame");
    ok(ed_n == 2, "JOIN: the reply is two bytes, no echo");
    req_rc = -1;
    n = join_req(r, (const uint8_t *)"HOME", 4, "12345678");
    cmd(r, n);
    ok(ed_out[1] == 2, "JOIN: MODE OFF -> rc 2");
    req_rc = -2;
    n = join_req(r, (const uint8_t *)"HOME", 4, "");
    cmd(r, n);
    ok(ed_out[1] == 3, "JOIN: no password and not saved -> rc 3");
    req_rc = 0;
    n = join_req(r, (const uint8_t *)"HOME", 4, "short");
    seen_len = 0;
    cmd(r, n);
    ok(ed_out[1] == 1 && seen_len == 0, "JOIN: a 5-character password refused, wc_request not called");
    n = join_req(r, (const uint8_t *)"HOME", 4, "0123456789012345678901234567890123456789012345678901234567890123");
    cmd(r, n);
    ok(ed_out[1] == 1 && seen_len == 0, "JOIN: 64 characters refused");
    n = join_req(r, (const uint8_t *)"HOME", 4, "tab\there0");
    cmd(r, n);
    ok(ed_out[1] == 1 && seen_len == 0, "JOIN: a control character refused");
    n = join_req(r, (const uint8_t *)"", 0, "12345678");
    cmd(r, n);
    ok(ed_out[1] == 1, "JOIN: an empty SSID refused");
    n = join_req(r, (const uint8_t *)"0123456789012345678901234567890123", 33, "12345678");
    cmd(r, n);
    ok(ed_out[1] == 1, "JOIN: a 33-byte SSID refused");
    n = join_req(r, (const uint8_t *)"HOME", 4, "12345678");
    ok(cmd(r, n - 3) == 1 && ed_out[1] == 1 && seen_len == 0, "JOIN: a cut frame refused");
    wc.st = WC_RADIO;
    cmd(r, n);
    ok(ed_out[1] == 4 && seen_len == 0, "JOIN while the radio starts -> rc 4");
    wc.st = WC_IDLE;
    n = join_req(r, (const uint8_t *)"HOME", 4, "");
    cmd(r, n);
    ok(ed_out[1] == 0 && seen_len == 4 && !seen_pass[0], "JOIN: a saved network by SSID alone");

    /* DISCONNECT, unknown */
    reset();
    q[0] = 4;
    wc.st = WC_IDLE;
    cmd(q, 1);
    ok(left == 0 && ed_out[1] == 0, "DISCONNECT when not joined does nothing");
    wc.st = WC_UP;
    cmd(q, 1);
    ok(left == 1, "DISCONNECT when joined");
    q[0] = 10;
    ok(cmd(q, 2) == 0, "an unknown op: no reply");
    ok(ed_wifi(111, q, 1) == 0, "another command: not ours");
    ok(ed_wifi(ED_WIFI, q, 0) == 0, "no op: no reply");

#if FELUCCA_WIFI_AP
    /* MODE AP: the state's block, MODE 3, the password (and name), the channel, the key cleared */
    reset();
    wa_stat = WAS_UP; wa_cl = 2; wa_pass_calls = wa_forgets = wa_chsets = 0; wa_rc = 0;
    wn_st.mode = WN_MODE_AP;
    wn_st.ap.set = 1;
    memset(wn_st.ap.pmk, 0x6B, 32);
    wn_st.ap.ch = 11;
    q[0] = 0;
    cmd(q, 1);
    ok(ed_out[ed_n - 1 - JAMB] == 2, "STATE: the Link peers (2) after the AP block");
    p = ed_n - JAMB - APB;
    ok(ed_out[1] == WN_MODE_AP, "STATE: mode AP");
    ok(ed_out[p] == 1 && ed_out[p + 1] == 1 && ed_out[p + 2] == WAS_UP && ed_out[p + 3] == 2 && ed_out[p + 4] == 11, "STATE: the AP block: cap, key kept, ACTIVE, 2 clients, channel 11");
    {
        uint8_t back[16];
        ok(ed_out[p + 5] == 8 && ed_unpack7(ed_out + p + 6, 10, back, 16) == 8 && !memcmp(back, "FM1-55B0", 8), "STATE: the automatic name");
    }
    for (i = 0; i < ed_n; i++)
        if (ed_out[i] == 0x6B)
            break;
    ok(i == ed_n, "STATE carries no key byte (the AP's either)");
    wn_st.ap.set = 0;
    wn_st.ap.ch = 0;
    wa_stat = WAS_NOKEY;
    cmd(q, 1);
    p = ed_n - JAMB - APB;
    ok(ed_out[p + 1] == 0 && ed_out[p + 2] == WAS_NOKEY && ed_out[p + 4] == 6, "STATE: no key kept, NO PASSWORD, the default channel 6");
    /* MODE 3 */
    reset();
    q[0] = 1; q[1] = WN_MODE_AP;
    ok(cmd(q, 2) == 1 && ed_out[1] == 0 && ed_out[2] == WN_MODE_AP && wn_st.mode == WN_MODE_AP && enabled == 0 && settings_later, "MODE AP: accepted, the station manager stopped, saved");
    q[1] = WN_MODE_AP_MAN;
    enabled = 1;
    cmd(q, 2);
    ok(ed_out[1] == 0 && wn_st.mode == WN_MODE_AP_MAN && enabled == 0, "MODE AP MAN (4): accepted, the station manager stopped");
#ifndef WIFI_ON_FM1B                                        /* (JAM's builds: 5 is JAM) */
    q[1] = 5;
    cmd(q, 2);
    ok(ed_out[1] == 1 && wn_st.mode == WN_MODE_AP_MAN, "MODE 5 refused");
#endif
    q[1] = WN_MODE_MAN;
    cmd(q, 2);
    ok(wn_st.mode == WN_MODE_MAN && enabled == 0, "... and back to MAN");
    /* AP MAN's start / stop: op 8 */
    q[0] = 8; q[1] = 1; wa_run_calls = 0; wa_run_rc = 0;
    ok(cmd(q, 2) == 1 && ed_out[0] == 8 && ed_out[1] == 0 && wa_run_calls == 1 && wa_run_last == 1, "START (op 8, 1): done");
    q[1] = 0;
    cmd(q, 2);
    ok(ed_out[1] == 0 && wa_run_last == 0, "STOP (op 8, 0): done");
    wa_run_rc = -1;
    cmd(q, 2);
    ok(ed_out[1] == 2, "op 8 outside AP MAN: rc 2");
    wa_run_rc = -2;
    q[1] = 1;
    cmd(q, 2);
    ok(ed_out[1] == 3, "op 8 with no key kept: rc 3");
    ok(cmd(q, 1) == 0, "op 8 without its argument: no reply");
    /* the password: op 5 */
    reset();
    wn_st.mode = WN_MODE_AP;
    wa_pass_calls = 0; wa_rc = 0;
    req_op = 5;
    n = join_req(r, (const uint8_t *)"", 0, "ap pass word!1");
    ok(cmd(r, n) == 1 && ed_out[0] == 5 && ed_out[1] == 0 && ed_n == 2, "AP password: accepted, a two-byte reply");
    ok(wa_pass_calls == 1 && wa_seen_len == 0 && !strcmp(wa_seen_pass, "ap pass word!1"), "... the automatic name (length 0) and the password reach the daemon intact");
    ok(r[3] == 0 && r[n - 1] == 0, "... the password is wiped from the frame");
    wa_pass_calls = 0;
    n = join_req(r, odd, 10, "12345678");
    ok(cmd(r, n) == 1 && ed_out[1] == 0 && wa_seen_len == 10 && !memcmp(wa_seen_ssid, odd, 10), "AP password with a name of any bytes: the name intact");
    n = join_req(r, (const uint8_t *)"", 0, "short");
    wa_pass_calls = 0;
    cmd(r, n);
    ok(ed_out[1] == 1 && !wa_pass_calls, "AP password: 5 characters refused, the daemon not called");
    n = join_req(r, (const uint8_t *)"", 0, "0123456789012345678901234567890123456789012345678901234567890123");
    cmd(r, n);
    ok(ed_out[1] == 1 && !wa_pass_calls, "... 64 characters refused");
    n = join_req(r, (const uint8_t *)"", 0, "tab\there0");
    cmd(r, n);
    ok(ed_out[1] == 1 && !wa_pass_calls, "... a control character refused");
    n = join_req(r, (const uint8_t *)"0123456789012345678901234567890123", 33, "12345678");
    cmd(r, n);
    ok(ed_out[1] == 1 && !wa_pass_calls, "... a 33-byte name refused");
    n = join_req(r, (const uint8_t *)"", 0, "12345678");
    ok(cmd(r, n - 3) == 1 && ed_out[1] == 1 && !wa_pass_calls, "... a cut frame refused");
    wn_st.mode = WN_MODE_MAN;
    n = join_req(r, (const uint8_t *)"", 0, "12345678");
    cmd(r, n);
    ok(ed_out[1] == 2 && !wa_pass_calls && r[3] == 0, "MODE not AP: rc 2, the daemon not called, the frame wiped all the same");
    wn_st.mode = WN_MODE_AP;
    wa_rc = -1;
    n = join_req(r, (const uint8_t *)"", 0, "12345678");
    cmd(r, n);
    ok(ed_out[1] == 1, "the daemon refusing: rc 1");
    req_op = 3;
    /* the channel: op 6 */
    reset();
    wa_chsets = 0;
    q[0] = 6; q[1] = 11;
    ok(cmd(q, 2) == 1 && ed_out[0] == 6 && ed_out[1] == 0 && ed_out[2] == 11 && wn_st.ap.ch == 11 && wa_chsets == 1, "CHANNEL 11: accepted");
    q[1] = 0;
    cmd(q, 2);
    ok(ed_out[1] == 1 && ed_out[2] == 11 && wn_st.ap.ch == 11, "CHANNEL 0 refused (the channel in use is answered)");
    q[1] = 14;
    cmd(q, 2);
    ok(ed_out[1] == 1 && wn_st.ap.ch == 11 && wa_chsets == 1, "CHANNEL 14 refused");
    ok(cmd(q, 1) == 0, "CHANNEL without its argument: no reply");
    /* the key cleared: op 7 */
    wn_st.ap.set = 1;
    q[0] = 7;
    ok(cmd(q, 1) == 1 && ed_out[0] == 7 && ed_out[1] == 0 && wn_st.ap.set == 0 && wa_forgets == 1, "CLEAR KEY: done");
#else
    q[0] = 5; r[0] = 5; r[1] = 0; r[2] = 8;
    ok(cmd(r, 11) == 0 && cmd(q, 1) == 0, "a build without MODE AP: ops 5, 6, 7 get no reply");
    q[0] = 6; q[1] = 6;
    ok(cmd(q, 2) == 0, "... (6)");
    q[0] = 7;
    ok(cmd(q, 1) == 0, "... (7)");
#endif

    printf("%d checks, %d failed\n", checks, fails);
    return fails;
}
