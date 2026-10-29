/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP (IEEE 802.11-2020 §12.7.6): the WPA2-PSK authenticator for one client, the 4-way handshake (EAPOL-Key M1-M4)
 * as the other side of wifi_sta.c. Its PTK derivation is the station's (wifi_sta_derive_ptk); the group key is wrapped
 * (RFC 3394, wc_aes_wrap) in M3. Also here: the beacon and the probe, authentication, association and deauthentication
 * answers, the association table, and the EAPOL data frames. The DHCP server is wifi_ap_dhcp.h. Not yet: the per-client
 * MAC key slots (wifi_apd.c holds one handshake record per client), the page, the settings field and the builder item. */
#ifndef WIFI_AP_H
#define WIFI_AP_H
#include <stdint.h>
#include "wifi_sta.h"

enum wifi_ap_state {
    WIFI_AP_STATE_IDLE = 0,
    WIFI_AP_STATE_WAIT_M2,                 /* M1 sent, waiting for the client's M2 */
    WIFI_AP_STATE_PTK,                     /* M2 checked, PTK derived, M3 to send */
    WIFI_AP_STATE_WAIT_M4,                 /* M3 sent, waiting for M4 */
    WIFI_AP_STATE_CONNECTED,               /* M4 checked: the client holds the same PTK and GTK */
};

#define WIFI_AP_CLIENTS 4u                      /* the association table: the most stations at once */
#define WIFI_AP_FRAME_MAX 192u                  /* the largest frame built here: M3 in its data frame, 187 B (a beacon: 117 B) */
enum wifi_ap_client_state { WIFI_AP_CL_NONE = 0, WIFI_AP_CL_AUTH, WIFI_AP_CL_ASSOC };
struct wifi_ap_client {
    uint8_t mac[6];                        /* the station's MAC address (SA) */
    uint8_t state;                         /* enum wifi_ap_client_state */
    uint16_t aid;                          /* association ID: the table slot + 1 */
};

struct wifi_ap {
    enum wifi_ap_state state;
    uint8_t mac[6];                        /* AP MAC address (AA) */
    uint8_t client[6];                     /* client MAC address (SPA) */
    uint8_t pmk[32];                       /* Pairwise Master Key (the same as the client's: PBKDF2 of the passphrase) */
    uint8_t anonce[32];                    /* AP Nonce */
    uint8_t snonce[32];                    /* client Nonce, from M2 */
    struct wifi_sta_ptk ptk;               /* Derived PTK */
    uint8_t gtk[16];                       /* Group Temporal Key, random at init */
    uint8_t gtk_id;                        /* GTK Key ID (1) */
    uint64_t replay;                       /* Replay counter of the last M1 or M3 sent */
    void (*rand)(uint8_t *out, uint8_t n); /* random bytes (firmware: ble_hw_rand) */
    uint8_t ssid[WIFI_STA_SSID_MAX];       /* the network's name, as in the beacons */
    uint32_t ssid_len;
    uint8_t channel;                       /* the radio channel: the DS parameter in the beacon */
    uint16_t seq;                          /* management and data sequence counter */
    struct wifi_ap_client clients[WIFI_AP_CLIENTS];
};

void wifi_ap_init(struct wifi_ap *ap, const uint8_t ap_mac[6], const uint8_t pmk[32], void (*rand)(uint8_t *out, uint8_t n));
/* a new client: a fresh ANonce, the replay counter carries on (it only ever goes up) */
void wifi_ap_start_client(struct wifi_ap *ap, const uint8_t client[6]);
/* builds M1 (99 bytes); returns the frame length */
uint32_t wifi_ap_build_m1(struct wifi_ap *ap, uint8_t *tx_buf);
/* checks the client's M2 (MIC with the PTK derived from SNonce); 0 on success, -1 otherwise (state unchanged) */
int wifi_ap_rx_m2(struct wifi_ap *ap, const uint8_t *pkt, uint32_t len);
/* builds M3 (MIC; the wrapped key data: the RSN element as in the beacon, the GTK); returns the frame length (155 bytes) */
uint32_t wifi_ap_build_m3(struct wifi_ap *ap, uint8_t *tx_buf);
/* checks the client's M4 (MIC, replay); 0 on success: the client has installed the same keys */
int wifi_ap_rx_m4(struct wifi_ap *ap, const uint8_t *pkt, uint32_t len);

/* the network's name and channel (the PMK is the caller's: wc_pmk over the same name) */
void wifi_ap_set_net(struct wifi_ap *ap, const uint8_t *ssid, uint32_t ssid_len, uint8_t channel);
/* a beacon (every 102 TU; tsf is the caller's clock): returns the frame length */
uint32_t wifi_ap_build_beacon(struct wifi_ap *ap, uint64_t tsf, uint8_t *tx_buf);
/* a management frame from a station: probe request, authentication, association, deauthentication or disassociation.
 * Returns 1 with the answer in tx_buf (tx_len set), 0 when there is no answer, -1 when the frame is not for this AP */
int wifi_ap_rx_mgmt(struct wifi_ap *ap, const uint8_t *frame, uint32_t len, uint64_t tsf, uint8_t *tx_buf, uint32_t *tx_len);
struct wifi_ap_client *wifi_ap_find_client(struct wifi_ap *ap, const uint8_t mac[6]);
/* a station's data frame carrying EAPOL (LLC/SNAP 88-8E) from an associated station: the EAPOL packet, or NULL */
const uint8_t *wifi_ap_unwrap_eapol(struct wifi_ap *ap, const uint8_t *frame, uint32_t len, uint32_t *eapol_len);
/* a deauthentication frame to a station (reason 3: leaving, 15: the 4-way handshake timed out): returns the frame length (26) */
uint32_t wifi_ap_build_deauth(struct wifi_ap *ap, const uint8_t da[6], uint16_t reason, uint8_t *tx_buf);
/* an EAPOL packet to a station, as a FromDS data frame with LLC/SNAP: returns the frame length */
uint32_t wifi_ap_encap_eapol(struct wifi_ap *ap, const uint8_t da[6], const uint8_t *eapol, uint32_t eapol_len, uint8_t *tx_buf);

/* the default password of an Optimist access point of that name (WIFI_AP_KEY_LEN lower-case letters and digits, no
 * i l o 0 1; a pure function of the name: the station side computes the same). out holds WIFI_AP_KEY_LEN + 1 */
#define WIFI_AP_KEY_LEN 8u
void wifi_ap_default_key(const uint8_t *ssid, uint32_t len, char out[WIFI_AP_KEY_LEN + 1u]);

#endif
