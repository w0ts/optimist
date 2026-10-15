/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNO CHORUS, a generic FX unit: the Juno-106's MN3009 BBD chorus as fm-106 models it (PatttF, github.com/PatttF/fm-106
 * be74757, fm106_chorus.h, GPL-3.0-only; after Ultramaster KR-106, GPL-3.0-only), sample for sample:
 *   - one delay line of 1024 samples (fm-106 writes the same mono sample into two: one is enough), 2 KB;
 *   - a triangle LFO: MODE I 0.514 Hz, II 0.842 Hz, I+II 7.85 Hz; two taps in antiphase around 3.5 ms (154 samples),
 *     +- 47 / 37 / 5 samples, read by 4-point Hermite interpolation;
 *   - the BBD's reconstruction low-pass (2nd-order Butterworth, 9.66 kHz) on each tap;
 *   - out L / R = dry x 0.863 + tap L / R x 1.257 (the Juno's IC6 summer) at MIX 127; MIX 0 is the dry signal itself.
 * Mono in, stereo out (L and R differ only by the taps): juno_cho_run; juno_cho_ms gives the mid and the side instead,
 * for a mono slot whose side is carried on (a part's insert); juno_cho_st: a stereo bus, its mid into the line. fm-106's soft limiter on its input and output is left out (the
 * mix has its own headroom and limiter): below its knee (24576) the two are the same.
 * Self-contained (stdint only), so the host test can build it beside fm-106's own header. No divide a sample. */
#ifndef JUNO_CHO_H
#define JUNO_CHO_H
#include <stdint.h>

#define JUNO_CHO_LEN 1024u
#define JUNO_CHO_INL static inline __attribute__((always_inline))

typedef struct {
    int16_t ln[JUNO_CHO_LEN];    /* the line */
    uint32_t wr;                 /* the next write place */
    uint32_t ph;                 /* the triangle's phase */
    int32_t f[2][2];             /* the low-passes' states, L and R */
} juno_cho_t;

enum { JUNO_CHO_I, JUNO_CHO_II, JUNO_CHO_I_II };   /* MODE */

/* 4-point Hermite, Q15 fraction (fm106_hermite) */
JUNO_CHO_INL int32_t juno_cho_herm(int32_t fr, int32_t y0, int32_t y1, int32_t y2, int32_t y3)
{
    int32_t c1 = (y2 - y0) >> 1, c2 = y0 - y1 + (y2 - y1) - ((y1 - (y2 << 1) + y3) >> 1);
    int32_t c3 = ((y3 - y0) >> 1) + y1 - y2 + ((y1 - y2) >> 1);
    int64_t a = (((int64_t)c3 * fr) >> 15) + c2;
    a = ((a * fr) >> 15) + c1;
    return (int32_t)(((a * fr) >> 15) + y1);
}
/* the tap d (16.16 samples back) after the sample at w was written */
JUNO_CHO_INL int32_t juno_cho_tap(const int16_t *ln, uint32_t w, int32_t d)
{
    uint32_t r1 = (w - 1u - (uint32_t)(d >> 16)) & (JUNO_CHO_LEN - 1u);
    return juno_cho_herm((d >> 1) & 0x7FFF, ln[(r1 + 1u) & (JUNO_CHO_LEN - 1u)], ln[r1],
                         ln[(r1 - 1u) & (JUNO_CHO_LEN - 1u)], ln[(r1 - 2u) & (JUNO_CHO_LEN - 1u)]);
}
/* the reconstruction low-pass (fm106_bbd_filter_process: g 0.826, a1 0.352) */
JUNO_CHO_INL int32_t juno_cho_lp(int32_t *s, int32_t in)
{
    int32_t v3 = in - s[1], v1 = ((11534 * s[0]) >> 15) + ((9527 * v3) >> 15), v2 = s[1] + ((27066 * v1) >> 15);
    s[0] = (v1 << 1) - s[0];
    s[1] = (v2 << 1) - s[1];
    return v2;
}

/* the unit's loop: n samples of in (mono, into the line) -> l, r = the dry dl / dr x the dry gain + the taps (any of
 * them may be the same buffer: each sample is read before it is written). mode: JUNO_CHO_*; mix 0..127; sh: the
 * buffers' headroom (the line holds in >> sh, 16 bits; the dry and the output keep their scale) */
JUNO_CHO_INL void juno_cho_core(juno_cho_t *c, uint32_t mode, uint32_t mix, const int32_t *in, const int32_t *dl,
                                const int32_t *dr, int32_t *l, int32_t *r, uint32_t n, uint32_t sh)
{
    static const uint32_t INC[3] = {50098u, 82068u, 765109u};   /* the LFO's step a sample */
    static const int32_t DEPTH[3] = {94, 75, 10};               /* (x the triangle >> 16: samples) */
    /* MIX: the dry from 1 down to 0.863 (21209 Q15), the taps from 0 up to 1.257 (30892): fm-106's at 127 */
    int32_t gd = 32768 - (int32_t)(((32768 - 21209) * mix) / 127u), gw = (int32_t)((30892u * mix) / 127u);
    int32_t dep = DEPTH[mode % 3u];
    uint32_t inc = INC[mode % 3u], i, w = c->wr, ph = c->ph;
    for (i = 0; i < n; i++) {
        int32_t x = in[i] >> sh, xl = dl[i], xr = dr[i], t, d, wl, wrt;
        x = x < -32768 ? -32768 : x > 32767 ? 32767 : x;
        c->ln[w] = (int16_t)x;
        ph += inc;
        t = (int32_t)(ph >> 16) - 32768;
        t = ((t < 0 ? -t : t) - 16384) << 1;           /* the triangle, -32768..32766 */
        d = t * dep;
        wl = juno_cho_lp(c->f[0], juno_cho_tap(c->ln, w, (154 << 16) + d));
        wrt = juno_cho_lp(c->f[1], juno_cho_tap(c->ln, w, (154 << 16) - d));
        w = (w + 1u) & (JUNO_CHO_LEN - 1u);
        if (!sh) {                                      /* (fm-106's own arithmetic: the line's scale is the dry's) */
            l[i] = (int32_t)(((int64_t)xl * gd + (int64_t)wl * gw) >> 15);
            r[i] = (int32_t)(((int64_t)xr * gd + (int64_t)wrt * gw) >> 15);
        } else {
            l[i] = (int32_t)(((int64_t)xl * gd) >> 15) + (int32_t)((((int64_t)wl * gw) >> 15) << sh);
            r[i] = (int32_t)(((int64_t)xr * gd) >> 15) + (int32_t)((((int64_t)wrt * gw) >> 15) << sh);
        }
    }
    c->wr = w, c->ph = ph;
}
/* mono in -> stereo out (l, r; either may be in) */
static void juno_cho_run(juno_cho_t *c, uint32_t mode, uint32_t mix, const int32_t *in, int32_t *l, int32_t *r,
                         uint32_t n, uint32_t sh)
{
    juno_cho_core(c, mode, mix, in, in, in, l, r, n, sh);
}
/* stereo in place: the line fed the mid, each side keeps its own dry (a stereo bus: the drum bus, the master) */
static void juno_cho_st(juno_cho_t *c, uint32_t mode, uint32_t mix, int32_t *l, int32_t *r, uint32_t n, uint32_t sh)
{
    int32_t m[64];
    uint32_t o, i;
    for (o = 0; o < n; o += 64u) {
        uint32_t k = n - o < 64u ? n - o : 64u;
        for (i = 0; i < k; i++)
            m[i] = (l[o + i] + r[o + i]) >> 1;               /* (|l|, |r| < 2^30) */
        juno_cho_core(c, mode, mix, m, l + o, r + o, l + o, r + o, k, sh);
    }
}
/* the same as a mid and a side: b (in and out) = (L + R) / 2, side = (L - R) / 2 */
static void juno_cho_ms(juno_cho_t *c, uint32_t mode, uint32_t mix, int32_t *b, int32_t *side, uint32_t n, uint32_t sh)
{
    uint32_t i;
    juno_cho_run(c, mode, mix, b, b, side, n, sh);
    for (i = 0; i < n; i++) {
        int32_t L = b[i], R = side[i];
        b[i] = (L + R) >> 1, side[i] = (L - R) >> 1;
    }
}
#endif
