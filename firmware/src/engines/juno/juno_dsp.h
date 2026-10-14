/* SPDX-License-Identifier: GPL-3.0-only */
/* The JUNO engine's voice (eng_juno.c): the Juno-106 DCO, the IR3109 / BA662 4-pole OTA ladder and the HPF of fm-106
 * by PatttF (github.com/PatttF/fm-106 be74757: fm106_osc.h, fm106_vcf.h, fm106_engine.h fm106_render_voice), a
 * fixed-point port of Ultramaster KR-106 (kayrockscreenprinting/ultramaster_kr106); both GPL-3.0-only.
 *
 * Self-contained (stdint only, no SLOOP names) so the comparison harness can build it beside fm-106's own headers.
 * juno_run renders one voice's block before the VCA: DCO -> 1.59 Hz coupling -> the cutoff CV's RC slew -> ladder ->
 * 1.59 Hz coupling. The cutoff DAC value comes once a block (juno_ctl_t.dac, the uPD7811 routine in eng_juno.c);
 * fm-106 works it out every sample from its own envelope, the rest is its arithmetic, sample for sample. */
#ifndef JUNO_DSP_H
#define JUNO_DSP_H
#include <stdint.h>
#include "juno_tables.h"

#define JUNO_INL static inline __attribute__((always_inline))   /* (the firmware builds at -Os: no call per sample) */

typedef struct {                 /* a voice's state */
    uint32_t ph;                 /* the DCO phase: saw, pulse and sub share it */
    uint32_t nseed;              /* the noise generator */
    uint32_t vseed;              /* the ladder's thermal noise */
    int32_t nlpf;                /* the noise's 5.3 kHz low-pass */
    int32_t z[4];                /* the ladder's integrators, Q24 */
    int32_t follow;              /* the ladder's input peak follower */
    int32_t slew;                /* the cutoff CV's RC slew, 0 = not started */
    int32_t sub;                 /* the sub's flip-flop: 0 / 1 */
    int64_t dc_osc, dc_vcf;      /* the coupling capacitors, Q16 */
} juno_vs_t;

typedef struct {                 /* a block's controls */
    uint32_t inc;                /* the DCO's phase increment */
    uint8_t saw, pulse;          /* 0 / 1 */
    uint8_t sub, noise;          /* SUB and NOISE levels, 0..127 */
    uint8_t pw;                  /* the pulse width, 0..105 (50..95 %) */
    uint8_t res;                 /* 0..127 */
    uint16_t dac;                /* the cutoff, 14 bits */
} juno_ctl_t;

typedef struct {                 /* the HPF of a part (after its voices) */
    int64_t bb1, bb2, hp, dc;
} juno_hpf_t;

JUNO_INL int32_t juno_blep(uint32_t ph, uint32_t inc)
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

JUNO_INL uint32_t juno_xorshift(uint32_t *seed)
{
    uint32_t s = *seed;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *seed = s;
    return s;
}

/* the DCO: saw 0.6, the Juno's inverted pulse 0.5, the CD4013 sub 0.75, the filtered noise (Q15) */
JUNO_INL int32_t juno_dco(juno_vs_t *s, const juno_ctl_t *c)
{
    uint32_t prev = s->ph, ph = prev + c->inc;
    int32_t blep, total = 0;
    s->ph = ph;
    if (ph < prev)
        s->sub = !s->sub;
    blep = juno_blep(ph, c->inc);
    if (c->saw)
        total += ((((int32_t)(ph >> 16) - 32768) - blep) * 19660) >> 15;
    if (c->pulse) {
        uint32_t eff = 0xFFFFFFFFu - (0x80000000u + (uint32_t)c->pw * 18407003u);
        int32_t pulse = (ph < eff ? -32767 : 32767) - blep;
        pulse += juno_blep(ph - eff, c->inc);
        total += (pulse * 16384) >> 15;
    }
    if (c->sub) {
        int32_t sign = s->sub ? -32767 : 32767, ab = blep < 0 ? -blep : blep, sub;
        if (ab > 32768)
            ab = 32768;
        sub = (sign * (32768 - ab)) >> 15;
        total += (((sub * (int32_t)JUNO_SUB_LEVEL_J106[c->sub & 127u]) >> 15) * 24576) >> 15;
    }
    if (c->noise) {
        int32_t raw = (int32_t)(juno_xorshift(&s->nseed) >> 16) - 32768;
        s->nlpf += (9300 * (raw - s->nlpf)) >> 15;
        total += (int32_t)(((int64_t)((s->nlpf * (int32_t)JUNO_NOISE_LEVEL_J106[c->noise & 127u]) >> 15) * 91000LL) >> 15);
    }
    return total;
}

