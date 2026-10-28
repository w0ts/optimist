/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* MODE AP authenticator (wifi_ap.h): the 4-way handshake for one client, the mirror of wifi_sta.c. */
#include "wifi_ap.h"
#include "wifi_crypto.h"
#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
/* (the firmware's own C library, system/libc.c: no <string.h> in the toolchain's freestanding build) */
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#else
#include <string.h>
#endif

#define WIFI_AP_EAPOL_MAX 256u
#define WIFI_AP_M1_LEN 99u
#define WIFI_AP_RSN_LEN 22u                     /* the RSN element, as in the beacon (the supplicant compares it with M3's) */
#define WIFI_AP_M3_KDE_LEN 24u                  /* GTK KDE: DD 16 00-0F-AC 01 keyid 00 gtk[16] */
#define WIFI_AP_M3_PLAIN_LEN 48u                /* RSN element + GTK KDE + the pad (DD 00): a multiple of 8 */
#define WIFI_AP_M3_KEYDATA_LEN 56u              /* wrapped: 48 + 8 */
#define WIFI_AP_M3_LEN (99u + WIFI_AP_M3_KEYDATA_LEN)   /* 155 */

static const uint8_t ap_rsn_ie[WIFI_AP_RSN_LEN] = {
    0x30, 0x14,                                     /* Tag 0x30 (RSN), length 20 */
    0x01, 0x00,                                     /* Version 1 */
    0x00, 0x0F, 0xAC, 0x04,                         /* Group cipher: AES-CCMP */
    0x01, 0x00,                                     /* Pairwise cipher count: 1 */
    0x00, 0x0F, 0xAC, 0x04,                         /* Pairwise cipher: AES-CCMP */
    0x01, 0x00,                                     /* AKM suite count: 1 */
    0x00, 0x0F, 0xAC, 0x02,                         /* AKM: PSK */
    0x00, 0x00                                      /* RSN capabilities */
};

static void ap_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t ap_get_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void ap_put_u64(uint8_t *p, uint64_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        p[i] = (uint8_t)(v >> (56 - 8 * i));
}

static uint64_t ap_get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    uint32_t i;
    for (i = 0; i < 8u; i++)
        v = (v << 8) | p[i];
    return v;
}

static void ap_eapol_mic(const uint8_t kck[16], const uint8_t *pkt, uint32_t len, uint8_t mic[16])
{
    uint8_t tmp[WIFI_AP_EAPOL_MAX];
    uint8_t digest[20];
    memcpy(tmp, pkt, len);
    memset(tmp + 81, 0, 16);
    wc_hmac_sha1(kck, 16, tmp, len, digest);
    memcpy(mic, digest, 16);
}

static int ap_eapol_check(const uint8_t *pkt, uint32_t len, uint16_t key_info_mask, uint16_t key_info_want)
{
    uint16_t body_len;
    if (len < WIFI_AP_M1_LEN || pkt[1] != 0x03u || pkt[4] != 0x02u)
        return -1;
    body_len = ap_get_u16(pkt + 2);
    if ((uint32_t)body_len + 4u > len)
        return -1;
    if ((ap_get_u16(pkt + 5) & key_info_mask) != key_info_want)
        return -1;
    return 0;
}

void wifi_ap_init(struct wifi_ap *ap, const uint8_t ap_mac[6], const uint8_t pmk[32], void (*rand)(uint8_t *out, uint8_t n))
{
    memset(ap, 0, sizeof(*ap));
    memcpy(ap->mac, ap_mac, 6);
    memcpy(ap->pmk, pmk, 32);
    ap->rand = rand;
    ap->gtk_id = 1;
    ap->rand(ap->gtk, 16);
    ap->state = WIFI_AP_STATE_IDLE;
}

void wifi_ap_start_client(struct wifi_ap *ap, const uint8_t client[6])
{
    memcpy(ap->client, client, 6);
    ap->rand(ap->anonce, 32);
    ap->state = WIFI_AP_STATE_IDLE;
}

