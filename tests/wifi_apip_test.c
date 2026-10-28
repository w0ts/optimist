/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP's IP layer on the host (firmware/src/wifi/wifi_ip.c in AP mode, wifi_ap.c's deauth and QoS EAPOL unwrap): FromDS
 * frames out, a client's ARP request answered, a client's DHCP request handed to the server and its reply built for the
 * client's own address, Link's multicast addressed to the group, no router to send an unknown address to, a station's
 * addresses learned. The radio side (wifi_apd.c) is the hardware's. Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/wifi/wifi_crypto.c"
#include "../firmware/src/wifi/wifi_sta.c"
#include "../firmware/src/wifi/wifi_ap.c"
#include "../firmware/src/wifi/wifi_ap_dhcp.c"
#include "../firmware/src/wifi/wifi_ip.c"
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

static uint8_t rs = 0x3C;
static void test_rand(uint8_t *out, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        rs = (uint8_t)(rs * 29u + 7u);
        out[i] = rs;
    }
}

static const uint8_t ap_mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static const uint8_t cl_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint32_t AP_IP = 0xC0A80401u;                         /* 192.168.4.1 */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }

/* a station's data frame to the AP (ToDS), plain, LLC/SNAP and the type */
static uint32_t to_ds(uint8_t *f, uint16_t type, uint8_t qos)
{
    uint32_t hl = qos ? 26u : 24u;
    memset(f, 0, 64);
    f[0] = qos ? 0x88 : 0x08;
    f[1] = 0x01;
    memcpy(f + 4, ap_mac, 6);
    memcpy(f + 10, cl_mac, 6);
    memset(f + 16, 0xFF, 6);
    f[hl] = 0xAA; f[hl + 1] = 0xAA; f[hl + 2] = 0x03;
    put16(f + hl + 6, type);
    return hl + 8u;
}

/* the DHCP server the way the firmware wires it: wifi_ap_dhcp_reply, built into the client's own MAC */
static struct wifi_ap_dhcp dhcp;
static uint32_t srv_calls;
static uint32_t test_srv(struct wifi_ip *wip, const uint8_t *src_mac, const uint8_t *msg, uint32_t n, uint8_t *tx)
{
    uint8_t rep[WIFI_AP_DHCP_MSG_MAX];
    uint32_t rl = wifi_ap_dhcp_reply(&dhcp, msg, n, rep);
    (void)src_mac;
    srv_calls++;
    if (!rl)
        return 0;
    return wifi_ip_reply_udp(wip, rep + 28, 0xFFFFFFFFu, 67, 68, rep, rl, tx);
}

