/* SPDX-License-Identifier: GPL-3.0-only */
/* The JUNO engine's voice (eng_juno.c): the Juno-106 DCO, the IR3109 / BA662 4-pole OTA ladder and the HPF of fm-106
 * by PatttF (github.com/PatttF/fm-106 be74757: fm106_osc.h, fm106_vcf.h, fm106_engine.h fm106_render_voice), a
 * fixed-point port of Ultramaster KR-106 (kayrockscreenprinting/ultramaster_kr106); both GPL-3.0-only.
 *
 * Self-contained (stdint only, no SLOOP names) so the comparison harness can build it beside fm-106's own headers.
 * juno_run renders one voice's block before the VCA: DCO -> 1.6 Hz coupling -> ladder, the ladder's x 8.59 makeup;
 * the coupling after the ladder (fm-106: one a voice, before its VCA) is the part's, after the VCAs (juno_couple). fm-106's arithmetic, made cheaper (the straight port: git history of this file):
 *   - the ladder's coefficients (the cutoff table and its peak pull, g^2 g^3 g^4, the resonance and its fade near
 *     Nyquist, 1 / (1 + kG): the divide) once every 16 samples, at the cutoff CV's RC slew taken to the middle of
 *     the 16 (the slew's 16-sample step is exact: (1 - 1393 / 32768)^16 = 0.499); the OTA's gains folded into them;
 *   - the cutoff table (fm-106's 4096 measured points) at every 4th point, linear between: 2 KB, not 8 (at most
 *     17 cents off the table above its lowest 200 values, where it is 1 step of its own rounding);
 *   - the OTA saturation's two divides by 384 a multiply, its table (fm-106's 257 points) at every 2nd point, linear
 *     between (2 LSB off the curve; the resonance tables, the cutoff table and the VCA's packed without loss:
 *     juno_dtab, the resonance's decoded when RES changes, eng_juno.c);
 *   - the thermal noise's peak follower once every 16 samples (it only matters to a silent voice);
 *   - the DCO's coupling capacitor in 32 bits (alpha 1 / 4096: 1.71 Hz for fm-106's 1.59 Hz); the ladder's on the part
 *     (one, not six: linear, it differs only by the VCA's movement below 2 Hz).
 * The cutoff DAC value comes once a block (juno_ctl_t.dac, the uPD7811 routine juno_dac). */
#ifndef JUNO_DSP_H
#define JUNO_DSP_H
#include <stdint.h>
#include "juno_tables.h"

#define JUNO_INL static inline __attribute__((always_inline))   /* (the firmware builds at -Os: no call per sample) */

#ifndef JUNO_SUB
#define JUNO_SUB 16u             /* the samples a set of ladder coefficients holds */
#endif

typedef struct {                 /* a voice's state */
    uint32_t ph;                 /* the DCO phase: saw, pulse and sub share it */
    uint32_t nseed;              /* the noise generator */
    uint32_t vseed;              /* the ladder's thermal noise */
    int32_t nlpf;                /* the noise's 5.3 kHz low-pass */
    int32_t z[4];                /* the ladder's integrators, Q24 */
    int32_t follow;              /* the ladder's input peak follower */
    int32_t slew;                /* the cutoff CV's RC slew, 0 = not started */
    int32_t sub;                 /* the sub's flip-flop: 0 / 1 */
    int32_t dc_osc;              /* the DCO's coupling capacitor, Q12 */
} juno_vs_t;

/* a packed table's value i (gen_juno_tables.py): its 16th value at or below i and the steps after it (a call:
 * once a block, or once a set of ladder coefficients) */
static __attribute__((noinline)) int32_t juno_dtab(const uint16_t *a, const uint8_t *d, int32_t bias, uint32_t i)
{
    uint32_t k = (i & ~15u) + 1u;
    int32_t s = (int32_t)a[i >> 4] + bias * (int32_t)(i & 15u);
    for (; k <= i; k++)
        s += d[k];
    return s;
}
#define JUNO_DTAB(T, i) juno_dtab(JUNO_##T##_A, JUNO_##T##_D, JUNO_##T##_B, (i))
#define JUNO_DNEXT(T, v, i) ((v) + (int32_t)JUNO_##T##_D[(i) + 1u] + JUNO_##T##_B)   /* (the value after i) */

typedef struct {                 /* what the resonance sets (juno_res_at) */
    int32_t k, pull, fb, inv_fb; /* RES_K, PEAK_PULL, FB_SCALE, INV_FB_SCALE */
} juno_rset_t;

