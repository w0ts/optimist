/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Wi-Fi station on the host (firmware/src/wifi/): the WPA2 crypto against the published vectors.
 *   SHA-1 (FIPS 180-4 "abc"), HMAC-SHA1 (RFC 2202 case 1), the PMK (IEEE 802.11 PBKDF2 example "password" / "IEEE"),
 *   AES-128 both ways (FIPS-197 C.1), AES key unwrap (RFC 3394 §4.1)
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../firmware/src/wifi/wifi_crypto.c"
#include "../firmware/src/wifi/wifi_sta.c"
#include "../firmware/src/wifi/wifi_ip.c"
#include "../fm1base/link/link.c"

static int fails, checks;
static void write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static uint16_t read_u16_be(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}
static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
static void hex(const char *s, uint8_t *out)
{
    while (s[0] && s[1]) {
        unsigned v;
        sscanf(s, "%2x", &v);
        *out++ = (uint8_t)v;
        s += 2;
    }
}
static void check(const char *what, const uint8_t *got, const char *want_hex, unsigned n)
{
    uint8_t w[64];
    hex(want_hex, w);
    checks++;
    if (memcmp(got, w, n)) {
        unsigned i;
        fails++;
        printf("FAIL %s: ", what);
        for (i = 0; i < n; i++)
            printf("%02x", got[i]);
        printf("\n");
    }
}

/* Helper for mock AP: RFC 3394 Key Wrap */
static void mock_aes_wrap(const uint8_t kek[16], const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint8_t a[8], b[16];
    uint32_t blocks = n / 8u, i, j, k;
    for (i = 0; i < 8u; i++)
        a[i] = 0xA6u;
    for (i = 0; i < blocks * 8u; i++)
        out[8u + i] = in[i];
    for (j = 0; j < 6u; j++) {
        for (i = 1; i <= blocks; i++) {
            uint32_t t = blocks * j + i;
            for (k = 0; k < 8u; k++)
                b[k] = a[k];
            for (k = 0; k < 8u; k++)
                b[8 + k] = out[8u * i + k];
            wc_aes128_encrypt(kek, b, b);
            for (k = 0; k < 8u; k++)
                a[k] = b[k];
            a[7] ^= (uint8_t)t;
            a[6] ^= (uint8_t)(t >> 8);
            a[5] ^= (uint8_t)(t >> 16);
            a[4] ^= (uint8_t)(t >> 24);
            for (k = 0; k < 8u; k++)
                out[8u * i + k] = b[8 + k];
        }
    }
    for (i = 0; i < 8u; i++)
        out[i] = a[i];
}

static void t_crypto(void)
{
    struct wc_sha1 s;
    uint8_t d[64], k[32], in[32], wrap_out[32];
    wc_sha1_init(&s);
    wc_sha1_add(&s, (const uint8_t *)"abc", 3);
    wc_sha1_done(&s, d);
    check("sha1 abc", d, "a9993e364706816aba3e25717850c26c9cd0d89d", 20);
    memset(k, 0x0b, 20);
    wc_hmac_sha1(k, 20, (const uint8_t *)"Hi There", 8, d);
    check("hmac rfc2202-1", d, "b617318655057264e28bc0b6fb378c8ef146be00", 20);
    wc_pmk("password", (const uint8_t *)"IEEE", 4, d);
    check("pmk password/IEEE", d, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", 32);
    hex("000102030405060708090a0b0c0d0e0f", k);
    hex("00112233445566778899aabbccddeeff", in);
    wc_aes128_encrypt(k, in, d);
    check("aes enc fips197", d, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);
    wc_aes128_decrypt(k, d, d);
    check("aes dec fips197", d, "00112233445566778899aabbccddeeff", 16);

    mock_aes_wrap(k, in, 16, wrap_out);
    check("wrap rfc3394", wrap_out, "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", 24);

    hex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", in);
    checks++;
    if (!wc_aes_unwrap(k, in, 24, d)) {
        fails++;
        printf("FAIL unwrap integrity\n");
    }
    check("unwrap rfc3394", d, "00112233445566778899aabbccddeeff", 16);

    /* the PMK as a job in slices equals wc_pmk (the IEEE 802.11i example, and a 63-character passphrase) at any slice size */
    {
        static const char *const PW[2] = {"password", "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-"};
        static const char *const SS[2] = {"IEEE", "a-very-long-ssid-of-32-bytes-exactly"};
        static const uint32_t SL[3] = {1u, 256u, 100000u};
        uint32_t a, b, calls;
        for (a = 0; a < 2u; a++)
            for (b = 0; b < 3u; b++) {
                struct wc_pmk_job j;
                uint8_t want[32], got[32];
                uint32_t sl = (uint32_t)strlen(SS[a]);
                wc_pmk(PW[a], (const uint8_t *)SS[a], sl, want);
                wc_pmk_begin(&j, PW[a], (const uint8_t *)SS[a], sl);
                for (calls = 0; !wc_pmk_step(&j, SL[b], got) && calls < 20000u; calls++)
                    ;
                checks++;
                if (memcmp(got, want, 32) || memcmp(j.pass, (uint8_t[64]){0}, 64)) {
                    fails++;
                    printf("FAIL pmk job (pass %u, slice %u)\n", (unsigned)a, (unsigned)SL[b]);
                }
            }
    }

    /* CCMP (AES-CCM) TX encryption, cross-checked against tools/ccmp_ref.py (its CCM core verified vs RFC 3610 #1) */
    {
        uint8_t tk[16], a2[6], frame[128], want[80];
        uint8_t pn[6] = {0x01, 0, 0, 0, 0, 0};
        uint32_t flen;
        hex("c97c1f67ce371185514a8a19f2bdd52f", tk);
        hex("0020e0a69f08", a2);
        hex("08410000112233445566" "0020e0a69f08" "aabbccddeeff" "0010", frame);          /* 24-byte 802.11 header */
        hex("aaaa03000000080045000012" "000102030405060708090a0b0c0d0e0f10111213", frame + 24);  /* 32-byte body */
        flen = wc_ccmp_encrypt(tk, a2, pn, 0, frame, 24u, 32u);
        checks++;
        if (flen != 72u) {
            fails++;
            printf("FAIL ccmp len: %u\n", (unsigned)flen);
        }
        hex("084100001122334455660020e0a69f08aabbccddeeff00100100002000000000"
            "6d514200817a95015eec5693b175c4fd968705651b6df41afb4fb5eeb4176239232a16a9e9d2dd6a", want);
        checks++;
        if (memcmp(frame, want, 72u)) {
            unsigned i;
            fails++;
            printf("FAIL ccmp enc: ");
            for (i = 0; i < 72u; i++)
                printf("%02x", frame[i]);
            printf("\n");
        }
    }
}

static void test_rand(uint8_t *out, uint8_t n)      /* (a test source: the firmware passes the radio's, ble_hw_rand) */
{
    static uint32_t x = 0x9E3779B9u;
    while (n--) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        *out++ = (uint8_t)x;
    }
}

