/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* WPA2-PSK Station (IEEE 802.11i / 802.11-2020 §12.7): PTK derivation, 4-way handshake
 * state machine (EAPOL-Key M1-M4), GTK unwrapping, 802.11 Auth/Assoc framing, LLC/SNAP
 * EAPOL encapsulation, MAC address discovery, and MAC key table installation. */
#include "wifi_sta.h"

#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
/* (the firmware's own C library, system/libc.c: no <string.h> in the toolchain's freestanding build) */
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
int st_read(uint32_t off, void *dst, uint32_t n);
uint32_t ble_crc16_xmodem(uint32_t crc, const uint8_t *p, uint32_t n);
#include "../../hal/fm1_wifi.h"
#define STA_MAC_WR(r, v) fm1_wifi_mac_wr((r), (v))
#else
#include <string.h>
/* Host stub / hook for testing */
static void (*sta_test_mac_wr_fn)(uint32_t r, uint32_t v) = 0;
void wifi_sta_set_test_mac_wr(void (*fn)(uint32_t r, uint32_t v)) { sta_test_mac_wr_fn = fn; }
#define STA_MAC_WR(r, v) do { if (sta_test_mac_wr_fn) sta_test_mac_wr_fn((r), (v)); } while (0)

static uint32_t host_crc16_xmodem(uint32_t crc, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    while (n--) {
        crc ^= (uint32_t)*p++ << 8;
        for (k = 0; k < 8u; k++)
            crc = crc & 0x8000u ? (crc << 1) ^ 0x1021u : crc << 1;
        crc &= 0xFFFFu;
    }
    return crc;
}
#define ble_crc16_xmodem host_crc16_xmodem
static int (*sta_test_st_read_fn)(uint32_t off, void *dst, uint32_t n) = 0;
void wifi_sta_set_test_st_read(int (*fn)(uint32_t off, void *dst, uint32_t n)) { sta_test_st_read_fn = fn; }
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    return sta_test_st_read_fn ? sta_test_st_read_fn(off, dst, n) : -1;
}
#endif

/* Standard WPA2-PSK RSN Information Element for Station (22 bytes) */
static const uint8_t sta_rsn_ie[22] = {
    0x30, 0x14,                                     /* Tag 0x30 (RSN), length 20 */
    0x01, 0x00,                                     /* Version 1 */
    0x00, 0x0F, 0xAC, 0x04,                         /* Group cipher: AES-CCMP */
    0x01, 0x00,                                     /* Pairwise cipher count: 1 */
    0x00, 0x0F, 0xAC, 0x04,                         /* Pairwise cipher: AES-CCMP */
    0x01, 0x00,                                     /* AKM suite count: 1 */
    0x00, 0x0F, 0xAC, 0x02,                         /* AKM: PSK (00-0F-AC-2) */
    0x00, 0x00                                      /* RSN capabilities */
};

void wifi_sta_init(struct wifi_sta *sta, const uint8_t sta_mac[6], void (*rand)(uint8_t *out, uint8_t n))
{
    memset(sta, 0, sizeof(*sta));
    if (sta_mac)
        memcpy(sta->mac, sta_mac, 6);
    sta->state = WIFI_STA_STATE_IDLE;
    sta->rand = rand;
}

void wifi_sta_set_creds(struct wifi_sta *sta, const char *pass, const uint8_t *ssid, uint32_t ssid_len)
{
    if (ssid_len > WIFI_STA_SSID_MAX)
        ssid_len = WIFI_STA_SSID_MAX;
    memcpy(sta->ssid, ssid, ssid_len);
    sta->ssid_len = ssid_len;
    wc_pmk(pass, ssid, ssid_len, sta->pmk);
}

void wifi_sta_set_ap(struct wifi_sta *sta, const uint8_t bssid[6])
{
    memcpy(sta->bssid, bssid, 6);
    sta->state = WIFI_STA_STATE_WAIT_M1;
}

