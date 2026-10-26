/* SPDX-License-Identifier: MIT */
/* PLAITS: four of Mutable Instruments Plaits' synthesis models in fixed point (engine 17, FELUCCA_ENG_PLAITS, off by
 * default). This file is MIT, as the code it ports; the rest of Optimist is GPL-3.0-only.
 *
 * ---- integer ports of Mutable Instruments' algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2016 Emilie Gillet (emilie.o.gillet@gmail.com).
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
 * DEALINGS IN THE SOFTWARE.
 *
 * From github.com/pichenettes/eurorack (plaits/dsp/engine/waveshaping_engine.cc, additive_engine.cc,
 * grain_engine.cc, particle_engine.cc; oscillator/harmonic_oscillator.h, grainlet_oscillator.h, z_oscillator.h;
 * noise/particle.h; the tables: plaits/resources/lookup_tables.py, here tools/gen_plaits_tables.py ->
 * plaits_tables.h), rewritten in Q15 / Q24 integers with 64-bit products where the range needs them (as Hortator's
 * port of Plaits' drums, drums/synth/drum_plaits.c), no float. The controls each Plaits model reads (HARMONICS,
 * TIMBRE, MORPH) and its two outputs (OUT, AUX) are the same; the differences, all for the CPU or the RAM: the
 * parameters are held for a block (32 samples; Plaits interpolates them per sample), the slope oscillator has no
 * integrated BLEP, the particles' diffuser (MORPH below the middle in Plaits: an 8 K-sample reverb) is left out, so
 * MORPH is the resonance from Plaits' middle up, and the particles draw one random number a sample, not six.
 *
 * MODEL (P_E0), the values stored (a new one is appended):
 *   WAVSH  waveshaping: a slope (triangle .. saw) through five waveshapers and a wavefolder. HARMONICS the shaper,
 *          TIMBRE the fold, MORPH the slope's asymmetry. AUX: a sine through the second (Serge-like) folder
 *   ADDTV  additive: 24 harmonics (a Chebyshev recursion, Q29), their amplitudes a bump spectrum. HARMONICS the
 *          bumps, TIMBRE the brightest harmonic, MORPH from flat and wide to peaked. AUX: the organ's 8 drawbars
 *          (harmonics 1 2 3 4 6 8 10 12) under the same spectrum
 *   GRFMT  granular formant: two grainlets (a sine window a period, a formant synced to it). TIMBRE the formant,
 *          HARMONICS the second formant's interval (-2 .. +2 octaves; below the middle the carrier bleeds in),
 *          MORPH the window's shape. AUX: the Z oscillator (a CZ-like synced formant, HARMONICS its mode)
 *   PRTCL  particles: random impulses into six resonant band-passes, each struck at a random pitch around the
 *          note, through a low-pass at the note. TIMBRE the density, HARMONICS the pitches' spread (up to 4
 *          octaves), MORPH the resonance. AUX: the raw impulses. A note strikes all six at once (Plaits' trigger)
 * The OUT page: AUX crossfades OUT -> AUX; TONE a one-pole low-pass (the track's FLT moves it), LPG how far it closes
 * as the envelope falls (Plaits' low-pass gate colour), VEL how far the velocity opens TIMBRE. ENV 1's cards: FLT on
 * TONE, SHP on TIMBRE (the track's SHP too), a move of MORPH.
 *
 * Polyphony 4 a part (PL_POLY): each voice's state in the part's engine memory (engines.c part_mem, pool), the
 * largest model's ~150 B; a voice silent before its note starts its model afresh, a sounding one keeps its phases. */
#include "plaits_tables.h"

#define PL_POLY 4
#define PL_NH 24                                         /* ADDTV's harmonics */
#define PL_NO 8                                          /* its organ (AUX) */
#define PL_NP 6                                          /* PRTCL's particles (Plaits' kNumParticles) */
#define PL_FMAX 0x40000000u                              /* Plaits' kMaxFrequency: 0.25 of FS */
enum { PLM_WSH, PLM_ADD, PLM_GRN, PLM_PRT, PLM_N };

typedef struct {
    uint8_t model;                                       /* PLM_* the state belongs to */
    uint8_t spare;
    uint8_t trig;                                        /* PRTCL: strike every particle (a new note) */
    uint8_t alive;                                       /* PRTCL: the particles still ringing, bit j */
    int32_t lp;                                          /* TONE's one-pole */
    uint32_t rng;
    union {
        struct { uint32_t ph; } w;
        struct {
            uint32_t ph;
            int32_t amp[PL_NH], org[PL_NO];              /* the spectra, normalized, Q24 */
            int32_t mslope, mbumps, margin;              /* the margin (Q24) of that slope and bumps (0, 0: none yet) */
        } a;
        struct {
            uint32_t cph, fph[2], zc, zd, zf;            /* carrier, the formants; the Z oscillator's three */
            int32_t nxt, znx, dc, zdc;                   /* the BLEPs' next samples, the DC blockers (Q18) */
        } g;
        struct {
            int32_t ic1[PL_NP], ic2[PL_NP];
            tsvf_t c[PL_NP];
            uint16_t pgm[PL_NP];                         /* each one's gain: pgm >> sh */
            uint8_t sh[PL_NP];
            int32_t p1, p2;                              /* the low-pass at the note */
        } p;
    } u;
} pl_voice_t;
AINL pl_voice_t *pl_mem(uint32_t p);                     /* (engines.c, after every engine: the part's engine memory) */