/* 1-pole coupling capacitor, 1.59 Hz */
JUNO_INL int32_t juno_dc(int32_t in, int64_t *st)
{
    int64_t diff = ((int64_t)in << 16) - *st;
    *st += (diff * 3800LL) >> 24;
    return in - (int32_t)(*st >> 16);
}

/* the BA662 OTA's Padé saturation, Q15 in (0..3.0) */
JUNO_INL int32_t juno_ota_sat(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a / 384, y;
    if (i >= 256) {
        y = 32767;
    } else {
        int32_t rem = a - i * 384;
        y = (int32_t)JUNO_OTA_SAT[i] + (((int32_t)(JUNO_OTA_SAT[i + 1] - JUNO_OTA_SAT[i]) * rem) / 384);
    }
    return x < 0 ? -y : y;
}

/* the 4-pole TPT OTA ladder: input Q15, dac 0..16383, res 0..127 -> Q15 (with headroom) */
JUNO_INL int32_t juno_vcf(juno_vs_t *f, int32_t input, uint32_t dac, uint32_t res)
{
    int32_t in_abs = input < 0 ? -input : input, decay = (f->follow * 32734) >> 15;
    int32_t z0 = f->z[0] >> 9, z1 = f->z[1] >> 9, z2 = f->z[2] >> 9, z3 = f->z[3] >> 9, zs, energy;
    uint32_t code = dac >> 2;
    int32_t g1, k, g1_2, g1_3, G, den, inv, sum_z, S, diff, sat, ota, u, uq;
    int64_t sp;
    int j;
    f->follow = in_abs > decay ? in_abs : decay;
    zs = (z0 < 0 ? -z0 : z0) + (z1 < 0 ? -z1 : z1) + (z2 < 0 ? -z2 : z2) + (z3 < 0 ? -z3 : z3);
    energy = f->follow > zs ? f->follow : zs;
    f->vseed = f->vseed * 196314165u + 907633515u;
    if (energy < 50) {                         /* thermal noise seeds self-oscillation when silent */
        int32_t white = (int32_t)(f->vseed >> 16) - 32768;
        input += (white * (327 / (1 + (energy >> 2)))) >> 15;
    }
    if (code > 4095u)
        code = 4095u;
    g1 = JUNO_VCF_G1[code];
    if (res > 127u)
        res = 127u;
    k = JUNO_RES_K[res];
    if (code > 3500u)                          /* no resonance at Nyquist */
        k = (k * (4095 - (int32_t)code)) / 595;
    g1 = (g1 * (int32_t)JUNO_PEAK_PULL[res]) >> 12;
    if (g1 > 32767)
        g1 = 32767;
    g1_2 = (g1 * g1) >> 15;
    g1_3 = (g1_2 * g1) >> 15;
    G = (g1_2 * g1_2) >> 15;
    den = 4096 + ((k * G) >> 15);
    inv = (4096 * 4096) / (den > 0 ? den : 1);
    sp = (int64_t)z0 * g1_3 + (int64_t)z1 * g1_2 + (int64_t)z2 * g1;
    sum_z = (int32_t)(sp >> 15) + z3;
    S = (int32_t)(((int64_t)(32768 - g1) * sum_z) >> 15);
    diff = (int32_t)(((int64_t)input * 2130) >> 15) - S;
    sat = juno_ota_sat((int32_t)(((int64_t)diff * JUNO_FB_SCALE[res]) >> 12));
    ota = (int32_t)(((int64_t)sat * JUNO_INV_FB_SCALE[res]) >> 12);
    u = (int32_t)(((int64_t)((int32_t)(((int64_t)input * 4653) >> 15) + (int32_t)(((int64_t)k * ota) >> 12)) * inv) >> 12);
    uq = u << 9;
    for (j = 0; j < 4; j++) {
        int32_t delta = (int32_t)(((int64_t)g1 * (uq - f->z[j])) >> 15), y = f->z[j] + delta, nz = y + delta;
        if (nz > 134217727)
            nz = 134217727;
        else if (nz < -134217728)
            nz = -134217728;
        f->z[j] = nz;
        uq = y;
    }
    return (int32_t)(((int64_t)(uq >> 9) * 8796LL) >> 10);   /* x 8.59, the passband's makeup */
}

/* one voice's block before the VCA into y[] */
JUNO_INL void juno_run(juno_vs_t *s, const juno_ctl_t *c, int32_t *y, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t d = juno_dc(juno_dco(s, c), &s->dc_osc);
        if (s->slew == 0)
            s->slew = (int32_t)c->dac;
        else
            s->slew += (int32_t)(((int32_t)c->dac - s->slew) * 1393) >> 15;   /* tau 522 us */
        y[i] = juno_dc(juno_vcf(s, d, (uint16_t)s->slew, c->res), &s->dc_vcf);
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