JUNO_INL void juno_res_at(juno_rset_t *r, uint32_t res)   /* (res 0..127) */
{
    uint32_t i = res > JUNO_FB_SCALE_LO ? res - JUNO_FB_SCALE_LO : 0u, nt = sizeof JUNO_FB_SCALE_T / 2u;
    r->k = JUNO_DTAB(RES_K, res);
    r->pull = JUNO_DTAB(PEAK_PULL, res);
    r->fb = JUNO_FB_SCALE_T[i < nt ? i : nt - 1u];      /* (its flat ends: the first and the last value) */
    r->inv_fb = (int32_t)((16777216u + ((uint32_t)r->fb >> 1)) / (uint32_t)r->fb) - (res == JUNO_INV_FB_FIX);
}

typedef struct {                 /* a block's controls */
    uint32_t inc;                /* the DCO's phase increment */
    uint8_t saw, pulse;          /* 0 / 1 */
    uint8_t sub, noise;          /* SUB and NOISE levels, 0..127 */
    uint8_t pw;                  /* the pulse width, 0..105 (50..95 %) */
    uint8_t res;                 /* 0..127 */
    uint16_t dac;                /* the cutoff, 14 bits */
    juno_rset_t r;                /* what res sets: juno_res_at(&c.r, res), the caller's (eng_juno.c: once a change) */
} juno_ctl_t;

typedef struct {                 /* the HPF of a part (after its voices), the coupling before it */
    int64_t bb1, bb2, hp, dc, cpl;
} juno_hpf_t;

/* the polyBLEP residual at a wave's edge (a call: only on the samples next to an edge) */
static __attribute__((noinline)) int32_t juno_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d;
    if (inc < 2u)
        return 0;
    d = inc >> 15;
    if (!d)
        d = 1;
    if (ph < inc) {
        int32_t x = (int32_t)(ph / d);
        return (x << 1) - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        int32_t x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        return ((x * x) >> 15) + (x << 1) + 32768;
    }
    return 0;
}

/* 1-pole coupling capacitor (1 / 4096 a sample: 1.71 Hz), the state Q12 */
JUNO_INL int32_t juno_dc(int32_t in, int32_t *st)
{
    *st += ((in << 12) - *st) >> 12;
    return in - (*st >> 12);
}

/* the DCO and its coupling capacitor over n samples into x[]; returns the largest |x| */
JUNO_INL int32_t juno_dco_run(juno_vs_t *s, const juno_ctl_t *c, int32_t *x, uint32_t n)
{
    uint32_t ph = s->ph, inc = c->inc, i;
    uint32_t eff = 0xFFFFFFFFu - (0x80000000u + (uint32_t)c->pw * 18407003u);
    int32_t sub = s->sub, peak = 0, dc = s->dc_osc;
    int32_t subg = c->sub ? ((int32_t)JUNO_SUB_LEVEL_J106[c->sub & 127u] * 24576) >> 15 : 0;
    int32_t nzg = c->noise ? (int32_t)JUNO_NOISE_LEVEL_J106[c->noise & 127u] : 0;
    for (i = 0; i < n; i++) {
        int32_t blep = 0, t = 0, a;
        ph += inc;
        if (ph < inc) {
            sub = !sub;
            blep = juno_blep(ph, inc);
        } else if (ph > 0xFFFFFFFFu - inc) {
            blep = juno_blep(ph, inc);
        }
        if (c->saw)
            t = ((((int32_t)(ph >> 16) - 32768) - blep) * 19660) >> 15;
        if (c->pulse) {
            uint32_t e = ph - eff;
            int32_t p = (ph < eff ? -32767 : 32767) - blep;
            if (e < inc || e > 0xFFFFFFFFu - inc)
                p += juno_blep(e, inc);
            t += p >> 1;
        }
        if (subg) {
            int32_t g = sub ? -subg : subg;
            if (blep) {
                int32_t ab = blep < 0 ? -blep : blep;
                g = (g * (32768 - (ab > 32768 ? 32768 : ab))) >> 15;
            }
            t += g;
        }
        if (nzg) {
            uint32_t r = s->nseed;
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            s->nseed = r;
            s->nlpf += (9300 * (((int32_t)(r >> 16) - 32768) - s->nlpf)) >> 15;
            t += (int32_t)(((int64_t)(s->nlpf * nzg) * 91000) >> 30);
        }
        t = juno_dc(t, &dc);
        x[i] = t;
        a = t < 0 ? -t : t;
        peak = a > peak ? a : peak;
    }
    s->ph = ph;
    s->sub = sub;
    s->dc_osc = dc;
    return peak;
}

