/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (DeadActive/hortator 7e34159: firmware/src/dm_dsp.c, dm_kick.c, dm_snare.c) */
/* PLAITS drums (builder item DS_PLAITS; drum_synth.c DW_PKICK / DW_PSNARE): two of Mutable Instruments Plaits' drum
 * models in Q24 integers, as Hortator ported them: KPUNC, the synthetic bass drum (a distorted sine, FM and pitch
 * envelopes, a click) and SCRAK, the synthetic snare (two distorted sines with FM, filtered noise, a hold). A kit
 * sound (dsnd_t) drives them: its note (pitch, fine; the lane's TUNE), DECAY (the DECAY_K index read as the model's
 * DECAY knob, 0..127), the filter cutoff index (fcut) as TONE (KPUNC) or FM (SCRAK), CLICK as KPUNC's FM, the noise
 * level (nlev) as SCRAK's SNAP; the hit's velocity is the model's accent. The drum EDIT moves them as on the other
 * sounds (TUNE, DECAY, CUT +, CLICK, SNAP); DRIVE, the kit's crush and LEVEL come after, as on every sound.
 *
 * ---- integer ports of Mutable Instruments' drum algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com).
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE. */

/* ---- Q24 toolkit (Hortator dm_dsp.c, the part these two models use). 1.0 = 1 << 24, range +-128; products and
 * quotients in 64 bits. Q24(x) is for constants only: the compiler folds it, no float reaches the firmware. */