uint32_t wifi_ap_build_m1(struct wifi_ap *ap, uint8_t *tx_buf)
{
    memset(tx_buf, 0, WIFI_AP_M1_LEN);
    tx_buf[0] = 0x01;                           /* 802.1X-2001 */
    tx_buf[1] = 0x03;                           /* EAPOL-Key */
    ap_put_u16(tx_buf + 2, 95u);                /* Body length */
    tx_buf[4] = 0x02;                           /* RSN Key Descriptor */
    ap_put_u16(tx_buf + 5, 0x008Au);            /* Version 2, Pairwise, Key ACK */
    ap_put_u16(tx_buf + 7, 16u);                /* Key Length */
    ap_put_u64(tx_buf + 9, ++ap->replay);       /* Replay counter, M3 will be one higher */
    memcpy(tx_buf + 17, ap->anonce, 32);        /* ANonce */
    ap->state = WIFI_AP_STATE_WAIT_M2;
    return WIFI_AP_M1_LEN;
}

int wifi_ap_rx_m2(struct wifi_ap *ap, const uint8_t *pkt, uint32_t len)
{
    struct wifi_sta_ptk ptk;
    uint8_t derived[48];
    uint16_t body_len;
    uint8_t expected[16];
    uint32_t n;

    if (ap->state != WIFI_AP_STATE_WAIT_M2)
        return -1;
    /* MIC set, not an ACK (M2 is the client's), pairwise */
    if (ap_eapol_check(pkt, len, (1u << 3) | (1u << 7) | (1u << 8), (1u << 3) | (1u << 8)) != 0)
        return -1;
    body_len = ap_get_u16(pkt + 2);
    if (ap_get_u64(pkt + 9) != ap->replay)
        return -1;

    /* the PTK from the client's SNonce: a copy until the MIC checks */
    wifi_sta_derive_ptk(ap->pmk, ap->client, ap->mac, pkt + 17, ap->anonce, derived);
    memcpy(&ptk, derived, sizeof(ptk));

    n = 4u + (uint32_t)body_len;
    if (n > WIFI_AP_EAPOL_MAX)
        return -1;
    ap_eapol_mic(ptk.kck, pkt, n, expected);
    if (memcmp(pkt + 81, expected, 16) != 0)
        return -1;

    memcpy(ap->snonce, pkt + 17, 32);
    ap->ptk = ptk;
    ap->state = WIFI_AP_STATE_PTK;
    return 0;
}

uint32_t wifi_ap_build_m3(struct wifi_ap *ap, uint8_t *tx_buf)
{
    uint8_t kd[WIFI_AP_M3_PLAIN_LEN];

    memset(tx_buf, 0, WIFI_AP_M3_LEN);
    tx_buf[0] = 0x01;
    tx_buf[1] = 0x03;
    ap_put_u16(tx_buf + 2, (uint16_t)(95u + WIFI_AP_M3_KEYDATA_LEN));
    tx_buf[4] = 0x02;
    /* Pairwise, Install, ACK, MIC, Secure, Encrypted Key Data */
    ap_put_u16(tx_buf + 5, 0x13CAu);
    ap_put_u16(tx_buf + 7, 16u);
    ap_put_u64(tx_buf + 9, ++ap->replay);
    memcpy(tx_buf + 17, ap->anonce, 32);
    ap_put_u16(tx_buf + 97, (uint16_t)WIFI_AP_M3_KEYDATA_LEN);

    /* the key data: the RSN element (a supplicant compares it with the beacon's and leaves if it differs), then the GTK KDE
     * (its key id, the 16-byte group key), then the pad DD 00; all wrapped under the KEK */
    memcpy(kd, ap_rsn_ie, WIFI_AP_RSN_LEN);
    kd[22] = 0xDD;
    kd[23] = 22u;
    kd[24] = 0x00;
    kd[25] = 0x0F;
    kd[26] = 0xAC;
    kd[27] = 0x01;
    kd[28] = ap->gtk_id;
    kd[29] = 0x00;
    memcpy(kd + 30, ap->gtk, 16);
    kd[46] = 0xDD;
    kd[47] = 0x00;
    wc_aes_wrap(ap->ptk.kek, kd, WIFI_AP_M3_PLAIN_LEN, tx_buf + 99);

    ap_eapol_mic(ap->ptk.kck, tx_buf, WIFI_AP_M3_LEN, tx_buf + 81);
    ap->state = WIFI_AP_STATE_WAIT_M4;
    return WIFI_AP_M3_LEN;
}

