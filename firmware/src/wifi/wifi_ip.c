/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Lightweight IP layer for Ableton Link on FM-1 Wi-Fi (docs/LINK-DESIGN.md §2):
 * - ARP responder & request sender
 * - DHCP client state machine (Discover -> Offer -> Request -> Ack / Bound)
 * - IGMPv2 group membership report for 224.76.78.75
 * - UDP dispatch to Ableton Link peer (lk_rx / lk_cfg.send)
 * - 20-instruction early drop filter right after RX DMA */
#include "wifi_ip.h"

#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#include "../../hal/fm1_wifi.h"
#if FELUCCA_WIFI_SWCCMP
uint32_t fm1_wifi_sw_ccmp = 0u;          /* 1 = CCMP in software (wl_send); 0 = plaintext body + Protected bit, the MAC
                                          * encrypts with WCID 1's key (try26: HW CCMP works on this DMA path once the TX
                                          * buffer is in main RAM; software CCMP at WCID 1 got double-encrypted, at a
                                          * keyless WCID dropped). wifiswc [N] flips it at runtime. */
#endif
uint32_t fm1_wifi_tx_ack = 1u;           /* TXWI ACK flag: a frame waits for the receiver's ACK (MODE AP: 0 for beacons and group frames) */
uint32_t fm1_wifi_tx_wcid = 1u;          /* TXWI WCID. 1 works for the handshake (EAPOL, pre-key). After keys install,
                                          * WCID 1 has a key -> HW garbles protected frames; set a KEYLESS WCID (wifiwcid
                                          * 2..) for software-CCMP data TX so the HW passes our encrypted frame through raw.
                                          * WCID 0 is a special drop entry - do not use. */
#else
#include <string.h>
#endif

