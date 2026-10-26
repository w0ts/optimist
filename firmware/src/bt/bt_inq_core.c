/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The inquiry probe's portable part (bt_inq.h): written from the Core specification, host-tested (tests/bt_inq_test.c). */
#include "bt_inq.h"

/* a 64-bit value as two words (no 64-bit arithmetic: the firmware is freestanding) */
struct bti_u64 {
    uint32_t lo, hi;
};

static uint32_t bti_bit(struct bti_u64 v, uint32_t i) { return i < 32u ? (v.lo >> i) & 1u : (v.hi >> (i - 32u)) & 1u; }

static struct bti_u64 bti_shl(struct bti_u64 v, uint32_t n)   /* n < 64 */
{
    struct bti_u64 r;
    if (n == 0)
        return v;
    if (n >= 32u) {
        r.hi = v.lo << (n - 32u);
        r.lo = 0;
    } else {
        r.hi = v.hi << n | v.lo >> (32u - n);
        r.lo = v.lo << n;
    }
    return r;
}

/* Vol 2 Part B §6.3.3.2: the PN sequence p0..p63 (p0 the least significant bit) and the (64,30) code's generator,
 * octal 260534236651 (degree 34) */
#define BTI_PN_LO 0xBBCC54FCu
#define BTI_PN_HI 0x83848D96u
#define BTI_G_LO 0x85713DA9u
#define BTI_G_HI 0x5u

void bti_parity(uint32_t lap, uint8_t out[5])
{
    struct bti_u64 v, g, pn = {BTI_PN_LO, BTI_PN_HI};
    uint32_t info, x, i;
    /* the information bits a0..a29: the LAP, then the Barker bits 001101 (a23 = 0) or 110010 (a23 = 1), a24 first */
    info = (lap & 0xFFFFFFu) | ((lap & 0x800000u) ? 0x13u : 0x2Cu) << 24;
    x = (info ^ pn.hi >> 2) & 0x3FFFFFFFu;                 /* XOR with p34..p63 (bits 34..63 of the PN = hi >> 2) */
    v = bti_shl((struct bti_u64){x, 0}, 34);               /* x(D) * D^34: bits 34..63 */
    for (i = 63; i >= 34u; i--) {                          /* the remainder modulo g(D) */
        if (bti_bit(v, i)) {
            g = bti_shl((struct bti_u64){BTI_G_LO, BTI_G_HI}, i - 34u);
            v.lo ^= g.lo;
            v.hi ^= g.hi;
        }
    }
    v.lo ^= pn.lo;                                         /* XOR with p0..p33 */
    v.hi = (v.hi ^ pn.hi) & 3u;
    out[0] = (uint8_t)v.lo;
    out[1] = (uint8_t)(v.lo >> 8);
    out[2] = (uint8_t)(v.lo >> 16);
    out[3] = (uint8_t)(v.lo >> 24);
    out[4] = (uint8_t)v.hi;
}

static uint32_t bti_bits(const uint8_t *p, uint32_t at, uint32_t n)   /* n <= 26 bits from bit at, LSB first */
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++)
        v |= (uint32_t)((p[(at + i) >> 3] >> ((at + i) & 7u)) & 1u) << i;
    return v;
}

/* Vol 2 Part B §6.5.1.4: parity 34, LAP 24, EIR 1, undefined 1, SR 2, reserved 2, UAP 8, NAP 16, class 24,
 * LT_ADDR 3, CLK27-2 26, page scan mode 3 (144 bits) */
void bti_fhs_parse(const uint8_t p[18], struct bti_fhs *f)
{
    uint32_t lap = bti_bits(p, 34, 24), nap = bti_bits(p, 72, 16);
    f->parity_lo = bti_bits(p, 0, 16) | bti_bits(p, 16, 16) << 16;
    f->parity_hi = (uint8_t)bti_bits(p, 32, 2);
    f->addr[0] = (uint8_t)lap;
    f->addr[1] = (uint8_t)(lap >> 8);
    f->addr[2] = (uint8_t)(lap >> 16);
    f->addr[3] = (uint8_t)bti_bits(p, 64, 8);
    f->addr[4] = (uint8_t)nap;
    f->addr[5] = (uint8_t)(nap >> 8);
    f->eir = (uint8_t)bti_bits(p, 58, 1);
    f->sr = (uint8_t)bti_bits(p, 60, 2);
    f->reserved = (uint8_t)bti_bits(p, 62, 2);
    f->cod = bti_bits(p, 88, 24);
    f->lt_addr = (uint8_t)bti_bits(p, 112, 3);
    f->clk = bti_bits(p, 115, 26);
    f->psm = (uint8_t)bti_bits(p, 141, 3);
}

static void bti_put(uint8_t *p, uint32_t at, uint32_t n, uint32_t v)   /* n <= 26 bits at bit at, LSB first */
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint8_t m = (uint8_t)(1u << ((at + i) & 7u));
        if ((v >> i) & 1u)
            p[(at + i) >> 3] |= m;
        else
            p[(at + i) >> 3] &= (uint8_t)~m;
    }
}

void bti_fhs_build(const struct bti_fhs *f, uint8_t p[18])
{
    uint8_t par[5];
    uint32_t lap = f->addr[0] | (uint32_t)f->addr[1] << 8 | (uint32_t)f->addr[2] << 16, i;
    bti_parity(lap, par);
    for (i = 0; i < 4u; i++)
        p[i] = par[i];
    bti_put(p, 32, 2, par[4]);
    bti_put(p, 34, 24, lap);
    bti_put(p, 58, 1, f->eir);
    bti_put(p, 59, 1, 0);
    bti_put(p, 60, 2, f->sr);
    bti_put(p, 62, 2, 2u);
    bti_put(p, 64, 8, f->addr[3]);
    bti_put(p, 72, 16, f->addr[4] | (uint32_t)f->addr[5] << 8);
    bti_put(p, 88, 24, f->cod);
    bti_put(p, 112, 3, f->lt_addr);
    bti_put(p, 115, 26, f->clk);
    bti_put(p, 141, 3, f->psm);
}

uint32_t bti_eir_name(const uint8_t *eir, uint32_t len, char *name, uint32_t max)
{
    uint32_t i = 0, best = 0, n = 0, k;
    const uint8_t *at = 0;
    while (i < len && eir[i] != 0) {                       /* [length][type][data length-1], a 0 length ends it */
        uint32_t l = eir[i];
        if (i + 1u + l > len)
            break;
        if (eir[i + 1] == 0x09u || (eir[i + 1] == 0x08u && best != 0x09u)) {
            best = eir[i + 1];
            at = eir + i + 2;
            n = l - 1u;
        }
        i += 1u + l;
    }
    if (!at || max == 0)
        return 0;
    while (n && at[n - 1] == 0)                            /* (a name padded with zeros, as stock V15's "FM-1\0") */
        n--;
    for (k = 0; k < n && k + 1u < max; k++)
        name[k] = (at[k] >= 0x20u && at[k] < 0x7Fu) ? (char)at[k] : '.';
    name[k] = 0;
    return n;
}