static const char *const N_PLAITS_MODEL[] = {"WAVSH", "ADDTV", "GRFMT", "PRTCL"};
/* the output level of each model, Q12 (the presets' level near ANALOG's: tests/plaits_test.c) */
static const int16_t PL_GAIN[PLM_N] = {4096, 4505, 5120, 4096};

static pl_voice_t *pl_voice(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < PL_POLY ? &pl_mem((uint32_t)(t - trk))[i] : 0;
}

static void pl_reset(pl_voice_t *S, uint32_t md, uint32_t seed)
{
    memset(S, 0, sizeof *S);
    S->model = (uint8_t)md;
    S->rng = seed | 1u;
}

static void plaits_note_on(track_t *t, voice_t *v)
{
    pl_voice_t *S = pl_voice(t, v);
    if (!S)
        return;
    if (!v->env || S->model != (uint32_t)t->p[P_E0] % PLM_N)   /* silent: afresh (sounding: its phases kept) */
        pl_reset(S, (uint32_t)t->p[P_E0] % PLM_N, (v->note + 1u) * 0x9E3779B9u ^ (uint32_t)v->fine * 0x85EBCA6Bu);
    S->trig = 1;
}

/* ---- helpers */
/* 2^x, x Q16 (-16 .. 14 octaves), Q16: the octaves a shift, the fraction a cubic (minimax, 1e-4) */
AINL uint32_t pl_exp2(int32_t x)
{
    int32_t n = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu;
    uint32_t y = 65536u + ((f * (45559u + ((f * (14820u + ((f * 5156u) >> 16))) >> 16))) >> 16);
    return n >= 0 ? y << n : y >> -n;
}
/* the increment of pitch p (1/16 semitone), past the table's top by octaves (up to MIDI ~140) */
static uint32_t pl_inc_raw(int32_t p)
{
    uint32_t sh = 0;
    while (p > 2047 && sh < 2u) {
        p -= 192;
        sh++;
    }
    return pitch_inc(clamp(p, 0, 2047)) << sh;
}
AINL uint32_t pl_inc(int32_t p)
{
    uint32_t i = pl_inc_raw(p);
    return i > PL_FMAX ? PL_FMAX : i;
}
static uint32_t pl_isqrt64(uint64_t x)
{
    uint64_t r = 0, b = 1ull << 62;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (uint32_t)r;
}
AINL int32_t pl_abs(int32_t x) { return x < 0 ? -x : x; }   /* (the firmware links no libc abs) */
/* a 257-point Q15 table at t (Q15, 0..32767), linear */
AINL int32_t pl_lut(const int16_t *tab, int32_t t)
{
    int32_t j = t >> 7, a = tab[j];
    return a + (((tab[j + 1] - a) * ((t & 127) << 8)) >> 15);
}
/* Plaits' Tame(f0, mult, order): (1 - (f0 mult - mf) / (0.5 - mf))^3 clamped to 0..1, Q15; f0 Q24, mult Q12, mf =
 * 0.5 / order (Q24) and k16 = 2^40 / (2^23 - mf) given as constants */
AINL int32_t pl_tame(uint32_t f24, int32_t m12, int32_t mf, int64_t k16)
{
    int64_t fh = ((int64_t)f24 * m12) >> 12, a = 32768 - (((fh - mf) * k16) >> 25);
    int32_t x = a < 0 ? 0 : a > 32768 ? 32768 : (int32_t)a;
    return (int32_t)((((int64_t)x * x >> 15) * x) >> 15);
}
#define PL_T16 524288, 139810                            /* order 16: 0.5 / 16 in Q24, 2^40 / (2^23 - it) */
#define PL_T12 699051, 142987                            /* order 12 */

