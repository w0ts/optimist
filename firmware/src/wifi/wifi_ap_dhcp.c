/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP DHCP server (wifi_ap_dhcp.h): DISCOVER -> OFFER, REQUEST -> ACK or NAK, RELEASE frees the address. */
#include "wifi_ap_dhcp.h"
#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
/* (the firmware's own C library, system/libc.c: no <string.h> in the toolchain's freestanding build) */
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#else
#include <string.h>
#endif

#define DHCP_FIXED 240u                         /* the BOOTP fixed part, the magic cookie, then the options */
#define DHCP_LEASE_SECS 3600u                   /* one hour */

static const uint8_t dhcp_cookie[4] = {0x63, 0x82, 0x53, 0x63};
static const uint8_t mask[4] = {255, 255, 255, 0};

static int dhcp_lease_index(const struct wifi_ap_dhcp *d, const uint8_t mac[6])
{
    uint32_t i;
    for (i = 0; i < WIFI_AP_DHCP_LEASES; i++)
        if (d->leases[i].used && memcmp(d->leases[i].mac, mac, 6) == 0)
            return (int)i;
    return -1;
}

static int dhcp_lease_add(struct wifi_ap_dhcp *d, const uint8_t mac[6])
{
    uint32_t i;
    int li = dhcp_lease_index(d, mac);
    if (li >= 0)
        return li;
    for (i = 0; i < WIFI_AP_DHCP_LEASES; i++) {
        if (!d->leases[i].used) {
            d->leases[i].used = 1;
            memcpy(d->leases[i].mac, mac, 6);
            return (int)i;
        }
    }
    return -1;
}

static void dhcp_lease_ip(const struct wifi_ap_dhcp *d, uint32_t i, uint8_t ip[4])
{
    memcpy(ip, d->server, 4);
    ip[3] = (uint8_t)(i + 2u);
}

static uint32_t dhcp_put_addr_option(uint8_t *out, uint32_t o, uint8_t tag, const uint8_t addr[4])
{
    out[o] = tag;
    out[o + 1u] = 4;
    memcpy(out + o + 2u, addr, 4);
    return o + 6u;
}

/* the reply: type 2 (offer), 5 (ack), 6 (nak); li is the lease for an offer or ack, -1 for a nak */
static uint32_t dhcp_build(const struct wifi_ap_dhcp *d, const uint8_t *msg, uint8_t type, int li, uint8_t *out)
{
    uint8_t yiaddr[4] = {0, 0, 0, 0};
    uint32_t o;

    memset(out, 0, WIFI_AP_DHCP_MSG_MAX);
    out[0] = 2u;                                    /* BOOTREPLY */
    out[1] = 1u;                                    /* Ethernet */
    out[2] = 6u;                                    /* hardware address length */
    memcpy(out + 4, msg + 4, 4);                    /* xid */
    memcpy(out + 10, msg + 10, 2);                  /* flags */
    if (li >= 0) {
        dhcp_lease_ip(d, (uint32_t)li, yiaddr);
        memcpy(out + 16, yiaddr, 4);                /* yiaddr: the address offered or acked */
    }
    memcpy(out + 20, d->server, 4);                 /* siaddr: the next server is us */
    memcpy(out + 28, msg + 28, 16);                 /* chaddr */
    memcpy(out + 236u, dhcp_cookie, 4);

    o = DHCP_FIXED;
    out[o] = 53u;                                   /* DHCP message type */
    out[o + 1u] = 1u;
    out[o + 2u] = type;
    o += 3u;
    o = dhcp_put_addr_option(out, o, 54u, d->server);   /* server identifier */
    if (type != 6u) {
        out[o] = 51u;                               /* lease time */
        out[o + 1u] = 4;
        out[o + 2u] = 0;
        out[o + 3u] = 0;
        out[o + 4u] = (uint8_t)(DHCP_LEASE_SECS >> 8);
        out[o + 5u] = (uint8_t)DHCP_LEASE_SECS;
        o += 6u;
        o = dhcp_put_addr_option(out, o, 1u, mask);        /* subnet mask */
        o = dhcp_put_addr_option(out, o, 3u, d->server);   /* router: the AP */
        o = dhcp_put_addr_option(out, o, 6u, d->server);   /* DNS: the AP */
    }
    out[o] = 255u;                                  /* end */
    return WIFI_AP_DHCP_MSG_MAX;
}

void wifi_ap_dhcp_init(struct wifi_ap_dhcp *d, const uint8_t server[4])
{
    memset(d, 0, sizeof(*d));
    memcpy(d->server, server, 4);
}

uint32_t wifi_ap_dhcp_reply(struct wifi_ap_dhcp *d, const uint8_t *msg, uint32_t len, uint8_t *out)
{
    uint32_t i = DHCP_FIXED;
    uint8_t type = 0;
    uint8_t req_ip[4];
    uint8_t lease_ip[4];
    int have_req = 0;
    int li;

    if (!d || !msg || !out || len < DHCP_FIXED)
        return 0;
    if (msg[0] != 1u || msg[1] != 1u || msg[2] != 6u)   /* BOOTREQUEST, Ethernet, 6 */
        return 0;
    if (memcmp(msg + 236u, dhcp_cookie, 4) != 0)
        return 0;

    while (i < len) {
        uint8_t tag = msg[i];
        if (tag == 255u)
            break;
        if (tag == 0u) {
            i++;
            continue;
        }
        if (i + 1u >= len || i + 2u + msg[i + 1u] > len)
            break;
        if (tag == 53u && msg[i + 1u] == 1u)
            type = msg[i + 2u];
        if (tag == 50u && msg[i + 1u] == 4u) {
            memcpy(req_ip, msg + i + 2u, 4);
            have_req = 1;
        }
        i += 2u + msg[i + 1u];
    }
    if (!have_req) {                                /* a renewal names its address in ciaddr */
        memcpy(req_ip, msg + 12, 4);
    }

    switch (type) {
    case 1u:                                        /* DISCOVER */
        li = dhcp_lease_add(d, msg + 28);
        if (li < 0)
            return 0;
        return dhcp_build(d, msg, 2u, li, out);
    case 3u:                                        /* REQUEST */
        li = dhcp_lease_index(d, msg + 28);
        if (li < 0)
            return dhcp_build(d, msg, 6u, -1, out);
        dhcp_lease_ip(d, (uint32_t)li, lease_ip);
        if (memcmp(req_ip, lease_ip, 4) == 0)
            return dhcp_build(d, msg, 5u, li, out);
        return dhcp_build(d, msg, 6u, -1, out);
    case 7u:                                        /* RELEASE */
        li = dhcp_lease_index(d, msg + 28);
        if (li >= 0)
            d->leases[li].used = 0;
        return 0;
    default:
        return 0;
    }
}