void wifi_sta_derive_ptk(const uint8_t pmk[32], const uint8_t spa[6], const uint8_t aa[6],
                         const uint8_t snonce[32], const uint8_t anonce[32], uint8_t ptk[48])
{
    uint8_t data[76];
    const uint8_t *min_mac, *max_mac;
    const uint8_t *min_nonce, *max_nonce;

    if (memcmp(spa, aa, 6) < 0) {
        min_mac = spa;
        max_mac = aa;
    } else {
        min_mac = aa;
        max_mac = spa;
    }

    if (memcmp(snonce, anonce, 32) < 0) {
        min_nonce = snonce;
        max_nonce = anonce;
    } else {
        min_nonce = anonce;
        max_nonce = snonce;
    }

    memcpy(data, min_mac, 6);
    memcpy(data + 6, max_mac, 6);
    memcpy(data + 12, min_nonce, 32);
    memcpy(data + 44, max_nonce, 32);

    wc_prf(pmk, 32, "Pairwise key expansion", data, 76, ptk, 48);
}

/* the SNonce: from the caller's random source (the firmware: the radio's, ble_hw_rand), never a predictable sequence */
static void gen_snonce(struct wifi_sta *sta, uint8_t nonce[32])
{
    sta->rand(nonce, 32);
}

static uint64_t read_u64_be(const uint8_t *p)
{
    uint64_t v = 0;
    uint32_t i;
    for (i = 0; i < 8u; i++)
        v = (v << 8) | p[i];
    return v;
}

static void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void compute_eapol_mic(const uint8_t kck[16], const uint8_t *pkt, uint32_t len, uint8_t mic[16])
{
    uint8_t tmp[WIFI_STA_EAPOL_MAX];
    uint8_t digest[20];
    if (len > sizeof(tmp))
        return;
    memcpy(tmp, pkt, len);
    memset(tmp + 81, 0, 16);
    wc_hmac_sha1(kck, 16, tmp, len, digest);
    memcpy(mic, digest, 16);
}

void wifi_sta_install_hw_keys(const struct wifi_sta *sta)
{
    uint32_t i, w;

    /* 1. Program AP BSSID into WCID entry 1 (0x1808) */
    w = (uint32_t)sta->bssid[0] | ((uint32_t)sta->bssid[1] << 8) |
        ((uint32_t)sta->bssid[2] << 16) | ((uint32_t)sta->bssid[3] << 24);
    STA_MAC_WR(0x1808u, w);
    w = (uint32_t)sta->bssid[4] | ((uint32_t)sta->bssid[5] << 8);
    STA_MAC_WR(0x180Cu, w);

    /* 2. Program Pairwise Key (TK) into Pairwise Key Table entry 1 (0x4020) */
    for (i = 0; i < 4u; i++) {
        w = (uint32_t)sta->ptk.tk[4 * i] | ((uint32_t)sta->ptk.tk[4 * i + 1] << 8) |
            ((uint32_t)sta->ptk.tk[4 * i + 2] << 16) | ((uint32_t)sta->ptk.tk[4 * i + 3] << 24);
        STA_MAC_WR(0x4020u + 4u * i, w);
    }
    /* Clear MIC keys for CCMP */
    for (i = 4; i < 8u; i++)
        STA_MAC_WR(0x4020u + 4u * i, 0);

    /* 3. Program Group Key (GTK) into Shared Key Table (0x6C00 + gtk_id * 32) */
    if (sta->gtk_id < 4u) {
        uint32_t base = 0x6C00u + (uint32_t)sta->gtk_id * 32u;
        for (i = 0; i < 4u; i++) {
            w = (uint32_t)sta->gtk[4 * i] | ((uint32_t)sta->gtk[4 * i + 1] << 8) |
                ((uint32_t)sta->gtk[4 * i + 2] << 16) | ((uint32_t)sta->gtk[4 * i + 3] << 24);
            STA_MAC_WR(base + 4u * i, w);
        }
        for (i = 4; i < 8u; i++)
            STA_MAC_WR(base + 4u * i, 0);

        /* Set Shared Key Mode (0x7000): cipher 4 (AES-CCMP) for this key */
        w = 4u << ((uint32_t)sta->gtk_id * 4u);
        STA_MAC_WR(0x7000u, w);
    }

    /* 4. Configure WCID Attribute Table (0x6800 + 1 * 4 = 0x6804): cipher 4 (AES-CCMP) */
    STA_MAC_WR(0x6804u, 0x04u);

    /* 5. Clear IV/EIV for WCID 1 (0x6008) */
    STA_MAC_WR(0x6008u, 0);
    STA_MAC_WR(0x600Cu, 0);
}