/* the BA662 OTA's Padé saturation, Q15 in (0..3.0): /384 as * 43691 >> 24 (exact below 3.0) */
JUNO_INL int32_t juno_ota_sat(int32_t x)
{
    int32_t a = x < 0 ? -x : x, y;
    if (a >= 98304) {
        y = 32767;
    } else {
        uint32_t q = ((uint32_t)a * 43691u) >> 16, i = q >> 9;   /* a / 384 in Q8; the table at every 2nd point */
        y = (int32_t)JUNO_OTA_SAT2[i] + (((int32_t)(JUNO_OTA_SAT2[i + 1] - JUNO_OTA_SAT2[i]) * (int32_t)(q & 511u)) >> 9);
    }
    return x < 0 ? -y : y;
}

/* the ladder's coefficients at a cutoff CV (14 bits): g = the cutoff table's g1 pulled to the resonance's peak, g^2, g^3;
 * A = the input's gain into u, B = the OTA's (both >> 27: 0.142 / (1 + kG), k inv_fb / (1 + kG)) */
JUNO_INL void juno_coef(int32_t cv, const juno_rset_t *r, int32_t *cf)
{
    uint32_t code = (uint32_t)cv >> 2, q;
    int32_t k = r->k, g1, g1n, g2, inv;
    if (code > 4095u)
        code = 4095u;
    if (code > 3500u)                                   /* no resonance at Nyquist */
        k = (k * (4095 - (int32_t)code)) / 595;
    q = code >> 2;                                      /* the cutoff table at every 4th code, linear between */
    g1 = JUNO_DTAB(VCF_G1Q, q);
    g1n = JUNO_DNEXT(VCF_G1Q, g1, q);
    g1 += ((g1n - g1) * (int32_t)(code & 3u)) >> 2;
    g1 = (g1 * r->pull) >> 12;
    if (g1 > 32767)
        g1 = 32767;
    g2 = (g1 * g1) >> 15;
    inv = (4096 * 4096) / (4096 + ((k * ((g2 * g2) >> 15)) >> 15));
    cf[0] = g1;
    cf[1] = g2;
    cf[2] = (g2 * g1) >> 15;
    cf[3] = 4653 * inv;
    cf[4] = (((k * r->inv_fb) >> 12) * inv) << 3;
}

/* one voice's block before the VCA into y[]: the ladder's coefficients once every 16 samples, at the cutoff CV's RC
 * slew taken to their middle; the slew to their end (its 16-sample step: (1 - 1393 / 32768)^16 = 0.499) */
JUNO_INL void juno_run(juno_vs_t *s, const juno_ctl_t *c, int32_t *y, uint32_t n)
{
    int32_t x[JUNO_SUB], cf[5];
    uint32_t o;
    int32_t fbs = c->r.fb, pin = (2130 * fbs) >> 12, dac = c->dac;
    int32_t z0 = s->z[0], z1 = s->z[1], z2 = s->z[2], z3 = s->z[3];
    if (s->slew == 0)
        s->slew = dac;
    for (o = 0; o < n; o += JUNO_SUB) {
        uint32_t i, m = n - o < JUNO_SUB ? n - o : JUNO_SUB;   /* (shorter: a voice starting inside the block) */
        int32_t peak = juno_dco_run(s, c, x, m), sl = s->slew - dac, g, g2, g3, om, A, B, zs, energy, noisy;
        juno_coef(dac + ((sl * 23150) >> 15), &c->r, cf);   /* (1 - 1393 / 32768)^8 = 0.7065 */
        s->slew = dac + ((sl * 16353) >> 15);
        g = cf[0], g2 = cf[1], g3 = cf[2], om = 32768 - g, A = cf[3], B = cf[4];
        /* the thermal noise: only into a voice gone silent (input and ladder), checked once per 16 */
        s->follow = (s->follow * 32228) >> 15;           /* (32734 / 32768)^16 */
        if (peak > s->follow)
            s->follow = peak;
        zs = (z0 < 0 ? -z0 : z0) + (z1 < 0 ? -z1 : z1) + (z2 < 0 ? -z2 : z2) + (z3 < 0 ? -z3 : z3);
        energy = s->follow > (zs >> 9) ? s->follow : zs >> 9;
        noisy = energy < 50 ? 327 / (1 + (energy >> 2)) : 0;
        for (i = 0; i < m; i++) {
            int32_t in = x[i], sz, S, sat, u, d, yv;
            if (noisy) {
                s->vseed = s->vseed * 196314165u + 907633515u;
                in += (((int32_t)(s->vseed >> 16) - 32768) * noisy) >> 15;
            }
            sz = (int32_t)(((int64_t)(z0 >> 9) * g3 + (int64_t)(z1 >> 9) * g2 + (int64_t)(z2 >> 9) * g) >> 15) + (z3 >> 9);
            S = (int32_t)(((int64_t)om * sz) >> 15);
            sat = juno_ota_sat(((in * pin) >> 15) - (int32_t)(((int64_t)S * fbs) >> 12));
            u = (int32_t)(((int64_t)in * A + (int64_t)sat * B) >> 27) << 9;
            d = (int32_t)(((int64_t)g * (u - z0)) >> 15), yv = z0 + d, z0 = yv + d, u = yv;
            d = (int32_t)(((int64_t)g * (u - z1)) >> 15), yv = z1 + d, z1 = yv + d, u = yv;
            d = (int32_t)(((int64_t)g * (u - z2)) >> 15), yv = z2 + d, z2 = yv + d, u = yv;
            d = (int32_t)(((int64_t)g * (u - z3)) >> 15), yv = z3 + d, z3 = yv + d;
            y[o + i] = ((yv >> 9) * 2199) >> 8;               /* x 8.59 (8796 / 1024): 32 bits */
        }
    }
    s->z[0] = z0, s->z[1] = z1, s->z[2] = z2, s->z[3] = z3;
}

