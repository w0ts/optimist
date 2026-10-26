/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Classic probe's L2CAP signalling and SDP client (bt_l2cap.h), from the Core spec Vol 3 Parts A and B;
 * host-tested (tests/bt_l2cap_test.c). */
#include "bt_l2cap.h"

static uint32_t bl_le16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return 2;
}

static uint32_t bl_sig(uint8_t *out, uint32_t code, uint32_t id, uint32_t dlen)   /* the headers; data at out + 8 */
{
    bl_le16(out, 4u + dlen);
    bl_le16(out + 2, BL_CID_SIG);
    out[4] = (uint8_t)code;
    out[5] = (uint8_t)id;
    bl_le16(out + 6, dlen);
    return 8u + dlen;
}

uint32_t bl_conn_req(uint8_t *out, uint32_t id, uint32_t psm, uint32_t scid)
{
    bl_le16(out + 8, psm);
    bl_le16(out + 10, scid);
    return bl_sig(out, BL_CONN_REQ, id, 4);
}

uint32_t bl_conn_rsp(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t scid, uint32_t result)
{
    bl_le16(out + 8, dcid);
    bl_le16(out + 10, scid);
    bl_le16(out + 12, result);
    bl_le16(out + 14, 0);
    return bl_sig(out, BL_CONN_RSP, id, 8);
}

uint32_t bl_conf_req(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t mtu)
{
    bl_le16(out + 8, dcid);
    bl_le16(out + 10, 0);                          /* flags */
    out[12] = 1;                                   /* option: MTU */
    out[13] = 2;
    bl_le16(out + 14, mtu);
    return bl_sig(out, BL_CONF_REQ, id, 8);
}

uint32_t bl_conf_rsp(uint8_t *out, uint32_t id, uint32_t scid, uint32_t result)
{
    bl_le16(out + 8, scid);
    bl_le16(out + 10, 0);
    bl_le16(out + 12, result);
    return bl_sig(out, BL_CONF_RSP, id, 6);
}

uint32_t bl_disc_req(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t scid)
{
    bl_le16(out + 8, dcid);
    bl_le16(out + 10, scid);
    return bl_sig(out, BL_DISC_REQ, id, 4);
}

uint32_t bl_disc_rsp(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t scid)
{
    bl_le16(out + 8, dcid);
    bl_le16(out + 10, scid);
    return bl_sig(out, BL_DISC_RSP, id, 4);
}

uint32_t bl_info_req(uint8_t *out, uint32_t id, uint32_t type)   /* we ask: 2 extended features, 3 fixed channels */
{
    bl_le16(out + 8, type);
    return bl_sig(out, BL_INFO_REQ, id, 2);
}

uint32_t bl_info_rsp(uint8_t *out, uint32_t id, uint32_t type)
{
    uint32_t i, n = 4;
    bl_le16(out + 8, type);
    if (type == 2u) {                              /* extended features: Fixed Channels (bit 7) only. "None" left the
                                                    * AirPods without the fixed-channel question BlueZ is asked
                                                    * (0x2B8), 2026-10-20: we claim no mode we do not run */
        bl_le16(out + 10, 0);
        for (i = 0; i < 4u; i++)
            out[12 + i] = (uint8_t)(i == 0 ? 0x80u : 0);
        n = 8;
    } else if (type == 3u) {                       /* fixed channels: signalling only */
        bl_le16(out + 10, 0);
        for (i = 0; i < 8u; i++)
            out[12 + i] = (uint8_t)(i == 0 ? 0x02u : 0);
        n = 12;
    } else {
        bl_le16(out + 10, 1);                      /* not supported */
    }
    return bl_sig(out, BL_INFO_RSP, id, n);
}

uint32_t bl_cmd_reject(uint8_t *out, uint32_t id)
{
    bl_le16(out + 8, 0);                           /* command not understood */
    return bl_sig(out, BL_CMD_REJECT, id, 2);
}

uint32_t bl_sdp_ssa_req(uint8_t *out, uint32_t dcid, uint32_t tid, uint32_t uuid16, const uint8_t *cont, uint32_t n)
{
    uint8_t *p = out + 4;
    uint32_t k = 0, i, plen;
    p[k++] = 0x06;                                 /* ServiceSearchAttributeRequest */
    p[k++] = (uint8_t)(tid >> 8);
    p[k++] = (uint8_t)tid;
    k += 2;                                        /* parameter length, below */
    p[k++] = 0x35, p[k++] = 3;                     /* sequence of one UUID16 */
    p[k++] = 0x19, p[k++] = (uint8_t)(uuid16 >> 8), p[k++] = (uint8_t)uuid16;
    p[k++] = 0x02, p[k++] = 0x00;                  /* maximum attribute bytes: 512 */
    p[k++] = 0x35, p[k++] = 5;                     /* attribute range 0x0000-0xFFFF */
    p[k++] = 0x0A, p[k++] = 0, p[k++] = 0, p[k++] = 0xFF, p[k++] = 0xFF;
    p[k++] = (uint8_t)n;
    for (i = 0; i < n && i < 16u; i++)
        p[k++] = cont[i];
    plen = k - 5u;
    p[3] = (uint8_t)(plen >> 8);
    p[4] = (uint8_t)plen;
    bl_le16(out, k);
    bl_le16(out + 2, dcid);
    return 4u + k;
}

