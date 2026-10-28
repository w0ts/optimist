/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP on the radio (docs/WIFI-UI-DESIGN.md "MODE AP"): the FM-1 is the access point. Included by io/console.c after
 * wifi_conn.c when FELUCCA_WIFI_AP. wa_poll runs once a main-loop pass beside wc_poll; the receive loop is wl_poll's (the
 * Link service runs as it does for the station: wl.ap = 1 hands each frame to wa_rx and each frame out to wa_send).
 *
 * The pure parts are wifi_ap.c (beacon, management answers, the authenticator) and wifi_ap_dhcp.c; this file is the radio:
 *   - a beacon every 102 ms (no ACK wanted), probe / auth / assoc answered from wa_rx;
 *   - one handshake record per client slot (hs[i], from the shared net record), M1 20 ms after the association answer,
 *     M1 / M3 again after 1 s without an answer, the client dropped (deauth, reason 15) after WA_HS_TRIES sends;
 *   - the MAC's keys per client: WCID i + 1 (address 0x1800 + 8 * wcid, pairwise key 0x4000 + 32 * wcid, attribute, IV);
 *   - what is not data (beacons, management, EAPOL) goes out at WA_PLAIN, a WCID nobody keys (the MAC encrypts whatever it
 *     sends at a keyed WCID, whatever the frame's own Protected bit says);
 *   - group frames (Link's multicast): wa.mc == 0: once to every keyed client as a unicast frame carrying the group
 *     address in its IP header (the pairwise path is the one proven on this MAC); wa.mc == N: once, at WCID N set up with
 *     the group key (an experiment; WCID 0 is the MAC's drop entry).
 * Link and DHCP: the AP is 192.168.4.1, its clients 192.168.4.2.. (wifi_ap_dhcp.c). */
#define WA_PLAIN 8u                                     /* the WCID of frames that are not encrypted: never keyed */
#define WA_BEACON_MS 102u
#define WA_M1_DELAY_MS 20u                              /* association answered -> M1 (the client needs a moment to take it in) */
#define WA_HS_RETRY_MS 1000u
#define WA_HS_TRIES 4u
#define WA_IP 0xC0A80401u                               /* 192.168.4.1 */
enum { WA_OFF, WA_PMK, WA_RADIO, WA_UP, WA_FAIL };
#define WA_CH_DEFAULT 6u
#define WA_RESTART_MS 1200u                             /* a channel turned: the AP restarts once the knob has rested this long */

static struct {
    uint8_t st, ch, mc, ssid_len;
    uint8_t ssid[WIFI_STA_SSID_MAX];
    uint8_t pmk[32];
    uint8_t user_on;                                    /* AP MAN: the user started it (SAVE on the page, the web editor); never kept: off at power-on */
    uint8_t by_mode;                                    /* started by SYSTEM > WI-FI MODE AP (wn_st.mode), else by the console */
    uint8_t save_key;                                   /* the key job in hand is a new password: kept in the store when done */
    uint8_t key_new;                                    /* a new password was set and is in use (the page says it once) */
    uint8_t mac_ok, mac[6];                             /* the radio's own address (flash), once read */
    uint8_t custom;                                     /* the name in hand is not the automatic one */
    uint32_t t_restart;                                 /* fm1_ms the AP restarts (a channel change); 0: none */
    uint8_t keyed[WIFI_AP_CLIENTS];                     /* the client's keys are in the MAC */
    uint8_t tries[WIFI_AP_CLIENTS];                     /* handshake sends */
    uint32_t t_due[WIFI_AP_CLIENTS];                    /* fm1_ms of the next handshake send (0: none due) */
    uint32_t t_beacon, t_start;
    char pass[64];                                      /* until the key job has it */
    struct wc_pmk_job job;
    struct wifi_ap ap;                                  /* the network: association table, group key */
    struct wifi_ap hs[WIFI_AP_CLIENTS];                 /* each client's handshake: ANonce, PTK, replay counter */
    struct wifi_ap_dhcp dhcp;
    struct {                                            /* counters (wifiap prints them) */
        uint32_t beacon, mgmt, probe, auth, assoc, eapol, m2, m4, bad, data, dec, prot, dhcp, tx, tx_err, link_rx, link_uc, mc_tx, stray, probe_tx;
    } n;
} wa IN_POOL;

static void wa_tx_raw(uint32_t len, uint32_t wcid, uint32_t ack)
{
    fm1_wifi_tx_wcid = wcid;
    fm1_wifi_tx_ack = ack;
    fm1_wifi_tx_prepare(wifi_txb, len);
    if (fm1_wifi_tx(wifi_txb) == 0)
        wa.n.tx++;
    else
        wa.n.tx_err++;
    fm1_wifi_tx_ack = 1u;
}

static int wa_slot_of(const uint8_t mac[6])             /* the client's table slot, or -1 */
{
    const struct wifi_ap_client *cl = wifi_ap_find_client(&wa.ap, mac);
    return cl ? (int)(cl - wa.ap.clients) : -1;
}

/* a frame wl_txb holds (the IP layer's, the handshake's, Link's), out at the right WCID */
static void wa_send(uint32_t len)
{
    uint8_t *f = (uint8_t *)wifi_txb + FM1_WIFI_TX_HDR;
    uint32_t i;
    int s;
    if (!(f[0] == 0x08u && (f[1] & 0x40u))) {           /* not protected data: beacons, management, EAPOL */
        wa_tx_raw(len, WA_PLAIN, (f[4] & 1u) ? 0u : 1u);
        return;
    }
    if (f[4] & 1u) {                                    /* group-addressed (Link's multicast) */
        if (wa.mc) {
            wa_tx_raw(len, wa.mc, 0u);
            return;
        }
        for (i = 0; i < WIFI_AP_CLIENTS; i++) {
            if (!wa.keyed[i])
                continue;
            memcpy(f + 4, wa.ap.clients[i].mac, 6);
            wa_tx_raw(len, i + 1u, 1u);
            wa.n.mc_tx++;
        }
        return;
    }
    s = wa_slot_of(f + 4);
    if (s >= 0 && wa.keyed[s])
        wa_tx_raw(len, (uint32_t)s + 1u, 1u);
}

/* ---- the MAC's key tables, per client */
static void wa_key_clear(uint32_t wcid)
{
    uint32_t i;
    STA_MAC_WR(0x6800u + 4u * wcid, 0);
    STA_MAC_WR(0x6000u + 8u * wcid, 0);
    STA_MAC_WR(0x6004u + 8u * wcid, 0);
    for (i = 0; i < 8u; i++)
        STA_MAC_WR(0x4000u + 32u * wcid + 4u * i, 0);
    STA_MAC_WR(0x1800u + 8u * wcid, 0);
    STA_MAC_WR(0x1804u + 8u * wcid, 0);
}

static void wa_key_install(uint32_t wcid, const uint8_t mac[6], const uint8_t tk[16])
{
    STA_MAC_WR(0x1800u + 8u * wcid, (uint32_t)mac[0] | (uint32_t)mac[1] << 8 | (uint32_t)mac[2] << 16 | (uint32_t)mac[3] << 24);
    STA_MAC_WR(0x1804u + 8u * wcid, (uint32_t)mac[4] | (uint32_t)mac[5] << 8);
    sta_mac_wr_blk(0x4000u + 32u * wcid, tk);
    STA_MAC_WR(0x6800u + 4u * wcid, (4u << 1) | 1u);    /* CCMP, the pairwise table (as the station's WCID 1) */
    STA_MAC_WR(0x6000u + 8u * wcid, 0x20000001u);       /* IV: PN 1, ExtIV, key id 0; EIV 0 */
    STA_MAC_WR(0x6004u + 8u * wcid, 0);
}

static void wa_group_install(void)
{
    uint32_t id = wa.ap.gtk_id;
    sta_mac_wr_blk(0x6C00u + id * 32u, wa.ap.gtk);      /* the shared key table, BSS 0 */
    STA_MAC_WR(0x7000u, 4u << (id * 4u));               /* its cipher: CCMP */
    if (wa.mc) {                                        /* the experiment: a WCID whose attribute points at the shared table */
        STA_MAC_WR(0x6800u + 4u * wa.mc, 4u << 1);
        STA_MAC_WR(0x6000u + 8u * wa.mc, (id << 30) | 0x20000001u);
        STA_MAC_WR(0x6004u + 8u * wa.mc, 0);
    }
}

static void wa_client_drop(uint32_t i)                  /* its keys out of the MAC, its handshake over */
{
    wa.keyed[i] = 0;
    wa.t_due[i] = 0;
    wa.tries[i] = 0;
    wa_key_clear(i + 1u);
}

static void wa_deauth(const uint8_t mac[6], uint16_t reason)
{
    uint8_t *f = (uint8_t *)wifi_txb + FM1_WIFI_TX_HDR;
    wa_tx_raw(wifi_ap_build_deauth(&wa.ap, mac, reason, f), WA_PLAIN, 1u);
}

/* ---- DHCP: the datagram the IP layer hands over (port 67); the reply goes to the client's own MAC */
static uint32_t wa_udp_srv(struct wifi_ip *wip, const uint8_t *src_mac, const uint8_t *msg, uint32_t n, uint8_t *tx_buf)
{
    uint8_t rep[WIFI_AP_DHCP_MSG_MAX];
    uint32_t rl, yi;
    (void)src_mac;
    rl = wifi_ap_dhcp_reply(&wa.dhcp, msg, n, rep);
    if (!rl)
        return 0;
    wa.n.dhcp++;
    yi = (uint32_t)rep[16] << 24 | (uint32_t)rep[17] << 16 | (uint32_t)rep[18] << 8 | rep[19];
    if (yi)
        wifi_ip_arp_learn(wip, yi, rep + 28);           /* (chaddr: where unicast to that address goes) */
    return wifi_ip_reply_udp(wip, rep + 28, 0xFFFFFFFFu, WIFI_IP_DHCP_PORT_SERVER, WIFI_IP_DHCP_PORT_CLIENT, rep, rl, tx_buf);
}

/* ---- the handshake, one record per client */
static void wa_start_hs(uint32_t i, const uint8_t mac[6])   /* the association was answered: M1 soon */
{
    if (wa.keyed[i])
        wa_client_drop(i);
    wa.hs[i] = wa.ap;                                   /* (the shared record: key, group key, name) */
    wifi_ap_start_client(&wa.hs[i], mac);
    wa.tries[i] = 0;
    wa.t_due[i] = fm1_ms + WA_M1_DELAY_MS;
    if (!wa.t_due[i])
        wa.t_due[i] = 1u;
}

static void wa_send_eapol(uint32_t i, const uint8_t *eapol, uint32_t n)
{
    uint8_t *f = (uint8_t *)wifi_txb + FM1_WIFI_TX_HDR;
    wa_send(wifi_ap_encap_eapol(&wa.ap, wa.ap.clients[i].mac, eapol, n, f));
}

static void wa_hs_step(uint32_t i, uint32_t now)        /* M1 / M3 due: send it, or give the client up */
{
    uint8_t eb[WIFI_AP_FRAME_MAX];
    uint32_t n;
    struct wifi_ap *h = &wa.hs[i];
    if (!wa.t_due[i] || (int32_t)(now - wa.t_due[i]) < 0)
        return;
    if (wa.keyed[i] || wa.ap.clients[i].state != WIFI_AP_CL_ASSOC) {
        wa.t_due[i] = 0;
        return;
    }
    if (++wa.tries[i] > WA_HS_TRIES) {                  /* the 4-way timed out: reason 15 */
        wa_deauth(wa.ap.clients[i].mac, 15u);
        memset(&wa.ap.clients[i], 0, sizeof wa.ap.clients[i]);
        wa_client_drop(i);
        return;
    }
    n = (h->state == WIFI_AP_STATE_WAIT_M4 || h->state == WIFI_AP_STATE_PTK) ? wifi_ap_build_m3(h, eb) : wifi_ap_build_m1(h, eb);
    wa_send_eapol(i, eb, n);
    wa.t_due[i] = now + WA_HS_RETRY_MS;
}

static void wa_on_eapol(uint32_t i, const uint8_t *pkt, uint32_t n)
{
    uint8_t eb[WIFI_AP_FRAME_MAX];
    struct wifi_ap *h = &wa.hs[i];
    wa.n.eapol++;
    if (h->state == WIFI_AP_STATE_WAIT_M2) {
        if (wifi_ap_rx_m2(h, pkt, n) != 0) {
            wa.n.bad++;
            return;
        }
        wa.n.m2++;
        wa_send_eapol(i, eb, wifi_ap_build_m3(h, eb));
        wa.tries[i] = 1;
        wa.t_due[i] = fm1_ms + WA_HS_RETRY_MS;
    } else if (h->state == WIFI_AP_STATE_WAIT_M4) {
        if (wifi_ap_rx_m4(h, pkt, n) != 0) {
            wa.n.bad++;
            return;
        }
        wa.n.m4++;
        wa_key_install(i + 1u, wa.ap.clients[i].mac, h->ptk.tk);
        wa.keyed[i] = 1;
        wa.t_due[i] = 0;
    }
}

/* one 802.11 frame the radio heard (RXWI stripped) */
static void wa_rx(const uint8_t *f, uint32_t len, int64_t now)
{
    uint8_t *txf = (uint8_t *)wifi_txb + FM1_WIFI_TX_HDR;
    uint32_t tx_len = 0, hl, el;
    const uint8_t *e;
    int s, r;
    wl.n_rx++;
    if (len < 24u)
        return;
    if ((f[0] & 0x0Cu) == 0u) {                         /* management */
        uint8_t sub = f[0] & 0xFCu;
        if (sub == 0x80u || sub == 0x50u)               /* beacons, probe answers: the neighbours' */
            return;
        wa.n.mgmt++;
        s = wa_slot_of(f + 10);
        if (s >= 0 && wa.keyed[s] && (sub == 0xB0u || sub == 0x00u))
            wa_client_drop((uint32_t)s);                /* it starts over: the keys it held are gone */
        r = wifi_ap_rx_mgmt(&wa.ap, f, len, (uint64_t)now, txf, &tx_len);
        if (sub == 0x40u)
            wa.n.probe++;
        if (sub == 0xB0u)
            wa.n.auth++;
        if (sub == 0xA0u || sub == 0xC0u) {             /* it left */
            if (s >= 0)
                wa_client_drop((uint32_t)s);
            return;
        }
        if (r != 1)
            return;
        if (sub == 0x40u)
            wa.n.probe_tx++;
        wa_send(tx_len);
        if (sub == 0x00u && txf[0] == 0x10u && txf[26] == 0 && txf[27] == 0) {   /* association accepted */
            s = wa_slot_of(f + 10);
            if (s >= 0) {
                wa.n.assoc++;
                wa_start_hs((uint32_t)s, f + 10);
            }
        }
        return;
    }
    if ((f[0] & 0x0Cu) != 0x08u || (f[1] & 0x03u) != 0x01u || memcmp(f + 4, wa.ap.mac, 6) != 0)
        return;                                         /* (data frames to us from a station only) */
    wa.n.data++;
    hl = (f[0] & 0x80u) ? 26u : 24u;
    e = wifi_ap_unwrap_eapol(&wa.ap, f, len, &el);
    if (e) {
        s = wa_slot_of(f + 10);
        if (s >= 0)
            wa_on_eapol((uint32_t)s, e, el);
        return;
    }
    s = wa_slot_of(f + 10);
    if (s < 0) {                                        /* a station we do not know sends data (it was associated before a restart, or
                                                         * dropped for silence): told, so it joins again (class 3 frame, reason 7) */
        static uint32_t t_stray;
        if (fm1_ms - t_stray >= 1000u && !(f[10] & 1u)) {
            t_stray = fm1_ms;
            wa.n.stray++;
            wa_deauth(f + 10, 7u);
        }
        return;
    }
    if (!wa.keyed[s] || len < hl + 8u)
        return;                                         /* (only a client whose keys are in) */
    if (f[hl] == 0xAAu && f[hl + 1u] == 0xAAu && f[hl + 2u] == 0x03u)
        wa.n.dec++;                                     /* (LLC/SNAP came out: the pairwise key decrypts what it sends) */
    else if (f[1] & 0x40u)
        wa.n.prot++;
    if (!wifi_ip_early_filter(f, len, wl.ip.mac, wl.ip.ip, wl.ip.link_port))
        return;
    if (f[hl + 6u] == 0x08u && f[hl + 7u] == 0x00u && len >= hl + 8u + 20u + 8u && f[hl + 8u + 9u] == 17u) {   /* UDP to Link's port: counted */
        const uint8_t *ip = f + hl + 8u;
        uint32_t dp = (uint32_t)ip[22] << 8 | ip[23];
        if (dp == WIFI_IP_LINK_PORT)
            wa.n.link_rx++;
        if (ip[16] != 224u && dp == wl.ip.link_port)
            wa.n.link_uc++;
    }
    if (f[hl + 6u] == 0x08u && f[hl + 7u] == 0x00u && len >= hl + 8u + 20u) {   /* IPv4: where its source is */
        const uint8_t *ip = f + hl + 8u;
        wifi_ip_arp_learn(&wl.ip, (uint32_t)ip[12] << 24 | (uint32_t)ip[13] << 16 | (uint32_t)ip[14] << 8 | ip[15], f + 10);
    }
    r = wifi_ip_rx_frame(&wl.ip, f, len, now, txf, &tx_len);
    if (r == 1 && tx_len)
        wa_send(tx_len);
}

/* ---- start and stop */
static void wa_bring_up(void)
{
    uint8_t mac[6];
    uint32_t i;
    uint8_t srv[4] = {192, 168, 4, 1};
    wifi_sta_get_mac(mac);
    wifi_sta_set_mac_regs(mac, mac, wa.ch);             /* our address, the BSSID the same, the channel, every frame in */
    wifi_sta_clear_hw_keys();
    for (i = 1; i <= WIFI_AP_CLIENTS; i++)
        wa_key_clear(i);
    wa_key_clear(WA_PLAIN);
    wifi_ap_init(&wa.ap, mac, wa.pmk, ble_hw_rand);
    wifi_ap_set_net(&wa.ap, wa.ssid, wa.ssid_len, wa.ch);
    wa_group_install();
    wifi_ap_dhcp_init(&wa.dhcp, srv);
    for (i = 0; i < WIFI_AP_CLIENTS; i++)
        wa.keyed[i] = wa.tries[i] = 0, wa.t_due[i] = 0;
    wl_start_ap(mac, WA_IP);
    wa.t_beacon = fm1_ms;
    wa.st = WA_UP;
}

static void wa_stop(void)                               /* everyone told, the keys cleared, Link and the radio's duty off */
{
    uint32_t i;
    if (wa.st == WA_UP) {
        for (i = 0; i < WIFI_AP_CLIENTS; i++)
            if (wa.ap.clients[i].state != WIFI_AP_CL_NONE) {
                wa_key_clear(i + 1u);                   /* (cleared first: the goodbye goes out plain) */
                wa_deauth(wa.ap.clients[i].mac, 3u);
            }
        wifi_sta_clear_hw_keys();
        wl_stop();
        wl.ap = 0;
    }
    memset(wa.pass, 0, sizeof wa.pass);
    wa.st = WA_OFF;
}

/* ---- MODE AP, as the page and the web editor drive it (wn_st.mode == WN_MODE_AP; the network is wn_st.ap) */
static void wa_mac(void)
{
    if (!wa.mac_ok) {
        wifi_sta_get_mac(wa.mac);
        wa.mac_ok = 1;
    }
}
/* the network's name: the one chosen, else FM1- and the last four digits of the radio's address (what a phone lists) */
static uint32_t wa_name(uint8_t *out)
{
    static const char H[] = "0123456789ABCDEF";
    if (wn_st.ap.ssid_len) {
        memcpy(out, wn_st.ap.ssid, wn_st.ap.ssid_len);
        return wn_st.ap.ssid_len;
    }
    wa_mac();
    out[0] = 'F'; out[1] = 'M'; out[2] = '1'; out[3] = '-';
    out[4] = (uint8_t)H[wa.mac[4] >> 4]; out[5] = (uint8_t)H[wa.mac[4] & 15u];
    out[6] = (uint8_t)H[wa.mac[5] >> 4]; out[7] = (uint8_t)H[wa.mac[5] & 15u];
    return 8u;
}
static uint32_t wa_channel(void) { return wn_st.ap.ch ? wn_st.ap.ch : WA_CH_DEFAULT; }   /* the channel the store says (the AP may still be on the one before) */
static uint32_t wa_clients(void)                        /* the stations with their keys in */
{
    uint32_t i, n = 0;
    for (i = 0; i < WIFI_AP_CLIENTS; i++)
        n += wa.keyed[i];
    return n;
}
enum { WAS_OFF, WAS_NOKEY, WAS_STARTING, WAS_UP, WAS_NOCAL, WAS_STOPPED };   /* what the page says (STOPPED: AP MAN, not started) */
static int wa_mode_ap(void) { return wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN; }
static int wa_wanted(void)                              /* the mode asks for the access point now */
{
    return wn_st.mode == WN_MODE_AP || (wn_st.mode == WN_MODE_AP_MAN && wa.user_on);
}
static uint32_t wa_status(void)
{
    if (!wa_mode_ap())
        return WAS_OFF;
    if (wa.st == WA_UP)
        return WAS_UP;
    if (wa.st == WA_FAIL)
        return WAS_NOCAL;
    if (wa.st == WA_OFF && !wn_st.ap.set)
        return WAS_NOKEY;
    if (wa.st == WA_OFF && !wa_wanted())
        return WAS_STOPPED;
    return WAS_STARTING;
}
/* AP MAN: start (1) or stop (0) the access point; AP AUTO ignores it (it runs whenever the mode is on). 0 done, -1 not AP MAN, -2 no key */
static int wa_run(int on)
{
    if (wn_st.mode != WN_MODE_AP_MAN)
        return -1;
    if (on && !wn_st.ap.set)
        return -2;
    wa.user_on = (uint8_t)(on != 0);
    return 0;
}
static void wa_begin_mode(void)                         /* the stored network: no key job, the radio next */
{
    wa.by_mode = 1;
    wa.save_key = 0;
    wa.mc = 0;
    wa.ssid_len = (uint8_t)wa_name(wa.ssid);
    wa.ch = (uint8_t)wa_channel();
    memcpy(wa.pmk, wn_st.ap.pmk, 32);
    wa.t_start = fm1_ms;
    wa.st = WA_RADIO;
}
/* a new password for MODE AP (the page's entry, the web editor): the name (len 0: the automatic one), the passphrase (8..63
 * characters; copied, wiped when the key job has it). The AP in the air is stopped, the key derived a slice a pass, kept in
 * the store, the AP started again. 0, or -1 for a bad name or password */
static int wa_set_pass(const uint8_t *ssid, uint32_t len, const char *pass)
{
    uint32_t i, pl = 0;
    while (pass && pass[pl])
        pl++;
    if (len > WIFI_STA_SSID_MAX || (len && !ssid) || pl < 8u || pl > 63u)
        return -1;
    wa_stop();
    if (wn_st.mode == WN_MODE_AP_MAN)
        wa.user_on = 1;                                 /* (a password typed starts it) */
    wa.by_mode = 1;
    wa.save_key = 1;
    wa.mc = 0;
    if (len) {
        memcpy(wn_st.ap.ssid, ssid, len);
        wn_st.ap.ssid_len = (uint8_t)len;
    } else {
        wn_st.ap.ssid_len = 0;
    }
    wa.ssid_len = (uint8_t)wa_name(wa.ssid);
    wa.ch = (uint8_t)wa_channel();
    for (i = 0; i < pl; i++)
        wa.pass[i] = pass[i];
    wa.pass[i] = 0;
    wc_pmk_begin(&wa.job, wa.pass, wa.ssid, wa.ssid_len);
    wa.st = WA_PMK;
    return 0;
}
static void wa_forget_key(void)                         /* the key goes; no access point is run without one */
{
    wn_ap_forget(&wn_st);
    settings_later = 1;
    if (wa.by_mode)
        wa_stop();
}
static void wa_set_channel(uint32_t ch)                 /* kept at once; the AP restarts when the knob has rested */
{
    if (wn_ap_set_channel(&wn_st, ch) != 0)
        return;
    settings_later = 1;
    if (wa.by_mode && wa.st != WA_OFF) {
        wa.t_restart = fm1_ms + WA_RESTART_MS;
        if (!wa.t_restart)
            wa.t_restart = 1u;
    }
}

/* start an AP: the name, the passphrase (the key job takes it), the channel, the group-frame mode (see the top). 0 started,
 * -1 the manager is in use (MODE must be OFF) or the name is bad */
static int wa_start(const uint8_t *ssid, uint32_t len, const char *pass, uint32_t ch, uint32_t mc)
{
    uint32_t i, pl = 0;
    while (pass && pass[pl])
        pl++;
    if (wn_st.mode != WN_MODE_OFF || len < 1u || len > WIFI_STA_SSID_MAX || pl < 8u || pl > 63u || ch < 1u || ch > 13u)
        return -1;                                      /* (a WPA2 passphrase is 8 to 63 characters) */
    if (wa.st != WA_OFF && wa.st != WA_FAIL)
        wa_stop();
    wc_enable(0);                                       /* (the station's manager steps aside) */
    memcpy(wa.ssid, ssid, len);
    wa.by_mode = 0;
    wa.save_key = 0;
    wa.ssid_len = (uint8_t)len;
    wa.ch = (uint8_t)ch;
    wa.mc = (uint8_t)(mc <= 200u ? mc : 0u);
    for (i = 0; i < 63u && pass[i]; i++)
        wa.pass[i] = pass[i];
    wa.pass[i] = 0;
    wc_pmk_begin(&wa.job, wa.pass, wa.ssid, wa.ssid_len);
    memset(&wa.n, 0, sizeof wa.n);
    wa.st = WA_PMK;
    return 0;
}

static void wa_poll(void)                               /* once a main-loop pass: one step */
{
    uint32_t now = fm1_ms, i;
    if (wn_st.mode != WN_MODE_AP_MAN)
        wa.user_on = 0;                                 /* (AP MAN's own switch is off whenever another mode is chosen) */
    if (wa.by_mode && (!wa_mode_ap() || !wa_wanted()) && wa.st != WA_OFF) {   /* MODE left AP, or AP MAN was stopped: off */
        wa_stop();
        wa.by_mode = wa_mode_ap();
        return;
    }
    if (wa.t_restart && (int32_t)(now - wa.t_restart) >= 0) {   /* a channel turned and rested: the AP again on the new one */
        wa.t_restart = 0;
        if (wa.by_mode && wa.st != WA_OFF) {
            wa_stop();
            wa.by_mode = 1;                             /* (stays this mode's: WA_OFF below starts it from the store) */
        }
    }
    switch (wa.st) {
    case WA_OFF:
        if (wa_wanted() && wn_st.ap.set)
            wa_begin_mode();
        break;
    case WA_PMK:
        if (wc_pmk_step(&wa.job, 256u, wa.pmk)) {
            memset(wa.pass, 0, sizeof wa.pass);
            if (wa.save_key) {                          /* a new password: its key kept (the name and channel as the store has them) */
                wn_ap_put(&wn_st, wn_st.ap.ssid_len ? wn_st.ap.ssid : 0, wn_st.ap.ssid_len, wn_st.ap.ch, wa.pmk);
                settings_later = 1;
                wa.save_key = 0;
                wa.key_new = 1;
            }
            wa.t_start = now;
            wa.st = WA_RADIO;
        }
        break;
    case WA_RADIO:
        if (now < 3000u)
            break;                                      /* (the start-up's own work first, as the manager waits) */
        if (wifi_radio_up() < 0) {
            wa.st = WA_FAIL;
            break;
        }
        wa_bring_up();
        break;
    case WA_UP:
        if (now - wa.t_beacon >= WA_BEACON_MS) {
            uint8_t *f = (uint8_t *)wifi_txb + FM1_WIFI_TX_HDR;
            wa.t_beacon += WA_BEACON_MS;
            if (now - wa.t_beacon >= 4u * WA_BEACON_MS)
                wa.t_beacon = now;                      /* (the main loop stalled: no burst of catch-up beacons) */
            wa_tx_raw(wifi_ap_build_beacon(&wa.ap, (uint64_t)now * 1000u, f), WA_PLAIN, 0u);
            wa.n.beacon++;
        }
        for (i = 0; i < WIFI_AP_CLIENTS; i++)
            wa_hs_step(i, now);
        break;
    default:
        break;
    }
}