int wifi_sta_rx_eapol(struct wifi_sta *sta, const uint8_t *pkt, uint32_t len, uint8_t *tx_buf, uint32_t *tx_len)
{
    uint16_t body_len, key_info, key_data_len;
    uint8_t desc_type;
    uint64_t replay;

    if (!sta || !pkt || len < 99u || !tx_buf || !tx_len)
        return -1;

    /* Check EAPOL Type = 3 (EAPOL-Key) */
    if (pkt[1] != 0x03u)
        return -1;

    body_len = ((uint16_t)pkt[2] << 8) | pkt[3];
    if ((uint32_t)body_len + 4u > len)
        return -1;

    desc_type = pkt[4];
    if (desc_type != 0x02u)                         /* 2 = RSN (802.11i) */
        return -1;

    len = 4u + body_len;                            /* (the EAPOL frame itself: any padding after it is not MIC'd) */
    key_info = ((uint16_t)pkt[5] << 8) | pkt[6];
    replay = read_u64_be(pkt + 9);
    key_data_len = ((uint16_t)pkt[97] << 8) | pkt[98];
    if (99u + key_data_len > len)                   /* (key data past the frame: refused) */
        return -1;

    /* Must be Pairwise key */
    if (!(key_info & (1u << 3)))
        return -1;

    /* Message 1 from AP: Key ACK=1, MIC=0 */
    if ((key_info & (1u << 7)) && !(key_info & (1u << 8))) {
        sta->replay_cnt = replay;
        memcpy(sta->anonce, pkt + 17, 32);

        /* Generate SNonce and derive PTK */
        gen_snonce(sta, sta->snonce);
        wifi_sta_derive_ptk(sta->pmk, sta->mac, sta->bssid, sta->snonce, sta->anonce, (uint8_t *)&sta->ptk);

        /* Build Message 2 */
        memset(tx_buf, 0, 121);
        tx_buf[0] = 0x01;                           /* 802.1X-2001 */
        tx_buf[1] = 0x03;                           /* EAPOL-Key */
        write_u16_be(tx_buf + 2, 95u + sizeof(sta_rsn_ie)); /* Body length */
        tx_buf[4] = 0x02;                           /* RSN Key Descriptor */
        /* Key Info: Version 2 (HMAC-SHA1/AES), Key Type Pairwise, Key MIC */
        write_u16_be(tx_buf + 5, 0x010Au);
        write_u16_be(tx_buf + 7, 16u);              /* Key Length */
        memcpy(tx_buf + 9, pkt + 9, 8);             /* Replay counter */
        memcpy(tx_buf + 17, sta->snonce, 32);       /* SNonce */
        /* Key Data: Station RSN IE */
        write_u16_be(tx_buf + 97, sizeof(sta_rsn_ie));
        memcpy(tx_buf + 99, sta_rsn_ie, sizeof(sta_rsn_ie));

        /* Compute MIC */
        compute_eapol_mic(sta->ptk.kck, tx_buf, 121, tx_buf + 81);

        *tx_len = 121;
        sta->state = WIFI_STA_STATE_WAIT_M3;
        return 1;
    }

    /* Message 3 from AP: Key ACK=1, MIC=1 */
    if ((key_info & (1u << 7)) && (key_info & (1u << 8))) {
        uint8_t check_buf[WIFI_STA_EAPOL_MAX];
        uint8_t expected_mic[16];
        uint8_t unwrapped[128];
        uint32_t i;

        if (sta->state != WIFI_STA_STATE_WAIT_M3)
            return -1;
        if (replay <= sta->replay_cnt)                  /* (a replayed or old M3) */
            return -1;

        if (len > sizeof(check_buf))
            return -1;

        /* Verify MIC: zero out MIC field and compute HMAC-SHA1 */
        memcpy(check_buf, pkt, len);
        memset(check_buf + 81, 0, 16);
        compute_eapol_mic(sta->ptk.kck, check_buf, len, expected_mic);

        if (memcmp(pkt + 81, expected_mic, 16) != 0) {
            sta->state = WIFI_STA_STATE_FAILED;
            return -1;
        }

        /* Check ANonce matches */
        if (memcmp(pkt + 17, sta->anonce, 32) != 0) {
            sta->state = WIFI_STA_STATE_FAILED;
            return -1;
        }

        /* If Key Data is encrypted, unwrap GTK */
        if ((key_info & (1u << 12)) && key_data_len >= 24u) {
            if (key_data_len > sizeof unwrapped + 8u || key_data_len % 8u) {
                sta->state = WIFI_STA_STATE_FAILED;
                return -1;
            }
            if (!wc_aes_unwrap(sta->ptk.kek, pkt + 99, key_data_len, unwrapped)) {
                sta->state = WIFI_STA_STATE_FAILED;
                return -1;
            }

            /* Search for GTK KDE (0xDD, len, 00-0F-AC, type 0x01) */
            uint32_t ulen = key_data_len - 8u;
            for (i = 0; i + 6u < ulen;) {
                uint8_t tag = unwrapped[i];
                uint8_t tlen = unwrapped[i + 1];
                if (i + 2u + tlen > ulen)
                    break;
                if (tag == 0xDDu && tlen >= 6u &&
                    unwrapped[i + 2] == 0x00 && unwrapped[i + 3] == 0x0F &&
                    unwrapped[i + 4] == 0xAC && unwrapped[i + 5] == 0x01) {
                    sta->gtk_id = unwrapped[i + 6] & 0x03u;
                    if (tlen >= 22u)
                        memcpy(sta->gtk, unwrapped + i + 8, 16);
                    break;
                }
                i += 2u + tlen;
            }
        }

        sta->replay_cnt = replay;

        /* Build Message 4 */
        memset(tx_buf, 0, 99);
        tx_buf[0] = 0x01;                           /* 802.1X-2001 */
        tx_buf[1] = 0x03;                           /* EAPOL-Key */
        write_u16_be(tx_buf + 2, 95u);              /* Body length */
        tx_buf[4] = 0x02;                           /* RSN Key Descriptor */
        /* Key Info: Version 2, Key Type Pairwise, Key MIC, Secure */
        write_u16_be(tx_buf + 5, 0x030Au);
        write_u16_be(tx_buf + 7, 16u);              /* Key Length */
        memcpy(tx_buf + 9, pkt + 9, 8);             /* Replay counter */
        write_u16_be(tx_buf + 97, 0);               /* Key Data Length 0 */

        /* Compute MIC */
        compute_eapol_mic(sta->ptk.kck, tx_buf, 99, tx_buf + 81);

        *tx_len = 99;

        /* Install hardware keys */
        wifi_sta_install_hw_keys(sta);
        sta->state = WIFI_STA_STATE_CONNECTED;
        return 1;
    }

    return 0;
}