/* ---- WAVSH (waveshaping_engine.cc) */
static void pl_wsh(pl_voice_t *S, const vmod_t *m, int32_t h, int32_t tb, int32_t mo, int32_t ax, int32_t *y, uint32_t n)
{
    uint32_t f0 = m->inc > PL_FMAX ? PL_FMAX : m->inc, f24 = f0 >> 8, pw, ph = S->u.w.ph, i;
    int32_t slope, sa, saa, inner, waa, sh, si, sf, wfg, og, ru, rd;
    const int16_t *wa, *wb;
    pw = 0x80000000u + (uint32_t)mo * 58982u;            /* 0.5 + 0.45 MORPH, at most 1 - 2 f0 */
    if (pw > 0xFFFFFFFFu - 2u * f0)
        pw = 0xFFFFFFFFu - 2u * f0;
    ru = (int32_t)(0x80000000u / ((pw >> 16) | 1u));     /* the slope's two rates (Q16 phase -> Q15) */
    rd = (int32_t)(0x80000000u / (65536u - (pw >> 16)));
    slope = 12288 + ((pl_abs(mo - 16384) * 5) >> 3);     /* 3 + 5 |MORPH - 0.5|, Q12 */
    sa = pl_abs(h - 16384) * 2;                          /* |HARMONICS - 0.5| 2 */
    saa = pl_tame(f24, slope, PL_T16);
    inner = 12288 + ((((sa * saa) >> 15) * 5) >> 3);
    waa = pl_tame(f24, (slope * inner) >> 12, PL_T12);
    sh = (16384 + (((h - 16384) * saa) >> 15)) * 4;      /* the shapers, 0 .. 4 (Q15 x 4) */
    if (sh > 131071)
        sh = 131071;
    si = sh >> 15, sf = sh & 32767;
    wa = PL_WS[si], wb = PL_WS[si + 1 > 4 ? 4 : si + 1];
    wfg = 983 + ((15073 * ((tb * waa) >> 15)) >> 15);    /* 0.03 + 0.46 TIMBRE (tamed) */
    og = (tb * (65536 - tb)) >> 15;                      /* AUX's overtones: t (2 - t), twice */
    og = (og * (65536 - og)) >> 15;
    for (i = 0; i < n; i++) {
        int32_t s, pos, wi, wf, x, z, tq, out;
        ph += f0;
        s = ph < pw ? (int32_t)(((ph >> 16) * (uint32_t)ru) >> 16) : 32768 - (int32_t)((((ph - pw) >> 16) * (uint32_t)rd) >> 16);
        s = 2 * s - 32768;                               /* -1 .. 1 */
        pos = 127 * s + (128 << 15);
        wi = pos >> 15, wf = pos & 32767;
        if (wi > 255)
            wi = 255, wf = 32767;
        x = wa[wi] + (((wa[wi + 1] - wa[wi]) * wf) >> 15);
        z = wb[wi] + (((wb[wi + 1] - wb[wi]) * wf) >> 15);
        x += ((z - x) * sf) >> 15;
        tq = clamp(16384 + ((x * wfg) >> 15), 0, 32767);
        out = pl_lut(PL_FOLD, tq);
        if (ax) {                                        /* AUX: a sine of the triangle, into the second folder */
            int32_t sn = sine_i(0x80000000u + ((uint32_t)osc_tri(ph) << 15)), f2 = -pl_lut(PL_FOLD2, tq);
            int32_t a = sn + (((f2 - sn) * og) >> 15);
            out += ((a - out) * ax) >> 15;
        }
        y[i] = out;
    }
    S->u.w.ph = ph;
}

/* ---- ADDTV (additive_engine.cc, harmonic_oscillator.h) */
/* n / d in 64 bits, d > 0, shift and subtract over the quotient's bits only (n < d 2^31 at every caller: at most 31
 * steps): the target has no 64-bit divide (no __divdi3 / __udivdi3 in the link). Only when the margin changes */
static uint32_t pl_udiv64(uint64_t n, uint64_t d)
{
    uint32_t q = 0;
    int32_t b;
    for (b = 30; b >= 0; b--)
        if (n >= d << b) {
            n -= d << b;
            q |= 1u << b;
        }
    return q;
}
/* the bump spectrum's margin (1 / slope - 1) / (1 + bumps), Q24, for slope24 (Q24, 0.01 .. 2) and bumps16 (Q16, 0 ..
 * 16): kept with its inputs (S->u.a), so a block that keeps HARMONICS and MORPH divides nothing */
static int32_t pl_margin(pl_voice_t *S, int32_t slope24, int32_t bumps16)
{
    if (S->u.a.mslope != slope24 || S->u.a.mbumps != bumps16) {
        int64_t inv = pl_udiv64(1ull << 48, (uint64_t)slope24);   /* 1 / slope, Q24 (< 2^31: slope >= 0.01) */
        uint64_t a = (uint64_t)(inv < (1 << 24) ? (1 << 24) - inv : inv - (1 << 24));
        int32_t mq = (int32_t)pl_udiv64(a << 16, (uint64_t)(65536 + bumps16));
        S->u.a.margin = inv < (1 << 24) ? -mq : mq;
        S->u.a.mslope = slope24, S->u.a.mbumps = bumps16;
    }
    return S->u.a.margin;
}
/* UpdateAmplitudes: the bump spectrum of num harmonics (the list's i-th: the main's harmonic i + 1, the organ's
 * PL_ORG[i] + 1), each followed by its one-pole (Plaits' 0.001 a 12-sample block = 0.0029 a 32-sample one, Q16 190)
 * and the set normalized with Plaits' 0.001 (Q24). In Q24 with 64-bit products: a peaked spectrum sits where a
 * (1 + sine) nears 0, so a small error in the order is a large one in the gain (Q16 was 10 % off). From zeros (a new
 * note, AUX raised): the set installed at once (its rest), not faded in. slope24 Q24, bumps16 Q16, margin Q24 */