#define QONE (1 << 24)
#define Q24(x) ((int32_t)((x) * 16777216.0 + ((x) < 0 ? -0.5 : 0.5)))
typedef struct { int32_t g, r, h, s1, s2; } qsvf_t;     /* stmlib Svf: coefficients, two states */
typedef struct { int32_t g, gi, s; } qpole_t;           /* stmlib OnePole */
AINL int32_t qm(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 23)) >> 24); }
AINL int32_t qdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 24) / b); }
/* x * k for a decay (k < 1), rounded; where rounding stops the decay it steps one LSB toward 0 instead */
AINL int32_t qdecay(int32_t x, int32_t k)
{
    int32_t y = qm(x, k);
    return y != x ? y : x > 0 ? x - 1 : x < 0 ? x + 1 : 0;
}
AINL int32_t qabs(int32_t x) { return x < 0 ? -x : x; }
AINL int32_t qmin(int32_t a, int32_t b) { return a < b ? a : b; }
AINL int32_t qmax(int32_t a, int32_t b) { return a > b ? a : b; }
AINL int32_t qlim(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
AINL int32_t qknob(int32_t p) { return (int32_t)(((int64_t)qlim(p, 0, 127) << 24) / 127); }   /* 0..127 as 0..1 */
/* v * 2^(st / 12), st in Q24 semitones; saturates at +-2^31 */
static int32_t qratio(int32_t v, int32_t st)
{
    int64_t oct = ((int64_t)st * 1398101) >> 24;          /* st / 12 in Q24 (1398101 = 2^24 / 12) */
    int32_t n = (int32_t)(oct >> 24), f = (int32_t)(oct & 0xFFFFFF);   /* floor octave, fraction Q24 */
    int64_t m = 22370;                                     /* 2^f = sum (f ln 2)^k / k!, k <= 5 (0.3 cent) */
    int64_t y;
    m = 161365 + ((m * f) >> 24);
    m = 931204 + ((m * f) >> 24);
    m = 4030332 + ((m * f) >> 24);
    m = 11629080 + ((m * f) >> 24);
    m = QONE + ((m * f) >> 24);
    y = ((int64_t)v * m) >> 24;
    if (n >= 0)
        y = n > 30 ? (y ? (y > 0 ? INT32_MAX : INT32_MIN) : 0) : y << n;
    else
        y = n < -40 ? 0 : y >> -n;
    return y > INT32_MAX ? INT32_MAX : y < INT32_MIN ? INT32_MIN : (int32_t)y;
}
/* stmlib tan approximations of pi f (f: Q24 normalized frequency) */
AINL int32_t qtan_fast(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x);
    return qm(x, QONE + qm(x2, Q24(0.3260) + qm(Q24(0.1823), x2)));
}
static int32_t qtan_acc(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x), p;
    p = Q24(9.5168091e-03);
    p = Q24(2.900525e-03) + qm(p, x2);
    p = Q24(5.33740603e-02) + qm(p, x2);
    p = Q24(1.333923995e-01) + qm(p, x2);
    p = Q24(3.333314036e-01) + qm(p, x2);
    return qm(x, QONE + qm(p, x2));
}
/* atan(x) in radians (Q24), x >= 0 (Abramowitz & Stegun 4.4.49 on [0, 1]; above 1: pi / 2 - atan(1 / x)) */
static int32_t qatan(int32_t x)
{
    int32_t inv = x > QONE, t = inv ? qdiv(QONE, x) : x, t2 = qm(t, t), p;
    p = Q24(0.0028662257);
    p = Q24(-0.0161657367) + qm(p, t2);
    p = Q24(0.0429096138) + qm(p, t2);
    p = Q24(-0.0752896400) + qm(p, t2);
    p = Q24(0.1065626393) + qm(p, t2);
    p = Q24(-0.1420889944) + qm(p, t2);
    p = Q24(0.1999355085) + qm(p, t2);
    p = Q24(-0.3333314528) + qm(p, t2);
    p = qm(t, QONE + qm(p, t2));
    return inv ? Q24(1.57079632679) - p : p;
}
/* the 44.1 kHz coefficient of a stmlib filter Plaits sets at f48 (normalized at 48 kHz, <= 0.5): the same cutoff in Hz */
static int32_t qtan48(int32_t f48)
{
    int32_t a = qm(qatan(qtan_fast(f48)), Q24(48000.0 / 44100.0 / 3.14159265358979)), b, t;   /* the 44.1 kHz f */
    b = a <= QONE / 4 ? a : QONE / 2 - a;
    t = qtan_acc(b / 2);                                   /* tan(pi b) = 2 t / (1 - t^2): the polynomial below pi / 8 */
    t = qdiv(2 * t, QONE - qm(t, t));
    return a <= QONE / 4 ? t : qdiv(QONE, t);
}
static void qsvf_set(qsvf_t *f, int32_t g, int32_t r)  /* stmlib Svf (trapezoidal, Simper) */
{
    int64_t d = (int64_t)QONE + qm(r, g) + qm(g, g);   /* 1 / (1 + r g + g^2) */
    f->g = g;
    f->r = r;
    f->h = d <= INT32_MAX ? (int32_t)((1LL << 48) / (int32_t)d) : (int32_t)((1LL << 44) / (int32_t)(d >> 4));
}
AINL int32_t qsvf_tick(qsvf_t *f, int32_t in, int32_t *lp, int32_t *bp)   /* one sample: hp, *lp, *bp */
{
    int32_t hp = qm(in - qm(f->r + f->g, f->s1) - f->s2, f->h), b, l;
    b = qm(f->g, hp) + f->s1;
    f->s1 = qm(f->g, hp) + b;
    l = qm(f->g, b) + f->s2;
    f->s2 = qm(f->g, b) + l;
    *lp = l;
    *bp = b;
    return hp;
}
static void qpole_set(qpole_t *p, int32_t g) { p->g = g; p->gi = qdiv(QONE, QONE + g); }   /* stmlib OnePole */
AINL int32_t qpole_lp(qpole_t *p, int32_t in)
{
    int32_t lp = qm(qm(p->g, in) + p->s, p->gi);
    p->s = qm(p->g, in - lp) + lp;
    return lp;
}
AINL int32_t qrand(uint32_t *st)                       /* stmlib Random: the same LCG, per voice; Q24 in [0, 1) */
{
    *st = *st * 1664525u + 1013904223u;
    return (int32_t)(*st >> 8);
}
static int32_t qsqrt(int32_t x)                         /* sqrt of x (Q24, >= 0) */
{
    uint64_t v = (uint64_t)(x < 0 ? 0 : x) << 24, r = 0, b = 1ULL << 62;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (int32_t)r;
}
/* MIDI note -> normalized frequency at 44.1 kHz (Q24); its 48 kHz equivalent (f * 44100 / 48000) */
static int32_t qnote(int32_t note) { return qratio(Q24(8.17579891564 / 44100.0 * 64.0), note << 24) >> 6; }
AINL int32_t q48(int32_t f) { return qm(f, Q24(44100.0 / 48000.0)); }
/* per-sample coefficient c of Plaits (48 kHz) at 44.1 kHz: 1 - (1 - c)^(48000 / 44100) (literals) */
#define C44_50 Q24(0.5297289)
#define C44_10 Q24(0.1083469)
#define C44_05 Q24(0.0542996)
#define C44_04 Q24(0.0434595)
#define C44_005 Q24(0.005441)
#define C44_002 Q24(0.0021767)
AINL int32_t qsine(int32_t ph) { return sine_i((uint32_t)ph << 8) * 512; }   /* sin(2 pi ph), any phase */

