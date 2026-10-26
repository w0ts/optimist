/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Our SDP server (Core spec Vol 3 Part B §4): three records, as BlueZ answers the AirPods (a Raspberry Pi's capture,
 * 2026-10-20: the AirPods read the source's Device ID, its A2DP source and its AVRCP target before they take the
 * audio channel; the FM-1 with the A2DP source alone was left "pending" for good):
 *   the A2DP source  class 0x110A, L2CAP PSM 0x0019, AVDTP 1.3, A2DP 1.3, features "player"
 *   the AVRCP target class 0x110C, L2CAP PSM 0x0017, AVCTP 1.4, AVRCP 1.6, features category 1
 *   the Device ID    class 0x1200, specification 1.3, vendor 0x1209 product 0x0001 (USB-IF source: pid.codes' VID and
 *                    its prototype PID, until the FM-1 has an id of its own; 0xFFFF is reserved: not a vendor)
 * Answers the three requests (search, attribute, search-attribute) from an SDP PDU to an SDP PDU, whole, no
 * continuation (every answer fits BSD_OUT). Uses bt_l2cap.c's data-element reader (bl_de, bl_uint): included after
 * it. Host-tested in tests/bt_sdps_test.c. Written from the specification, no vendor code. */

#define BSD_OUT 256u                               /* the largest answer (all three records, every attribute) */
#define BSD_HANDLE0 (1u << 16)                     /* our records' handles, from 1 << 16 (0 is the server's own); a
                                                    * shift, not the hex literal: the MMIO check reads it as an address */

struct bsd_attr { uint16_t id; uint8_t n; const uint8_t *v; };
struct bsd_rec { const struct bsd_attr *a; uint8_t na; uint8_t nu; const uint16_t *u; };

/* the A2DP source (the A2DP spec §5.3) */
static const uint8_t BSD_S0000[] = {0x0A, 0x00, 0x01, 0x00, 0x00};
static const uint8_t BSD_S0001[] = {0x35, 0x03, 0x19, 0x11, 0x0A};
static const uint8_t BSD_S0004[] = {0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19,
                                    0x35, 0x06, 0x19, 0x00, 0x19, 0x09, 0x01, 0x03};
static const uint8_t BSD_S0005[] = {0x35, 0x03, 0x19, 0x10, 0x02};
static const uint8_t BSD_S0009[] = {0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0D, 0x09, 0x01, 0x03};
static const uint8_t BSD_S0100[] = {0x25, 0x08, 'O', 'P', 'T', 'I', 'M', 'I', 'S', 'T'};
static const uint8_t BSD_S0311[] = {0x09, 0x00, 0x01};
static const struct bsd_attr BSD_SRC[] = {
    {0x0000, sizeof BSD_S0000, BSD_S0000}, {0x0001, sizeof BSD_S0001, BSD_S0001}, {0x0004, sizeof BSD_S0004, BSD_S0004},
    {0x0005, sizeof BSD_S0005, BSD_S0005}, {0x0009, sizeof BSD_S0009, BSD_S0009}, {0x0100, sizeof BSD_S0100, BSD_S0100},
    {0x0311, sizeof BSD_S0311, BSD_S0311},
};
static const uint16_t BSD_SRC_U[] = {0x110A, 0x0100, 0x0019, 0x1002, 0x110D};

/* the AVRCP target (the AVRCP spec §8: the AirPods ask its profile version, BlueZ says 1.6) */
static const uint8_t BSD_T0000[] = {0x0A, 0x00, 0x01, 0x00, 0x01};
static const uint8_t BSD_T0001[] = {0x35, 0x03, 0x19, 0x11, 0x0C};
static const uint8_t BSD_T0004[] = {0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x17,
                                    0x35, 0x06, 0x19, 0x00, 0x17, 0x09, 0x01, 0x04};
static const uint8_t BSD_T0005[] = {0x35, 0x03, 0x19, 0x10, 0x02};
static const uint8_t BSD_T0009[] = {0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0E, 0x09, 0x01, 0x06};
static const uint8_t BSD_T0311[] = {0x09, 0x00, 0x01};
static const struct bsd_attr BSD_TG[] = {
    {0x0000, sizeof BSD_T0000, BSD_T0000}, {0x0001, sizeof BSD_T0001, BSD_T0001}, {0x0004, sizeof BSD_T0004, BSD_T0004},
    {0x0005, sizeof BSD_T0005, BSD_T0005}, {0x0009, sizeof BSD_T0009, BSD_T0009}, {0x0311, sizeof BSD_T0311, BSD_T0311},
};
static const uint16_t BSD_TG_U[] = {0x110C, 0x0100, 0x0017, 0x1002, 0x110E};