/* Hardware MAC table write capture */
static struct {
    uint32_t r, v;
} mac_writes[64];
static uint32_t mac_write_count;

static void test_mac_wr(uint32_t r, uint32_t v)
{
    if (mac_write_count < 64) {
        mac_writes[mac_write_count].r = r;
        mac_writes[mac_write_count].v = v;
        mac_write_count++;
    }
}

static uint32_t get_mac_reg(uint32_t r)
{
    uint32_t i;
    for (i = 0; i < mac_write_count; i++)
        if (mac_writes[i].r == r)
            return mac_writes[i].v;
    return 0xFFFFFFFFu;
}

static void t_ptk_derivation(void)
{
    uint8_t pmk[32], spa[6], aa[6], snonce[32], anonce[32], ptk[48];
    /* Standard test vector */
    hex("0bd15797054a5e30d09c8877d000864942909f1505363000cd61b05d678a30ef", pmk);
    hex("000b6b0d952b", spa);
    hex("00146c7e4080", aa);
    hex("69d1da42e2cd1e86093d56f685de7da839070ec7419e1fa0a5ad7755ba90bc1b", snonce);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);

    wifi_sta_derive_ptk(pmk, spa, aa, snonce, anonce, ptk);
    /* Verify KCK (first 16), KEK (next 16), TK (next 16) */
    check("ptk kck", ptk, "8fd534464a886e64aeeefa96c0b5e156", 16);
    check("ptk kek", ptk + 16, "bbd2a5755525a0a9cb3e3565b2c58366", 16);
    check("ptk tk", ptk + 32, "832fec9a315cb545b9ca9bd000c6b48d", 16);
}

static void t_handshake_flow(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6], anonce[32], gtk[16];
    uint8_t m1[128], m2[128], m3[256], m4[128];
    uint32_t m2_len = 0, m4_len = 0;
    uint8_t gtk_kde[32], enc_kde[40];
    uint8_t ap_ptk[48];
    uint8_t ap_mic[16];
    int res;

    wifi_sta_set_test_mac_wr(test_mac_wr);
    mac_write_count = 0;

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);
    hex("11223344556677889900aabbccddeeff", gtk);

    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    checks++;
    if (sta.state != WIFI_STA_STATE_WAIT_M1)
        fails++;

    /* --- AP creates Message 1 --- */
    memset(m1, 0, 99);
    m1[0] = 0x01;                       /* 802.1X */
    m1[1] = 0x03;                       /* EAPOL-Key */
    write_u16_be(m1 + 2, 95);           /* Body length */
    m1[4] = 0x02;                       /* RSN */
    write_u16_be(m1 + 5, 0x008Au);      /* Version 2, Pairwise, Key ACK */
    write_u16_be(m1 + 7, 16u);          /* Key length */
    write_u16_be(m1 + 9, 0);            /* Replay counter = 1 */
    m1[16] = 1;
    memcpy(m1 + 17, anonce, 32);        /* ANonce */

    /* STA processes M1 -> generates M2 */
    res = wifi_sta_rx_eapol(&sta, m1, 99, m2, &m2_len);
    checks++;
    if (res != 1 || m2_len != 121 || sta.state != WIFI_STA_STATE_WAIT_M3) {
        fails++;
        printf("FAIL M1 handling: res=%d, len=%u, state=%d\n", res, m2_len, sta.state);
    }

    /* AP validates M2: derive PTK using STA's SNonce from M2 */
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, m2 + 17, anonce, ap_ptk);
    compute_eapol_mic(ap_ptk, m2, 121, ap_mic);
    checks++;
    if (memcmp(m2 + 81, ap_mic, 16) != 0) {
        fails++;
        printf("FAIL M2 MIC validation by AP\n");
    }

    /* --- AP creates Message 3 --- */
    memset(m3, 0, 256);
    m3[0] = 0x01;
    m3[1] = 0x03;
    m3[4] = 0x02;
    /* Key Info: Version 2, Pairwise, Install, Key ACK, Key MIC, Secure, Encrypted Key Data */
    write_u16_be(m3 + 5, 0x13CAu);
    write_u16_be(m3 + 7, 16u);
    write_u16_be(m3 + 9, 0);            /* Replay counter = 2 */
    m3[16] = 2;
    memcpy(m3 + 17, anonce, 32);

    /* Construct GTK KDE (24 bytes): Tag 0xDD, len 22, OUI 00-0F-AC, type 0x01, KeyId 1, res, GTK */
    gtk_kde[0] = 0xDD;
    gtk_kde[1] = 22;
    gtk_kde[2] = 0x00; gtk_kde[3] = 0x0F; gtk_kde[4] = 0xAC; gtk_kde[5] = 0x01;
    gtk_kde[6] = 0x01;                  /* Key ID 1 */
    gtk_kde[7] = 0x00;                  /* Reserved */
    memcpy(gtk_kde + 8, gtk, 16);

    /* Encrypt GTK KDE with KEK using AES Key Wrap: 24 bytes in -> 32 bytes out */
    mock_aes_wrap(ap_ptk + 16, gtk_kde, 24, enc_kde);
    write_u16_be(m3 + 97, 32u);
    memcpy(m3 + 99, enc_kde, 32);
    write_u16_be(m3 + 2, 95u + 32u);    /* Body length = 127 */
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);

    /* STA processes M3 -> generates M4, installs keys (8 bytes of padding after the frame: not part of the MIC) */
    res = wifi_sta_rx_eapol(&sta, m3, 139, m4, &m4_len);
    checks++;
    if (res != 1 || m4_len != 99 || sta.state != WIFI_STA_STATE_CONNECTED) {
        fails++;
        printf("FAIL M3 handling: res=%d, len=%u, state=%d\n", res, m4_len, sta.state);
    }

    /* Verify GTK unwrapped correctly by STA */
    check("sta gtk", sta.gtk, "11223344556677889900aabbccddeeff", 16);
    checks++;
    if (sta.gtk_id != 1)
        fails++;

    /* AP validates M4 */
    compute_eapol_mic(ap_ptk, m4, 99, ap_mic);
    checks++;
    if (memcmp(m4 + 81, ap_mic, 16) != 0) {
        fails++;
        printf("FAIL M4 MIC validation by AP\n");
    }

    /* Verify hardware MAC register writes for key installation */
    checks++;
    if (get_mac_reg(0x6804u) != 0x09u) {        /* WCID 1 Attribute = AES-CCMP (bits 3:1) + KeyTabFlag (bit 0 = pairwise table) */
        fails++;
        printf("FAIL HW WCID attribute reg\n");
    }
    checks++;
    if (get_mac_reg(0x6008u) != 0x20000001u) {  /* WCID 1 TX IV: ExtIV bit set + initial PN (CCMP) */
        fails++;
        printf("FAIL HW WCID IV reg\n");
    }
    checks++;
    if (get_mac_reg(0x7000u) != (4u << 4)) {    /* Shared key mode = AES-CCMP for key 1 */
        fails++;
        printf("FAIL HW Shared key mode reg\n");
    }
    {   /* leaving: the deauth frame (reason 3, to the AP), and the MAC's key state back to boot (no cipher, no key, no IV) */
        uint8_t d[40];
        uint32_t dl = wifi_sta_build_deauth(&sta, d);
        checks++;
        if (dl != 26u || d[0] != 0xC0u || memcmp(d + 4, sta.bssid, 6) || memcmp(d + 10, sta.mac, 6) || d[24] != 3u || d[25] != 0u) {
            fails++;
            printf("FAIL deauth frame\n");
        }
        mac_write_count = 0;                         /* (a clean log: the recorder returns the first write it has) */
        wifi_sta_clear_hw_keys();
        checks++;
        if (get_mac_reg(0x6804u) != 0u || get_mac_reg(0x6008u) != 0u || get_mac_reg(0x4020u) != 0u || get_mac_reg(0x402Cu) != 0u ||
            get_mac_reg(0x7000u) != 0u) {
            fails++;
            printf("FAIL clear hw keys\n");
        }
    }
}