static void pl_spectrum(int32_t *amp, uint32_t num, int32_t cen, int64_t slope24, int64_t bumps16, int64_t margin)
{
    int64_t center, sum = 0, g24[PL_NH];
    uint32_t i, fresh = 1, r, sh;
    int32_t e;
    center = (((int64_t)cen * (((int64_t)(num - 1u) << 24) + margin)) >> 15) - (margin >> 1);
    for (i = 0; i < num; i++) {
        int64_t d = ((int64_t)i << 24) - center, order = ((d < 0 ? -d : d) * slope24) >> 24, g = 0;
        if (order < (1 << 24)) {
            uint32_t b = 0x40000000u + (uint32_t)((order * bumps16) >> 8);   /* 0.25 + order bumps, turns Q32 */
            int64_t bf = 65536 + 2 * (int64_t)sine_i(b);
            g = ((1 << 24) - order) << 1;                /* 2 (1 - order), Q24 */
            g = (g * g) >> 24;
            g = (g * bf) >> 16;
            g = (g * g) >> 24;
            g = (g * g) >> 24;                           /* (at most 4096.0) */
        }
        g24[i] = g;
        fresh &= amp[i] == 0;
    }
    for (i = 0; i < num; i++) {
        int64_t a = fresh ? g24[i] : amp[i] + (((g24[i] - amp[i]) * 190) >> 16);
        g24[i] = a;
        sum += a;
    }
    sum += fresh ? (sum >> 10) : 16777;                  /* (+ 0.001; its share at rest when installed) */
    /* 1 / sum in 32 bits: its top 16 bits s (sum ~ s 2^e), r = 2^32 / s; a / sum 2^24 = a r >> (8 + e) */
    for (e = -2, sh = 0; (sum >> (e + 16)) != 0 && e < 40; e++)
        ;
    sh = (uint32_t)(e >= 0 ? sum >> e : sum << -e);      /* (sum >= 16777 = 2^14: e >= -2) */
    r = 0xFFFFFFFFu / (sh ? sh : 1u);
    for (i = 0; i < num; i++)
        amp[i] = (int32_t)((g24[i] * r) >> (8 + e));
}
static const uint8_t PL_ORG[PL_NO] = {0, 1, 2, 3, 5, 7, 9, 11};   /* the organ's harmonics (Plaits' organ_harmonics) */

static void pl_add(pl_voice_t *S, const vmod_t *m, int32_t h, int32_t tb, int32_t mo, int32_t ax, int32_t *y, uint32_t n)
{
    uint32_t f0 = m->inc > PL_FMAX ? PL_FMAX : m->inc, ph = S->u.a.ph, i, k, nh = 0;
    int64_t rs = ((int64_t)(163840 - 3 * h) * mo * 858993459) >> 32, rs3, slope, bumps;   /* (1 - 0.6 HARMONICS)
                                                         * MORPH, Q30 (/ 5 as x 2^32 / 5: no 64-bit divide) */
    int32_t a[PL_NH], mg;
    rs3 = (((rs * rs) >> 30) * rs) >> 30;
    slope = 167772 + ((rs3 * 2038) >> 16);              /* 0.01 + 1.99 rs^3, Q24 (1.99 / 64 = 2038 / 2^16) */
    bumps = ((int64_t)h * h) >> 10;                      /* 16 HARMONICS^2, Q16 */
    mg = pl_margin(S, (int32_t)slope, (int32_t)bumps);
    pl_spectrum(S->u.a.amp, PL_NH, tb, slope, bumps, mg);
    if (ax)
        pl_spectrum(S->u.a.org, PL_NO, tb, slope, bumps, mg);
    for (k = 0; k < PL_NH; k++)
        a[k] = S->u.a.amp[k] >> 9;                       /* Q15 */
    if (ax) {                                            /* AUX: the organ's spectrum (the same harmonics) */
        for (k = 0; k < PL_NH; k++)
            a[k] -= (a[k] * ax) >> 15;
        for (k = 0; k < PL_NO; k++)
            a[PL_ORG[k]] += ((S->u.a.org[k] >> 9) * ax) >> 15;
    }
    for (k = 0; k < PL_NH; k++) {                        /* each above 1 - 2 f its harmonic: none past Nyquist */
        int32_t aa = 32768 - (int32_t)((f0 >> 16) * (k + 1u));
        a[k] = aa > 0 ? (a[k] * aa) >> 15 : 0;
        if (a[k])
            nh = k + 1u;
    }
    for (i = 0; i < n; i++) {
        int32_t tx, prev = 1 << 29, cur, acc = 0;
        ph += f0;
        tx = sine_i(ph);                                 /* 2 sin, Q14 */
        cur = tx * 16384;                                /* sin, Q29 */
        for (k = 0; k < nh; k++) {
            int32_t nx;
            acc += a[k] * (cur >> 14);
            nx = (int32_t)(((int64_t)tx * cur) >> 14) - prev;
            prev = cur;
            cur = nx;
        }
        y[i] = acc >> 15;
    }
    S->u.a.ph = ph;
}