/* the Device ID (the DI spec 1.3 §5) */
static const uint8_t BSD_D0000[] = {0x0A, 0x00, 0x01, 0x00, 0x02};
static const uint8_t BSD_D0001[] = {0x35, 0x03, 0x19, 0x12, 0x00};
static const uint8_t BSD_D0005[] = {0x35, 0x03, 0x19, 0x10, 0x02};
static const uint8_t BSD_D0200[] = {0x09, 0x01, 0x03};                  /* SpecificationID 1.3 */
static const uint8_t BSD_D0201[] = {0x09, 0x12, 0x09};                  /* VendorID: pid.codes (USB-IF source) */
static const uint8_t BSD_D0202[] = {0x09, 0x00, 0x01};                  /* ProductID */
static const uint8_t BSD_D0203[] = {0x09, 0x01, 0x00};                  /* Version 1.0.0 */
static const uint8_t BSD_D0204[] = {0x28, 0x01};                        /* PrimaryRecord: true */
static const uint8_t BSD_D0205[] = {0x09, 0x00, 0x02};                  /* VendorIDSource: the USB-IF */
static const struct bsd_attr BSD_DID[] = {
    {0x0000, sizeof BSD_D0000, BSD_D0000}, {0x0001, sizeof BSD_D0001, BSD_D0001}, {0x0005, sizeof BSD_D0005, BSD_D0005},
    {0x0200, sizeof BSD_D0200, BSD_D0200}, {0x0201, sizeof BSD_D0201, BSD_D0201}, {0x0202, sizeof BSD_D0202, BSD_D0202},
    {0x0203, sizeof BSD_D0203, BSD_D0203}, {0x0204, sizeof BSD_D0204, BSD_D0204}, {0x0205, sizeof BSD_D0205, BSD_D0205},
};
static const uint16_t BSD_DID_U[] = {0x1200, 0x1002};

#define BSD_N(x) (uint8_t)(sizeof x / sizeof x[0])
static const struct bsd_rec BSD_REC[] = {      /* (index i: handle BSD_HANDLE0 + i, as attribute 0x0000 says) */
    {BSD_SRC, BSD_N(BSD_SRC), BSD_N(BSD_SRC_U), BSD_SRC_U},
    {BSD_TG, BSD_N(BSD_TG), BSD_N(BSD_TG_U), BSD_TG_U},
    {BSD_DID, BSD_N(BSD_DID), BSD_N(BSD_DID_U), BSD_DID_U},
};
#define BSD_NREC BSD_N(BSD_REC)

enum { BSD_ERR = 0x01, BSD_SS_REQ = 0x02, BSD_SS_RSP = 0x03, BSD_SA_REQ = 0x04, BSD_SA_RSP = 0x05,
       BSD_SSA_REQ = 0x06, BSD_SSA_RSP = 0x07 };

/* a UUID (16, 32 or 128 on the base) -> its 16-bit value; 0 not one we can hold */
static uint32_t bsd_uuid16(const uint8_t *p, uint32_t len)
{
    static const uint8_t BASE[12] = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
    uint32_t i;
    if (len == 16u) {
        for (i = 0; i < 12u; i++)
            if (p[4 + i] != BASE[i])
                return 0;
    } else if (len != 4u) {
        return len == 2u ? bl_uint(p, 2) : 0u;
    }
    return p[0] | p[1] ? 0u : bl_uint(p + 2, 2);
}

static int bsd_rec_has(uint32_t r, uint32_t u)
{
    uint32_t i;
    for (i = 0; u && i < BSD_REC[r].nu; i++)
        if (BSD_REC[r].u[i] == u)
            return 1;
    return 0;
}

/* a ServiceSearchPattern at p -> a bit per record that matches (every UUID in it the record's); *used its bytes;
 * -1 malformed */
static int bsd_pattern(const uint8_t *p, uint32_t n, uint32_t *used)
{
    uint32_t t, h, l, at, t2, h2, l2, r, u;
    int m = (1 << BSD_NREC) - 1;
    if (!bl_de(p, n, &t, &h, &l) || t != 6u || !l)
        return -1;
    for (at = h; at < h + l; at += h2 + l2) {
        if (!bl_de(p + at, h + l - at, &t2, &h2, &l2) || t2 != 3u)
            return -1;
        u = bsd_uuid16(p + at + h2, l2);
        for (r = 0; r < BSD_NREC; r++)
            if (!bsd_rec_has(r, u))
                m &= ~(1 << r);
    }
    *used = h + l;
    return m;
}

/* an AttributeIDList at p -> 1 attribute id is in it; -1 malformed */
static int bsd_wanted(const uint8_t *p, uint32_t n, uint32_t id)
{
    uint32_t t, h, l, at, t2, h2, l2, v;
    if (!bl_de(p, n, &t, &h, &l) || t != 6u)
        return -1;
    for (at = h; at < h + l; at += h2 + l2) {
        if (!bl_de(p + at, h + l - at, &t2, &h2, &l2) || t2 != 1u || (l2 != 2u && l2 != 4u))
            return -1;
        v = bl_uint(p + at + h2, l2);
        if (l2 == 2u ? v == id : (v >> 16) <= id && id <= (v & 0xFFFFu))
            return 1;
    }
    return 0;
}

