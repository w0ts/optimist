/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP authenticator on the host (firmware/src/wifi/wifi_ap.c): the 4-way handshake run against the station
 * (wifi_sta.c), so both ends are checked by the other's code. Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/wifi/wifi_crypto.c"
#include "../firmware/src/wifi/wifi_sta.c"
#include "../firmware/src/wifi/wifi_ap.c"
#include "../firmware/src/wifi/wifi_ap_dhcp.c"

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL: %s\n", what);
    }
}

static uint8_t rand_state = 0x5A;
static void test_rand(uint8_t *out, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        rand_state = (uint8_t)(rand_state * 37u + 11u);
        out[i] = rand_state;
    }
}

static const uint8_t ap_mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static const uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t ssid[4] = {'I', 'E', 'E', 'E'};
static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t mask24[4] = {255, 255, 255, 0};

/* a probe request from the station: the wildcard when nlen is 0, else the name */
static uint32_t make_probe(uint8_t *f, const uint8_t *name, uint8_t nlen)
{
    memset(f, 0, 26u + nlen);
    f[0] = 0x40;
    memcpy(f + 4, bcast, 6);
    memcpy(f + 10, sta_mac, 6);
    memcpy(f + 16, bcast, 6);
    f[24] = 0x00;
    f[25] = nlen;
    memcpy(f + 26, name, nlen);
    return 26u + nlen;
}

/* a DHCP BOOTREQUEST from mac: the message type, and the requested address when given */
static uint32_t dhcp_req_msg(uint8_t *m, uint8_t type, const uint8_t mac[6], const uint8_t *req_ip)
{
    uint32_t o = 240u;
    memset(m, 0, 300u);
    m[0] = 1;
    m[1] = 1;
    m[2] = 6;
    m[4] = 0xDE; m[5] = 0xAD; m[6] = 0xBE; m[7] = 0xEF;
    m[10] = 0x80;
    memcpy(m + 28, mac, 6);
    memcpy(m + 236, dhcp_cookie, 4);
    m[o++] = 53; m[o++] = 1; m[o++] = type;
    if (req_ip) {
        m[o++] = 50; m[o++] = 4;
        memcpy(m + o, req_ip, 4);
        o += 4;
    }
    m[o++] = 255;
    return o;
}

/* the value of an option in a reply: its length, or -1 */
static int dhcp_opt(const uint8_t *m, uint8_t tag, uint8_t *val)
{
    uint32_t i = 240u;
    while (i < 300u) {
        if (m[i] == 255u)
            break;
        if (m[i] == 0u) {
            i++;
            continue;
        }
        if (m[i] == tag) {
            memcpy(val, m + i + 2u, m[i + 1u]);
            return m[i + 1u];
        }
        i += 2u + m[i + 1u];
    }
    return -1;
}

/* One full handshake: the AP's M1 to the station, the station's M2 back, M3 out, M4 back. Returns 1 if all four
 * steps passed, and sets the station's M4 for the negative cases. */
static int run_handshake(struct wifi_ap *ap, struct wifi_sta *sta, uint8_t m4[256], uint32_t *m4_len)
{
    uint8_t buf[256];
    uint32_t n;
    int rc;

    n = wifi_ap_build_m1(ap, buf);
    rc = wifi_sta_rx_eapol(sta, buf, n, m4, m4_len);
    if (rc != 1)
        return 0;
    if (wifi_ap_rx_m2(ap, m4, *m4_len) != 0)
        return 0;
    n = wifi_ap_build_m3(ap, buf);
    rc = wifi_sta_rx_eapol(sta, buf, n, m4, m4_len);
    return rc == 1;
}