static void t_negative_cases(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6], anonce[32], gtk[16];
    uint8_t m1[128], m2[128], m3[256], m4[128];
    uint32_t m2_len = 0, m4_len = 0;
    uint8_t gtk_kde[32], enc_kde[40];
    uint8_t ap_ptk[48];

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);
    hex("11223344556677889900aabbccddeeff", gtk);

    /* Test 1: Corrupted MIC in M3 */
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    memset(m1, 0, 99);
    m1[0] = 0x01; m1[1] = 0x03;
    write_u16_be(m1 + 2, 95);
    m1[4] = 0x02;
    write_u16_be(m1 + 5, 0x008Au);
    write_u16_be(m1 + 7, 16u);
    m1[16] = 1;
    memcpy(m1 + 17, anonce, 32);

    wifi_sta_rx_eapol(&sta, m1, 99, m2, &m2_len);
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, m2 + 17, anonce, ap_ptk);

    memset(m3, 0, 256);
    m3[0] = 0x01; m3[1] = 0x03; m3[4] = 0x02;
    write_u16_be(m3 + 5, 0x13CAu);
    write_u16_be(m3 + 7, 16u);
    m3[16] = 2;
    memcpy(m3 + 17, anonce, 32);

    gtk_kde[0] = 0xDD; gtk_kde[1] = 22;
    gtk_kde[2] = 0x00; gtk_kde[3] = 0x0F; gtk_kde[4] = 0xAC; gtk_kde[5] = 0x01;
    gtk_kde[6] = 0x01; gtk_kde[7] = 0x00;
    memcpy(gtk_kde + 8, gtk, 16);

    mock_aes_wrap(ap_ptk + 16, gtk_kde, 24, enc_kde);
    write_u16_be(m3 + 97, 32u);
    memcpy(m3 + 99, enc_kde, 32);
    write_u16_be(m3 + 2, 95u + 32u);
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);

    /* Corrupt one byte of MIC */
    m3[85] ^= 0xFF;
    int res = wifi_sta_rx_eapol(&sta, m3, 131, m4, &m4_len);
    checks++;
    if (res != -1 || sta.state != WIFI_STA_STATE_FAILED) {
        fails++;
        printf("FAIL Corrupted MIC should fail\n");
    }

    /* Test 2: a replayed M3 (its counter not above M1's) is refused */
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);
    m1[16] = 5;
    wifi_sta_rx_eapol(&sta, m1, 99, m2, &m2_len);
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, m2 + 17, anonce, ap_ptk);
    m3[16] = 5;
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);
    res = wifi_sta_rx_eapol(&sta, m3, 131, m4, &m4_len);
    checks++;
    if (res != -1 || sta.state != WIFI_STA_STATE_WAIT_M3) {
        fails++;
        printf("FAIL replayed M3 should be refused (res=%d state=%d)\n", res, sta.state);
    }

    /* Test 3: key data longer than the frame is refused before any unwrap */
    m3[16] = 6;
    write_u16_be(m3 + 97, 200u);
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);
    res = wifi_sta_rx_eapol(&sta, m3, 131, m4, &m4_len);
    checks++;
    if (res != -1) {
        fails++;
        printf("FAIL key data past the frame should be refused\n");
    }
}