/* ---- the models' states (one union in dsv_t: drum_synth.c) */
typedef struct {
    int32_t f0, dirt, fm_amt, fm_dec, body_dec, tone_f, tlevel;
    int32_t phase, pnoise, fm, fm_lp, body, body_lp, trans, trans_lp, tone_lp;
    int32_t c_lp, c_hp, n_lp, n_hp;
    int32_t bpw, fpw;
    uint32_t rng;
    qsvf_t click;
} kpunc_t;
typedef struct {
    int32_t f0, fm_amt, ddec, sdec, dlvl, slvl, rna;
    int32_t ph0, ph1, damp, samp, fm;
    int32_t hold, t;
    uint32_t rng;
    qpole_t dlp, shp;
    qsvf_t slp;
} scrak_t;
typedef union { kpunc_t kp; scrak_t sc; } dplaits_t;

/* ---- KPUNC: TUNE DECAY TONE FM; synthetic bass drum (Plaits SyntheticBassDrum) */
static void kpunc_start(kpunc_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    int32_t h = qknob(chr), morph = qknob(decay), timbre = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), dec = qm(morph, morph), fed = qmax(2 * h - QONE, 0), lvl;
    k->f0 = f0;
    k->dirt = qm(Q24(0.4) - qm(Q24(0.25), dec), qmax(QONE - 8 * q48(f0), 0));
    k->fm_amt = qm(qmin(2 * h, QONE), Q24(3.5));
    fed = qm(fed, fed);
    /* 1 - 1 / (0.008 (1 + 4 fed) SR) */
    k->fm_dec = QONE - (int32_t)((1LL << 40) / (int32_t)(((int64_t)23121101 * (QONE + 4 * fed)) >> 24));   /* 352.8 = 0.008 SR, Q16 */
    k->body_dec = QONE - qratio(Q24(1.0 / (0.02 * FS)), -qm(dec, Q24(60.0)));
    k->tone_f = qmin(qratio(4 * f0, qm(timbre, Q24(108.0))), QONE);
    k->tlevel = timbre;
    lvl = Q24(0.3) + qm(Q24(0.7), acc);
    k->fm = QONE;
    k->body = k->trans = lvl;
    k->phase = k->pnoise = k->fm_lp = k->body_lp = k->trans_lp = k->tone_lp = 0;
    k->c_lp = k->c_hp = QONE;                             /* the click's rest state: its input is 1 between hits */
    k->n_lp = k->n_hp = QONE / 2;                         /* the noise filters at rest: the noise's mean */
    k->bpw = FS / 1000;                                   /* 1 ms */
    k->fpw = (int32_t)(FS * 0.0013);                      /* 1.3 ms */
    k->rng = seed;
    qsvf_set(&k->click, qtan_fast(Q24(5000.0 / FS)), Q24(0.5));
    k->click.s1 = k->click.s2 = 0;
}
AINL int32_t kpunc_dsine(int32_t ph, int32_t pn, int32_t dirt)   /* DistortedSine */
{
    int32_t x = ph + qm(pn, dirt), p = x >= 0 ? x & (QONE - 1) : -((-x) & (QONE - 1)), tri, s;   /* x - trunc(x) */
    tri = (p < QONE / 2 ? p : QONE - p) * 4 - QONE;
    s = qdiv(2 * tri, QONE + qabs(tri));
    return s + qm(QONE - dirt, qsine(p + Q24(0.75)) - s);
}
static HOT int32_t kpunc_tick(kpunc_t *k)
{
    static const int32_t TDEC = Q24(1.0 - 1.0 / (0.005 * FS));
    int32_t mix, s, g, lp, bp, tr;
    k->pnoise += qm(C44_002, qrand(&k->rng) - QONE / 2 - k->pnoise);
    if (k->fpw) {
        k->fpw--;
        k->phase = QONE / 4;
    } else {
        k->fm = qdecay(k->fm, k->fm_dec);
        k->phase += qmin(k->f0 + qm(k->f0, qm(k->fm_amt, k->fm_lp)), QONE / 2);
        if (k->phase >= QONE)
            k->phase -= QONE;
    }
    if (k->bpw) {
        k->bpw--;
    } else {
        k->body = qdecay(k->body, k->body_dec);
        k->trans = qdecay(k->trans, TDEC);
    }
    k->body_lp += qm(C44_10, k->body - k->body_lp);
    k->trans_lp += qm(C44_10, k->trans - k->trans_lp);
    k->fm_lp += qm(C44_10, k->fm - k->fm_lp);
    s = (k->bpw ? 0 : QONE) - k->c_lp;                       /* click: SLOPE, ONE_POLE, Svf LP */
    k->c_lp += qm(s > 0 ? C44_50 : C44_10, s);
    k->c_hp += qm(C44_04, k->c_lp - k->c_hp);
    qsvf_tick(&k->click, k->c_lp - k->c_hp, &lp, &bp);
    k->n_lp += qm(C44_05, qrand(&k->rng) - k->n_lp);         /* attack noise */
    k->n_hp += qm(C44_005, k->n_lp - k->n_hp);
    tr = lp + k->n_lp - k->n_hp;
    g = k->body_lp;                                          /* TransistorVCA */
    s = qm(kpunc_dsine(k->phase, k->pnoise, k->dirt) - Q24(0.6), g);
    mix = -(qdiv(3 * s, 2 * QONE + qabs(s)) + qm(g, Q24(0.3)));
    mix -= qm(qm(tr, k->trans_lp), k->tlevel);
    k->tone_lp += qm(k->tone_f, mix - k->tone_lp);
    return k->tone_lp;
}