/* the coupling capacitor after the ladders (fm106_dc_block, 1.59 Hz), on the part's sum of voices */
JUNO_INL void juno_couple(int64_t *st, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        *st += ((((int64_t)x[i] << 16) - *st) * 3800LL) >> 24;
        x[i] -= (int32_t)(*st >> 16);
    }
}

/* ---- the HPF (one per part, after the voices): 0 bass boost, 1 flat, 2 236 Hz, 3 754 Hz; 0.35 Hz coupling ---- */
JUNO_INL int32_t juno_hpf_dc(int32_t in, int64_t *st)
{
    int64_t diff = ((int64_t)in << 16) - *st;
    *st += (diff * 837LL) >> 24;
    return in - (int32_t)(*st >> 16);
}
JUNO_INL int32_t juno_hpf_cut(int32_t in, int64_t *st, int64_t g)
{
    int64_t v = ((((int64_t)in << 16) - *st) * g) >> 24, lp = *st + v;
    *st = lp + v;
    return in - (int32_t)(lp >> 16);
}
JUNO_INL int32_t juno_boost(juno_hpf_t *h, int32_t in)
{
    int64_t v1 = ((((int64_t)in << 16) - h->bb1) * 70704LL) >> 24, l1 = h->bb1 + v1, v2, l2;
    int32_t lp1, y1, lp2, y2;
    h->bb1 = l1 + v1;
    lp1 = (int32_t)(l1 >> 16);
    y1 = lp1 + (int32_t)(((int64_t)(in - lp1) * 27019LL) >> 15);
    v2 = ((((int64_t)y1 << 16) - h->bb2) * 86019LL) >> 24;
    l2 = h->bb2 + v2;
    h->bb2 = l2 + v2;
    lp2 = (int32_t)(l2 >> 16);
    y2 = lp2 + (int32_t)(((int64_t)(y1 - lp2) * 2979LL) >> 15);
    return in + (int32_t)(((int64_t)y2 * 77000LL) >> 15);
}
JUNO_INL void juno_hpf(juno_hpf_t *h, uint32_t mode, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t s = x[i];
        if (mode == 0u)
            s = juno_hpf_dc(juno_boost(h, s), &h->dc);
        else if (mode == 1u)
            s = juno_hpf_dc(s, &h->dc);
        else
            s = juno_hpf_cut(juno_hpf_dc(s, &h->dc), &h->hp, mode == 2u ? 277423LL : 856006LL);
        x[i] = s;
    }
}

/* the uPD7811 cutoff DAC routine (fm106_calc_vcf_freq, with no bend): the slider (14 bits), the LFO's and the
 * envelope's amounts (signed, 14-bit units), the key tracking (0..254) around C4; clamped to 14 bits */
JUNO_INL uint32_t juno_dac(int32_t cut14, int32_t mod14, uint32_t env14, int32_t envmod, uint32_t keytrk,
                                uint32_t note88)
{
    int32_t ea = cut14 + mod14, e = (int32_t)((env14 * (uint32_t)(envmod < 0 ? -envmod : envmod)) >> 8);
    uint32_t ps = (note88 >> 2) + (note88 >> 3);
    ea += envmod < 0 ? -e : e;
    if (ps > 0x1680u)
        ea += (int32_t)((keytrk * (ps - 0x1680u)) >> 8);
    else
        ea -= (int32_t)((keytrk * (0x1680u - ps)) >> 8);
    return ea < 0 ? 0u : ea > 0x3FFF ? 0x3FFFu : (uint32_t)ea;
}
#endif