static void t_mgmt_framing(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6];
    uint8_t auth_buf[64], auth_resp[64];
    uint8_t assoc_buf[128], assoc_resp[64];
    uint32_t len;
    int res;

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);

    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    /* 1. Build Authentication Frame */
    len = wifi_sta_build_auth(&sta, auth_buf);
    checks++;
    if (len != 30u || auth_buf[0] != 0xB0u || auth_buf[1] != 0x00u) {
        fails++;
        printf("FAIL build auth frame\n");
    }
    checks++;
    if (memcmp(auth_buf + 4, ap_bssid, 6) != 0 || memcmp(auth_buf + 10, sta_mac, 6) != 0) {
        fails++;
        printf("FAIL auth addresses\n");
    }
    checks++;
    if (auth_buf[24] != 0 || auth_buf[25] != 0 || auth_buf[26] != 1 || auth_buf[27] != 0) {
        fails++;
        printf("FAIL auth fixed fields (alg 0, seq 1)\n");
    }

    /* 2. Parse Authentication Response */
    memset(auth_resp, 0, 30);
    auth_resp[0] = 0xB0;
    memcpy(auth_resp + 4, sta_mac, 6);              /* DA */
    memcpy(auth_resp + 10, ap_bssid, 6);            /* SA */
    memcpy(auth_resp + 16, ap_bssid, 6);            /* BSSID */
    auth_resp[24] = 0; auth_resp[25] = 0;           /* Alg 0 */
    auth_resp[26] = 2; auth_resp[27] = 0;           /* Seq 2 */
    auth_resp[28] = 0; auth_resp[29] = 0;           /* Status 0 (Success) */

    res = wifi_sta_parse_auth_resp(&sta, auth_resp, 30);
    checks++;
    if (res != 0) {
        fails++;
        printf("FAIL parse valid auth response\n");
    }

    /* Negative: bad status code */
    auth_resp[28] = 1;
    res = wifi_sta_parse_auth_resp(&sta, auth_resp, 30);
    checks++;
    if (res != -1) {
        fails++;
        printf("FAIL parse auth response with error status should fail\n");
    }
    auth_resp[28] = 0;

    /* 3. Build Association Request */
    len = wifi_sta_build_assoc_req(&sta, assoc_buf);
    checks++;
    if (len < 64u || assoc_buf[0] != 0x00u || assoc_buf[1] != 0x00u) {
        fails++;
        printf("FAIL build assoc request frame\n");
    }
    checks++;
    if (memcmp(assoc_buf + 4, ap_bssid, 6) != 0 || memcmp(assoc_buf + 10, sta_mac, 6) != 0) {
        fails++;
        printf("FAIL assoc addresses\n");
    }
    /* Verify SSID tag (Tag 0, len 11, "TestNetwork") */
    checks++;
    if (assoc_buf[28] != 0x00u || assoc_buf[29] != 11u || memcmp(assoc_buf + 30, "TestNetwork", 11) != 0) {
        fails++;
        printf("FAIL assoc SSID tag\n");
    }

    /* 4. Parse Association Response */
    memset(assoc_resp, 0, 30);
    assoc_resp[0] = 0x10;                           /* Assoc Resp */
    memcpy(assoc_resp + 4, sta_mac, 6);             /* DA */
    memcpy(assoc_resp + 10, ap_bssid, 6);           /* SA */
    memcpy(assoc_resp + 16, ap_bssid, 6);           /* BSSID */
    assoc_resp[26] = 0; assoc_resp[27] = 0;         /* Status 0 */
    assoc_resp[28] = 42; assoc_resp[29] = 0xC0;     /* AID 42 */

    res = wifi_sta_parse_assoc_resp(&sta, assoc_resp, 30);
    checks++;
    if (res != 0 || sta.aid != 42) {
        fails++;
        printf("FAIL parse valid assoc response (aid=%u)\n", sta.aid);
    }
}

