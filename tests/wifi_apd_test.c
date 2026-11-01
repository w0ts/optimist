/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP's daemon on the host (firmware/src/wifi/wifi_apd.c) over a stand-in radio: the frames it sends are logged, the MAC
 * registers it writes are recorded, and our own station (wifi_sta.c) and IP layer (wifi_ip.c) are the clients. Covers the
 * start from the stored key, a new password's key job, the beacons, a client's join (auth, assoc, the 4-way handshake, the
 * keys at its own WCID), DHCP, ARP and ping through the keys, several clients, group frames sent to each, a leaving client,
 * a client that starts over, a handshake that fails, the retries and the give-up, stopping with MODE, a channel change, the
 * key forgotten, no radio calibration, and the console's start. Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#define FM1_WIFI_TX_HDR 24u
#define IN_POOL
#include "../firmware/src/wifi/wifi_crypto.c"
#include "../firmware/src/wifi/wifi_sta.c"
#include "../firmware/src/wifi/wifi_ap.c"
#include "../firmware/src/wifi/wifi_ap_dhcp.c"
#include "../firmware/src/wifi/wifi_ip.c"
#include "../firmware/src/wifi/wifi_nets.c"
#include "../firmware/src/link/link.c"

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL: %s\n", what);
    }
}

/* ---- the stand-in radio and firmware */
static uint32_t fm1_ms;
static uint32_t wifi_txb[(FM1_WIFI_TX_HDR + 576u) / 4u];
uint32_t fm1_wifi_tx_wcid = 1u, fm1_wifi_tx_ack = 1u;
static uint32_t pending_len;
static void fm1_wifi_tx_prepare(uint32_t *buf, uint32_t len) { (void)buf; pending_len = len; }
#define TXMAX 96
static struct txe { uint8_t f[600]; uint32_t len, wcid, ack; } txlog[TXMAX];
static uint32_t ntx;
static int fm1_wifi_tx(uint32_t *buf)
{
    struct txe *e = &txlog[ntx % TXMAX];
    memcpy(e->f, (uint8_t *)buf + FM1_WIFI_TX_HDR, pending_len);
    e->len = pending_len;
    e->wcid = fm1_wifi_tx_wcid;
    e->ack = fm1_wifi_tx_ack;
    ntx++;
    return 0;
}
static uint32_t regs[0x8000 / 4];
static int in_client;                                       /* the clients' own station code writes MAC registers too (its key install): not ours */
static void reg_wr(uint32_t r, uint32_t v) { if (!in_client) regs[r / 4] = v; }
static uint32_t reg(uint32_t r) { return regs[r / 4]; }

static uint8_t rs = 0x21;
static void ble_hw_rand(uint8_t *out, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        rs = (uint8_t)(rs * 53u + 17u);
        out[i] = rs;
    }
}
static int radio_ok = 1, radio_calls;
static int wifi_radio_up(void) { radio_calls++; return radio_ok ? 1 : -1; }
static uint32_t settings_later;
static struct wn_store wn_st;
static int wc_enables;
static void wc_enable(int on) { (void)on; wc_enables++; }
static struct { uint8_t ap, on; struct wifi_ip ip; uint32_t n_rx; } wl;
static void wl_stop(void) { wl.on = 0; }
static void wl_start_ap(const uint8_t mac[6], uint32_t ip);
static void wa_send(uint32_t len);
static uint32_t wa_udp_srv(struct wifi_ip *wip, const uint8_t *src_mac, const uint8_t *msg, uint32_t n, uint8_t *tx_buf);
#include "../firmware/src/wifi/wifi_apd.c"
static void wl_send_cb(uint32_t len) { wa_send(len); }
static void wl_start_ap(const uint8_t mac[6], uint32_t ip)
{
    wl.ap = 1;
    wifi_ip_ap_init(&wl.ip, mac, ip, ble_hw_rand);
    wl.ip.protect = 1;
    wl.ip.send = wl_send_cb;
    wl.ip.udp_srv = wa_udp_srv;
    wl.on = 1;
}