uint32_t wifi_sta_build_auth(struct wifi_sta *sta, uint8_t *tx_buf)
{
    uint16_t seq;
    if (!sta || !tx_buf)
        return 0;
    seq = sta->tx_seq++;
    memset(tx_buf, 0, 30);
    tx_buf[0] = 0xB0;                               /* Type: Management, Subtype: Authentication */
    tx_buf[1] = 0x00;
    tx_buf[2] = 0x3A; tx_buf[3] = 0x01;             /* Duration */
    memcpy(tx_buf + 4, sta->bssid, 6);              /* Addr 1: DA (AP BSSID) */
    memcpy(tx_buf + 10, sta->mac, 6);               /* Addr 2: SA (Station MAC) */
    memcpy(tx_buf + 16, sta->bssid, 6);             /* Addr 3: BSSID */
    tx_buf[22] = (uint8_t)(seq << 4);               /* Sequence Control */
    tx_buf[23] = (uint8_t)(seq >> 4);
    /* Auth body: Algorithm Open System (0), Seq 1, Status 0 */
    tx_buf[24] = 0x00; tx_buf[25] = 0x00;           /* Auth algorithm 0 */
    tx_buf[26] = 0x01; tx_buf[27] = 0x00;           /* Auth transaction seq 1 */
    tx_buf[28] = 0x00; tx_buf[29] = 0x00;           /* Status code 0 */
    return 30u;
}