/* ---- GRFMT (grain_engine.cc, grainlet_oscillator.h, z_oscillator.h) */
typedef struct { int32_t ci, k8, bp, r1, r2; } pl_car_t;
/* the grainlet's window at phase p (Q16 turns, 0 .. 65536), Q15 0 .. 16384 (Carrier()) */
AINL int32_t pl_carrier(const pl_car_t *c, uint32_t p)
{
    uint32_t q;
    if (c->ci == 0) {
        q = (p * (uint32_t)c->k8) >> 8;
        q = (q > 65536u ? 65536u : q) + 49152u;
    } else if (c->ci == 1) {
        q = p < (uint32_t)c->bp ? (p * (uint32_t)c->r1) >> 16 : 32768u + (((p - (uint32_t)c->bp) * (uint32_t)c->r2) >> 16);
        q += 49152u;
    } else {
        q = 16384u + ((p * (uint32_t)c->k8) >> 8);
        q = q > 49152u ? 49152u : q;
    }
    return (sine_i(q << 16) + 32768) >> 2;
}
/* a one-pole DC blocker (Q18 state), g Q15 */
AINL int32_t pl_dc(int32_t *st, int32_t x, int32_t g)
{
    int32_t x18 = x * 8;
    *st += (int32_t)(((int64_t)(x18 - *st) * g) >> 15);
    return (x18 - *st) >> 3;
}
/* the reset's fraction of a sample (Q15): the phase past the wrap over the increment */
AINL int32_t pl_rt(uint32_t past, uint32_t inc) { return (int32_t)(((past >> 14) << 15) / ((inc >> 14) | 1u)); }
/* the Z oscillator's value: c, d, f in Q16 turns (65536 = 1.0, apart from 0), sh MORPH, ps / off its mode's */
AINL int32_t pl_z(int32_t c, int32_t d, int32_t f, int32_t sh, int32_t ps, int32_t off)
{
    int32_t rd = (32768 + sine_i((uint32_t)(d / 2 + 16384) << 16)) >> 1, ds = sine_i((uint32_t)(f + ps) << 16), ct;
    if (sh < 16384) {
        int32_t s2 = sh * 2;
        if (c >= 32768)
            rd = (rd * s2) >> 15;
        ct = 32767 + (((sine_i((uint32_t)(c + 16384) << 16) - 32767) * s2) >> 15);
    } else {
        ct = sine_i((uint32_t)(c + sh) << 16);
    }
    return ((((rd * (off + ds)) >> 15) - off) * ct) >> 15;
}