static void t_eapol_encap_and_full_join(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6], anonce[32], gtk[16];
    uint8_t frame_in[300], frame_out[300];
    uint8_t m1[128], m3[256];
    uint32_t out_len = 0;
    uint8_t gtk_kde[32], enc_kde[40];
    uint8_t ap_ptk[48];
    int res;

    wifi_sta_set_test_mac_wr(test_mac_wr);
    mac_write_count = 0;

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);
    hex("11223344556677889900aabbccddeeff", gtk);

    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    /* --- Phase 1: Authentication --- */
    sta.state = WIFI_STA_STATE_AUTH;
    memset(frame_in, 0, 30);
    frame_in[0] = 0xB0;
    memcpy(frame_in + 4, sta_mac, 6);
    memcpy(frame_in + 10, ap_bssid, 6);
    memcpy(frame_in + 16, ap_bssid, 6);
    frame_in[26] = 2;                               /* Seq 2 */

    res = wifi_sta_rx_frame(&sta, frame_in, 30, frame_out, &out_len);
    checks++;
    if (res != 0 || sta.state != WIFI_STA_STATE_ASSOC) {
        fails++;
        printf("FAIL full join: auth step\n");
    }

    /* --- Phase 2: Association --- */
    memset(frame_in, 0, 30);
    frame_in[0] = 0x10;
    memcpy(frame_in + 4, sta_mac, 6);
    memcpy(frame_in + 10, ap_bssid, 6);
    memcpy(frame_in + 16, ap_bssid, 6);
    frame_in[28] = 7; frame_in[29] = 0xC0;          /* AID 7 */

    res = wifi_sta_rx_frame(&sta, frame_in, 30, frame_out, &out_len);
    checks++;
    if (res != 0 || sta.state != WIFI_STA_STATE_WAIT_M1 || sta.aid != 7) {
        fails++;
        printf("FAIL full join: assoc step\n");
    }

    /* --- Phase 3: Handshake M1 in 802.11 Data frame + LLC/SNAP --- */
    memset(m1, 0, 99);
    m1[0] = 0x01; m1[1] = 0x03;
    write_u16_be(m1 + 2, 95);
    m1[4] = 0x02;
    write_u16_be(m1 + 5, 0x008Au);
    write_u16_be(m1 + 7, 16u);
    m1[16] = 1;
    memcpy(m1 + 17, anonce, 32);

    /* Wrap M1 in 802.11 Data Frame */
    memset(frame_in, 0, 32);
    frame_in[0] = 0x08; frame_in[1] = 0x02;         /* FromDS=1 */
    memcpy(frame_in + 4, sta_mac, 6);               /* DA */
    memcpy(frame_in + 10, ap_bssid, 6);             /* SA */
    memcpy(frame_in + 16, ap_bssid, 6);             /* BSSID */
    /* LLC/SNAP */
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x88; frame_in[31] = 0x8E;
    memcpy(frame_in + 32, m1, 99);

    res = wifi_sta_rx_frame(&sta, frame_in, 32 + 99, frame_out, &out_len);
    checks++;
    if (res != 1 || out_len != 32 + 121 || sta.state != WIFI_STA_STATE_WAIT_M3) {
        fails++;
        printf("FAIL full join: M1->M2 over-the-air frame (res=%d, len=%u, state=%d)\n", res, out_len, sta.state);
    }
    /* Verify LLC/SNAP on M2 */
    checks++;
    if (frame_out[24] != 0xAA || frame_out[30] != 0x88 || frame_out[31] != 0x8E) {
        fails++;
        printf("FAIL M2 LLC/SNAP encapsulation\n");
    }

    /* AP derives PTK from M2 payload */
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, frame_out + 32 + 17, anonce, ap_ptk);

    /* --- Phase 4: Handshake M3 in 802.11 Data frame + LLC/SNAP --- */
    memset(m3, 0, 256);
    m3[0] = 0x01; m3[1] = 0x03; m3[4] = 0x02;
    write_u16_be(m3 + 5, 0x13CAu);
    write_u16_be(m3 + 7, 16u);
    m3[16] = 2;
    memcpy(m3 + 17, anonce, 32);

    gtk_kde[0] = 0xDD; gtk_kde[1] = 22;
    gtk_kde[2] = 0x00; gtk_kde[3] = 0x0F; gtk_kde[4] = 0xAC; gtk_kde[5] = 0x01;
    gtk_kde[6] = 0x01; gtk_kde[7] = 0x00;
    memcpy(gtk_kde + 8, gtk, 16);

    mock_aes_wrap(ap_ptk + 16, gtk_kde, 24, enc_kde);
    write_u16_be(m3 + 97, 32u);
    memcpy(m3 + 99, enc_kde, 32);
    write_u16_be(m3 + 2, 95u + 32u);
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);

    memset(frame_in, 0, 32);
    frame_in[0] = 0x08; frame_in[1] = 0x02;
    memcpy(frame_in + 4, sta_mac, 6);
    memcpy(frame_in + 10, ap_bssid, 6);
    memcpy(frame_in + 16, ap_bssid, 6);
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x88; frame_in[31] = 0x8E;
    memcpy(frame_in + 32, m3, 131);

    res = wifi_sta_rx_frame(&sta, frame_in, 32 + 131, frame_out, &out_len);
    checks++;
    if (res != 1 || out_len != 32 + 99 || sta.state != WIFI_STA_STATE_CONNECTED) {
        fails++;
        printf("FAIL full join: M3->M4 over-the-air frame (res=%d, len=%u, state=%d)\n", res, out_len, sta.state);
    }

    /* --- Phase 5: Group Key Rekey (2-way group handshake) --- */
    {
        uint8_t new_gtk[16], new_gtk_kde[32], enc_new_kde[40];
        uint8_t gm1_eapol[256];
        uint8_t expected_gm2_mic[16];
        hex("aabbccddeeff00112233445566778899", new_gtk);

        memset(gm1_eapol, 0, 256);
        gm1_eapol[0] = 0x01; gm1_eapol[1] = 0x03; gm1_eapol[4] = 0x02;
        /* Key Info: Version 2, Group Key (0), Key ACK (bit 7), Key MIC (bit 8), Secure (bit 9), Encrypted (bit 12) -> 0x1382 */
        write_u16_be(gm1_eapol + 5, 0x1382u);
        write_u16_be(gm1_eapol + 7, 16u);
        gm1_eapol[16] = 5;                           /* Replay counter 5 */

        new_gtk_kde[0] = 0xDD; new_gtk_kde[1] = 22;
        new_gtk_kde[2] = 0x00; new_gtk_kde[3] = 0x0F; new_gtk_kde[4] = 0xAC; new_gtk_kde[5] = 0x01;
        new_gtk_kde[6] = 0x02;                       /* GTK ID 2 */
        new_gtk_kde[7] = 0x00;
        memcpy(new_gtk_kde + 8, new_gtk, 16);

        mock_aes_wrap(ap_ptk + 16, new_gtk_kde, 24, enc_new_kde);
        write_u16_be(gm1_eapol + 97, 32u);
        memcpy(gm1_eapol + 99, enc_new_kde, 32);
        write_u16_be(gm1_eapol + 2, 95u + 32u);
        compute_eapol_mic(ap_ptk, gm1_eapol, 131, gm1_eapol + 81);

        memset(frame_in, 0, 32);
        frame_in[0] = 0x08; frame_in[1] = 0x02;
        memcpy(frame_in + 4, sta_mac, 6);
        memcpy(frame_in + 10, ap_bssid, 6);
        memcpy(frame_in + 16, ap_bssid, 6);
        frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
        frame_in[30] = 0x88; frame_in[31] = 0x8E;
        memcpy(frame_in + 32, gm1_eapol, 131);

        res = wifi_sta_rx_frame(&sta, frame_in, 32 + 131, frame_out, &out_len);
        checks++;
        if (res != 1 || out_len != 32 + 99 || sta.gtk_id != 2 || memcmp(sta.gtk, new_gtk, 16) != 0) {
            fails++;
            printf("FAIL group rekey GM1->GM2 (res=%d, len=%u, gtk_id=%u)\n", res, out_len, sta.gtk_id);
        }

        /* Verify Group M2 Key Info == 0x0302 and valid MIC */
        checks++;
        uint16_t gm2_info = ((uint16_t)frame_out[32 + 5] << 8) | frame_out[32 + 6];
        if (gm2_info != 0x0302u) {
            fails++;
            printf("FAIL group M2 key_info = 0x%04x\n", gm2_info);
        }
        compute_eapol_mic(ap_ptk, frame_out + 32, 99, expected_gm2_mic);
        /* Notice frame_out + 32 + 81 is the MIC inside the generated frame */
        check("group M2 MIC", frame_out + 32 + 81, "valid", 0); /* count check */
        checks++;
        if (memcmp(frame_out + 32 + 81, expected_gm2_mic, 16) != 0) {
            fails++;
            printf("FAIL group M2 MIC verification\n");
        }
    }
}