int wifi_sta_parse_auth_resp(const struct wifi_sta *sta, const uint8_t *frame, uint32_t len)
{
    if (!sta || !frame || len < 30u)
        return -1;
    /* Check Frame Control: Management Authentication (subtype 0x0B -> 0xB0) */
    if ((frame[0] & 0xFCu) != 0xB0u)
        return -1;
    /* Check DA == our MAC, SA == BSSID */
    if (memcmp(frame + 4, sta->mac, 6) != 0)
        return -1;
    if (memcmp(frame + 10, sta->bssid, 6) != 0)
        return -1;
    /* Body: Alg 0, Seq 2, Status 0 */
    if (frame[24] != 0 || frame[25] != 0)           /* Alg 0 */
        return -1;
    if (frame[26] != 2 || frame[27] != 0)           /* Seq 2 */
        return -1;
    if (frame[28] != 0 || frame[29] != 0)           /* Status 0 (Success) */
        return -1;
    return 0;
}

uint32_t wifi_sta_build_assoc_req(struct wifi_sta *sta, uint8_t *tx_buf)
{
    static const uint8_t rates[10] = {0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};
    static const uint8_t ext_rates[6] = {0x32, 0x04, 0x30, 0x48, 0x60, 0x6C};
    uint16_t seq;
    uint32_t off;

    if (!sta || !tx_buf)
        return 0;
    seq = sta->tx_seq++;
    memset(tx_buf, 0, 24);
    tx_buf[0] = 0x00;                               /* Type: Management, Subtype: Assoc Req */
    tx_buf[1] = 0x00;
    tx_buf[2] = 0x3A; tx_buf[3] = 0x01;             /* Duration */
    memcpy(tx_buf + 4, sta->bssid, 6);              /* Addr 1: DA (AP BSSID) */
    memcpy(tx_buf + 10, sta->mac, 6);               /* Addr 2: SA (Station MAC) */
    memcpy(tx_buf + 16, sta->bssid, 6);             /* Addr 3: BSSID */
    tx_buf[22] = (uint8_t)(seq << 4);               /* Sequence Control */
    tx_buf[23] = (uint8_t)(seq >> 4);
    off = 24;

    /* Fixed Parameters: Capability Info (0x0431), Listen Interval (10) */
    tx_buf[off++] = 0x31; tx_buf[off++] = 0x04;     /* ESS, Short Preamble, Short Slot, Privacy */
    tx_buf[off++] = 0x0A; tx_buf[off++] = 0x00;     /* Listen interval: 10 */

    /* Tag 0: SSID */
    tx_buf[off++] = 0x00;
    tx_buf[off++] = (uint8_t)sta->ssid_len;
    memcpy(tx_buf + off, sta->ssid, sta->ssid_len);
    off += sta->ssid_len;

    /* Tag 1: Supported Rates (1, 2, 5.5, 11, 6, 9, 12, 18 Mbps) */
    memcpy(tx_buf + off, rates, sizeof(rates));
    off += sizeof(rates);

    /* Tag 50 (0x32): Extended Supported Rates (24, 36, 48, 54 Mbps) */
    memcpy(tx_buf + off, ext_rates, sizeof(ext_rates));
    off += sizeof(ext_rates);

    /* Tag 48 (0x30): RSN IE (22 bytes) */
    memcpy(tx_buf + off, sta_rsn_ie, sizeof(sta_rsn_ie));
    off += sizeof(sta_rsn_ie);

    return off;
}