/* ---- helpers */
static const char *PW = "password1";
static const uint8_t NAME[7] = {'F', 'M', '1', 'T', 'E', 'S', 'T'};
static uint8_t AP_MAC[6];
static uint32_t now_ms(void) { return fm1_ms; }
static int64_t now_us(void) { return (int64_t)fm1_ms * 1000; }
static void tick(uint32_t ms) { uint32_t i; for (i = 0; i < ms; i++) { fm1_ms++; if (i % 5u == 0u) wa_poll(); } wa_poll(); }
static void run_until_up(void)
{
    uint32_t i;
    for (i = 0; i < 200u && wa.st != WA_UP; i++) {
        fm1_ms += 10u;
        wa_poll();
    }
}
static uint32_t count_sub(uint32_t from, uint8_t fc0)
{
    uint32_t i, n = 0;
    for (i = from; i < ntx; i++)
        if (txlog[i % TXMAX].f[0] == fc0)
            n++;
    return n;
}
static const struct txe *last_sub(uint8_t fc0)
{
    uint32_t i;
    for (i = ntx; i > 0 && i + TXMAX > ntx; i--)
        if (txlog[(i - 1u) % TXMAX].f[0] == fc0)
            return &txlog[(i - 1u) % TXMAX];
    return 0;
}

struct client {
    struct wifi_sta sta;
    uint8_t mac[6];
    uint32_t cur;                                           /* the next txlog entry to read */
};
static void client_init(struct client *c, uint8_t last, const uint8_t pmk[32])
{
    memset(c, 0, sizeof *c);
    c->mac[0] = 0x02; c->mac[1] = 0x11; c->mac[2] = 0x22; c->mac[3] = 0x33; c->mac[4] = 0x44; c->mac[5] = last;
    wifi_sta_init(&c->sta, c->mac, ble_hw_rand);
    wifi_sta_set_pmk(&c->sta, pmk, NAME, sizeof NAME);
    wifi_sta_set_ap(&c->sta, AP_MAC);
    c->sta.state = WIFI_STA_STATE_AUTH;
    c->cur = ntx;
}
/* what the AP sent to this client since the last call goes to the station; its answers go back to the AP */
static void pump(struct client *c)
{
    uint8_t out[600];
    uint32_t n = 0;
    int r;
    while (c->cur < ntx) {
        const struct txe *e = &txlog[c->cur % TXMAX];
        c->cur++;
        if (memcmp(e->f + 4, c->mac, 6) != 0 || e->f[0] == 0xC0)
            continue;
        in_client = 1;
        n = 0;
        r = wifi_sta_rx_frame(&c->sta, e->f, e->len, out, &n);
        in_client = 0;
        if (r == 1 && n) {
            uint8_t cp[600];
            memcpy(cp, out, n);
            wa_rx(cp, n, now_us());
        }
    }
}
static void client_join(struct client *c)
{
    uint8_t f[200];
    uint32_t n;
    n = wifi_sta_build_auth(&c->sta, f);
    wa_rx(f, n, now_us());
    pump(c);
    n = wifi_sta_build_assoc_req(&c->sta, f);
    wa_rx(f, n, now_us());
    pump(c);
    tick(WA_M1_DELAY_MS + 5u);                              /* M1 */
    pump(c);                                                /* M2 back, M3 out */
    pump(c);                                                /* M4 back */
}

/* a data frame from the client to the AP (ToDS), plain: the hardware has decrypted it */
static uint32_t cl_frame(uint8_t *f, const uint8_t mac[6], uint16_t type, uint8_t qos)
{
    uint32_t hl = qos ? 26u : 24u;
    memset(f, 0, 64);
    f[0] = qos ? 0x88 : 0x08;
    f[1] = 0x41;
    memcpy(f + 4, AP_MAC, 6);
    memcpy(f + 10, mac, 6);
    memset(f + 16, 0xFF, 6);
    f[hl] = 0xAA; f[hl + 1] = 0xAA; f[hl + 2] = 0x03;
    f[hl + 6] = (uint8_t)(type >> 8); f[hl + 7] = (uint8_t)type;
    return hl + 8u;
}
static uint16_t csum(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0, i;
    for (i = 0; i + 1u < n; i += 2u)
        s += (uint32_t)p[i] << 8 | p[i + 1u];
    if (n & 1u)
        s += (uint32_t)p[n - 1u] << 8;
    while (s >> 16)
        s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~s;
}