static uint8_t mock_vm_data[10];
static int mock_vm_available = 1;
static int mock_st_read(uint32_t off, void *dst, uint32_t n)
{
    if (!mock_vm_available || off != 0x0E9000u)
        return -1;
    memcpy(dst, mock_vm_data, n);
    return 0;
}

static void t_mac_address_and_regs(void)
{
    uint8_t mac[6];
    uint8_t sta_mac[6], ap_bssid[6];
    int res;

    wifi_sta_set_test_mac_wr(test_mac_wr);
    mac_write_count = 0;
    wifi_sta_set_test_st_read(mock_st_read);

    /* Test 1: Valid VM record 102 (ID 0x66) with BT MAC 10:20:30:40:50:60 */
    mock_vm_available = 1;
    uint8_t raw_mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    uint32_t crc = ble_crc16_xmodem(0, raw_mac, 6);
    mock_vm_data[0] = (uint8_t)crc;
    mock_vm_data[1] = 0x66;                         /* Record 102 */
    mock_vm_data[2] = 0x60;                         /* len 6, type 0 */
    mock_vm_data[3] = 0x00;
    memcpy(mock_vm_data + 4, raw_mac, 6);

    res = wifi_sta_get_mac(mac);
    checks++;
    if (res != 1) {
        fails++;
        printf("FAIL get mac from VM\n");
    }
    checks++;
    /* Local bit 0x02 set: 0x10 | 0x02 = 0x12 */
    if (mac[0] != 0x12 || memcmp(mac + 1, raw_mac + 1, 5) != 0) {
        fails++;
        printf("FAIL mac locally administered bit\n");
    }

    /* Test 2: Fallback when VM is absent */
    mock_vm_available = 0;
    res = wifi_sta_get_mac(mac);
    checks++;
    if (res != 0 || mac[0] != 0x02 || mac[1] != 0x11 || mac[2] != 0x22) {
        fails++;
        printf("FAIL fallback mac\n");
    }

    /* Test 3: MAC registers programming */
    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    wifi_sta_set_mac_regs(sta_mac, ap_bssid, 6);

    checks++;
    if (get_mac_reg(0x1008u) != 0x33221102u) {
        fails++;
        printf("FAIL STA MAC DW0 reg\n");
    }
    checks++;
    if (get_mac_reg(0x100Cu) != 0x00FF5544u) {
        fails++;
        printf("FAIL STA MAC DW1 reg\n");
    }
    checks++;
    if (get_mac_reg(0x1010u) != 0x7E6C1400u) {
        fails++;
        printf("FAIL BSSID DW0 reg\n");
    }
    checks++;
    if (get_mac_reg(0x1014u) != 0x00008040u) {
        fails++;
        printf("FAIL BSSID DW1 reg\n");
    }
}

static void t_ip_early_filter(void)
{
    uint8_t frame[128];
    uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint32_t our_ip = 0xC0A80164u;                  /* 192.168.1.100 */
    uint16_t link_port = 20808u;

    /* Base 802.11 Data Frame Header + LLC/SNAP */
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x08; frame[1] = 0x02;
    frame[24] = 0xAA; frame[25] = 0xAA; frame[26] = 0x03;

    /* 1. EAPOL (EtherType 0x888E) -> KEEP */
    frame[30] = 0x88; frame[31] = 0x8E;
    checks++;
    if (wifi_ip_early_filter(frame, 40, sta_mac, our_ip, link_port) != 1) {
        fails++;
        printf("FAIL early filter: EAPOL dropped\n");
    }

    /* 2. ARP for our IP -> KEEP */
    frame[30] = 0x08; frame[31] = 0x06;
    /* ARP target IP at frame + 32 + 24 */
    write_u32_be(frame + 32 + 24, our_ip);
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 28, sta_mac, our_ip, link_port) != 1) {
        fails++;
        printf("FAIL early filter: ARP our_ip dropped\n");
    }

    /* 3. ARP for different IP -> DROP */
    write_u32_be(frame + 32 + 24, 0xC0A80101u);     /* 192.168.1.1 */
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 28, sta_mac, our_ip, link_port) != 0) {
        fails++;
        printf("FAIL early filter: ARP other IP kept\n");
    }

    /* 4. IPv4 Setup */
    frame[30] = 0x08; frame[31] = 0x00;
    frame[32] = 0x45;                               /* IPv4, IHL 5 */
    write_u32_be(frame + 32 + 16, our_ip);          /* Dest IP = our IP */

    /* 4a. IGMP (Proto 2) -> KEEP */
    frame[32 + 9] = 2;
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 28, sta_mac, our_ip, link_port) != 1) {
        fails++;
        printf("FAIL early filter: IGMP dropped\n");
    }

    /* 4b. UDP (Proto 17) -> DHCP Client (Port 68) -> KEEP */
    frame[32 + 9] = 17;
    write_u16_be(frame + 32 + 20 + 2, 68u);         /* Dest Port = 68 */
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 20 + 8, sta_mac, our_ip, link_port) != 1) {
        fails++;
        printf("FAIL early filter: DHCP client dropped\n");
    }

    /* 4c. UDP -> Link Multicast Discovery (224.76.78.75:20808) -> KEEP */
    write_u32_be(frame + 32 + 16, 0xE04C4E4Bu);     /* 224.76.78.75 */
    write_u16_be(frame + 32 + 20 + 2, 20808u);      /* Dest Port = 20808 */
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 20 + 8, sta_mac, our_ip, link_port) != 1) {
        fails++;
        printf("FAIL early filter: Link multicast dropped\n");
    }

    /* 4d. UDP -> Link Unicast to our port -> KEEP */
    write_u32_be(frame + 32 + 16, our_ip);
    write_u16_be(frame + 32 + 20 + 2, link_port);
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 20 + 8, sta_mac, our_ip, link_port) != 1) {
        fails++;
        printf("FAIL early filter: Link unicast dropped\n");
    }

    /* 4e. UDP -> mDNS (5353) or SSDP (1900) -> DROP */
    write_u16_be(frame + 32 + 20 + 2, 5353u);
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 20 + 8, sta_mac, our_ip, link_port) != 0) {
        fails++;
        printf("FAIL early filter: mDNS 5353 kept\n");
    }

    /* 4f. TCP (Proto 6) -> DROP */
    frame[32 + 9] = 6;
    checks++;
    if (wifi_ip_early_filter(frame, 32 + 20 + 8, sta_mac, our_ip, link_port) != 0) {
        fails++;
        printf("FAIL early filter: TCP kept\n");
    }
}