int main(void)
{
    struct wifi_ip wip;
    uint8_t f[700], tx[700];
    uint8_t srv[4] = {192, 168, 4, 1};
    uint32_t off, tx_len = 0, n;
    int r;

    /* 1. the AP's own address, FromDS frames */
    wifi_ip_ap_init(&wip, ap_mac, AP_IP, test_rand);
    check(wip.ap == 1 && wip.ip == AP_IP && wip.netmask == 0xFFFFFF00u, "AP init: our address, a /24");
    check(wip.dhcp_state == WIFI_IP_DHCP_BOUND && memcmp(wip.bssid, ap_mac, 6) == 0, "AP init: no DHCP client to run, BSSID = us");
    check(wifi_ip_poll(&wip, 100000000, tx) == 0, "AP: the IP poll sends nothing (no Discover, no membership report)");

    /* 2. a client's ARP request for us: answered FromDS to the client, the client's address learned */
    off = to_ds(f, 0x0806, 0);
    put16(f + off + 0, 1); put16(f + off + 2, 0x0800); f[off + 4] = 6; f[off + 5] = 4; put16(f + off + 6, 1);
    memcpy(f + off + 8, cl_mac, 6);
    put32(f + off + 14, 0xC0A80402u);
    put32(f + off + 24, AP_IP);
    r = wifi_ip_rx_frame(&wip, f, off + 28u, 1000, tx, &tx_len);
    check(r == 1 && tx_len == 32u + 28u, "ARP request for the AP: a reply of 60 bytes");
    check(tx[0] == 0x08 && tx[1] == 0x02, "ARP reply: data, FromDS (not ToDS)");
    check(memcmp(tx + 4, cl_mac, 6) == 0 && memcmp(tx + 10, ap_mac, 6) == 0 && memcmp(tx + 16, ap_mac, 6) == 0,
          "ARP reply: RA = the client, TA = BSSID = us, addr3 = us");
    check(tx[32 + 7] == 2 && memcmp(tx + 32 + 8, ap_mac, 6) == 0, "ARP reply: opcode 2, our MAC");
    check(memcmp(tx + 32 + 14, (uint8_t[]){0xC0, 0xA8, 0x04, 0x01}, 4) == 0, "ARP reply: our address");
    wip.protect = 1;
    off = to_ds(f, 0x0806, 1);                                       /* the same request in a QoS data frame */
    put16(f + off + 6, 1); memcpy(f + off + 8, cl_mac, 6); put32(f + off + 14, 0xC0A80402u); put32(f + off + 24, AP_IP);
    r = wifi_ip_rx_frame(&wip, f, off + 28u, 1000, tx, &tx_len);
    check(r == 1 && tx[1] == (0x02 | 0x40), "a QoS request is answered too; the Protected bit once keys are in");
    wip.protect = 0;

    /* 3. a client's DHCP Discover (ToDS, UDP 68 -> 67): handed to the server, the Offer built for the client */
    {
        uint8_t m[300];
        memset(m, 0, sizeof m);
        m[0] = 1; m[1] = 1; m[2] = 6;
        put32(m + 4, 0x11223344u);
        memcpy(m + 28, cl_mac, 6);
        m[236] = 0x63; m[237] = 0x82; m[238] = 0x53; m[239] = 0x63;
        m[240] = 53; m[241] = 1; m[242] = 1; m[243] = 255;
        off = to_ds(f, 0x0800, 0);
        f[off] = 0x45;
        put16(f + off + 2, 20 + 8 + 300);
        f[off + 9] = 17;
        put32(f + off + 12, 0);
        put32(f + off + 16, 0xFFFFFFFFu);
        put16(f + off + 20, 68); put16(f + off + 22, 67); put16(f + off + 24, 8 + 300);
        memcpy(f + off + 28, m, 300);
        wifi_ap_dhcp_init(&dhcp, srv);
        r = wifi_ip_rx_frame(&wip, f, off + 28u + 300u, 1000, tx, &tx_len);
        check(r == 0 && srv_calls == 0, "DHCP request with no server hook: nothing sent");
        wip.udp_srv = test_srv;
        r = wifi_ip_rx_frame(&wip, f, off + 28u + 300u, 1000, tx, &tx_len);
        check(r == 1 && srv_calls == 1, "DHCP Discover: the server is asked, a reply is built");
        check(tx_len == 32u + 20u + 8u + WIFI_AP_DHCP_MSG_MAX, "DHCP Offer: 360 bytes in all");
        check(tx[1] == 0x02 && memcmp(tx + 4, cl_mac, 6) == 0, "DHCP Offer: FromDS, to the client's own MAC");
        check(tx[32 + 9] == 17 && tx[32 + 20 + 1] == 67 && tx[32 + 20 + 3] == 68, "DHCP Offer: UDP 67 -> 68");
        check(memcmp(tx + 32 + 12, (uint8_t[]){0xC0, 0xA8, 0x04, 0x01}, 4) == 0, "DHCP Offer: from the AP's address");
        check(memcmp(tx + 32 + 28 + 16, (uint8_t[]){192, 168, 4, 2}, 4) == 0 && tx[32 + 28] == 2, "DHCP Offer: BOOTREPLY, 192.168.4.2");
        r = wifi_ip_early_filter(f, off + 28u + 300u, ap_mac, AP_IP, 20808);
        check(r == 1, "the early filter keeps a DHCP request (port 67)");
    }

    /* 4. Link's multicast goes to the group address; an unknown unicast has nowhere to go; a learned one does */
    wip.protect = 1;
    n = wifi_ip_send_udp(&wip, WIFI_IP_LINK_MCAST_IP, WIFI_IP_LINK_PORT, (const uint8_t *)"ABCD", 4, tx);
    check(n == 32u + 20u + 8u + 4u, "multicast datagram: 64 bytes");
    check(tx[1] == (0x02 | 0x40), "multicast: FromDS, Protected");
    check(tx[4] == 0x01 && tx[5] == 0x00 && tx[6] == 0x5E && tx[7] == 0x4C && tx[8] == 0x4E && tx[9] == 0x4B,
          "multicast: RA = 01:00:5E:4C:4E:4B");
    check(memcmp(tx + 10, ap_mac, 6) == 0 && memcmp(tx + 16, ap_mac, 6) == 0, "multicast: TA and SA = us");
    check(wifi_ip_send_udp(&wip, 0xC0A80409u, 1234, (const uint8_t *)"x", 1, tx) == 0, "AP: an address nobody has is not sent to (no router)");
    wifi_ip_arp_learn(&wip, 0xC0A80409u, cl_mac);
    n = wifi_ip_send_udp(&wip, 0xC0A80409u, 1234, (const uint8_t *)"x", 1, tx);
    check(n == 32u + 20u + 8u + 1u && memcmp(tx + 4, cl_mac, 6) == 0, "a learned address: sent to its station");
    check(wifi_ip_reply_udp(&wip, cl_mac, 0xFFFFFFFFu, 67, 68, (const uint8_t *)"zz", 2, tx) == 32u + 20u + 8u + 2u, "reply_udp: any ports");

    /* 5. the station's behaviour is unchanged: ToDS, the router as a fallback */
    {
        struct wifi_ip st;
        uint8_t bssid[6] = {0x06, 0x01, 0x02, 0x03, 0x04, 0x05};
        wifi_ip_init(&st, cl_mac, bssid, test_rand);
        st.ip = 0xC0A80164u;
        n = wifi_ip_send_udp(&st, 0xC0A80101u, 5, (const uint8_t *)"s", 1, tx);
        check(n == 32u + 20u + 8u + 1u && tx[1] == 0x01 && memcmp(tx + 4, bssid, 6) == 0 && memcmp(tx + 16, bssid, 6) == 0,
              "station: ToDS to the BSSID, an unknown address goes to the BSSID as before");
    }

    /* 6. wifi_ap.c: a QoS EAPOL frame from an associated station is unwrapped; a deauth is built */
    {
        struct wifi_ap ap;
        uint8_t ssid[4] = {'F', 'M', '1', 'x'}, pmk[32] = {1}, fr[96], pr[32];
        uint32_t el = 0, dl;
        const uint8_t *e;
        wifi_ap_init(&ap, ap_mac, pmk, test_rand);
        wifi_ap_set_net(&ap, ssid, 4, 6);
        memset(fr, 0, sizeof fr);
        fr[0] = 0x0B; fr[1] = 0; memcpy(fr + 4, ap_mac, 6); memcpy(fr + 10, cl_mac, 6); memcpy(fr + 16, ap_mac, 6);
        wifi_ap_rx_mgmt(&ap, (uint8_t[30]){0xB0, 0, 0, 0, 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                                          0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0, 0, 0, 0, 1, 0, 0, 0}, 30, 1, pr, &dl);
        memset(fr, 0, sizeof fr);
        fr[0] = 0x88; fr[1] = 0x01; memcpy(fr + 4, ap_mac, 6); memcpy(fr + 10, cl_mac, 6);
        fr[26] = 0xAA; fr[27] = 0xAA; fr[28] = 0x03; fr[32] = 0x88; fr[33] = 0x8E; fr[34] = 0x01; fr[35] = 0x03;
        e = wifi_ap_unwrap_eapol(&ap, fr, 60, &el);
        check(e == 0, "EAPOL from a station that is only authenticated: refused");
        ap.clients[0].state = WIFI_AP_CL_ASSOC;
        e = wifi_ap_unwrap_eapol(&ap, fr, 60, &el);
        check(e == fr + 34 && el == 60u - 34u, "QoS EAPOL from an associated station: the packet after the 26 + 8 bytes");
        fr[0] = 0x08;
        memmove(fr + 24, fr + 26, 34);
        e = wifi_ap_unwrap_eapol(&ap, fr, 58, &el);
        check(e == fr + 32 && el == 58u - 32u, "plain EAPOL: after the 24 + 8 bytes");
        dl = wifi_ap_build_deauth(&ap, cl_mac, 15, pr);
        check(dl == 26 && pr[0] == 0xC0 && memcmp(pr + 4, cl_mac, 6) == 0 && memcmp(pr + 10, ap_mac, 6) == 0 && memcmp(pr + 16, ap_mac, 6) == 0
              && pr[24] == 15 && pr[25] == 0, "deauth: management 0xC0 to the client from the AP, reason 15");
    }

    /* 7. a ping to the AP is answered: the echo reply, checksums right, to the sender's own MAC and address */
    {
        uint8_t req[200];
        uint32_t o2 = to_ds(req, 0x0800, 0), k;
        uint8_t *ip = req + o2, *ic = ip + 20;
        wip.protect = 0;
        ip[0] = 0x45; put16(ip + 2, 20 + 8 + 32); ip[8] = 64; ip[9] = 1;
        put32(ip + 12, 0xC0A80402u); put32(ip + 16, AP_IP);
        ic[0] = 8; ic[1] = 0; put16(ic + 4, 0x1234); put16(ic + 6, 7);
        for (k = 0; k < 32u; k++)
            ic[8 + k] = (uint8_t)(0x40 + k);
        put16(ic + 2, 0);
        put16(ic + 2, ip_chksum(ic, 40));
        check(wifi_ip_early_filter(req, o2 + 20u + 40u, ap_mac, AP_IP, 20808) == 1, "the early filter keeps a ping to our address");
        r = wifi_ip_rx_frame(&wip, req, o2 + 20u + 40u, 1000, tx, &tx_len);
        check(r == 1 && tx_len == 32u + 20u + 40u, "ping: an echo reply of the same size");
        check(tx[1] == 0x02 && memcmp(tx + 4, cl_mac, 6) == 0, "ping: FromDS to the sender");
        check(tx[32 + 9] == 1 && memcmp(tx + 32 + 12, (uint8_t[]){192, 168, 4, 1}, 4) == 0 && memcmp(tx + 32 + 16, (uint8_t[]){192, 168, 4, 2}, 4) == 0,
              "ping: from the AP to the sender's address");
        check(ip_chksum(tx + 32, 20) == 0 && ip_chksum(tx + 32 + 20, 40) == 0, "ping: both checksums are right");
        check(tx[32 + 20] == 0 && memcmp(tx + 32 + 24, ic + 4, 4) == 0 && memcmp(tx + 32 + 28, ic + 8, 32) == 0, "ping: type 0, the id, the sequence and the data echoed");
        put32(ip + 16, 0xC0A80409u);
        check(wifi_ip_early_filter(req, o2 + 20u + 40u, ap_mac, AP_IP, 20808) == 0, "a ping to someone else is dropped by the filter");
        r = wifi_ip_rx_frame(&wip, req, o2 + 20u + 40u, 1000, tx, &tx_len);
        check(r == 0, "a ping to someone else is not answered");
        put32(ip + 16, AP_IP);
        wip.ap = 0;
        r = wifi_ip_rx_frame(&wip, req, o2 + 20u + 40u, 1000, tx, &tx_len);
        check(r != 1, "the station does not answer a ping");
        wip.ap = 1;
    }

    printf("wifi_apip: %d / %d checks passed\n", checks - fails, checks);
    return fails;
}
