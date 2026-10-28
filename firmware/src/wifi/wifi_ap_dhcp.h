/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP's DHCP server (RFC 2131, the options a small network needs): an address for each station that asks, from the
 * AP's own /24 (server .1, the stations .2 to .5), the AP as the router and the DNS, and the lease time. Pure: the caller
 * carries the UDP datagrams (port 67 in, 68 out) and the BOOTP payload is the only thing this module reads or writes. */
#ifndef WIFI_AP_DHCP_H
#define WIFI_AP_DHCP_H
#include <stdint.h>

#define WIFI_AP_DHCP_LEASES 4u
#define WIFI_AP_DHCP_MSG_MAX 300u               /* the BOOTP minimum: a reply is this long */

struct wifi_ap_dhcp_lease {
    uint8_t mac[6];                             /* the client's hardware address (chaddr) */
    uint8_t used;
};

struct wifi_ap_dhcp {
    uint8_t server[4];                          /* the AP's address, e.g. 192.168.4.1: the pool is the rest of its /24 */
    struct wifi_ap_dhcp_lease leases[WIFI_AP_DHCP_LEASES];   /* lease i is server's network + (i + 2) */
};

void wifi_ap_dhcp_init(struct wifi_ap_dhcp *d, const uint8_t server[4]);
/* a BOOTREQUEST payload (msg, len bytes): returns the reply's length, which is WIFI_AP_DHCP_MSG_MAX and is written to
 * out, or 0 for no reply (not a request, a release, or no free address) */
uint32_t wifi_ap_dhcp_reply(struct wifi_ap_dhcp *d, const uint8_t *msg, uint32_t len, uint8_t *out);

#endif