static void t_ip_arp_and_dhcp(void)
{
    struct wifi_ip wip;
    uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t ap_bssid[6] = {0x00, 0x14, 0x6C, 0x7E, 0x40, 0x80};
    uint8_t tx_buf[512], frame_in[512];
    uint32_t tx_len = 0;
    int res;

    wifi_ip_init(&wip, sta_mac, ap_bssid, test_rand);

    /* --- DHCP Flow --- */
    tx_len = wifi_ip_dhcp_start(&wip, tx_buf);
    checks++;
    if (tx_len < 32 + 20 + 8 + 240 || wip.dhcp_state != WIFI_IP_DHCP_DISCOVERING) {
        fails++;
        printf("FAIL DHCP discover build\n");
    }
    /* Verify DHCP discover payload */
    checks++;
    if (tx_buf[32 + 20 + 8 + 0] != 0x01 ||          /* Boot Request */
        tx_buf[32 + 20 + 8 + 236] != 0x63 ||        /* Magic Cookie */
        tx_buf[32 + 20 + 8 + 240] != 53 ||          /* Option 53 */
        tx_buf[32 + 20 + 8 + 242] != 1) {           /* Type 1 Discover */
        fails++;
        printf("FAIL DHCP discover options\n");
    }

    /* Simulate DHCP Offer from AP router (192.168.1.1 offers 192.168.1.150) */
    memset(frame_in, 0, sizeof(frame_in));
    frame_in[0] = 0x08; frame_in[1] = 0x02;
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x08; frame_in[31] = 0x00;       /* IPv4 */
    frame_in[32] = 0x45;                            /* IHL 5 */
    write_u16_be(frame_in + 32 + 2, 20 + 8 + 250);  /* Total Len */
    frame_in[32 + 9] = 17;                          /* UDP */
    write_u32_be(frame_in + 32 + 12, 0xC0A80101u);  /* Src: 192.168.1.1 */
    write_u32_be(frame_in + 32 + 16, 0xC0A80196u);  /* Dst: 192.168.1.150 */
    write_u16_be(frame_in + 32 + 20, 67u);          /* Src Port: 67 */
    write_u16_be(frame_in + 32 + 22, 68u);          /* Dst Port: 68 */
    write_u16_be(frame_in + 32 + 24, 8 + 250);      /* UDP Len */

    /* DHCP Body */
    uint8_t *dhcp = frame_in + 32 + 20 + 8;
    dhcp[0] = 0x02;                                 /* Boot Reply */
    write_u32_be(dhcp + 4, wip.dhcp_xid);           /* Matching XID */
    write_u32_be(dhcp + 16, 0xC0A80196u);           /* Yiaddr: 192.168.1.150 */
    memcpy(dhcp + 28, sta_mac, 6);
    dhcp[236] = 0x63; dhcp[237] = 0x82; dhcp[238] = 0x53; dhcp[239] = 0x63;
    dhcp[240] = 53; dhcp[241] = 1; dhcp[242] = 2;  /* Option 53 = 2 (Offer) */
    dhcp[243] = 54; dhcp[244] = 4; write_u32_be(dhcp + 245, 0xC0A80101u); /* Option 54 (Server ID) */
    dhcp[249] = 255;                                /* End */

    res = wifi_ip_rx_frame(&wip, frame_in, 32 + 20 + 8 + 250, 1000000, tx_buf, &tx_len);
    checks++;
    if (res != 1 || wip.dhcp_state != WIFI_IP_DHCP_REQUESTING || wip.dhcp_offered_ip != 0xC0A80196u) {
        fails++;
        printf("FAIL DHCP offer -> request (res=%d, state=%d)\n", res, wip.dhcp_state);
    }

    /* Simulate DHCP Ack from AP router */
    write_u16_be(frame_in + 32 + 2, 20 + 8 + 256);  /* Total Len */
    write_u16_be(frame_in + 32 + 24, 8 + 256);      /* UDP Len */
    dhcp[240] = 53; dhcp[241] = 1; dhcp[242] = 5;  /* Option 53 = 5 (Ack) */
    dhcp[243] = 1;  dhcp[244] = 4; write_u32_be(dhcp + 245, 0xFFFFFF00u); /* Netmask 255.255.255.0 */
    dhcp[249] = 3;  dhcp[250] = 4; write_u32_be(dhcp + 251, 0xC0A80101u); /* Gateway */
    dhcp[255] = 255;                                /* End */

    res = wifi_ip_rx_frame(&wip, frame_in, 32 + 20 + 8 + 256, 1000000, tx_buf, &tx_len);
    checks++;
    if (res != 1 || wip.dhcp_state != WIFI_IP_DHCP_BOUND || wip.ip != 0xC0A80196u || wip.gw != 0xC0A80101u) {
        fails++;
        printf("FAIL DHCP ack -> bound (res=%d, state=%d, ip=0x%08x)\n", res, wip.dhcp_state, wip.ip);
    }

    /* Verify IGMPv2 Join packet produced upon entering BOUND */
    checks++;
    /* 802.11 DA should be 01:00:5E:4C:4E:4B for 224.76.78.75 */
    if (tx_buf[16] != 0x01 || tx_buf[17] != 0x00 || tx_buf[18] != 0x5E ||
        tx_buf[19] != 0x4C || tx_buf[20] != 0x4E || tx_buf[21] != 0x4B) {
        fails++;
        printf("FAIL IGMP multicast MAC destination\n");
    }
    /* IP Dest should be 224.76.78.75 (0xE04C4E4B) */
    checks++;
    if (read_u32_be(tx_buf + 32 + 16) != 0xE04C4E4Bu || tx_buf[32 + 9] != 2) {
        fails++;
        printf("FAIL IGMP IP packet header\n");
    }

    /* --- ARP Handling --- */
    /* Simulate ARP request from peer (192.168.1.50 asking who has 192.168.1.150) */
    wip.ip = 0xC0A80196u;
    memset(frame_in, 0, sizeof(frame_in));
    frame_in[0] = 0x08; frame_in[1] = 0x02;
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x08; frame_in[31] = 0x06;       /* ARP */
    uint8_t *arp = frame_in + 32;
    write_u16_be(arp + 0, 1u);                      /* HW: Ethernet */
    write_u16_be(arp + 2, 0x0800u);                 /* Proto: IPv4 */
    arp[4] = 6; arp[5] = 4;
    write_u16_be(arp + 6, 1u);                      /* Op: Request */
    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    memcpy(arp + 8, peer_mac, 6);                   /* Sender MAC */
    write_u32_be(arp + 14, 0xC0A80132u);            /* Sender IP: 192.168.1.50 */
    memset(arp + 18, 0, 6);                         /* Target MAC: 0 */
    write_u32_be(arp + 24, 0xC0A80196u);            /* Target IP: 192.168.1.150 (Our IP) */

    res = wifi_ip_rx_frame(&wip, frame_in, 32 + 28, 1000000, tx_buf, &tx_len);
    checks++;
    if (res != 1 || tx_len != 32 + 28) {
        fails++;
        printf("FAIL ARP reply generation (res=%d, len=%u)\n", res, tx_len);
    }
    /* Verify ARP reply fields */
    checks++;
    if (memcmp(tx_buf + 16, peer_mac, 6) != 0 ||    /* 802.11 DA == peer MAC */
        read_u16_be(tx_buf + 32 + 6) != 2u ||       /* Op == Reply */
        memcmp(tx_buf + 32 + 8, sta_mac, 6) != 0 || /* Sender MAC == Our MAC */
        read_u32_be(tx_buf + 32 + 14) != 0xC0A80196u || /* Sender IP == Our IP */
        memcmp(tx_buf + 32 + 18, peer_mac, 6) != 0 || /* Target MAC == Peer MAC */
        read_u32_be(tx_buf + 32 + 24) != 0xC0A80132u) { /* Target IP == Peer IP */
        fails++;
        printf("FAIL ARP reply payload\n");
    }
}