/* ---- SCRAK: TUNE DECAY FM SNAP; synthetic snare (Plaits SyntheticSnareDrum) */
static void scrak_start(scrak_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    int32_t sn = qknob(chr), d = qknob(decay), fa = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), dxt = qm(d, QONE + qm(d, d - QONE)), r;
    fa = qm(fa, fa);
    k->f0 = f0;
    k->fm_amt = 4 * fa;
    k->ddec = QONE - qratio(Q24(1.0 / (0.015 * FS)), -qm(dxt, Q24(72.0)) - qm(fa, Q24(12.0)) + qm(sn, Q24(7.0)));
    k->sdec = QONE - qratio(Q24(1.0 / (0.01 * FS)), -qm(d, Q24(60.0)) - qm(sn, Q24(7.0)));
    sn = qlim(qm(sn, Q24(1.1)) - Q24(0.05), 0, QONE);
    k->dlvl = qsqrt(QONE - sn);
    k->slvl = qsqrt(sn);
    r = qlim(qm(Q24(0.125) - q48(f0), Q24(8.0)), 0, QONE);
    k->rna = qm(qm(r, r), fa);
    qpole_set(&k->shp, qtan_fast(qmin(10 * f0, QONE / 2)));
    qsvf_set(&k->slp, qtan48(qmin(35 * q48(f0), QONE / 2)), qdiv(QONE, Q24(0.5) + 2 * sn));   /* up to Nyquist: in Hz */
    qpole_set(&k->dlp, qtan_fast(3 * f0));
    k->dlp.s = k->shp.s = k->slp.s1 = k->slp.s2 = 0;
    k->samp = k->damp = Q24(0.3) + qm(Q24(0.7), acc);
    k->fm = QONE;
    k->ph0 = k->ph1 = 0;
    k->hold = (int32_t)(((int64_t)(Q24(0.04) + qm(d, Q24(0.03))) * FS) >> 24);
    k->t = 0;
    k->rng = seed;
}
AINL int32_t scrak_dsine(int32_t p)
{
    int32_t tri = (p < QONE / 2 ? p : QONE - p) * 4 - Q24(1.3);
    return qdiv(2 * tri, QONE + qabs(tri));
}
static HOT int32_t scrak_tick(scrak_t *k)
{
    static const int32_t FDEC = Q24(1.0 - 1.0 / (0.007 * FS));
    int32_t rn, f, drum, noise, snare, lp, bp;
    if (k->damp > Q24(0.03) || (k->t & 1))
        k->damp = qdecay(k->damp, k->ddec);
    if (k->hold)
        k->hold--;
    else
        k->samp = qdecay(k->samp, k->sdec);
    k->fm = qdecay(k->fm, FDEC);
    k->t++;
    rn = (k->ph0 > QONE / 2 ? -QONE : QONE) + (k->ph1 > QONE / 2 ? -QONE : QONE);
    rn = qm(rn, qm(k->rna, Q24(0.025)));
    f = k->f0 + qm(k->f0, qm(k->fm_amt, k->fm));
    k->ph0 += f;
    k->ph1 += qm(f, Q24(1.47));
    if (k->rna > Q24(0.1)) {
        if (k->ph0 >= QONE + rn)
            k->ph0 = QONE - k->ph0;
        if (k->ph1 >= QONE + rn)
            k->ph1 = QONE - k->ph1;
    } else {
        if (k->ph0 >= QONE)
            k->ph0 -= QONE;
        if (k->ph1 >= QONE)
            k->ph1 -= QONE;
    }
    drum = -Q24(0.1) + qm(scrak_dsine(k->ph0), Q24(0.60)) + qm(scrak_dsine(k->ph1), Q24(0.25));
    drum = qpole_lp(&k->dlp, qm(qm(drum, k->damp), k->dlvl));
    noise = qrand(&k->rng);
    qsvf_tick(&k->slp, noise, &lp, &bp);
    snare = lp - qpole_lp(&k->shp, lp);                      /* OnePole high-pass */
    snare = qm(qm(snare + Q24(0.1), k->samp + k->fm), k->slvl);
    return snare + drum;
}

