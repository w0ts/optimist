/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Classic probe's AVDTP initiator (bt_avdtp.h), from AVDTP 1.3 and A2DP 1.3; host-tested (tests/bt_avdtp_test.c). */
#include "bt_avdtp.h"

uint32_t bv_cmd(uint8_t *out, uint32_t label, uint32_t sig, const uint8_t *par, uint32_t n)
{
    uint32_t i;
    out[0] = (uint8_t)((label & 15u) << 4 | BV_CMD);   /* packet type single (0), message type command (0) */
    out[1] = (uint8_t)(sig & 0x3Fu);
    for (i = 0; i < n; i++)
        out[2 + i] = par[i];
    return 2u + n;
}

int bv_hdr(const uint8_t *p, uint32_t n, uint32_t *label, uint32_t *msg, uint32_t *sig)
{
    if (n < 2u || ((p[0] >> 2) & 3u) != 0)
        return 0;
    *label = p[0] >> 4;
    *msg = p[0] & 3u;
    *sig = p[1] & 0x3Fu;
    return 1;
}

uint32_t bv_pick_sink(const uint8_t *p, uint32_t n)
{
    uint32_t at;
    for (at = 2; at + 1u < n; at += 2) {           /* per endpoint: SEID << 2 | in use << 1; media type << 4 | TSEP << 3 */
        uint32_t seid = p[at] >> 2, inuse = (p[at] >> 1) & 1u, media = p[at + 1] >> 4, tsep = (p[at + 1] >> 3) & 1u;
        if (!inuse && media == 0 && tsep == 1u && seid)
            return seid;
    }
    return 0;
}

int bv_caps_sbc(const uint8_t *p, uint32_t n, uint8_t sbc[4])
{
    uint32_t at = 2, i;
    while (at + 2u <= n) {                         /* service category, LOSC, information */
        uint32_t cat = p[at], losc = p[at + 1];
        if (at + 2u + losc > n)
            return 0;
        if (cat == 7u && losc >= 6u && (p[at + 2] >> 4) == 0 && p[at + 3] == 0) {   /* media codec: audio, SBC */
            for (i = 0; i < 4u; i++)
                sbc[i] = p[at + 4 + i];
            return 1;
        }
        at += 2u + losc;
    }
    return 0;
}

int bv_sbc_choose(const uint8_t cap[4], uint8_t conf[4])
{
    uint32_t freq = cap[0] & 0x20u ? 0x20u : cap[0] & 0x10u ? 0x10u : 0;   /* 44.1 kHz, else 48 kHz */
    uint32_t mode = cap[0] & 0x01u ? 0x01u : cap[0] & 0x02u ? 0x02u : 0;   /* joint stereo, else stereo */
    uint32_t blk = cap[1] & 0x10u ? 0x10u : 0, sub = cap[1] & 0x04u ? 0x04u : 0;
    uint32_t alloc = cap[1] & 0x01u ? 0x01u : cap[1] & 0x02u ? 0x02u : 0;  /* loudness, else SNR */
    uint32_t lo = cap[2] < 2u ? 2u : cap[2], hi = cap[3] > 53u ? 53u : cap[3];
    if (!freq || !mode || !blk || !sub || !alloc || lo > hi)
        return 0;
    conf[0] = (uint8_t)(freq | mode);
    conf[1] = (uint8_t)(blk | sub | alloc);
    conf[2] = (uint8_t)lo;
    conf[3] = (uint8_t)hi;
    return 1;
}

uint32_t bv_setconf_par(uint8_t *out, uint32_t acp, uint32_t intseid, const uint8_t conf[4])
{
    uint32_t k = 0, i;
    out[k++] = (uint8_t)(acp << 2);
    out[k++] = (uint8_t)(intseid << 2);
    out[k++] = 1, out[k++] = 0;                    /* media transport */
    out[k++] = 7, out[k++] = 6;                    /* media codec, 6 bytes */
    out[k++] = 0x00, out[k++] = 0x00;              /* audio, SBC */
    for (i = 0; i < 4u; i++)
        out[k++] = conf[i];
    return k;
}