/* ---- SDP data elements (Vol 3 Part B §3) */
static int bl_de(const uint8_t *p, uint32_t n, uint32_t *type, uint32_t *hdr, uint32_t *len)
{
    static const uint8_t fixed[5] = {1, 2, 4, 8, 16};
    uint32_t idx;
    if (n < 1u)
        return 0;
    *type = p[0] >> 3;
    idx = p[0] & 7u;
    if (*type == 0) {
        *hdr = 1, *len = 0;
    } else if (idx < 5u) {
        *hdr = 1, *len = fixed[idx];
    } else if (idx == 5u) {
        if (n < 2u)
            return 0;
        *hdr = 2, *len = p[1];
    } else if (idx == 6u) {
        if (n < 3u)
            return 0;
        *hdr = 3, *len = (uint32_t)p[1] << 8 | p[2];
    } else {
        if (n < 5u)
            return 0;
        *hdr = 5, *len = (uint32_t)p[3] << 8 | p[4];   /* (a 4-byte length: the top two bytes ignored) */
    }
    return *hdr + *len <= n;
}

static uint32_t bl_uint(const uint8_t *p, uint32_t len)   /* a big-endian unsigned or UUID16 / 32's low part */
{
    uint32_t v = 0, i;
    for (i = 0; i < len && i < 4u; i++)
        v = v << 8 | p[i];
    return v;
}

/* the first UUID and the first uint inside a sequence (one protocol descriptor or one profile descriptor) */
static void bl_pair(const uint8_t *p, uint32_t n, uint16_t *uuid, uint16_t *num)
{
    uint32_t t, h, l, at = 0;
    *uuid = 0, *num = 0;
    while (at < n && bl_de(p + at, n - at, &t, &h, &l)) {
        if (t == 3u && !*uuid)
            *uuid = (uint16_t)bl_uint(p + at + h, l);
        else if (t == 1u && !*num)
            *num = (uint16_t)bl_uint(p + at + h, l);
        at += h + l;
    }
}

static void bl_attr(uint32_t id, const uint8_t *p, uint32_t n, uint32_t cls, struct bl_sdp_info *info)
{
    uint32_t t, h, l, at, t2, h2, l2;
    if (!bl_de(p, n, &t, &h, &l))
        return;
    if (id == 0x0001u && t == 6u) {                /* ServiceClassIDList */
        for (at = h; at < h + l && bl_de(p + at, h + l - at, &t2, &h2, &l2); at += h2 + l2)
            if (t2 == 3u && bl_uint(p + at + h2, l2) == cls)
                info->has_class = 1;
    } else if (id == 0x0004u && t == 6u) {         /* ProtocolDescriptorList: (L2CAP, PSM), (protocol, version) */
        for (at = h; at < h + l && bl_de(p + at, h + l - at, &t2, &h2, &l2); at += h2 + l2) {
            uint16_t u, v;
            if (t2 != 6u)
                continue;
            bl_pair(p + at + h2, l2, &u, &v);
            if (u == 0x0100u)
                info->psm = v;
            else if (u)
                info->proto_uuid = u, info->proto_ver = v;
        }
    } else if (id == 0x0009u && t == 6u) {         /* BluetoothProfileDescriptorList */
        if (bl_de(p + h, l, &t2, &h2, &l2) && t2 == 6u)
            bl_pair(p + h + h2, l2, &info->profile_uuid, &info->profile_ver);
    } else if (id == 0x0311u && t == 1u) {
        info->features = (uint16_t)bl_uint(p + h, l);
    } else if (id == 0x0100u && t == 4u) {
        uint32_t k;
        for (k = 0; k < l && k + 1u < sizeof info->name; k++)
            info->name[k] = (char)(p[h + k] >= 0x20u && p[h + k] < 0x7Fu ? p[h + k] : '.');
        info->name[k] = 0;
    }
}

static int bl_record(const uint8_t *p, uint32_t n, uint32_t cls, struct bl_sdp_info *info)
{
    uint32_t t, h, l, at = 0, id;
    while (at < n) {                               /* attribute id (uint16), value */
        if (!bl_de(p + at, n - at, &t, &h, &l) || t != 1u || l != 2u)
            return 0;
        id = bl_uint(p + at + h, 2);
        at += h + l;
        if (!bl_de(p + at, n - at, &t, &h, &l))
            return 0;
        bl_attr(id, p + at, h + l, cls, info);
        at += h + l;
    }
    return 1;
}

int bl_sdp_parse(const uint8_t *p, uint32_t n, uint32_t cls, struct bl_sdp_info *info)
{
    uint32_t t, h, l, at, t2, h2, l2;
    uint8_t *z = (uint8_t *)info;
    for (at = 0; at < sizeof *info; at++)
        z[at] = 0;
    if (!bl_de(p, n, &t, &h, &l) || t != 6u)
        return 0;
    for (at = h; at < h + l; at += h2 + l2) {      /* AttributeLists: a sequence of attribute lists */
        if (!bl_de(p + at, h + l - at, &t2, &h2, &l2) || t2 != 6u)
            return 0;
        if (!bl_record(p + at + h2, l2, cls, info))
            return 0;
        info->records++;
    }
    return 1;
}