int wifi_ap_rx_m4(struct wifi_ap *ap, const uint8_t *pkt, uint32_t len)
{
    uint8_t expected[16];

    if (ap->state != WIFI_AP_STATE_WAIT_M4)
        return -1;
    /* pairwise, MIC, Secure, not an ACK */
    if (ap_eapol_check(pkt, len, (1u << 3) | (1u << 7) | (1u << 8) | (1u << 9), (1u << 3) | (1u << 8) | (1u << 9)) != 0)
        return -1;
    if (ap_get_u16(pkt + 2) != 95u || ap_get_u64(pkt + 9) != ap->replay)
        return -1;
    ap_eapol_mic(ap->ptk.kck, pkt, WIFI_AP_M1_LEN, expected);
    if (memcmp(pkt + 81, expected, 16) != 0)
        return -1;
    ap->state = WIFI_AP_STATE_CONNECTED;
    return 0;
}

/* ---- beacons, management answers, the association table, EAPOL data frames */

static const uint8_t ap_rates[10] = {0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};
static const uint8_t ap_ext_rates[6] = {0x32, 0x04, 0x30, 0x48, 0x60, 0x6C};
static const uint8_t ap_llc[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
static const uint8_t ap_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void ap_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void ap_le64(uint8_t *p, uint64_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* the 24-byte header: fc0 (type, subtype), fc1 (the To/From DS flags), RA = the receiver; addr2 and addr3 are this AP */
static void ap_hdr(struct wifi_ap *ap, uint8_t fc0, uint8_t fc1, const uint8_t ra[6], uint8_t *tx_buf)
{
    uint16_t seq = ap->seq++;
    memset(tx_buf, 0, 24);
    tx_buf[0] = fc0;
    tx_buf[1] = fc1;
    memcpy(tx_buf + 4, ra, 6);
    memcpy(tx_buf + 10, ap->mac, 6);
    memcpy(tx_buf + 16, ap->mac, 6);
    ap_le16(tx_buf + 22, (uint16_t)(seq << 4));
}

/* the SSID tag among a frame's tagged parameters: 0 our name, 1 a wildcard (empty name), -1 another name or none */
static int ap_ssid_tag(const struct wifi_ap *ap, const uint8_t *tags, uint32_t n)
{
    uint32_t i = 0;
    while (i + 2u <= n) {
        uint8_t tag = tags[i];
        uint8_t tlen = tags[i + 1u];
        if (i + 2u + tlen > n)
            break;
        if (tag == 0u) {
            if (tlen == 0u)
                return 1;
            if (tlen == ap->ssid_len && memcmp(tags + i + 2u, ap->ssid, tlen) == 0)
                return 0;
            return -1;
        }
        i += 2u + tlen;
    }
    return -1;
}

/* a beacon (beacon = 1) or a probe answer (beacon = 0): the fixed parameters and the tags */
static uint32_t ap_build_info(struct wifi_ap *ap, uint8_t fc0, const uint8_t ra[6], uint64_t tsf, int beacon, uint8_t *tx_buf)
{
    uint32_t off;
    ap_hdr(ap, fc0, 0, ra, tx_buf);
    ap_le64(tx_buf + 24, tsf);                      /* Timestamp */
    ap_le16(tx_buf + 32, 102u);                     /* Beacon interval: 102 TU */
    ap_le16(tx_buf + 34, 0x0431u);                  /* ESS, Privacy, Short Preamble, Short Slot */
    off = 36;
    tx_buf[off++] = 0x00;                           /* Tag 0: SSID */
    tx_buf[off++] = (uint8_t)ap->ssid_len;
    memcpy(tx_buf + off, ap->ssid, ap->ssid_len);
    off += ap->ssid_len;
    memcpy(tx_buf + off, ap_rates, sizeof(ap_rates));
    off += sizeof(ap_rates);
    tx_buf[off++] = 0x03;                           /* Tag 3: DS Parameter Set, the channel */
    tx_buf[off++] = 0x01;
    tx_buf[off++] = ap->channel;
    if (beacon) {
        tx_buf[off++] = 0x05;                       /* Tag 5: TIM, DTIM count 0, period 1, no group traffic buffered */
        tx_buf[off++] = 0x04;
        tx_buf[off++] = 0x00;
        tx_buf[off++] = 0x01;
        tx_buf[off++] = 0x00;
        tx_buf[off++] = 0x00;
    }
    memcpy(tx_buf + off, ap_ext_rates, sizeof(ap_ext_rates));
    off += sizeof(ap_ext_rates);
    memcpy(tx_buf + off, ap_rsn_ie, sizeof(ap_rsn_ie));
    off += sizeof(ap_rsn_ie);
    return off;
}

void wifi_ap_set_net(struct wifi_ap *ap, const uint8_t *ssid, uint32_t ssid_len, uint8_t channel)
{
    if (ssid_len > WIFI_STA_SSID_MAX)
        ssid_len = WIFI_STA_SSID_MAX;
    memcpy(ap->ssid, ssid, ssid_len);
    ap->ssid_len = ssid_len;
    ap->channel = channel;
}

uint32_t wifi_ap_build_beacon(struct wifi_ap *ap, uint64_t tsf, uint8_t *tx_buf)
{
    return ap_build_info(ap, 0x80u, ap_bcast, tsf, 1, tx_buf);
}

struct wifi_ap_client *wifi_ap_find_client(struct wifi_ap *ap, const uint8_t mac[6])
{
    uint32_t i;
    for (i = 0; i < WIFI_AP_CLIENTS; i++)
        if (ap->clients[i].state != WIFI_AP_CL_NONE && memcmp(ap->clients[i].mac, mac, 6) == 0)
            return &ap->clients[i];
    return 0;
}

/* the station's entry, or a free slot for it (its AID is the slot + 1); 0 when the table is full */
static struct wifi_ap_client *ap_client_add(struct wifi_ap *ap, const uint8_t mac[6])
{
    struct wifi_ap_client *cl = wifi_ap_find_client(ap, mac);
    uint32_t i;
    if (cl)
        return cl;
    for (i = 0; i < WIFI_AP_CLIENTS; i++) {
        if (ap->clients[i].state == WIFI_AP_CL_NONE) {
            cl = &ap->clients[i];
            memcpy(cl->mac, mac, 6);
            cl->aid = (uint16_t)(i + 1u);
            return cl;
        }
    }
    return 0;
}

int wifi_ap_rx_mgmt(struct wifi_ap *ap, const uint8_t *frame, uint32_t len, uint64_t tsf, uint8_t *tx_buf, uint32_t *tx_len)
{
    struct wifi_ap_client *cl;
    const uint8_t *sa;

    if (!ap || !frame || !tx_buf || !tx_len || len < 24u || (frame[0] & 0x0Cu) != 0u)
        return -1;
    sa = frame + 10;
    switch (frame[0] & 0xFCu) {
    case 0x40u:                                     /* probe request: to all, or to our network's name */
        if (len < 26u || (memcmp(frame + 4, ap_bcast, 6) != 0 && memcmp(frame + 4, ap->mac, 6) != 0))
            return -1;
        if (ap_ssid_tag(ap, frame + 24, len - 24u) < 0)
            return 0;
        *tx_len = ap_build_info(ap, 0x50u, sa, tsf, 0, tx_buf);
        return 1;
    case 0xB0u:                                     /* authentication: open system, transaction 1 */
        if (len < 30u || memcmp(frame + 4, ap->mac, 6) != 0)
            return -1;
        if (frame[24] != 0 || frame[25] != 0 || frame[26] != 1 || frame[27] != 0)
            return -1;
        cl = ap_client_add(ap, sa);
        if (cl)
            cl->state = WIFI_AP_CL_AUTH;
        ap_hdr(ap, 0xB0u, 0, sa, tx_buf);
        ap_le16(tx_buf + 24, 0u);                   /* Open System */
        ap_le16(tx_buf + 26, 2u);                   /* transaction 2 */
        ap_le16(tx_buf + 28, cl ? 0u : 17u);        /* status 0, or 17: the AP cannot take another station */
        *tx_len = 30u;
        return 1;
    case 0x00u:                                     /* association request: an authenticated station, our name */
        if (len < 28u || memcmp(frame + 4, ap->mac, 6) != 0)
            return -1;
        cl = wifi_ap_find_client(ap, sa);
        if (!cl || ap_ssid_tag(ap, frame + 28, len - 28u) != 0)
            return 0;
        cl->state = WIFI_AP_CL_ASSOC;
        ap_hdr(ap, 0x10u, 0, sa, tx_buf);
        ap_le16(tx_buf + 24, 0x0431u);              /* Capability */
        ap_le16(tx_buf + 26, 0u);                   /* Status 0 */
        ap_le16(tx_buf + 28, (uint16_t)(0xC000u | cl->aid));
        memcpy(tx_buf + 30, ap_rates, sizeof(ap_rates));          /* (a client leaves an answer without its rate set) */
        memcpy(tx_buf + 30 + sizeof(ap_rates), ap_ext_rates, sizeof(ap_ext_rates));
        *tx_len = 30u + sizeof(ap_rates) + sizeof(ap_ext_rates);
        return 1;
    case 0xA0u:                                     /* disassociation */
    case 0xC0u:                                     /* deauthentication: the station is gone, its slot is free */
        if (len < 26u || memcmp(frame + 4, ap->mac, 6) != 0)
            return -1;
        cl = wifi_ap_find_client(ap, sa);
        if (cl)
            memset(cl, 0, sizeof(*cl));
        return 0;
    default:
        return -1;
    }
}

const uint8_t *wifi_ap_unwrap_eapol(struct wifi_ap *ap, const uint8_t *frame, uint32_t len, uint32_t *eapol_len)
{
    struct wifi_ap_client *cl;
    uint32_t hl;
    /* a data frame (QoS or not) from a station to us (ToDS, RA = this AP), LLC/SNAP 802.1X, from an associated station */
    if (!ap || !frame || !eapol_len || len < 36u)
        return 0;
    if ((frame[0] & 0x7Cu) != 0x08u || (frame[1] & 0x03u) != 0x01u || memcmp(frame + 4, ap->mac, 6) != 0)
        return 0;
    hl = (frame[0] & 0x80u) ? 26u : 24u;
    if (len < hl + 8u + 4u || memcmp(frame + hl, ap_llc, sizeof(ap_llc)) != 0)
        return 0;
    cl = wifi_ap_find_client(ap, frame + 10);
    if (!cl || cl->state != WIFI_AP_CL_ASSOC)
        return 0;
    *eapol_len = len - hl - 8u;
    return frame + hl + 8u;
}

uint32_t wifi_ap_build_deauth(struct wifi_ap *ap, const uint8_t da[6], uint16_t reason, uint8_t *tx_buf)
{
    ap_hdr(ap, 0xC0u, 0, da, tx_buf);
    ap_le16(tx_buf + 24, reason);
    return 26u;
}

uint32_t wifi_ap_encap_eapol(struct wifi_ap *ap, const uint8_t da[6], const uint8_t *eapol, uint32_t eapol_len, uint8_t *tx_buf)
{
    ap_hdr(ap, 0x08u, 0x02u, da, tx_buf);          /* data, FromDS: RA = the station, TA = this AP */
    memcpy(tx_buf + 24, ap_llc, sizeof(ap_llc));
    memcpy(tx_buf + 32, eapol, eapol_len);
    return 32u + eapol_len;
}