static void pl_grn(pl_voice_t *S, const vmod_t *m, int32_t h, int32_t tb, int32_t mo, int32_t ax, int32_t *y, uint32_t n)
{
    uint32_t f0 = m->inc > PL_FMAX / 2u ? PL_FMAX / 2u : m->inc, f1, f2, fz, i;
    int32_t bl = h < 16384 ? 32768 - 2 * h : 0, bf, nrm, att, cs, s3, t, t3, after, gdc, ps, off;
    pl_car_t c;
    bf = (bl * (65536 - bl)) >> 15;                      /* the carrier's bleed b (2 - b) */
    nrm = (int32_t)((1u << 30) / (uint32_t)(32768 + bf));   /* 1 / (1 + bleed) */
    f1 = pl_inc(384 + ((1344 * tb) >> 15));              /* the formant: MIDI 24 + 84 TIMBRE */
    f2 = pl_inc(384 + ((1344 * tb) >> 15) - 384 + ((768 * h) >> 15));   /* x 2^((48 HARMONICS - 24) / 12) */
    att = 32768 - 24 * (int32_t)(f0 >> 17);
    att = att < 0 ? 0 : att;
    cs = 10813 + (((mo - 10813) * att) >> 15);           /* 0.33 + (MORPH - 0.33) max(1 - 24 f0, 0) */
    s3 = cs * 3;
    c.ci = s3 >> 15 > 2 ? 2 : s3 >> 15;
    t = s3 - c.ci * 32768;                               /* the fraction */
    if (c.ci < 2)
        t = 32768 - t;
    t3 = (int32_t)(((int64_t)t * t >> 15) * t >> 15);
    c.k8 = c.ci == 0 ? 256 + ((3840 * t3) >> 15) : 128 + ((3712 * t3) >> 15);
    c.bp = 66 + ((32702 * t3) >> 15);                    /* (ci 1: 0.001 + 0.499 t^3) */
    c.r1 = (int32_t)(0x80000000u / (uint32_t)c.bp);
    c.r2 = (int32_t)(0x80000000u / (uint32_t)(65536 - c.bp));
    after = (pl_carrier(&c, 0) * ((bf * nrm) >> 15)) >> 15;   /* a grainlet right after its reset (before: 0) */
    gdc = (int32_t)(((f0 >> 17) * 30870u) >> 15);       /* the DC blocker at 0.3 f0 */
    fz = pl_inc(m->pitch16 + ((1536 * tb) >> 15));      /* AUX: the Z oscillator's formant, the note + 96 TIMBRE */
    if (h < 10912) {                                     /* its mode (HARMONICS): the phase shift (Q16 turns), the offset */
        ps = 16384 + h * 3, off = 32767;
    } else {
        ps = 49119 - (h - 10813) * 3 / 2;
        off = h < 21823 ? -sine_i((uint32_t)ps << 16) : 33;
    }
    for (i = 0; i < n; i++) {
        uint32_t cp = S->u.g.cph + f0, j;
        int32_t out = 0;
        if (ax < 32767) {                                /* OUT: the two grainlets */
            int32_t th = S->u.g.nxt, car;
            S->u.g.nxt = 0;
            if (cp < S->u.g.cph) {                       /* the carrier's reset: the formants synced, a BLEP */
                int32_t rt = pl_rt(cp, f0), u = 32768 - rt, d = 2 * after;
                th += (d * ((rt * rt) >> 15)) >> 16;
                S->u.g.nxt -= (d * ((u * u) >> 15)) >> 16;
                S->u.g.fph[0] = (uint32_t)(((uint64_t)f1 * (uint32_t)rt) >> 15);
                S->u.g.fph[1] = (uint32_t)(((uint64_t)f2 * (uint32_t)rt) >> 15);
            } else {
                S->u.g.fph[0] += f1;
                S->u.g.fph[1] += f2;
            }
            car = pl_carrier(&c, cp >> 16);
            for (j = 0; j < 2u; j++)
                S->u.g.nxt += (car * (((sine_i(S->u.g.fph[j]) + bf) * nrm) >> 15)) >> 15;
            out = pl_dc(&S->u.g.dc, th, gdc);
        }
        S->u.g.cph = cp;
        if (ax) {                                        /* AUX: the Z oscillator */
            uint32_t zd = S->u.g.zd + 2u * f0, zc = S->u.g.zc + f0;
            int32_t th = S->u.g.znx, z;
            S->u.g.znx = 0;
            if (zd < S->u.g.zd) {
                int32_t rt = pl_rt(zd, 2u * f0), u = 32768 - rt, wr = zc < S->u.g.zc;
                int32_t fb = (int32_t)((S->u.g.zf + (uint32_t)(((uint64_t)fz * (uint32_t)u) >> 15)) >> 16);
                int32_t bef = pl_z(wr ? 65536 : 32768, 65536, fb, mo, ps, off), aft = pl_z(wr ? 0 : 32768, 0, 0, mo, ps, off);
                int32_t dd = aft - bef;
                th += (dd * ((rt * rt) >> 15)) >> 16;
                S->u.g.znx -= (dd * ((u * u) >> 15)) >> 16;
                S->u.g.zf = (uint32_t)(((uint64_t)fz * (uint32_t)rt) >> 15);
                if (wr)
                    zc = zd >> 1;
            } else {
                S->u.g.zf += fz;
            }
            S->u.g.zd = zd, S->u.g.zc = zc;
            S->u.g.znx += pl_z((int32_t)(zc >> 16), (int32_t)(zd >> 16), (int32_t)(S->u.g.zf >> 16), mo, ps, off);
            z = pl_dc(&S->u.g.zdc, th, gdc);
            out += ((z - out) * ax) >> 15;
        }
        y[i] = out;
    }
}

/* ---- PRTCL (particle_engine.cc, particle.h) */
/* particle j struck by an impulse of s15 at pitch p16: its band-pass and gain (pre_gain = 0.5 / sqrt(q f sqrt(density)),
 * from the block's pg0 at the note by the pitch's offset d16: f^-1/2) */
static void pl_strike(pl_voice_t *S, uint32_t j, int32_t p16, int32_t d16, int32_t k12, int32_t pg0)
{
    int32_t cut, pg = (int32_t)(((int64_t)pg0 * pl_exp2(-(d16 * 512) / 3)) >> 16), sh = 12;
    p16 = clamp(p16 + d16, 0, 1996);                     /* (at most 0.25 of FS: 11 kHz) */
    cut = clamp(((p16 - 361) * 4785) >> 8, 0, 127 << 8);
    tsvf_coef_k(&S->u.p.c[j], cut, k12);
    while (pg > 8191 && sh > 3)                         /* (sh >= 3: six terms of a clamped band-pass fit an int32) */
        pg >>= 1, sh--;
    S->u.p.pgm[j] = (uint16_t)(pg > 8191 ? 8191 : pg);
    S->u.p.sh[j] = (uint8_t)sh;
    S->alive |= (uint8_t)(1u << j);
}