int main(void)
{
    uint8_t pmk[32], pmk_wrong[32];
    struct wifi_ap ap;
    struct wifi_sta sta;
    uint8_t m4[256];
    uint32_t m4_len = 0;
    uint8_t buf[256];
    uint32_t n;

    wc_pmk("password", ssid, sizeof(ssid), pmk);
    wc_pmk("wrongpass", ssid, sizeof(ssid), pmk_wrong);

    /* 1. the full handshake: both ends agree on the PTK and the AP's group key reaches the station */
    wifi_ap_init(&ap, ap_mac, pmk, test_rand);
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_pmk(&sta, pmk, ssid, sizeof(ssid));
    wifi_sta_set_ap(&sta, ap_mac);
    wifi_ap_start_client(&ap, sta_mac);
    check(run_handshake(&ap, &sta, m4, &m4_len), "handshake: M1 to M4 all accepted");
    check(ap.state == WIFI_AP_STATE_WAIT_M4, "AP waits for M4 after M3 is out");
    n = m4_len;
    check(n == 99u, "M4 is 99 bytes");
    check(wifi_ap_rx_m4(&ap, m4, n) == 0, "AP accepts the station's M4");
    check(ap.state == WIFI_AP_STATE_CONNECTED, "AP is connected after M4");
    check(memcmp(ap.ptk.tk, sta.ptk.tk, 16) == 0, "PTK TK agrees with the station");
    check(memcmp(ap.ptk.kck, sta.ptk.kck, 16) == 0, "PTK KCK agrees with the station");
    check(memcmp(ap.ptk.kek, sta.ptk.kek, 16) == 0, "PTK KEK agrees with the station");
    check(memcmp(sta.gtk, ap.gtk, 16) == 0, "station unwrapped the AP's group key from M3");
    check(sta.gtk_id == ap.gtk_id, "station got the AP's GTK key id");
    check(sta.state == WIFI_STA_STATE_CONNECTED, "station connected");

    /* 2. a wrong passphrase: the client's M2 MIC fails at the AP, which stays waiting */
    wifi_ap_init(&ap, ap_mac, pmk, test_rand);
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_pmk(&sta, pmk_wrong, ssid, sizeof(ssid));
    wifi_sta_set_ap(&sta, ap_mac);
    wifi_ap_start_client(&ap, sta_mac);
    n = wifi_ap_build_m1(&ap, buf);
    check(wifi_sta_rx_eapol(&sta, buf, n, m4, &m4_len) == 1, "wrong passphrase: station still answers M1");
    check(wifi_ap_rx_m2(&ap, m4, m4_len) == -1, "wrong passphrase: AP rejects M2");
    check(ap.state == WIFI_AP_STATE_WAIT_M2, "wrong passphrase: AP state unchanged");

    /* 3. a tampered M2: one byte of the SNonce flipped, the MIC fails */
    wifi_ap_init(&ap, ap_mac, pmk, test_rand);
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_pmk(&sta, pmk, ssid, sizeof(ssid));
    wifi_sta_set_ap(&sta, ap_mac);
    wifi_ap_start_client(&ap, sta_mac);
    n = wifi_ap_build_m1(&ap, buf);
    wifi_sta_rx_eapol(&sta, buf, n, m4, &m4_len);
    m4[20] ^= 0x01;
    check(wifi_ap_rx_m2(&ap, m4, m4_len) == -1, "tampered M2: AP rejects");

    /* 4. an M4 with a stale replay counter, and a tampered M4 MIC, are both refused */
    wifi_ap_init(&ap, ap_mac, pmk, test_rand);
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_pmk(&sta, pmk, ssid, sizeof(ssid));
    wifi_sta_set_ap(&sta, ap_mac);
    wifi_ap_start_client(&ap, sta_mac);
    check(run_handshake(&ap, &sta, m4, &m4_len), "M4 cases: handshake completes");
    {
        uint8_t bad[256];
        memcpy(bad, m4, m4_len);
        bad[12] ^= 0x01;
        check(wifi_ap_rx_m4(&ap, bad, m4_len) == -1, "stale replay counter in M4: AP rejects");
        memcpy(bad, m4, m4_len);
        bad[85] ^= 0x01;
        check(wifi_ap_rx_m4(&ap, bad, m4_len) == -1, "tampered M4 MIC: AP rejects");
        check(ap.state == WIFI_AP_STATE_WAIT_M4, "M4 cases: AP still waiting after rejects");
        check(wifi_ap_rx_m4(&ap, m4, m4_len) == 0, "M4 cases: the genuine M4 is accepted");
    }

    /* 5. RFC 3394 §4.1: 128-bit key data under a 128-bit KEK */
    {
        static const uint8_t kek[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
        static const uint8_t key[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        static const uint8_t want[24] = {0x1F, 0xA6, 0x8B, 0x0A, 0x81, 0x12, 0xB4, 0x47,
                                         0xAE, 0xF3, 0x4B, 0xD8, 0xFB, 0x5A, 0x7B, 0x82,
                                         0x9D, 0x3E, 0x86, 0x23, 0x71, 0xD2, 0xCF, 0xE5};
        uint8_t wrapped[24];
        wc_aes_wrap(kek, key, 16u, wrapped);
        check(memcmp(wrapped, want, 24) == 0, "RFC 3394 4.1 wrap vector");
    }

    /* 1b. M3's key data, as a supplicant checks it: the RSN element byte for byte as in the beacon, then the group key,
     * padded to a multiple of 8 (a supplicant that finds another RSN element in M3 deauthenticates) */
    {
        uint8_t fb[WIFI_AP_FRAME_MAX], kd[64], m3[256], m2[256];
        uint32_t l2 = 0, l3;
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        n = wifi_ap_build_beacon(&ap, 0, fb);
        wifi_sta_init(&sta, sta_mac, test_rand);
        wifi_sta_set_pmk(&sta, pmk, ssid, sizeof(ssid));
        wifi_sta_set_ap(&sta, ap_mac);
        wifi_ap_start_client(&ap, sta_mac);
        l3 = wifi_ap_build_m1(&ap, m3);
        wifi_sta_rx_eapol(&sta, m3, l3, m2, &l2);
        check(wifi_ap_rx_m2(&ap, m2, l2) == 0, "M3 key data: M2 accepted first");
        l3 = wifi_ap_build_m3(&ap, m3);
        check(l3 == 155u && m3[2] == 0 && m3[3] == 151u, "M3: 155 bytes, body length 151");
        check(m3[97] == 0 && m3[98] == 56u, "M3: key data length 56 (48 wrapped)");
        check(wc_aes_unwrap(ap.ptk.kek, m3 + 99, 56u, kd) != 0, "M3: the key data unwraps under the KEK");
        check(memcmp(kd, fb + 67, 22) == 0, "M3: the RSN element is the beacon's, byte for byte");
        check(kd[22] == 0xDD && kd[23] == 22 && kd[24] == 0x00 && kd[25] == 0x0F && kd[26] == 0xAC && kd[27] == 0x01 && kd[28] == ap.gtk_id,
              "M3: the GTK KDE follows with its key id");
        check(memcmp(kd + 30, ap.gtk, 16) == 0 && kd[46] == 0xDD && kd[47] == 0x00, "M3: the group key, then the pad DD 00");
    }

    /* 6. the beacon: its fixed fields and the tags in place for a 4-byte name on channel 6 */
    {
        uint8_t fb[WIFI_AP_FRAME_MAX];
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        n = wifi_ap_build_beacon(&ap, 0x0102030405060708ull, fb);
        check(n == 89u, "beacon: 89 bytes for a 4-byte name");
        check(fb[0] == 0x80 && memcmp(fb + 4, bcast, 6) == 0 && memcmp(fb + 10, ap_mac, 6) == 0 && memcmp(fb + 16, ap_mac, 6) == 0,
              "beacon: to all, from and BSSID the AP");
        check(fb[24] == 0x08 && fb[31] == 0x01, "beacon: timestamp, little-endian");
        check(fb[32] == 0x66 && fb[33] == 0x00, "beacon: interval 102 TU");
        check(fb[34] == 0x31 && fb[35] == 0x04, "beacon: ESS, privacy, short preamble and slot");
        check(fb[36] == 0x00 && fb[37] == 4 && memcmp(fb + 38, ssid, 4) == 0, "beacon: SSID tag");
        check(fb[52] == 0x03 && fb[53] == 0x01 && fb[54] == 6, "beacon: DS parameter, channel 6");
        check(fb[55] == 0x05 && fb[56] == 0x04, "beacon: TIM element");
        check(fb[67] == 0x30 && fb[68] == 0x14, "beacon: RSN IE last");
    }

    /* 7. probe requests: the wildcard and our name are answered, another name is not */
    {
        uint8_t req[40], fb[WIFI_AP_FRAME_MAX];
        uint32_t txl = 0;
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        n = make_probe(req, ssid, 0);
        check(wifi_ap_rx_mgmt(&ap, req, n, 0, fb, &txl) == 1 && txl == 83u, "probe, wildcard: answered with a 83-byte probe response");
        check(fb[0] == 0x50 && memcmp(fb + 4, sta_mac, 6) == 0, "probe answer: to the station");
        n = make_probe(req, ssid, 4);
        check(wifi_ap_rx_mgmt(&ap, req, n, 0, fb, &txl) == 1, "probe, our name: answered");
        n = make_probe(req, (const uint8_t *)"OTHER", 5);
        check(wifi_ap_rx_mgmt(&ap, req, n, 0, fb, &txl) == 0, "probe, another name: no answer");
    }

    /* 8. authentication, association and deauthentication with the station's own frames */
    {
        struct wifi_sta s8;
        struct wifi_ap_client *cl8;
        uint8_t fb[WIFI_AP_FRAME_MAX];
        uint32_t txl = 0;
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        wifi_sta_init(&s8, sta_mac, test_rand);
        wifi_sta_set_pmk(&s8, pmk, ssid, sizeof(ssid));
        wifi_sta_set_ap(&s8, ap_mac);

        n = wifi_sta_build_assoc_req(&s8, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 0, "assoc before auth: no answer");

        n = wifi_sta_build_auth(&s8, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 1 && txl == 30u, "auth: answered, 30 bytes");
        check(wifi_sta_parse_auth_resp(&s8, fb, txl) == 0, "auth: the station accepts the answer");
        cl8 = wifi_ap_find_client(&ap, sta_mac);
        check(cl8 != 0 && cl8->state == WIFI_AP_CL_AUTH, "auth: the client is in the table as AUTH");

        n = wifi_sta_build_assoc_req(&s8, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 1 && txl == 46u, "assoc: answered, 46 bytes with the rate set");
        check(wifi_sta_parse_assoc_resp(&s8, fb, txl) == 0 && s8.aid == 1, "assoc: the station accepts AID 1");
        check(fb[30] == 0x01 && fb[31] == 8 && fb[40] == 0x32 && fb[41] == 4, "assoc: the supported rates and the extended rates follow");
        cl8 = wifi_ap_find_client(&ap, sta_mac);
        check(cl8 != 0 && cl8->state == WIFI_AP_CL_ASSOC && cl8->aid == 1, "assoc: the client is ASSOC with AID 1");

        n = wifi_sta_build_deauth(&s8, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 0, "deauth: no answer");
        check(wifi_ap_find_client(&ap, sta_mac) == 0, "deauth: the slot is free");
    }

    /* 8b. an association naming another network gets no answer; the fifth station is refused (the table has 4) */
    {
        struct wifi_sta s8;
        uint8_t fb[WIFI_AP_FRAME_MAX];
        static const uint8_t other[5] = {'O', 'T', 'H', 'E', 'R'};
        uint32_t txl = 0, i;
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        wifi_sta_init(&s8, sta_mac, test_rand);
        wifi_sta_set_pmk(&s8, pmk, other, sizeof(other));
        wifi_sta_set_ap(&s8, ap_mac);
        n = wifi_sta_build_auth(&s8, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 1, "auth for the other network: answered (auth has no name)");
        n = wifi_sta_build_assoc_req(&s8, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 0, "assoc to another name: no answer");
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        for (i = 0; i < 5u; i++) {
            uint8_t fm[6] = {0x02, 0x33, 0x00, 0x00, 0x00, 0x00};
            fm[5] = (uint8_t)(i + 1u);
            wifi_sta_init(&s8, fm, test_rand);
            wifi_sta_set_ap(&s8, ap_mac);
            n = wifi_sta_build_auth(&s8, buf);
            check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 1 && fb[28] == (i < 4u ? 0 : 17),
                  i < 4u ? "auth: a station admitted" : "auth: a fifth station refused, status 17");
        }
    }

    /* 9. the handshake over the frames: the AP's M1 and M3 as the station gets them, its M2 and M4 as the AP reads them */
    {
        struct wifi_sta s9;
        uint8_t fb[WIFI_AP_FRAME_MAX], fr[WIFI_AP_FRAME_MAX];
        uint32_t txl = 0, el = 0, fl = 0;
        const uint8_t *ep;
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, sizeof(ssid), 6);
        wifi_sta_init(&s9, sta_mac, test_rand);
        wifi_sta_set_pmk(&s9, pmk, ssid, sizeof(ssid));
        wifi_sta_set_ap(&s9, ap_mac);
        s9.state = WIFI_STA_STATE_AUTH;

        n = wifi_sta_build_auth(&s9, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 1, "frames: auth answered");
        check(wifi_sta_rx_frame(&s9, fb, txl, fr, &fl) == 0 && s9.state == WIFI_STA_STATE_ASSOC, "frames: station ASSOC after the auth answer");
        n = wifi_sta_build_assoc_req(&s9, buf);
        check(wifi_ap_rx_mgmt(&ap, buf, n, 0, fb, &txl) == 1, "frames: assoc answered");
        check(wifi_sta_rx_frame(&s9, fb, txl, fr, &fl) == 0 && s9.state == WIFI_STA_STATE_WAIT_M1, "frames: station waits for M1 after the assoc answer");

        wifi_ap_start_client(&ap, sta_mac);
        n = wifi_ap_build_m1(&ap, buf);
        fl = wifi_ap_encap_eapol(&ap, sta_mac, buf, n, fb);
        check(wifi_sta_rx_frame(&s9, fb, fl, fr, &txl) == 1, "frames: station answers the AP's M1 with M2");
        ep = wifi_ap_unwrap_eapol(&ap, fr, txl, &el);
        check(ep != 0 && el == 121u, "frames: the AP reads the station's M2 frame");
        check(ep != 0 && wifi_ap_rx_m2(&ap, ep, el) == 0, "frames: the AP accepts M2 read from the frame");

        n = wifi_ap_build_m3(&ap, buf);
        fl = wifi_ap_encap_eapol(&ap, sta_mac, buf, n, fb);
        check(wifi_sta_rx_frame(&s9, fb, fl, fr, &txl) == 1, "frames: station answers M3 with M4");
        ep = wifi_ap_unwrap_eapol(&ap, fr, txl, &el);
        check(ep != 0 && el == 99u && wifi_ap_rx_m4(&ap, ep, el) == 0, "frames: the AP accepts M4 read from the frame");
        check(ap.state == WIFI_AP_STATE_CONNECTED && s9.state == WIFI_STA_STATE_CONNECTED, "frames: both ends connected");
        check(memcmp(s9.gtk, ap.gtk, 16) == 0, "frames: the group key reached the station");
    }

    /* 10. DHCP: an offer for a discover, the ack and the nak of a request, the pool's end, and a release */
    {
        struct wifi_ap_dhcp d;
        static const uint8_t server[4] = {192, 168, 4, 1};
        static const uint8_t want2[4] = {192, 168, 4, 2}, want3[4] = {192, 168, 4, 3}, want9[4] = {192, 168, 4, 9};
        static const uint8_t zero[4] = {0, 0, 0, 0};
        static const uint8_t mac2[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x66};
        static const uint8_t mac3[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x77};
        static const uint8_t mac4[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x88};
        static const uint8_t mac5[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x99};
        uint8_t m[300], r[WIFI_AP_DHCP_MSG_MAX], v[16];
        uint32_t dl;

        wifi_ap_dhcp_init(&d, server);
        dl = dhcp_req_msg(m, 1, sta_mac, 0);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == WIFI_AP_DHCP_MSG_MAX, "DHCP discover: an offer of 300 bytes");
        check(r[0] == 2 && memcmp(r + 4, m + 4, 4) == 0 && memcmp(r + 28, sta_mac, 6) == 0, "DHCP offer: BOOTREPLY, the xid, the client's hardware address");
        check(memcmp(r + 16, want2, 4) == 0, "DHCP offer: 192.168.4.2, the first address");
        check(dhcp_opt(r, 53, v) == 1 && v[0] == 2, "DHCP offer: message type 2");
        check(dhcp_opt(r, 54, v) == 4 && memcmp(v, server, 4) == 0, "DHCP offer: server identifier is the AP");
        check(dhcp_opt(r, 1, v) == 4 && memcmp(v, mask24, 4) == 0, "DHCP offer: subnet mask 255.255.255.0");
        check(dhcp_opt(r, 3, v) == 4 && memcmp(v, server, 4) == 0, "DHCP offer: router is the AP");
        check(dhcp_opt(r, 6, v) == 4 && memcmp(v, server, 4) == 0, "DHCP offer: DNS is the AP");
        check(dhcp_opt(r, 51, v) == 4, "DHCP offer: lease time");

        dl = dhcp_req_msg(m, 3, sta_mac, want2);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == WIFI_AP_DHCP_MSG_MAX && dhcp_opt(r, 53, v) == 1 && v[0] == 5, "DHCP request for the offered address: ack");
        check(memcmp(r + 16, want2, 4) == 0, "DHCP ack: the same address");

        dl = dhcp_req_msg(m, 3, sta_mac, want9);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == WIFI_AP_DHCP_MSG_MAX && dhcp_opt(r, 53, v) == 1 && v[0] == 6, "DHCP request for another address: nak");
        check(memcmp(r + 16, zero, 4) == 0, "DHCP nak: no address");

        dl = dhcp_req_msg(m, 1, mac2, 0);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == WIFI_AP_DHCP_MSG_MAX && memcmp(r + 16, want3, 4) == 0, "DHCP: a second station gets .3");
        dl = dhcp_req_msg(m, 1, mac3, 0);
        wifi_ap_dhcp_reply(&d, m, dl, r);
        dl = dhcp_req_msg(m, 1, mac4, 0);
        wifi_ap_dhcp_reply(&d, m, dl, r);
        dl = dhcp_req_msg(m, 1, mac5, 0);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == 0, "DHCP: a fifth station gets no offer (the pool is 4 addresses)");

        dl = dhcp_req_msg(m, 7, sta_mac, 0);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == 0, "DHCP release: no reply");
        dl = dhcp_req_msg(m, 1, mac5, 0);
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == WIFI_AP_DHCP_MSG_MAX && memcmp(r + 16, want2, 4) == 0, "DHCP: the released .2 goes to the fifth station");

        m[236] = 0;
        check(wifi_ap_dhcp_reply(&d, m, dl, r) == 0, "DHCP: a message without the magic cookie is ignored");
    }

    printf("wifi_ap: %d / %d checks passed\n", checks - fails, checks);
    return fails;
}