int main(void)
{
    uint8_t pmk[32], pmk_other[32], pmk_auto[32];
    struct client A, B, C;
    uint32_t i, mark;
    const struct txe *e;

    wifi_sta_set_test_mac_wr(reg_wr);
    wc_pmk(PW, NAME, sizeof NAME, pmk);
    wc_pmk("not-the-one", NAME, sizeof NAME, pmk_other);
    wn_reset(&wn_st);

    /* 0. the default password: a function of the name (fixed vectors; the same name, the same key; another name, another) */
    {
        char k1[16], k2[16], k3[16];
        wifi_ap_default_key((const uint8_t *)"FM1-0000", 8, k1);
        wifi_ap_default_key((const uint8_t *)"FM1-A1B2", 8, k2);
        wifi_ap_default_key((const uint8_t *)"FM1-A1B3", 8, k3);
        check(!strcmp(k1, "rxrm2jd2") && !strcmp(k2, "9kd4ctcd") && !strcmp(k3, "9fd3wpch"), "default key: the fixed vectors FM1-0000 / A1B2 / A1B3");
        check(strcmp(k2, k3) != 0 && strlen(k1) == 8u, "two names, two keys; 8 characters");
        wifi_ap_default_key((const uint8_t *)"FM1-A1B2", 8, k3);
        check(!strcmp(k2, k3), "the same name, the same key");
        wifi_ap_default_key((const uint8_t *)"", 0, k1);
        check(!strcmp(k1, "7jc84apt") && strpbrk(k1, "01oli") == 0, "an empty name has one too; no 0 1 o l i");
    }

    /* 1. MODE AP with no typed key: the default one (a function of the name) is made, nothing is kept */
    wn_st.mode = WN_MODE_AP;
    fm1_ms = 5000u;
    wa_poll();
    check(wa.st == WA_PMK && wa_status() == WAS_STARTING && ntx == 0 && radio_calls == 0 && wn_st.ap.set == 0u, "no typed key: the default key job starts, nothing kept");
    run_until_up();
    {
        char k[16];
        uint8_t p[32];
        wifi_ap_default_key(wa.ssid, wa.ssid_len, k);
        wc_pmk(k, wa.ssid, wa.ssid_len, p);
        check(wa.st == WA_UP && memcmp(p, wa.pmk, 32) == 0 && wn_st.ap.set == 0u && wa_default_key(k) == 1, "... up on the key made from the name's default password, none stored");
    }
    wn_st.mode = WN_MODE_OFF;
    check(wa_status() == WAS_OFF, "MODE not AP: status OFF");
    wn_st.mode = WN_MODE_AP;

    /* 2. a password: the key job runs a slice a pass, the key is kept, the AP comes up on the default channel */
    check(wa_set_pass(NAME, sizeof NAME, "short") == -1 && wa_set_pass(NAME, 33, PW) == -1, "a password under 8 characters and a 33-octet name refused");
    check(wa_set_pass(NAME, sizeof NAME, PW) == 0 && wa.st == WA_PMK && wa_status() == WAS_STARTING, "a password: the key job starts");
    wa_poll();
    check(wa.st == WA_PMK && wa.pass[0] == 'p', "the job runs a slice a pass, not at once");
    run_until_up();
    check(wa.st == WA_UP && wa_status() == WAS_UP, "the AP is up after the key job and the radio");
    check(wn_st.ap.set == 1u && memcmp(wn_st.ap.pmk, pmk, 32) == 0 && wn_st.ap.ssid_len == sizeof NAME, "the key kept in the store is the PBKDF2 of password and name");
    check(settings_later == 1u && wa.key_new == 1u, "the store is marked for saving; the new key is flagged for the page");
    for (i = 0; i < sizeof wa.pass && wa.pass[i] == 0; i++)
        ;
    check(i == sizeof wa.pass, "the password is wiped once the key job has it");
    check(wa.ch == 6u && wa.ssid_len == sizeof NAME && wa.mc == 0u, "default channel 6, the chosen name, group frames to each client");
    memcpy(AP_MAC, wa.ap.mac, 6);
    check(reg(0x1008) == ((uint32_t)AP_MAC[0] | (uint32_t)AP_MAC[1] << 8 | (uint32_t)AP_MAC[2] << 16 | (uint32_t)AP_MAC[3] << 24), "the radio's address is in the MAC");
    check(reg(0x1010) == reg(0x1008), "the BSSID is our address");
    check(wl.on == 1 && wl.ap == 1 && wl.ip.ip == 0xC0A80401u && wl.ip.ap == 1u, "Link's service runs as the AP at 192.168.4.1");

    /* 3. beacons every 102 ms, plain, no ACK wanted, at the unkeyed WCID */
    mark = ntx;
    tick(500);
    check(count_sub(mark, 0x80) >= 4u && count_sub(mark, 0x80) <= 6u, "about one beacon every 102 ms");
    e = last_sub(0x80);
    check(e && e->wcid == WA_PLAIN && e->ack == 0u, "a beacon: WCID 8 (never keyed), no ACK");
    check(e && e->f[37] == sizeof NAME && memcmp(e->f + 38, NAME, sizeof NAME) == 0, "the beacon carries the name");
    check(e && e->f[38 + 7 + 10 + 2] == 6, "the beacon's DS parameter: channel 6");

    /* 4. a probe request is answered, to the asker */
    {
        uint8_t pr[40];
        memset(pr, 0, sizeof pr);
        pr[0] = 0x40;
        memset(pr + 4, 0xFF, 6);
        memcpy(pr + 10, "\x02\x11\x22\x33\x44\xAA", 6);
        memset(pr + 16, 0xFF, 6);
        pr[24] = 0; pr[25] = 0;
        mark = ntx;
        wa_rx(pr, 26, now_us());
        e = last_sub(0x50);
        check(ntx == mark + 1u && e && e->wcid == WA_PLAIN && e->ack == 1u && memcmp(e->f + 4, pr + 10, 6) == 0, "a wildcard probe: one answer, plain, to the asker, with ACK");
    }

    /* 5. client A joins: the handshake over the radio, the keys in the MAC at WCID 1 */
    client_init(&A, 0xA1, pmk);
    client_join(&A);
    check(A.sta.state == WIFI_STA_STATE_CONNECTED, "A: the station reaches CONNECTED over the AP's frames");
    check(wa.keyed[0] == 1 && wa_clients() == 1u && wa.n.m2 == 1u && wa.n.m4 == 1u && wa.n.assoc == 1u, "A: M2 and M4 accepted, keyed, counters");
    check(memcmp(A.sta.gtk, wa.ap.gtk, 16) == 0, "A: the group key arrived");
    check(reg(0x6804) == 9u && reg(0x6008) == 0x20000001u && reg(0x600C) == 0u, "A: WCID 1 attribute CCMP pairwise, IV 0x20000001");
    check(reg(0x1808) == ((uint32_t)A.mac[0] | (uint32_t)A.mac[1] << 8 | (uint32_t)A.mac[2] << 16 | (uint32_t)A.mac[3] << 24) &&
          (reg(0x180C) & 0xFFFFu) == ((uint32_t)A.mac[4] | (uint32_t)A.mac[5] << 8), "A: its address is the WCID 1 address entry");
    {
        uint32_t k, same = 1;
        for (k = 0; k < 4u; k++) {
            uint32_t w = (uint32_t)A.sta.ptk.tk[4 * k] | (uint32_t)A.sta.ptk.tk[4 * k + 1] << 8 | (uint32_t)A.sta.ptk.tk[4 * k + 2] << 16 | (uint32_t)A.sta.ptk.tk[4 * k + 3] << 24;
            same &= reg(0x4020 + 4 * k) == w;
        }
        check(same, "A: the pairwise key in the MAC is the station's TK");
    }
    check(reg(0x6C00 + 32 * 1) != 0u || reg(0x6C00 + 32 * 1 + 4) != 0u, "the group key is in the shared key table");
    check(reg(0x7000) == (4u << 4), "the shared key mode: CCMP for key id 1");
    check(wa.t_due[0] == 0u, "A: no handshake send left due");

    /* 6. A: DHCP, ARP and a ping through the keys */
    {
        struct wifi_ip cip;
        uint8_t f[700], o[700];
        uint32_t n, on = 0;
        wifi_ip_init(&cip, A.mac, AP_MAC, ble_hw_rand);
        cip.protect = 1;
        mark = ntx;
        n = wifi_ip_dhcp_start(&cip, f);
        wa_rx(f, n, now_us());
        check(ntx == mark + 1u && txlog[mark % TXMAX].wcid == 1u && txlog[mark % TXMAX].ack == 1u && (txlog[mark % TXMAX].f[1] & 0x40), "DHCP Discover: an Offer back, protected, at WCID 1, unicast");
        memcpy(o, txlog[mark % TXMAX].f, txlog[mark % TXMAX].len);
        n = txlog[mark % TXMAX].len;
        check(wifi_ip_rx_frame(&cip, o, n, now_us(), f, &on) == 1 && on > 0u, "the client takes the Offer and asks (Request)");
        mark = ntx;
        wa_rx(f, on, now_us());
        n = txlog[mark % TXMAX].len;
        memcpy(o, txlog[mark % TXMAX].f, n);
        wifi_ip_rx_frame(&cip, o, n, now_us(), f, &on);
        check(cip.dhcp_state == WIFI_IP_DHCP_BOUND && cip.ip == 0xC0A80402u && cip.gw == 0xC0A80401u, "the Ack: bound to 192.168.4.2, the AP as the router");
        check(wa.n.dhcp == 2u, "the server counted two replies");

        /* ARP request for the AP */
        {
            uint32_t off = cl_frame(f, A.mac, 0x0806, 1);
            f[off + 1] = 1; f[off + 2] = 8; f[off + 4] = 6; f[off + 5] = 4; f[off + 7] = 1;
            memcpy(f + off + 8, A.mac, 6);
            f[off + 14] = 192; f[off + 15] = 168; f[off + 16] = 4; f[off + 17] = 2;
            f[off + 24] = 192; f[off + 25] = 168; f[off + 26] = 4; f[off + 27] = 1;
            mark = ntx;
            wa_rx(f, off + 28u, now_us());
            check(ntx == mark + 1u && txlog[mark % TXMAX].wcid == 1u && txlog[mark % TXMAX].f[32 + 7] == 2, "a QoS ARP request for the AP: a reply at WCID 1");
        }
        /* ping */
        {
            uint32_t off = cl_frame(f, A.mac, 0x0800, 0), k;
            uint8_t *ip = f + off, *ic = f + off + 20;
            ip[0] = 0x45; ip[2] = 0; ip[3] = 20 + 8 + 16; ip[8] = 64; ip[9] = 1;
            ip[12] = 192; ip[13] = 168; ip[14] = 4; ip[15] = 2; ip[16] = 192; ip[17] = 168; ip[18] = 4; ip[19] = 1;
            ic[0] = 8;
            for (k = 0; k < 16u; k++)
                ic[8 + k] = (uint8_t)k;
            ic[2] = ic[3] = 0;
            { uint16_t c = csum(ic, 24); ic[2] = (uint8_t)(c >> 8); ic[3] = (uint8_t)c; }
            mark = ntx;
            wa_rx(f, off + 20u + 24u, now_us());
            check(ntx == mark + 1u && txlog[mark % TXMAX].wcid == 1u && txlog[mark % TXMAX].f[32 + 20] == 0 && csum(txlog[mark % TXMAX].f + 32 + 20, 24) == 0,
                  "a ping from A: an echo reply at WCID 1 with a good checksum");
            check(wa.n.data > 0u && wa.n.dec > 0u, "the counters saw data from A, decrypted");
            {   /* a Link datagram to the group address, from A */
                uint32_t o2 = cl_frame(f, A.mac, 0x0800, 0);
                memset(f + o2, 0, 40);
                f[o2] = 0x45; f[o2 + 3] = 20 + 8 + 4; f[o2 + 8] = 1; f[o2 + 9] = 17;
                f[o2 + 12] = 192; f[o2 + 13] = 168; f[o2 + 14] = 4; f[o2 + 15] = 2;
                f[o2 + 16] = 224; f[o2 + 17] = 76; f[o2 + 18] = 78; f[o2 + 19] = 75;
                f[o2 + 22] = 20808 >> 8; f[o2 + 23] = 20808 & 255; f[o2 + 25] = 8 + 4;
                memcpy(f + 4, AP_MAC, 6);
                wa_rx(f, o2 + 20u + 12u, now_us());
                check(wa.n.link_rx == 1u, "a datagram to Link's port is counted");
            }
        }
        /* a client we have not keyed gets nothing: its data is ignored */
        {
            uint32_t off = cl_frame(f, (const uint8_t *)"\x02\x11\x22\x33\x44\xEE", 0x0800, 0);
            f[off] = 0x45; f[off + 3] = 28; f[off + 9] = 1; f[off + 19] = 1; f[off + 16] = 192; f[off + 17] = 168; f[off + 18] = 4;
            f[off + 20] = 8;
            mark = ntx;
            fm1_ms += 2000u;
            wa_rx(f, off + 28u, now_us());
            check(ntx == mark + 1u && txlog[mark % TXMAX].f[0] == 0xC0 && txlog[mark % TXMAX].f[24] == 7 && txlog[mark % TXMAX].wcid == WA_PLAIN &&
                  memcmp(txlog[mark % TXMAX].f + 4, "\x02\x11\x22\x33\x44\xEE", 6) == 0, "data from a station that is not ours: told to join again (deauth, reason 7, plain)");
            wa_rx(f, off + 28u, now_us());
            check(ntx == mark + 1u && wa.n.stray == 1u, "... once a second at most");
            fm1_ms += 1100u;
            wa_rx(f, off + 28u, now_us());
            check(ntx == mark + 2u && wa.n.stray == 2u, "... and again after a second");
        }
    }

    /* 7. client B joins at WCID 2; group frames go to each client at its own WCID */
    client_init(&B, 0xB2, pmk);
    client_join(&B);
    check(B.sta.state == WIFI_STA_STATE_CONNECTED && wa.keyed[1] == 1 && wa_clients() == 2u, "B: joined at the second slot");
    check(reg(0x6808) == 9u && reg(0x6010) == 0x20000001u && reg(0x1810) != 0u, "B: WCID 2 attribute, IV, address entry");
    check(reg(0x4040) != reg(0x4020) || reg(0x4044) != reg(0x4024), "B: its own pairwise key");
    {
        uint8_t f[300];
        uint32_t n;
        mark = ntx;
        n = wifi_ip_send_udp(&wl.ip, WIFI_IP_LINK_MCAST_IP, WIFI_IP_LINK_PORT, (const uint8_t *)"HELLO", 5, f);
        memcpy((uint8_t *)wifi_txb + FM1_WIFI_TX_HDR, f, n);
        wl.ip.send(n);
        check(ntx == mark + 2u, "a group frame: one frame for each client");
        check(txlog[mark % TXMAX].wcid == 1u && memcmp(txlog[mark % TXMAX].f + 4, A.mac, 6) == 0 && txlog[(mark + 1u) % TXMAX].wcid == 2u &&
              memcmp(txlog[(mark + 1u) % TXMAX].f + 4, B.mac, 6) == 0, "the copies: A at WCID 1, B at WCID 2, each to its own address");
        check(txlog[mark % TXMAX].f[32 + 16] == 224 && txlog[mark % TXMAX].f[32 + 17] == 76 && (txlog[mark % TXMAX].f[1] & 0x40), "the group address stays in the IP header; Protected");
        wa.mc = 9;
        mark = ntx;
        memcpy((uint8_t *)wifi_txb + FM1_WIFI_TX_HDR, f, n);   /* (a send rewrites the buffer for each client: built again, as the IP layer does) */
        wl.ip.send(n);
        check(ntx == mark + 1u && txlog[mark % TXMAX].wcid == 9u && txlog[mark % TXMAX].ack == 0u && (txlog[mark % TXMAX].f[4] & 1), "mc 9: one group-addressed frame at WCID 9, no ACK");
        wa.mc = 0;
        check(wifi_ip_send_udp(&wl.ip, 0xC0A80409u, 1, (const uint8_t *)"x", 1, f) == 0, "an address nobody holds is not sent to");
    }

    /* 8. A leaves: its slot and its keys go; B stays */
    {
        uint8_t dea[30];
        memset(dea, 0, sizeof dea);
        dea[0] = 0xC0;
        memcpy(dea + 4, AP_MAC, 6); memcpy(dea + 10, A.mac, 6); memcpy(dea + 16, AP_MAC, 6);
        wa_rx(dea, 26, now_us());
        check(wa.keyed[0] == 0 && wa_clients() == 1u && reg(0x6804) == 0u && reg(0x6008) == 0u && reg(0x4020) == 0u, "A leaves: slot free, WCID 1 cleared");
        check(wa.keyed[1] == 1 && reg(0x6808) == 9u, "B is untouched");
    }

    /* 9. B starts over (auth again while keyed): its keys are dropped, the join goes on from the start */
    {
        uint8_t f[200];
        uint32_t n;
        B.sta.state = WIFI_STA_STATE_AUTH;
        n = wifi_sta_build_auth(&B.sta, f);
        wa_rx(f, n, now_us());
        check(wa.keyed[1] == 0 && reg(0x6808) == 0u, "B authenticates again: its old keys are dropped");
        B.cur = ntx - 1u;
        pump(&B);
        n = wifi_sta_build_assoc_req(&B.sta, f);
        wa_rx(f, n, now_us());
        pump(&B);
        tick(WA_M1_DELAY_MS + 5u);
        pump(&B);
        pump(&B);
        check(B.sta.state == WIFI_STA_STATE_CONNECTED && wa.keyed[1] == 1, "B joins again with fresh keys");
    }

    /* 10. a client with the wrong password: M2 fails its check, M1 is repeated, then the client is let go (reason 15) */
    {
        uint8_t f[200];
        uint32_t n, m1s;
        client_init(&C, 0xC3, pmk_other);
        mark = ntx;
        n = wifi_sta_build_auth(&C.sta, f);
        wa_rx(f, n, now_us());
        pump(&C);
        n = wifi_sta_build_assoc_req(&C.sta, f);
        wa_rx(f, n, now_us());
        pump(&C);
        tick(WA_M1_DELAY_MS + 5u);
        pump(&C);
        check(wa.n.bad >= 1u && wa.keyed[wa_slot_of(C.mac)] == 0, "wrong password: M2 refused, not keyed");
        for (i = 0; i < 5u; i++) {
            tick(WA_HS_RETRY_MS + 20u);
            pump(&C);
        }
        m1s = 0;
        for (i = mark; i < ntx; i++)
            m1s += memcmp(txlog[i % TXMAX].f + 4, C.mac, 6) == 0 && txlog[i % TXMAX].f[0] == 0x08 && txlog[i % TXMAX].len == 32u + 99u;
        check(m1s == WA_HS_TRIES, "M1 sent WA_HS_TRIES times in all");
        e = last_sub(0xC0);
        check(e && memcmp(e->f + 4, C.mac, 6) == 0 && e->f[24] == 15 && e->wcid == WA_PLAIN, "then a deauthentication, reason 15, plain");
        check(wifi_ap_find_client(&wa.ap, C.mac) == 0, "the slot is free again");
    }

    /* 11. a channel turned: kept at once, the AP restarts when the knob has rested, on the new channel */
    wa_set_channel(0);
    wa_set_channel(14);
    check(wn_st.ap.ch == 0u, "channel 0 and 14 refused");
    wa_set_channel(11);
    check(wn_st.ap.ch == 11u && wa.ch == 6u && wa.st == WA_UP, "channel 11 kept in the store; the AP still on 6");
    tick(WA_RESTART_MS + 50u);
    run_until_up();
    check(wa.st == WA_UP && wa.ch == 11u && wa_clients() == 0u, "the AP restarted on channel 11 (the clients gone with the restart)");
    tick(150);
    e = last_sub(0x80);
    check(e && e->f[38 + 7 + 10 + 2] == 11, "the beacon says channel 11");
    check(reg(0x6808) == 0u, "the old clients' keys were cleared");

    /* 12. a client joins again after the restart; then MODE leaves AP: everyone told (reason 3), keys cleared, the AP stopped */
    client_init(&A, 0xA1, pmk);
    client_init(&A, 0xA1, pmk);
    client_join(&A);
    check(A.sta.state == WIFI_STA_STATE_CONNECTED, "A joins after the restart");
    mark = ntx;
    wn_st.mode = WN_MODE_MAN;
    wa_poll();
    check(wa.st == WA_OFF && wl.on == 0 && wl.ap == 0 && wa.by_mode == 0, "MODE left AP: the AP is off, Link's service stopped");
    e = last_sub(0xC0);
    check(count_sub(mark, 0xC0) == 1u && e && memcmp(e->f + 4, A.mac, 6) == 0 && e->f[24] == 3 && e->wcid == WA_PLAIN, "the client was told (deauth, reason 3), plain");
    check(reg(0x6804) == 0u && reg(0x4020) == 0u, "its keys are out of the MAC");
    mark = ntx;
    tick(300);
    check(ntx == mark, "no beacons while off");
    wn_st.mode = WN_MODE_AP;
    run_until_up();
    check(wa.st == WA_UP && wa.ch == 11u, "MODE AP again: the stored network comes up with no key job");

    /* 13. the typed key forgotten: the AP starts again on the default one */
    check(wa_default_key((char[16]){0}) == 0, "a typed key is kept: it replaces the default one");
    wa_forget_key();
    wa_poll();
    check(wn_st.ap.set == 0u && wn_st.ap.ch == 11u && wa_default_key((char[16]){0}) == 1, "forget: no typed key, the channel kept");
    run_until_up();
    check(wa.st == WA_UP, "and it is up again on the default password");
    check(wa_set_pass(0, 0, PW) == 0, "a new password with the automatic name");
    run_until_up();
    check(wa.st == WA_UP && wa.ssid_len == 8u && memcmp(wa.ssid, "FM1-4455", 8) == 0 && wn_st.ap.ssid_len == 0u, "the automatic name: FM1- and the last four digits of the address");

    /* 14. no radio calibration */
    wa_stop();
    wa.by_mode = 1;
    radio_ok = 0;
    fm1_ms += 10;
    wa_poll();
    for (i = 0; i < 50u && wa.st != WA_FAIL; i++) {
        fm1_ms += 10;
        wa_poll();
    }
    check(wa.st == WA_FAIL && wa_status() == WAS_NOCAL, "no radio calibration: FAIL, status NOCAL");
    radio_ok = 1;
    wa_stop();
    wa.by_mode = 0;

    /* 14b. AP MAN: off until started; a start / stop; another mode clears the switch; a typed password starts it */
    wn_st.mode = WN_MODE_AP_MAN;
    wa.user_on = 0;
    check(wn_st.ap.set == 1u, "(a key is kept)");
    fm1_ms += 20;
    wa_poll();
    tick(300);
    check(wa.st == WA_OFF && wa_status() == WAS_STOPPED, "AP MAN with a key and not started: nothing runs, status STOPPED");
    check(wa_run(1) == 0 && wa.user_on == 1, "wa_run(1): started");
    run_until_up();
    check(wa.st == WA_UP && wa_status() == WAS_UP, "... the access point comes up from the stored key");
    memcpy(pmk_auto, wn_st.ap.pmk, 32);
    client_init(&A, 0xA1, pmk_auto);
    memcpy(A.sta.ssid, wa.ssid, wa.ssid_len);
    A.sta.ssid_len = wa.ssid_len;
    client_join(&A);
    check(A.sta.state == WIFI_STA_STATE_CONNECTED && wa_clients() == 1u, "... and a client joins it");
    mark = ntx;
    check(wa_run(0) == 0, "wa_run(0): stopped");
    wa_poll();
    check(wa.st == WA_OFF && wl.on == 0 && wa_status() == WAS_STOPPED && count_sub(mark, 0xC0) == 1u, "... the client told (deauth), the AP off, status STOPPED again");
    check(wa.by_mode == 1, "... and still this mode's (a start brings it back)");
    wa_run(1);
    run_until_up();
    check(wa.st == WA_UP, "started again");
    wn_st.mode = WN_MODE_AUTO;
    wa_poll();
    check(wa.user_on == 0 && wa.st == WA_OFF, "another mode: the switch is off and the AP stopped");
    check(wa_run(1) == -1, "wa_run outside AP MAN: refused");
    wn_st.mode = WN_MODE_AP_MAN;
    wa_poll();
    check(wa.st == WA_OFF && wa_status() == WAS_STOPPED, "back in AP MAN: still stopped (never kept across a mode change)");
    wn_ap_forget(&wn_st);
    check(wa_run(1) == 0 && wa_status() == WAS_STARTING, "no typed key: wa_run(1) starts it on the default one");
    check(wa_set_pass(0, 0, PW) == 0 && wa.user_on == 1, "a typed password in AP MAN starts it");
    run_until_up();
    check(wa.st == WA_UP && wn_st.ap.set == 1u, "... the key derived and kept, the AP up");
    wa_stop();
    wa.by_mode = 0;
    wa.user_on = 0;
    wn_st.mode = WN_MODE_OFF;

    /* 15. the console's start: only with MODE OFF, a good password, a channel in range */
    wn_st.mode = WN_MODE_AP;
    check(wa_start(NAME, sizeof NAME, PW, 6, 0) == -1, "console start refused while MODE is AP");
    wn_st.mode = WN_MODE_OFF;
    check(wa_start(NAME, sizeof NAME, "short", 6, 0) == -1 && wa_start(NAME, sizeof NAME, PW, 14, 0) == -1 && wa_start(NAME, 0, PW, 6, 0) == -1, "console start: bad password, channel, name refused");
    check(wa_start(NAME, sizeof NAME, PW, 1, 0) == 0 && wa.by_mode == 0 && wa.st == WA_PMK, "console start: accepted");
    run_until_up();
    check(wa.st == WA_UP && wa.ch == 1u, "console AP up on channel 1");
    tick(500);
    check(wa.st == WA_UP, "a console AP is not stopped by MODE being OFF");
    wa_stop();
    check(wa.st == WA_OFF, "stopped");

    printf("wifi_apd: %d / %d checks passed\n", checks - fails, checks);
    return fails;
}
