/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Lightweight IP layer for Ableton Link on FM-1 Wi-Fi (docs/LINK-DESIGN.md §2):
 * - ARP responder & request sender
 * - DHCP client state machine (Discover -> Offer -> Request -> Ack / Bound)
 * - IGMPv2 group membership report for 224.76.78.75
 * - UDP dispatch to Ableton Link peer (lk_rx / lk_cfg.send)
 * - 20-instruction early drop filter right after RX DMA */
#ifndef WIFI_IP_H
#define WIFI_IP_H
#include <stdint.h>
#include "../../../fm1base/link/link.h"

#define WIFI_IP_80211_HDR 24u
#define WIFI_IP_LLC_SNAP 8u
#define WIFI_IP_IP_HDR 20u
#define WIFI_IP_UDP_HDR 8u
#define WIFI_IP_TOTAL_HDR (WIFI_IP_80211_HDR + WIFI_IP_LLC_SNAP + WIFI_IP_IP_HDR + WIFI_IP_UDP_HDR) /* 60 bytes */

#define WIFI_IP_DHCP_PORT_SERVER 67u
#define WIFI_IP_DHCP_PORT_CLIENT 68u
#define WIFI_IP_LINK_MCAST_IP 0xE04C4E4Bu  /* 224.76.78.75 (host order: 0xE04C4E4B) */
#define WIFI_IP_LINK_PORT 20808u

enum wifi_ip_dhcp_state {
    WIFI_IP_DHCP_IDLE = 0,
    WIFI_IP_DHCP_DISCOVERING,
    WIFI_IP_DHCP_REQUESTING,
    WIFI_IP_DHCP_BOUND,
    WIFI_IP_DHCP_FAILED
};

struct wifi_arp_entry {
    uint32_t ip;                           /* host order */
    uint8_t mac[6];
    uint8_t valid;
};

struct wifi_ip {
    uint8_t mac[6];                        /* our station MAC */
    uint8_t bssid[6];                      /* AP BSSID */
    uint32_t ip;                           /* our IPv4 address (host order), 0 = unassigned */
    uint32_t netmask;                      /* subnet mask (host order) */
    uint32_t gw;                           /* gateway IP (host order) */
    uint32_t dns;                          /* DNS server (host order) */
    uint32_t dhcp_server_ip;               /* DHCP server IP (host order) */
    uint32_t dhcp_offered_ip;              /* DHCP offered IP (host order) */
    uint32_t dhcp_xid;                     /* DHCP transaction ID */
    enum wifi_ip_dhcp_state dhcp_state;
    int64_t dhcp_t0;                       /* host time (us) of current state */
    uint8_t dhcp_retries;
    uint16_t link_port;                    /* our unicast Link port */
    struct lk_node *lk;                    /* Link node pointer */
    struct wifi_arp_entry arp_tbl[4];
    uint16_t tx_seq;                       /* 802.11 sequence counter */
    uint8_t protect;                       /* 1: our data frames carry the Protected bit (keys installed: the MAC encrypts) */
    uint8_t ap;                            /* 1: MODE AP: we are the access point (our data frames are FromDS, mac == bssid) */
    void (*send)(uint32_t len);            /* a finished frame (at tx_buf, len bytes) out: the owner's transmit, else the station's */
    uint32_t (*udp_srv)(struct wifi_ip *wip, const uint8_t *src_mac, const uint8_t *msg, uint32_t n, uint8_t *tx_buf);
                                           /* AP: a datagram to UDP port 67 (the DHCP server): the reply frame's length, 0 for none */
    int64_t igmp_t;                        /* host time (us) the next membership report is due */
    void (*rand)(uint8_t *out, uint8_t n);
};

/* Initialize IP layer state */
void wifi_ip_init(struct wifi_ip *wip, const uint8_t sta_mac[6], const uint8_t bssid[6], void (*rand_fn)(uint8_t *out, uint8_t n));

/* MODE AP: this is the access point at ip (host order): FromDS frames, the /24 netmask, no DHCP client, no gateway */
void wifi_ip_ap_init(struct wifi_ip *wip, const uint8_t ap_mac[6], uint32_t ip, void (*rand_fn)(uint8_t *out, uint8_t n));
/* a station's address, learned (the DHCP server's lease, a packet's source) */
void wifi_ip_arp_learn(struct wifi_ip *wip, uint32_t ip, const uint8_t mac[6]);
/* one UDP datagram to a given MAC and IP, from any source port (the DHCP server's reply). Returns the frame length. */
uint32_t wifi_ip_reply_udp(struct wifi_ip *wip, const uint8_t dst_mac[6], uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint32_t payload_len, uint8_t *tx_buf);

/* Attach Link peer node and unicast port */
void wifi_ip_set_link(struct wifi_ip *wip, struct lk_node *lk, uint16_t link_port);

/* Start DHCP client negotiation. Returns frame length written to tx_buf (or 0). */
uint32_t wifi_ip_dhcp_start(struct wifi_ip *wip, uint8_t *tx_buf);

/* Periodic timer polling (DHCP timeouts / retransmissions). Returns frame length written to tx_buf if transmit needed. */
uint32_t wifi_ip_poll(struct wifi_ip *wip, int64_t now_us, uint8_t *tx_buf);

/* Fast early drop filter (docs/LINK-DESIGN.md §2.7).
 * Call immediately after RX DMA (or after decryption).
 * Returns 1 to keep and process frame, 0 to drop immediately. */
int wifi_ip_early_filter(const uint8_t *frame, uint32_t len, const uint8_t sta_mac[6], uint32_t our_ip, uint16_t link_port);

/* Process an incoming 802.11 Data frame (LLC/SNAP encapsulated ARP, IPv4/IGMP, IPv4/UDP).
 * Delivers Link packets to lk_rx. If a response frame (ARP reply, DHCP req, etc.) is generated,
 * writes to tx_buf and sets tx_len, returning 1; returns 0 if consumed, -1 if ignored/error. */
int wifi_ip_rx_frame(struct wifi_ip *wip, const uint8_t *frame, uint32_t len, int64_t now_us, uint8_t *tx_buf, uint32_t *tx_len);

/* Send an Ableton Link UDP datagram. Encapsulates in 802.11 Data + LLC/SNAP + IPv4 + UDP.
 * Returns the total frame length written to tx_buf. */
uint32_t wifi_ip_send_udp(struct wifi_ip *wip, uint32_t dst_ip, uint16_t dst_port, const uint8_t *payload, uint32_t payload_len, uint8_t *tx_buf);

/* Construct an IGMPv2 Membership Report for multicast group 224.76.78.75.
 * Returns the total frame length written to tx_buf. */
uint32_t wifi_ip_igmp_join(struct wifi_ip *wip, uint32_t group_ip, uint8_t *tx_buf);

/* Link send callback adapter (compatible with lk_send_fn) */
int wifi_ip_link_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n);

#endif