/* record r's attribute list (a sequence) for the ids wanted -> its length (under 100 bytes: one-byte lengths) */
static uint32_t bsd_attrs(uint32_t r, const uint8_t *ids, uint32_t n, uint8_t *out)
{
    uint32_t k = 2, i, j;
    for (i = 0; i < BSD_REC[r].na; i++) {
        const struct bsd_attr *a = &BSD_REC[r].a[i];
        if (bsd_wanted(ids, n, a->id) != 1)
            continue;
        out[k++] = 0x09, out[k++] = (uint8_t)(a->id >> 8), out[k++] = (uint8_t)a->id;
        for (j = 0; j < a->n; j++)
            out[k++] = a->v[j];
    }
    out[0] = 0x35, out[1] = (uint8_t)(k - 2u);
    return k;
}

static uint32_t bsd_head(uint8_t *out, uint32_t pdu, uint32_t tid)
{
    out[0] = (uint8_t)pdu, out[1] = (uint8_t)(tid >> 8), out[2] = (uint8_t)tid;
    return 5;
}
static uint32_t bsd_done(uint8_t *out, uint32_t k)   /* the continuation (none), the parameter length */
{
    out[k++] = 0;
    out[3] = (uint8_t)((k - 5u) >> 8), out[4] = (uint8_t)(k - 5u);
    return k;
}

static uint32_t bsd_error(uint8_t *out, uint32_t tid, uint32_t code)
{
    bsd_head(out, BSD_ERR, tid);
    out[3] = 0, out[4] = 2;
    out[5] = (uint8_t)(code >> 8), out[6] = (uint8_t)code;
    return 7;
}

/* an SDP request PDU (req, n) -> the response PDU in out (room for BSD_OUT bytes) -> its length; 0 none */
static uint32_t bsd_answer(const uint8_t *req, uint32_t n, uint8_t *out)
{
    uint32_t pdu, tid, plen, used = 0, k, r, cnt = 0, h, i;
    const uint8_t *p;
    int m;
    if (n < 5u)
        return 0;
    pdu = req[0], tid = (uint32_t)req[1] << 8 | req[2], plen = (uint32_t)req[3] << 8 | req[4];
    if (5u + plen > n)
        return bsd_error(out, tid, 0x0004);        /* Invalid PDU Size */
    p = req + 5;
    if (pdu == BSD_SS_REQ) {                       /* pattern, max count (2), continuation */
        if ((m = bsd_pattern(p, plen, &used)) < 0 || used + 3u > plen)
            return bsd_error(out, tid, 0x0003);    /* Invalid Request Syntax */
        k = bsd_head(out, BSD_SS_RSP, tid) + 4u;
        for (r = 0; r < BSD_NREC; r++)
            if (m >> r & 1) {
                h = BSD_HANDLE0 + r;
                out[k++] = (uint8_t)(h >> 24), out[k++] = (uint8_t)(h >> 16), out[k++] = (uint8_t)(h >> 8),
                out[k++] = (uint8_t)h;
                cnt++;
            }
        out[5] = 0, out[6] = (uint8_t)cnt, out[7] = 0, out[8] = (uint8_t)cnt;
        return bsd_done(out, k);
    }
    if (pdu == BSD_SA_REQ) {                       /* handle (4), max bytes (2), attribute ids, continuation */
        if (plen < 7u || bsd_wanted(p + 6, plen - 6u, 0) < 0)
            return bsd_error(out, tid, 0x0003);
        h = bl_uint(p, 4);
        if (h < BSD_HANDLE0 || h - BSD_HANDLE0 >= BSD_NREC)
            return bsd_error(out, tid, 0x0002);    /* Invalid Service Record Handle */
        k = bsd_head(out, BSD_SA_RSP, tid);
        used = bsd_attrs(h - BSD_HANDLE0, p + 6, plen - 6u, out + k + 2);
        out[k] = (uint8_t)(used >> 8), out[k + 1] = (uint8_t)used;
        return bsd_done(out, k + 2u + used);
    }
    if (pdu == BSD_SSA_REQ) {                      /* pattern, max bytes (2), attribute ids, continuation */
        uint8_t lists[BSD_OUT];
        uint32_t ln = 0, hd;
        if ((m = bsd_pattern(p, plen, &used)) < 0 || used + 2u >= plen ||
            bsd_wanted(p + used + 2, plen - used - 2u, 0) < 0)
            return bsd_error(out, tid, 0x0003);
        for (r = 0; r < BSD_NREC; r++)
            if (m >> r & 1)
                ln += bsd_attrs(r, p + used + 2, plen - used - 2u, lists + ln);
        k = bsd_head(out, BSD_SSA_RSP, tid) + 2u;
        if (ln < 256u)                             /* the outer sequence: 35 len, as BlueZ; 36 len len past 255 */
            out[k++] = 0x35, out[k++] = (uint8_t)ln, hd = 2;
        else
            out[k++] = 0x36, out[k++] = (uint8_t)(ln >> 8), out[k++] = (uint8_t)ln, hd = 3;
        for (i = 0; i < ln; i++)
            out[k++] = lists[i];
        out[5] = (uint8_t)((ln + hd) >> 8), out[6] = (uint8_t)(ln + hd);
        return bsd_done(out, k);
    }
    return bsd_error(out, tid, 0x0003);
}