/* ---- the kit sound's hit and its samples (drum_synth.c ds_on_snd / ds_render) */
#define DP_QUIET 4228                                        /* -72 dB re 1.0 (Q24): a quiet block */
#define DP_QBLOCKS 64u                                       /* quiet blocks in a row that end a hit (46 ms) */
#define DP_LIFE ((uint32_t)FS * 6u)                          /* and every hit ends at 6 s */
static void dp_on(dplaits_t *p, const dsnd_t *d, int32_t base16, uint32_t vel, uint32_t seed)
{
    int32_t note = clamp((base16 + 8) >> 4, 0, 127);
    if (d->wave == DW_PKICK)
        kpunc_start(&p->kp, note, d->fcut, d->decay, d->click, (int32_t)vel, seed);
    else
        scrak_start(&p->sc, note, d->fcut, d->decay, d->nlev, (int32_t)vel, seed);
}
/* n samples (Q15-ish, as ds_render's x) into x[]; the peak of the block (Q24) */
static HOT __attribute__((noinline)) int32_t dp_render(dplaits_t *p, uint32_t wave, int32_t *x, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0, y;
    for (i = 0; i < n; i++) {
        y = wave == DW_PKICK ? kpunc_tick(&p->kp) : scrak_tick(&p->sc);
        pk = qmax(pk, qabs(y));
        x[i] = clamp((y + 256) >> 9, -4 * 32768, 4 * 32768);   /* (Q24 1.0 -> 32768) */
    }
    return pk;
}