int wifi_sta_parse_assoc_resp(struct wifi_sta *sta, const uint8_t *frame, uint32_t len)
{
    uint16_t status, aid;
    if (!sta || !frame || len < 30u)
        return -1;
    /* Check Frame Control: Management Assoc Response (subtype 0x01 -> 0x10) */
    if ((frame[0] & 0xFCu) != 0x10u)
        return -1;
    /* Check DA == our MAC, SA == BSSID */
    if (memcmp(frame + 4, sta->mac, 6) != 0)
        return -1;
    if (memcmp(frame + 10, sta->bssid, 6) != 0)
        return -1;
    /* Fixed parameters: Capability (24..25), Status (26..27), AID (28..29) */
    status = (uint16_t)frame[26] | ((uint16_t)frame[27] << 8);
    if (status != 0)
        return -1;
    aid = ((uint16_t)frame[28] | ((uint16_t)frame[29] << 8)) & 0x3FFFu;
    sta->aid = aid;
    return 0;
}

uint32_t wifi_sta_encap_eapol(struct wifi_sta *sta, const uint8_t *eapol, uint32_t eapol_len, uint8_t *tx_buf)
{
    uint16_t seq;
    if (!sta || !tx_buf)
        return 0;
    seq = sta->tx_seq++;
    /* 802.11 Data header (24 bytes): ToDS=1, FromDS=0 */
    memset(tx_buf, 0, 24);
    tx_buf[0] = 0x08;                               /* Type: Data, Subtype: Data */
    tx_buf[1] = 0x01;                               /* ToDS = 1 */
    tx_buf[2] = 0x00; tx_buf[3] = 0x00;             /* Duration */
    memcpy(tx_buf + 4, sta->bssid, 6);              /* Addr 1: RA / BSSID */
    memcpy(tx_buf + 10, sta->mac, 6);               /* Addr 2: TA / SA */
    memcpy(tx_buf + 16, sta->bssid, 6);             /* Addr 3: DA */
    tx_buf[22] = (uint8_t)(seq << 4);
    tx_buf[23] = (uint8_t)(seq >> 4);

    /* LLC/SNAP Header (8 bytes): AA-AA-03-00-00-00-88-8E */
    tx_buf[24] = 0xAA;
    tx_buf[25] = 0xAA;
    tx_buf[26] = 0x03;
    tx_buf[27] = 0x00;
    tx_buf[28] = 0x00;
    tx_buf[29] = 0x00;
    tx_buf[30] = 0x88;
    tx_buf[31] = 0x8E;

    /* EAPOL payload */
    if (eapol && eapol_len)
        memcpy(tx_buf + 32, eapol, eapol_len);

    return 32u + eapol_len;
}