static void pl_prt(pl_voice_t *S, const vmod_t *m, int32_t h, int32_t tb, int32_t mo, int32_t ax, int32_t *y, uint32_t n)
{
    uint32_t dinc = pl_inc_raw(960 + ((1152 * ((tb * tb) >> 15)) >> 15)), thr, f0, sd, i, j;
    int32_t spr = (768 * ((h * h) >> 15)) >> 15, k12, pg0;
    uint64_t q16, x;
    tsvf_t post;
    thr = (uint32_t)(((uint64_t)dinc * dinc) >> 32);    /* any of the six this sample: 6 density = density_sqrt^2 */
    sd = (uint32_t)(((uint64_t)dinc * 26755u) >> 16);   /* sqrt(density) = density_sqrt / sqrt(6) */
    q16 = pl_exp2(mo * 10);                              /* q = 0.5 + (2^(5 MORPH))^2, at most 256 */
    q16 = 32768u + ((q16 * q16) >> 16);
    if (q16 > (256u << 16))
        q16 = 256u << 16;
    k12 = (int32_t)((1u << 28) / (uint32_t)q16);         /* the damping 1 / q, Q12 (q16 <= 2^24: 32 bits) */
    f0 = m->inc > PL_FMAX ? PL_FMAX : m->inc;
    x = ((q16 * (f0 >> 16)) * (sd >> 16)) >> 16;         /* q f0 sqrt(density), Q32 */
    {
        uint32_t s16 = pl_isqrt64(x);
        pg0 = (int32_t)((1u << 27) / (s16 ? s16 : 1u)); /* 0.5 / sqrt(), Q12 */
    }
    tsvf_coef_k(&post, clamp(((m->pitch16 - 361) * 4785) >> 8, 0, 127 << 8), 8192);   /* the low-pass at the note, q 0.5 */
    for (i = 0; i < n; i++) {
        uint32_t r = noise32((int32_t *)&S->rng);
        int32_t imp = 0, sum = 0, o, ia = 0;
        uint32_t ji = PL_NP;                             /* the particle struck this sample (PL_NP: none; all: trig) */
        if (S->trig) {                                   /* a note: all six at once, s = 1 */
            S->trig = 0;
            for (j = 0; j < PL_NP; j++) {
                uint32_t u = noise32((int32_t *)&S->rng);
                pl_strike(S, j, m->pitch16, (spr * ((int32_t)(u >> 16) - 32768)) >> 15, k12, pg0);
            }
            imp = 32767, ia = 131068, ji = PL_NP + 1u;
        } else if (r < thr) {
            uint32_t u = noise32((int32_t *)&S->rng);
            ji = ((u >> 16) * PL_NP) >> 16;
            imp = (int32_t)(u & 0x7FFFu) + 1;            /* s: uniform in (0, 1] */
            ia = imp * 4;
            pl_strike(S, ji, m->pitch16, (spr * ((int32_t)((r ^ u) & 0xFFFFu) - 32768)) >> 15, k12, pg0);
        }
        for (j = 0; j < PL_NP; j++)
            if ((S->alive >> j) & 1u) {
                int32_t bp;
                tsvf_tick(&S->u.p.c[j], j == ji || ji > PL_NP ? ia : 0, &S->u.p.ic1[j], &S->u.p.ic2[j], &bp);
                sum += (bp * (int32_t)S->u.p.pgm[j]) >> S->u.p.sh[j];
            }
        o = tsvf_lp(&post, clamp(sum >> 2, -100000, 100000), &S->u.p.p1, &S->u.p.p2);
        o = clamp(o, -32767, 32767);
        if (ax)
            o += ((imp - o) * ax) >> 15;
        y[i] = o;
    }
    for (j = 0; j < PL_NP; j++)                          /* the rung-out ones: off (no work, no DC) */
        if (((S->alive >> j) & 1u) && pl_abs(S->u.p.ic1[j]) < 4 && pl_abs(S->u.p.ic2[j]) < 4) {
            S->u.p.ic1[j] = S->u.p.ic2[j] = 0;
            S->alive &= (uint8_t)~(1u << j);
        }
}