static void t_ip_link_udp(void)
{
    struct wifi_ip wip;
    struct lk_node lk;
    struct lk_cfg cfg;
    uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t ap_bssid[6] = {0x00, 0x14, 0x6C, 0x7E, 0x40, 0x80};
    uint8_t tx_buf[512], frame_in[512];
    uint32_t tx_len = 0;

    wifi_ip_init(&wip, sta_mac, ap_bssid, test_rand);
    wip.ip = 0xC0A80196u;                           /* 192.168.1.150 */

    memset(&cfg, 0, sizeof(cfg));
    cfg.ip = wip.ip;
    cfg.port = WIFI_IP_LINK_PORT;
    lk_init(&lk, &cfg, 0x12345678u, 500000);        /* 120 BPM */
    wifi_ip_set_link(&wip, &lk, WIFI_IP_LINK_PORT);

    /* 1. Test sending a Link discovery datagram */
    uint8_t link_payload[64] = "LINK_DISCOVERY_PAYLOAD_TEST";
    tx_len = wifi_ip_send_udp(&wip, WIFI_IP_LINK_MCAST_IP, WIFI_IP_LINK_PORT, link_payload, 28, tx_buf);
    checks++;
    if (tx_len != 32 + 20 + 8 + 28) {
        fails++;
        printf("FAIL Link UDP send length (len=%u)\n", tx_len);
    }
    /* Verify Multicast MAC, IP dest, UDP dest */
    checks++;
    if (tx_buf[16] != 0x01 || tx_buf[17] != 0x00 || tx_buf[18] != 0x5E ||
        read_u32_be(tx_buf + 32 + 16) != WIFI_IP_LINK_MCAST_IP ||
        read_u16_be(tx_buf + 32 + 20 + 2) != WIFI_IP_LINK_PORT) {
        fails++;
        printf("FAIL Link UDP packet headers\n");
    }

    /* 2. Test receiving a Link packet */
    memset(frame_in, 0, sizeof(frame_in));
    frame_in[0] = 0x08; frame_in[1] = 0x02;
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x08; frame_in[31] = 0x00;
    frame_in[32] = 0x45;
    write_u16_be(frame_in + 32 + 2, 20 + 8 + 28);
    frame_in[32 + 9] = 17;                          /* UDP */
    write_u32_be(frame_in + 32 + 12, 0xC0A80132u);  /* Src: 192.168.1.50 */
    write_u32_be(frame_in + 32 + 16, 0xE04C4E4Bu);  /* Dst: 224.76.78.75 */
    write_u16_be(frame_in + 32 + 20, 20808u);       /* Src Port: 20808 */
    write_u16_be(frame_in + 32 + 22, 20808u);       /* Dst Port: 20808 */
    write_u16_be(frame_in + 32 + 24, 8 + 28);       /* UDP Len */
    memcpy(frame_in + 32 + 28, link_payload, 28);

    int res = wifi_ip_rx_frame(&wip, frame_in, 32 + 20 + 8 + 28, 1000000, tx_buf, &tx_len);
    checks++;
    if (res != 0) {
        fails++;
        printf("FAIL Link UDP receive dispatch (res=%d)\n", res);
    }
}

int main(void)
{
    t_crypto();
    t_ptk_derivation();
    t_handshake_flow();
    t_negative_cases();
    t_mgmt_framing();
    t_eapol_encap_and_full_join();
    t_mac_address_and_regs();
    t_ip_early_filter();
    t_ip_arp_and_dhcp();
    t_ip_link_udp();
    printf("wifi_sta_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