int wifi_sta_rx_frame(struct wifi_sta *sta, const uint8_t *frame, uint32_t len, uint8_t *tx_buf, uint32_t *tx_len)
{
    uint32_t hdr_len;
    if (!sta || !frame || len < 24u)
        return -1;

    if (sta->state == WIFI_STA_STATE_AUTH) {
        if (wifi_sta_parse_auth_resp(sta, frame, len) == 0) {
            sta->state = WIFI_STA_STATE_ASSOC;
            return 0;
        }
        return -1;
    }

    if (sta->state == WIFI_STA_STATE_ASSOC) {
        if (wifi_sta_parse_assoc_resp(sta, frame, len) == 0) {
            sta->state = WIFI_STA_STATE_WAIT_M1;
            return 0;
        }
        return -1;
    }

    /* Data frame: check for LLC/SNAP 802.1X EAPOL */
    if ((frame[0] & 0x0Cu) != 0x08u)
        return -1;

    /* Check QoS Data bit */
    hdr_len = (frame[0] & 0x80u) ? 26u : 24u;
    if (len < hdr_len + 8u)
        return -1;

    /* Verify LLC/SNAP header (AA-AA-03-00-00-00-88-8E) */
    if (frame[hdr_len + 0] != 0xAAu || frame[hdr_len + 1] != 0xAAu ||
        frame[hdr_len + 2] != 0x03u || frame[hdr_len + 3] != 0x00u ||
        frame[hdr_len + 4] != 0x00u || frame[hdr_len + 5] != 0x00u ||
        frame[hdr_len + 6] != 0x88u || frame[hdr_len + 7] != 0x8Eu)
        return -1;

    {
        const uint8_t *eapol_pkt = frame + hdr_len + 8u;
        uint32_t eapol_len = len - (hdr_len + 8u);
        uint8_t eapol_resp[WIFI_STA_EAPOL_MAX];
        uint32_t eapol_resp_len = 0;

        int res = wifi_sta_rx_eapol(sta, eapol_pkt, eapol_len, eapol_resp, &eapol_resp_len);
        if (res == 1 && tx_buf && tx_len) {
            *tx_len = wifi_sta_encap_eapol(sta, eapol_resp, eapol_resp_len, tx_buf);
            return 1;
        }
        return res;
    }
}

int wifi_sta_get_mac(uint8_t mac[6])
{
    uint8_t h[10];
    if (st_read(0x0E9000u, h, sizeof(h)) == 0) {
        if (h[1] == 0x66 && (h[2] & 0x0Fu) == 0 && (h[2] >> 4) == 6 && h[3] == 0) {
            uint32_t crc = ble_crc16_xmodem(0, h + 4, 6);
            if ((crc & 0xFFu) == h[0]) {
                memcpy(mac, h + 4, 6);
                mac[0] |= 0x02;                     /* locally administered bit */
                return 1;
            }
        }
    }
    /* Fallback MAC address */
    mac[0] = 0x02;
    mac[1] = 0x11;
    mac[2] = 0x22;
    mac[3] = 0x33;
    mac[4] = 0x44;
    mac[5] = 0x55;
    return 0;
}

void wifi_sta_set_mac_regs(const uint8_t sta_mac[6], const uint8_t bssid[6], uint32_t channel)
{
    /* Station MAC: 0x1008 / 0x100C */
    STA_MAC_WR(0x1008u, (uint32_t)sta_mac[0] | ((uint32_t)sta_mac[1] << 8) |
                        ((uint32_t)sta_mac[2] << 16) | ((uint32_t)sta_mac[3] << 24));
    STA_MAC_WR(0x100Cu, (uint32_t)sta_mac[4] | ((uint32_t)sta_mac[5] << 8) | 0x00FF0000u);

    /* Target BSSID: 0x1010 / 0x1014 */
    STA_MAC_WR(0x1010u, (uint32_t)bssid[0] | ((uint32_t)bssid[1] << 8) |
                        ((uint32_t)bssid[2] << 16) | ((uint32_t)bssid[3] << 24));
    STA_MAC_WR(0x1014u, (uint32_t)bssid[4] | ((uint32_t)bssid[5] << 8));

    /* Radio channel: 0x1405C */
#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
    fm1_wifi_set_channel(channel);
    fm1_wifi_mac_wr(FM1_WIFI_RX_FILTER, FM1_WIFI_RX_FILTER_ALL);
#else
    (void)channel;
#endif
}