static inline void wip_write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void wip_write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline uint16_t wip_read_u16_be(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint32_t wip_read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint16_t ip_chksum(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t sum = 0;
    while (len > 1u) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p += 2;
        len -= 2u;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static void arp_cache_insert(struct wifi_ip *wip, uint32_t ip, const uint8_t mac[6])
{
    uint32_t i;
    if (!ip || !mac)
        return;
    for (i = 0; i < 4u; i++) {
        if (wip->arp_tbl[i].valid && wip->arp_tbl[i].ip == ip) {
            memcpy(wip->arp_tbl[i].mac, mac, 6);
            return;
        }
    }
    for (i = 0; i < 4u; i++) {
        if (!wip->arp_tbl[i].valid) {
            wip->arp_tbl[i].ip = ip;
            memcpy(wip->arp_tbl[i].mac, mac, 6);
            wip->arp_tbl[i].valid = 1;
            return;
        }
    }
    /* Simple overwrite of first entry */
    wip->arp_tbl[0].ip = ip;
    memcpy(wip->arp_tbl[0].mac, mac, 6);
    wip->arp_tbl[0].valid = 1;
}

static int arp_lookup(const struct wifi_ip *wip, uint32_t ip, uint8_t mac_out[6])
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        if (wip->arp_tbl[i].valid && wip->arp_tbl[i].ip == ip) {
            memcpy(mac_out, wip->arp_tbl[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

void wifi_ip_init(struct wifi_ip *wip, const uint8_t sta_mac[6], const uint8_t bssid[6], void (*rand_fn)(uint8_t *out, uint8_t n))
{
    if (!wip)
        return;
    memset(wip, 0, sizeof(*wip));
    if (sta_mac)
        memcpy(wip->mac, sta_mac, 6);
    if (bssid)
        memcpy(wip->bssid, bssid, 6);
    wip->rand = rand_fn;
    wip->link_port = WIFI_IP_LINK_PORT;
    wip->dhcp_state = WIFI_IP_DHCP_IDLE;
}

void wifi_ip_ap_init(struct wifi_ip *wip, const uint8_t ap_mac[6], uint32_t ip, void (*rand_fn)(uint8_t *out, uint8_t n))
{
    wifi_ip_init(wip, ap_mac, ap_mac, rand_fn);
    wip->ap = 1;
    wip->ip = ip;
    wip->netmask = 0xFFFFFF00u;
    wip->dhcp_state = WIFI_IP_DHCP_BOUND;           /* (no client to run: the address is ours) */
    wip->igmp_t = (int64_t)1 << 62;                 /* (no membership reports: we are the router) */
}

void wifi_ip_arp_learn(struct wifi_ip *wip, uint32_t ip, const uint8_t mac[6])
{
    if (wip)
        arp_cache_insert(wip, ip, mac);
}

void wifi_ip_set_link(struct wifi_ip *wip, struct lk_node *lk, uint16_t link_port)
{
    if (!wip)
        return;
    wip->lk = lk;
    wip->link_port = link_port ? link_port : WIFI_IP_LINK_PORT;
    if (wip->lk && wip->ip)
        lk_set_addr(wip->lk, wip->ip, wip->link_port);
}

int wifi_ip_early_filter(const uint8_t *frame, uint32_t len, const uint8_t sta_mac[6], uint32_t our_ip, uint16_t link_port)
{
    uint32_t hlen, eth_type;
    const uint8_t *llc, *ip_pkt;
    uint8_t proto;
    uint16_t dport;
    uint32_t dst_ip;

    (void)sta_mac;

    /* Must be 802.11 Data frame */
    if (!frame || len < 24u + 8u || (frame[0] & 0x0Cu) != 0x08u)
        return 0;

    /* QoS Data vs Data header length */
    hlen = (frame[0] & 0x80u) ? 26u : 24u;
    if (len < hlen + 8u)
        return 0;

    llc = frame + hlen;
    /* Verify LLC/SNAP prefix AA-AA-03-00-00-00 */
    if (llc[0] != 0xAAu || llc[1] != 0xAAu || llc[2] != 0x03u ||
        llc[3] != 0x00u || llc[4] != 0x00u || llc[5] != 0x00u)
        return 0;

    eth_type = ((uint32_t)llc[6] << 8) | (uint32_t)llc[7];

    /* 1. EAPOL (0x888E) */
    if (eth_type == 0x888Eu)
        return 1;

    /* 2. ARP (0x0806) */
    if (eth_type == 0x0806u) {
        if (len < hlen + 8u + 28u)
            return 0;
        const uint8_t *arp = llc + 8u;
        uint32_t tip = wip_read_u32_be(arp + 24);
        return (our_ip == 0 || tip == our_ip || tip == 0xFFFFFFFFu);
    }

    /* 3. IPv4 (0x0800) */
    if (eth_type == 0x0800u) {
        if (len < hlen + 8u + 20u)
            return 0;
        ip_pkt = llc + 8u;
        if ((ip_pkt[0] >> 4) != 4)                  /* IPv4 */
            return 0;
        proto = ip_pkt[9];

        /* IGMP (Protocol 2) */
        if (proto == 2u)
            return 1;

        /* ICMP echo request to our address (MODE AP answers a ping) */
        if (proto == 1u)
            return (our_ip != 0 && wip_read_u32_be(ip_pkt + 16) == our_ip);

        /* UDP (Protocol 17) */
        if (proto == 17u) {
            uint32_t ihl = (ip_pkt[0] & 0x0Fu) * 4u;
            if (len < hlen + 8u + ihl + 8u)
                return 0;
            const uint8_t *udp = ip_pkt + ihl;
            dport = wip_read_u16_be(udp + 2);
            dst_ip = wip_read_u32_be(ip_pkt + 16);

            /* DHCP Client (Port 68), and the DHCP server's (67: only an access point acts on it) */
            if (dport == WIFI_IP_DHCP_PORT_CLIENT || dport == WIFI_IP_DHCP_PORT_SERVER)
                return 1;

            /* Link Multicast Discovery (224.76.78.75:20808) */
            if (dport == WIFI_IP_LINK_PORT && dst_ip == WIFI_IP_LINK_MCAST_IP)
                return 1;

            /* Link Unicast Socket */
            if (dport == link_port && (dst_ip == our_ip || our_ip == 0))
                return 1;
        }
    }

    return 0;
}

static uint32_t build_80211_data_hdr(struct wifi_ip *wip, const uint8_t da[6], uint16_t eth_type, uint8_t *tx_buf)
{
    uint16_t seq = wip->tx_seq++;
    memset(tx_buf, 0, 32);
    tx_buf[0] = 0x08;                               /* Type: Data, Subtype: Data */
    tx_buf[2] = 0x00; tx_buf[3] = 0x00;             /* Duration */
    if (wip->ap) {                                  /* MODE AP: FromDS, RA = DA, TA = BSSID = us, addr3 = SA = us */
        tx_buf[1] = (uint8_t)(0x02u | (wip->protect ? 0x40u : 0u));
        memcpy(tx_buf + 4, da, 6);
        memcpy(tx_buf + 10, wip->mac, 6);
        memcpy(tx_buf + 16, wip->mac, 6);
    } else {
        tx_buf[1] = (uint8_t)(0x01u | (wip->protect ? 0x40u : 0u));   /* ToDS = 1, FromDS = 0; Protected once keys are in */
        memcpy(tx_buf + 4, wip->bssid, 6);          /* Addr 1: RA (AP BSSID) */
        memcpy(tx_buf + 10, wip->mac, 6);           /* Addr 2: TA/SA (Our Station MAC) */
        memcpy(tx_buf + 16, da, 6);                 /* Addr 3: DA (Destination MAC) */
    }
    tx_buf[22] = (uint8_t)(seq << 4);
    tx_buf[23] = (uint8_t)(seq >> 4);

    /* try15: hardware CCMP the standard Ralink way (vendor SDK TXWI_STRUC). Software hands the MAC ONLY the plaintext
     * 802.11 frame (24-byte header + LLC/SNAP + payload); it reserves no inline IV/EIV. The CCMP IV/EIV are TXWI words
     * 3/4 (left 0 so the MAC fills the PN) and encryption is driven by the WCID key table, not by any TXWI flag. The
     * CCMP is now applied in software (wl_send -> wc_ccmp_encrypt) before the frame is injected; the TXWI just carries
     * the full length. This builder only sets the Protected bit so the AP knows to decrypt. */

    /* LLC/SNAP Header (8 bytes): plaintext the MAC encrypts */
    tx_buf[24] = 0xAA;
    tx_buf[25] = 0xAA;
    tx_buf[26] = 0x03;
    tx_buf[27] = 0x00;
    tx_buf[28] = 0x00;
    tx_buf[29] = 0x00;
    wip_write_u16_be(tx_buf + 30, eth_type);

    return 32u;
}

static int get_dst_mac(const struct wifi_ip *wip, uint32_t dst_ip, uint8_t dst_mac[6])   /* 0: not known (AP: nothing to send to) */
{
    if (dst_ip == 0xFFFFFFFFu) {
        memset(dst_mac, 0xFF, 6);
        return 1;
    }
    if ((dst_ip & 0xF0000000u) == 0xE0000000u) {   /* Multicast: 01:00:5E:[low 23 bits] */
        dst_mac[0] = 0x01;
        dst_mac[1] = 0x00;
        dst_mac[2] = 0x5E;
        dst_mac[3] = (uint8_t)((dst_ip >> 16) & 0x7Fu);
        dst_mac[4] = (uint8_t)((dst_ip >> 8) & 0xFFu);
        dst_mac[5] = (uint8_t)(dst_ip & 0xFFu);
        return 1;
    }
    if (arp_lookup(wip, dst_ip, dst_mac))
        return 1;
    if (wip->ap)
        return 0;                                   /* (the AP has no router to hand an unknown address to) */
    if (wip->gw && arp_lookup(wip, wip->gw, dst_mac))
        return 1;
    /* Fallback: AP BSSID */
    memcpy(dst_mac, wip->bssid, 6);
    return 1;
}

static uint32_t build_udp(struct wifi_ip *wip, const uint8_t dst_mac[6], uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint32_t payload_len, uint8_t *tx_buf)
{
    uint32_t off;
    uint8_t *ip_hdr, *udp_hdr;
    uint16_t ip_len, udp_len;

    off = build_80211_data_hdr(wip, dst_mac, 0x0800u, tx_buf);

    ip_hdr = tx_buf + off;
    ip_len = 20u + 8u + (uint16_t)payload_len;
    memset(ip_hdr, 0, 20);
    ip_hdr[0] = 0x45;                               /* IPv4, IHL 5 */
    ip_hdr[1] = 0x00;                               /* DSCP/ECN */
    wip_write_u16_be(ip_hdr + 2, ip_len);
    wip_write_u16_be(ip_hdr + 4, wip->tx_seq);      /* Identification */
    wip_write_u16_be(ip_hdr + 6, 0x4000u);          /* Flags: DF */
    ip_hdr[8] = ((dst_ip & 0xF0000000u) == 0xE0000000u) ? 1u : 64u; /* TTL: 1 for mcast, 64 for ucast */
    ip_hdr[9] = 17u;                                /* Protocol: UDP */
    wip_write_u32_be(ip_hdr + 12, wip->ip);         /* Source IP */
    wip_write_u32_be(ip_hdr + 16, dst_ip);          /* Destination IP */
    wip_write_u16_be(ip_hdr + 10, ip_chksum(ip_hdr, 20));

    udp_hdr = ip_hdr + 20;
    udp_len = 8u + (uint16_t)payload_len;
    wip_write_u16_be(udp_hdr + 0, src_port);        /* Source Port */
    wip_write_u16_be(udp_hdr + 2, dst_port);        /* Destination Port */
    wip_write_u16_be(udp_hdr + 4, udp_len);
    wip_write_u16_be(udp_hdr + 6, 0x0000u);         /* UDP checksum optional */

    if (payload && payload_len)
        memcpy(udp_hdr + 8, payload, payload_len);

    return off + 20u + 8u + payload_len;
}

uint32_t wifi_ip_send_udp(struct wifi_ip *wip, uint32_t dst_ip, uint16_t dst_port, const uint8_t *payload, uint32_t payload_len, uint8_t *tx_buf)
{
    uint8_t dst_mac[6];

    if (!wip || !tx_buf)
        return 0;
    if (!get_dst_mac(wip, dst_ip, dst_mac))
        return 0;
    return build_udp(wip, dst_mac, dst_ip, wip->link_port, dst_port, payload, payload_len, tx_buf);
}

uint32_t wifi_ip_reply_udp(struct wifi_ip *wip, const uint8_t dst_mac[6], uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint32_t payload_len, uint8_t *tx_buf)
{
    if (!wip || !dst_mac || !tx_buf)
        return 0;
    return build_udp(wip, dst_mac, dst_ip, src_port, dst_port, payload, payload_len, tx_buf);
}

uint32_t wifi_ip_igmp_join(struct wifi_ip *wip, uint32_t group_ip, uint8_t *tx_buf)
{
    uint8_t dst_mac[6];
    uint32_t off;
    uint8_t *ip_hdr, *igmp;

    if (!wip || !tx_buf)
        return 0;

    get_dst_mac(wip, group_ip, dst_mac);
    off = build_80211_data_hdr(wip, dst_mac, 0x0800u, tx_buf);

    ip_hdr = tx_buf + off;
    memset(ip_hdr, 0, 20);
    ip_hdr[0] = 0x45;                               /* IPv4, IHL 5 */
    ip_hdr[1] = 0x00;
    wip_write_u16_be(ip_hdr + 2, 28u);              /* Total length: 20 + 8 */
    wip_write_u16_be(ip_hdr + 4, wip->tx_seq);
    wip_write_u16_be(ip_hdr + 6, 0x4000u);          /* DF */
    ip_hdr[8] = 1u;                                 /* TTL = 1 */
    ip_hdr[9] = 2u;                                 /* Protocol: IGMP */
    wip_write_u32_be(ip_hdr + 12, wip->ip);         /* Source IP */
    wip_write_u32_be(ip_hdr + 16, group_ip);        /* Destination IP */
    wip_write_u16_be(ip_hdr + 10, ip_chksum(ip_hdr, 20));

    igmp = ip_hdr + 20;
    igmp[0] = 0x16;                                 /* Type: IGMPv2 Membership Report */
    igmp[1] = 0x00;                                 /* Max Response Time */
    igmp[2] = 0x00; igmp[3] = 0x00;                 /* Checksum placeholder */
    wip_write_u32_be(igmp + 4, group_ip);
    wip_write_u16_be(igmp + 2, ip_chksum(igmp, 8));

    return off + 20u + 8u;
}

static uint32_t build_dhcp_msg(struct wifi_ip *wip, uint8_t msg_type, uint32_t req_ip, uint32_t srv_ip, uint8_t *tx_buf)
{
    uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t off = build_80211_data_hdr(wip, bcast_mac, 0x0800u, tx_buf);
    uint8_t *ip_hdr = tx_buf + off;
    uint8_t *udp_hdr = ip_hdr + 20;
    uint8_t *dhcp = udp_hdr + 8;
    uint32_t opt_len = 0;

    memset(ip_hdr, 0, 20);
    ip_hdr[0] = 0x45;
    ip_hdr[8] = 64u;
    ip_hdr[9] = 17u;                                /* UDP */
    wip_write_u32_be(ip_hdr + 12, 0u);              /* 0.0.0.0 */
    wip_write_u32_be(ip_hdr + 16, 0xFFFFFFFFu);     /* 255.255.255.255 */

    wip_write_u16_be(udp_hdr + 0, WIFI_IP_DHCP_PORT_CLIENT); /* 68 */
    wip_write_u16_be(udp_hdr + 2, WIFI_IP_DHCP_PORT_SERVER); /* 67 */
    wip_write_u16_be(udp_hdr + 6, 0u);

    memset(dhcp, 0, 236 + 64);
    dhcp[0] = 0x01;                                 /* Boot Request */
    dhcp[1] = 0x01;                                 /* Ethernet */
    dhcp[2] = 0x06;                                 /* HLen 6 */
    dhcp[3] = 0x00;
    wip_write_u32_be(dhcp + 4, wip->dhcp_xid);
    wip_write_u16_be(dhcp + 10, 0x8000u);           /* Broadcast flag */
    memcpy(dhcp + 28, wip->mac, 6);

    /* Magic Cookie (0x63, 0x82, 0x53, 0x63) */
    dhcp[236] = 0x63; dhcp[237] = 0x82; dhcp[238] = 0x53; dhcp[239] = 0x63;
    opt_len = 240;

    /* Option 53: DHCP Message Type */
    dhcp[opt_len++] = 53;
    dhcp[opt_len++] = 1;
    dhcp[opt_len++] = msg_type;

    if (msg_type == 1) {                            /* Discover: Option 55 Parameter Request List */
        dhcp[opt_len++] = 55;
        dhcp[opt_len++] = 4;
        dhcp[opt_len++] = 1;                        /* Subnet Mask */
        dhcp[opt_len++] = 3;                        /* Router */
        dhcp[opt_len++] = 6;                        /* DNS */
        dhcp[opt_len++] = 15;                       /* Domain Name */
    } else if (msg_type == 3) {                     /* Request: Option 50 (Req IP) & Option 54 (Server ID) */
        dhcp[opt_len++] = 50;
        dhcp[opt_len++] = 4;
        wip_write_u32_be(dhcp + opt_len, req_ip);
        opt_len += 4;

        if (srv_ip) {
            dhcp[opt_len++] = 54;
            dhcp[opt_len++] = 4;
            wip_write_u32_be(dhcp + opt_len, srv_ip);
            opt_len += 4;
        }
    }

    /* Option 255: End */
    dhcp[opt_len++] = 255;

    /* Finalize UDP & IP lengths */
    wip_write_u16_be(udp_hdr + 4, (uint16_t)(8u + opt_len));
    wip_write_u16_be(ip_hdr + 2, (uint16_t)(20u + 8u + opt_len));
    wip_write_u16_be(ip_hdr + 10, ip_chksum(ip_hdr, 20));

    return off + 20u + 8u + opt_len;
}

uint32_t wifi_ip_dhcp_start(struct wifi_ip *wip, uint8_t *tx_buf)
{
    if (!wip || !tx_buf)
        return 0;
    if (wip->rand)
        wip->rand((uint8_t *)&wip->dhcp_xid, 4);
    else
        wip->dhcp_xid = 0x12345678u;

    wip->dhcp_state = WIFI_IP_DHCP_DISCOVERING;
    wip->dhcp_retries = 0;
    wip->ip = 0;
    return build_dhcp_msg(wip, 1, 0, 0, tx_buf);
}

static uint32_t build_arp_reply(struct wifi_ip *wip, const uint8_t *arp_req, uint8_t *tx_buf)
{
    uint8_t target_mac[6];
    uint32_t target_ip;
    uint32_t off;
    uint8_t *arp_rep;

    memcpy(target_mac, arp_req + 8, 6);             /* Sender MAC in req -> Target MAC in rep */
    target_ip = wip_read_u32_be(arp_req + 14);      /* Sender IP in req -> Target IP in rep */

    off = build_80211_data_hdr(wip, target_mac, 0x0806u, tx_buf);
    arp_rep = tx_buf + off;

    wip_write_u16_be(arp_rep + 0, 0x0001u);         /* HW Type: Ethernet */
    wip_write_u16_be(arp_rep + 2, 0x0800u);         /* Proto Type: IPv4 */
    arp_rep[4] = 6;                                 /* HW Len */
    arp_rep[5] = 4;                                 /* Proto Len */
    wip_write_u16_be(arp_rep + 6, 0x0002u);         /* Opcode: Reply */
    memcpy(arp_rep + 8, wip->mac, 6);               /* Sender MAC: Our MAC */
    wip_write_u32_be(arp_rep + 14, wip->ip);        /* Sender IP: Our IP */
    memcpy(arp_rep + 18, target_mac, 6);            /* Target MAC */
    wip_write_u32_be(arp_rep + 24, target_ip);      /* Target IP */

    return off + 28u;
}

int wifi_ip_rx_frame(struct wifi_ip *wip, const uint8_t *frame, uint32_t len, int64_t now_us, uint8_t *tx_buf, uint32_t *tx_len)
{
    uint32_t hlen, eth_type;
    const uint8_t *llc;

    if (!wip || !frame || len < 32u)
        return -1;

    hlen = (frame[0] & 0x80u) ? 26u : 24u;
    if (len < hlen + 8u)
        return -1;

    llc = frame + hlen;
    if (llc[0] != 0xAAu || llc[1] != 0xAAu || llc[2] != 0x03u)
        return -1;

    eth_type = ((uint32_t)llc[6] << 8) | (uint32_t)llc[7];

    /* 1. ARP (0x0806) */
    if (eth_type == 0x0806u) {
        const uint8_t *arp = llc + 8u;
        uint16_t op;
        uint32_t sip, tip;
        if (len < hlen + 8u + 28u)
            return -1;
        op = wip_read_u16_be(arp + 6);
        sip = wip_read_u32_be(arp + 14);
        tip = wip_read_u32_be(arp + 24);

        /* Update ARP cache with sender */
        arp_cache_insert(wip, sip, arp + 8);

        /* If ARP request for our IP, reply */
        if (op == 1u && wip->ip && tip == wip->ip && tx_buf && tx_len) {
            *tx_len = build_arp_reply(wip, arp, tx_buf);
            return 1;
        }
        return 0;
    }

    /* 2. IPv4 (0x0800) */
    if (eth_type == 0x0800u) {
        const uint8_t *ip_hdr = llc + 8u;
        uint32_t ihl, ip_tot_len, src_ip;
        uint8_t proto;

        if (len < hlen + 8u + 20u || (ip_hdr[0] >> 4) != 4)
            return -1;

        ihl = (ip_hdr[0] & 0x0Fu) * 4u;
        ip_tot_len = wip_read_u16_be(ip_hdr + 2);
        proto = ip_hdr[9];
        src_ip = wip_read_u32_be(ip_hdr + 12);

        if (len < hlen + 8u + ihl || ip_tot_len < ihl)
            return -1;

        /* MODE AP: a ping to our address is answered (the quickest check of the whole path from a laptop) */
        if (wip->ap && proto == 1u && wip_read_u32_be(ip_hdr + 16) == wip->ip && tx_buf && tx_len) {
            const uint8_t *icmp = ip_hdr + ihl;
            uint32_t n = ip_tot_len - ihl, off;
            uint8_t *o;
            if (n < 8u || n > 512u || icmp[0] != 8u || len < hlen + 8u + ihl + n)
                return 0;
            off = build_80211_data_hdr(wip, frame + 10, 0x0800u, tx_buf);   /* (the sender's address: ToDS addr2) */
            o = tx_buf + off;
            memset(o, 0, 20);
            o[0] = 0x45;
            wip_write_u16_be(o + 2, (uint16_t)(20u + n));
            wip_write_u16_be(o + 4, wip->tx_seq);
            wip_write_u16_be(o + 6, 0x4000u);
            o[8] = 64u;
            o[9] = 1u;
            wip_write_u32_be(o + 12, wip->ip);
            wip_write_u32_be(o + 16, src_ip);
            wip_write_u16_be(o + 10, ip_chksum(o, 20));
            memcpy(o + 20, icmp, n);
            o[20] = 0u;                                 /* echo reply */
            o[22] = o[23] = 0;
            wip_write_u16_be(o + 22, ip_chksum(o + 20, n));
            *tx_len = off + 20u + n;
            return 1;
        }

        /* UDP Dispatch */
        if (proto == 17u) {
            const uint8_t *udp = ip_hdr + ihl;
            uint16_t src_port, dst_port, udp_len;
            const uint8_t *payload;
            uint32_t payload_len;

            if (len < hlen + 8u + ihl + 8u)
                return -1;

            src_port = wip_read_u16_be(udp + 0);
            dst_port = wip_read_u16_be(udp + 2);
            udp_len = wip_read_u16_be(udp + 4);

            if (udp_len < 8u)
                return -1;
            payload = udp + 8;
            payload_len = (uint32_t)udp_len - 8u;

            /* MODE AP: a client's DHCP request (port 67) goes to the server */
            if (wip->ap && dst_port == WIFI_IP_DHCP_PORT_SERVER) {
                if (wip->udp_srv && tx_buf && tx_len) {
                    *tx_len = wip->udp_srv(wip, frame + 10, payload, payload_len, tx_buf);
                    return *tx_len ? 1 : 0;
                }
                return 0;
            }

            /* DHCP Response (Port 68) */
            if (dst_port == WIFI_IP_DHCP_PORT_CLIENT && payload_len >= 240u) {
                uint32_t xid = wip_read_u32_be(payload + 4);
                uint32_t yiaddr = wip_read_u32_be(payload + 16);
                uint8_t msg_type = 0;
                uint32_t server_id = 0, netmask = 0, gw = 0, dns = 0;
                uint32_t o;

                if (xid != wip->dhcp_xid)
                    return 0;

                /* Parse DHCP options after magic cookie */
                if (payload[236] == 0x63 && payload[237] == 0x82 &&
                    payload[238] == 0x53 && payload[239] == 0x63) {
                    for (o = 240; o < payload_len && payload[o] != 255; ) {
                        uint8_t code = payload[o++];
                        uint8_t olen = 0;
                        if (code == 0) continue;
                        if (o >= payload_len) break;
                        olen = payload[o++];
                        if (o + olen > payload_len) break;

                        if (code == 53 && olen >= 1)
                            msg_type = payload[o];
                        else if (code == 54 && olen >= 4)
                            server_id = wip_read_u32_be(payload + o);
                        else if (code == 1 && olen >= 4)
                            netmask = wip_read_u32_be(payload + o);
                        else if (code == 3 && olen >= 4)
                            gw = wip_read_u32_be(payload + o);
                        else if (code == 6 && olen >= 4)
                            dns = wip_read_u32_be(payload + o);

                        o += olen;
                    }
                }

                if (wip->dhcp_state == WIFI_IP_DHCP_DISCOVERING && msg_type == 2) { /* Offer */
                    wip->dhcp_t0 = now_us;
                    wip->dhcp_offered_ip = yiaddr;
                    wip->dhcp_server_ip = server_id;
                    wip->dhcp_state = WIFI_IP_DHCP_REQUESTING;
                    if (tx_buf && tx_len) {
                        *tx_len = build_dhcp_msg(wip, 3, yiaddr, server_id, tx_buf);
                        return 1;
                    }
                    return 0;
                }

                if (wip->dhcp_state == WIFI_IP_DHCP_REQUESTING && msg_type == 5) { /* Ack */
                    wip->ip = yiaddr;
                    wip->netmask = netmask ? netmask : 0xFFFFFF00u;
                    wip->gw = gw;
                    wip->dns = dns;
                    wip->dhcp_state = WIFI_IP_DHCP_BOUND;
                    wip->igmp_t = now_us + 30000000;      /* (the join below; then every 30 s, wifi_ip_poll) */

                    /* Notify Link peer node */
                    if (wip->lk)
                        lk_set_addr(wip->lk, wip->ip, wip->link_port);

                    /* Send IGMP join for Link multicast discovery */
                    if (tx_buf && tx_len) {
                        *tx_len = wifi_ip_igmp_join(wip, WIFI_IP_LINK_MCAST_IP, tx_buf);
                        return 1;
                    }
                    return 0;
                }
                return 0;
            }

            /* Link Multicast / Unicast Datagram */
            if (wip->lk && (dst_port == WIFI_IP_LINK_PORT || dst_port == wip->link_port)) {
                lk_rx(wip->lk, now_us, src_ip, src_port, payload, payload_len);
                return 0;
            }
        }
    }

    return 0;
}

uint32_t wifi_ip_poll(struct wifi_ip *wip, int64_t now_us, uint8_t *tx_buf)
{
    if (!wip || !tx_buf)
        return 0;
    if (wip->dhcp_state == WIFI_IP_DHCP_DISCOVERING || wip->dhcp_state == WIFI_IP_DHCP_REQUESTING) {
        if (now_us - wip->dhcp_t0 < 2000000)            /* (an answer is awaited 2 s, then the Discover again) */
            return 0;
        wip->dhcp_t0 = now_us;
        if (++wip->dhcp_retries > 5u) {
            wip->dhcp_state = WIFI_IP_DHCP_FAILED;
            return 0;
        }
        wip->dhcp_state = WIFI_IP_DHCP_DISCOVERING;
        return build_dhcp_msg(wip, 1, 0, 0, tx_buf);
    }
    if (wip->dhcp_state == WIFI_IP_DHCP_BOUND && now_us >= wip->igmp_t) {
        wip->igmp_t = now_us + 30000000;                /* (a switch that snoops forgets a group nobody reports) */
        return wifi_ip_igmp_join(wip, WIFI_IP_LINK_MCAST_IP, tx_buf);
    }
    return 0;
}

#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
#ifndef IN_POOL
#define IN_POOL
#endif
static uint32_t wifi_txb[(FM1_WIFI_TX_HDR + 576u) / 4u];   /* THE transmit buffer (wifi_link.c, wifi_conn.c, Link): main RAM, not the pool, the
                                                         * frame DMA can't fetch the pool (2026-10-27); one buffer is enough: a send is from the main
                                                         * loop and waits for the DMA before it returns */
int wifi_ip_link_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n)
{
    struct wifi_ip *wip = (struct wifi_ip *)ctx;
    uint8_t *tx_frame = (uint8_t *)wifi_txb + FM1_WIFI_TX_HDR;
    uint32_t len;
    if (!wip || !p || n > LK_MSG_MAX)
        return -1;
    len = wifi_ip_send_udp(wip, ip, port, p, n, tx_frame);
    if (!len)
        return -1;
    if (wip->send) {
        wip->send(len);
        return 0;
    }
    fm1_wifi_tx_prepare(wifi_txb, len);
    fm1_wifi_tx(wifi_txb);
    return 0;
}
#else
int wifi_ip_link_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n)
{
    (void)ctx; (void)ip; (void)port; (void)p; (void)n;
    return 0;
}
#endif