static void plaits_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    pl_voice_t *S = pl_voice(t, v);
    uint32_t md = (uint32_t)p[P_E0] % PLM_N, i;
    int32_t y[CTL], h, tb, mo, ax, cut, gl = 0, gain;
    if (!S)
        return;
    if (n > CTL)
        n = CTL;
    if (S->model != md) {                                /* MODEL changed under the note: that model afresh */
        uint8_t tr = S->trig;
        pl_reset(S, md, S->rng);
        S->trig = tr;
    }
    h = clamp(p[P_E1] * 258, 0, 32767);
    tb = clamp(p[P_E2] * 258 + m->shape - (64 << 8) + p[P_E7] * v->vel * 2, 0, 32767);
    mo = clamp(p[P_E3] * 258, 0, 32767);
    ax = clamp(p[P_E4] * 258, 0, 32767);
    switch (md) {
    case PLM_WSH: pl_wsh(S, m, h, tb, mo, ax, y, n); break;
    case PLM_ADD: pl_add(S, m, h, tb, mo, ax, y, n); break;
    case PLM_GRN: pl_grn(S, m, h, tb, mo, ax, y, n); break;
    default: pl_prt(S, m, h, tb, mo, ax, y, n); break;
    }
    /* TONE (+ FLT), closed by LPG as the envelope falls: a one-pole (TPT), bypassed open */
    cut = clamp((p[P_E5] << 8) + m->cutoff - ((p[P_E6] * (32767 - clamp(m->envq15, 0, 32767))) >> 7), 0, 127 << 8);
    if (cut < (127 << 8)) {
        int32_t g = SVF_G[cut >> 8];
        g += ((SVF_G[(cut >> 8) + 1] - g) * (cut & 255)) >> 8;
        gl = (int32_t)(((uint32_t)g << 15) / (uint32_t)(4096 + g));
    }
    gain = PL_GAIN[md];
    for (i = 0; i < n; i++) {
        int32_t x = y[i];
        if (gl) {
            int32_t d = ((x - S->lp) * gl) >> 15;
            x = d + S->lp;
            S->lp = x + d;
        } else {
            S->lp = x;
        }
        x = soft_knee((x * gain) >> 12, 24000);
        out[i] += mulq15(mulq15(x, amp_at(m, i)), VOICE_FS);
    }
}

/* {MODEL, HARMONICS, TIMBRE, MORPH, AUX, TONE, LPG, VEL} */
static const preset_t PLAITS_PRESETS[] = {
    {"FOLD BASS", {PLM_WSH, 64, 60, 24, 0, 112, 30, 30}, {0, 70, 70, 30}, 16, 1, FX(0, 0, 10, 20)},
    {"TIDES LEAD", {PLM_WSH, 30, 86, 84, 0, 122, 0, 20}, {2, 80, 100, 40}, 0, 1, FX(0, 20, 30, 30)},
    {"SERGE BELL", {PLM_WSH, 96, 70, 64, 127, 120, 60, 10}, {0, 90, 0, 80}, 0, 0, FX(0, 0, 20, 50)},
    {"ADD ORGAN", {PLM_ADD, 16, 40, 50, 127, 127, 0, 0}, {4, 60, 120, 30}, 0, 0, FX(0, 30, 0, 40)},
    {"SPECTRA", {PLM_ADD, 64, 52, 96, 0, 120, 0, 30}, {60, 90, 110, 80}, 0, 0, FX(0, 20, 20, 70)},
    {"HARM BELL", {PLM_ADD, 100, 84, 110, 0, 127, 70, 20}, {0, 90, 0, 90}, 0, 0, FX(0, 0, 20, 60)},
    {"VOSIM VOX", {PLM_GRN, 50, 62, 24, 0, 110, 0, 0}, {10, 70, 110, 50}, 0, 1, FX(0, 20, 20, 40)},
    {"FORMANT SWP", {PLM_GRN, 90, 36, 70, 0, 120, 30, 40}, {0, 80, 60, 50}, 30, 0, FX(0, 0, 20, 40)},
    {"Z RESO", {PLM_GRN, 40, 70, 40, 127, 110, 40, 20}, {0, 70, 50, 40}, 0, 1, FX(0, 0, 20, 30)},
    {"DROPLETS", {PLM_PRT, 30, 50, 100, 0, 127, 0, 0}, {0, 100, 127, 90}, 0, 0, FX(0, 0, 30, 70)},
    {"RAIN GLASS", {PLM_PRT, 90, 76, 112, 0, 120, 0, 0}, {20, 100, 127, 100}, 0, 0, FX(0, 20, 30, 80)},
    {"GRIT DUST", {PLM_PRT, 60, 92, 30, 50, 100, 0, 0}, {0, 90, 110, 60}, 0, 0, FX(0, 0, 10, 50)},
};

static const engine_t ENG_PLAITS = {
    .name = "PLAITS",
    .page_title = {"MODEL", "OUT"},
    .edit = {
        PN("MODEL", F_ENUM, 0, PLM_N - 1, 0, N_PLAITS_MODEL),
        PU("HARM", F_PCT, 0, 127, 64, 0),
        PU("TIMB", F_PCT, 0, 127, 64, 0),
        PU("MORPH", F_PCT, 0, 127, 64, 0),
        PU("AUX", F_PCT, 0, 127, 0, 0),
        PU("TONE", F_PCT, 0, 127, 127, 0),
        PU("LPG", F_PCT, 0, 127, 0, 0),
        PU("VEL", F_PCT, 0, 127, 0, 0),
    },
    .presets = PRESETS_PTR(PLAITS_PRESETS),
    .npresets = sizeof(PLAITS_PRESETS) / sizeof(PLAITS_PRESETS[0]),
    .note_on = plaits_note_on,
    .render = plaits_render,
    .fil_page = 1,
    .color = 0x2DD1,                             /* #2fba8c (tools/colors.json) */
    .macro = {P_E1, P_E2, P_E3, P_E5},           /* HARMONICS TIMBRE MORPH TONE (Plaits' three knobs) */
    .poly = PL_POLY,
};
