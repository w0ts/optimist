/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012 Google Inc. (msfa: music-synthesizer-for-android)
 * Copyright 2016-2025 Pascal Gauthier (Dexed's msfa)
 * Copyright 2026 SLOOP DX7 port: C, fixed 32-sample blocks, operators 7 and 8
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing permissions and limitations.
 *
 * A C port of msfa, the DX7 core of Dexed (Source/msfa: dx7note, env, pitchenv, lfo, fm_core,
 * fm_op_kernel, sin, exp2, freqlut, and the VMEM unpack of Dexed's PluginData). Integer only, as msfa.
 * Changes from msfa / Dexed:
 *  - blocks of DX7_N = 32 samples (msfa: 64); every per-block rate is computed for 32 (the tables
 *    come from tools/gen_dx7_tables.py, exactly as msfa computes them at start-up);
 *  - no floating point at run time: Dexed's DETUNE (a double per note) is a table per MIDI note, its
 *    amplitude modulation (exp() per block) goes through the exp2 table;
 *  - no tuning tables, MTS, MPE, portamento, pitch bend or controllers (the SLOOP voice gives the
 *    pitch offset: glide, tune, LFO);
 *  - OPERATORS 7 AND 8: a voice is a DX7 voice (ops 1..6, unchanged: a DX7 patch sounds the same)
 *    plus two optional operators with the full DX7 operator parameters, off by default (EXT = OFF).
 *    EXT routes them: 8>7 OUT (an extra 2-op carrier stack), 7+8 OUT (two extra carriers), 8>7>OPn
 *    (the stack modulates DX7 operator n), 7+8>OPn (both modulate operator n). OP8 has its own
 *    feedback (0..7). They are rendered first, so they can modulate any DX7 operator.
 *
 * Voice data (DX7_VSIZE bytes): the 155 bytes of a DX7 single voice (VCED order: OP6 first, 21 bytes
 * per operator, then the globals and the name), then OP7 and OP8 (21 bytes each, the same layout),
 * EXT, its target (0..5 = OP1..OP6) and OP8's feedback. */
#include <stdint.h>
#include "dx7_tables.h"

#define DX7_N (1 << DX7_LG_N)
#define DX7_VCED 155                    /* a DX7 single voice */
#define DX7_VSIZE 200                   /* + OP7, OP8, EXT, its target, OP8 feedback */
#define DX7_PACKED 128                  /* a voice of a 32-voice bank (VMEM) */
#define DX7_NOPS 8
/* voice data offsets */
#define DX7_OP(i) ((i) < 6 ? (i) * 21 : 155 + ((i) - 6) * 21)   /* internal op i: 0..5 = OP6..OP1, 6 OP7, 7 OP8 */
enum { DX7_PR = 126, DX7_PL = 130, DX7_ALG = 134, DX7_FB = 135, DX7_OKS = 136, DX7_LFS = 137, DX7_LFD = 138,
       DX7_LPMD = 139, DX7_LAMD = 140, DX7_LFKS = 141, DX7_LFW = 142, DX7_LPMS = 143, DX7_TRNSP = 144,
       DX7_NAME = 145, DX7_EXT = 197, DX7_EXTT = 198, DX7_FB8 = 199 };
/* operator parameter offsets (in its 21 bytes) */
enum { DX7_R1, DX7_L1 = 4, DX7_BP = 8, DX7_LD, DX7_RD, DX7_LC, DX7_RC, DX7_RS, DX7_AMS, DX7_KVS, DX7_OL, DX7_MODE,
       DX7_COARSE, DX7_FINE, DX7_DET };
enum { DX7_EXT_OFF, DX7_EXT_STACK, DX7_EXT_PAIR, DX7_EXT_STACKMOD, DX7_EXT_PAIRMOD, DX7_EXT_COUNT };

/* highest value of each byte of an operator, of the globals (126..144) */
static const uint8_t DX7_OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
static const uint8_t DX7_GLMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};

/* ------------------------------------------------------------ lookups --- */
static int32_t dx7_sintab[1025];                        /* RAM: made at the first use (dx7_tables_init) */
static uint8_t dx7_tables_ready;
static void dx7_tables_init(void)                       /* Sin::init: a complex rotation, integer only */
{
    int64_t u = 1 << 30, v = 0;
    uint32_t i;
    if (dx7_tables_ready)
        return;
    for (i = 0; i < 512u; i++) {
        int64_t t;
        dx7_sintab[i] = (int32_t)((v + 32) >> 6);
        dx7_sintab[i + 512u] = -(int32_t)((v + 32) >> 6);
        t = (u * DX7_SIN_S + v * DX7_SIN_C + (1 << 29)) >> 30;
        u = (u * DX7_SIN_C - v * DX7_SIN_S + (1 << 29)) >> 30;
        v = t;
    }
    dx7_sintab[1024] = 0;
    dx7_tables_ready = 1;
}

static inline int32_t dx7_sin(int32_t phase)            /* Q24 phase (one cycle) -> Q24 */
{
    int32_t low = phase & 0x3FFF, i = (phase >> 14) & 1023;
    int32_t y0 = dx7_sintab[i];
    return y0 + (((dx7_sintab[i + 1] - y0) * low) >> 14);   /* |dy| < 2^17: the product fits 32 bits */
}

static int32_t dx7_freqtab(uint32_t i)                  /* Freqlut's table entry i (0..1024), exactly */
{
    uint32_t fix = (DX7_FREQ_FIX[i >> 2] >> ((i & 3u) * 2u)) & 3u;
    return (int32_t)(((uint64_t)DX7_EXP2[i] * DX7_FREQ_K + (1u << 31)) >> 32) + (int32_t)fix - 1;
}

static inline int32_t dx7_exp2(int32_t x)               /* Q24 in, Q24 out (Exp2::lookup) */
{
    int32_t low = x & 0x3FFF, i = (x >> 14) & 1023;
    uint32_t y0 = DX7_EXP2[i];
    int32_t dy = (int32_t)(DX7_EXP2[i + 1] - y0);
    int32_t y = (int32_t)y0 + (int32_t)(((int64_t)dy * low) >> 14);
    return y >> (6 - (x >> 24));
}

static int32_t dx7_freqlut(int32_t logfreq)             /* Q24 octaves -> phase increment (Freqlut::lookup) */
{
    int32_t ix, y0, low, hi, y;
    if (logfreq > (20 << 24) - 1)
        logfreq = (20 << 24) - 1;
    if (logfreq < -(10 << 24))
        logfreq = -(10 << 24);
    ix = (logfreq & 0xFFFFFF) >> 14;
    y0 = dx7_freqtab((uint32_t)ix);
    low = logfreq & 0x3FFF;
    y = y0 + (int32_t)(((int64_t)(dx7_freqtab((uint32_t)ix + 1u) - y0) * low) >> 14);
    hi = logfreq >> 24;
    return y >> (20 - hi);
}

/* ------------------------------------------------------- the envelope --- */
typedef struct {
    int32_t level, target, inc, statics;
    uint8_t rates[4], levels[4];
    int16_t outlevel;
    int8_t rate_scaling;
    uint8_t ix, rising, down;
} dx7_env_t;

static const int32_t DX7_STATICS[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150,
    502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530, 125685,
    110250, 110250, 88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562,
    27562, 22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512, 5512,
    4410, 3969, 3969, 3439, 2866, 2690, 2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705,
    573, 573, 529, 441, 441};
static const uint8_t DX7_LEVELLUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

static int dx7_scaleoutlevel(int ol) { return ol >= 20 ? 28 + ol : DX7_LEVELLUT[ol < 0 ? 0 : ol]; }

static void dx7_env_advance(dx7_env_t *e, int ix)
{
    int newlevel, actual, qrate;
    e->ix = (uint8_t)ix;
    if (ix >= 4)
        return;
    newlevel = e->levels[ix];
    actual = dx7_scaleoutlevel(newlevel) >> 1;
    actual = (actual << 6) + e->outlevel - 4256;
    actual = actual < 16 ? 16 : actual;
    e->target = actual << 16;
    e->rising = e->target > e->level;
    qrate = (e->rates[ix] * 41) >> 6;
    qrate += e->rate_scaling;
    qrate = qrate > 63 ? 63 : qrate;
    if (e->target == e->level || (ix == 0 && newlevel == 0)) {   /* ACCURATE_ENVELOPE: a hold */
        int sr = e->rates[ix] + e->rate_scaling;
        sr = sr > 99 ? 99 : sr;
        e->statics = sr < 77 ? DX7_STATICS[sr] : 20 * (99 - sr);
        if (sr < 77 && ix == 0 && newlevel == 0)
            e->statics /= 20;                                    /* attack is scaled faster */
    } else {
        e->statics = 0;
    }
    e->inc = (4 + (qrate & 3)) << (2 + DX7_LG_N + (qrate >> 2));
}

static void dx7_env_init(dx7_env_t *e, const uint8_t *r, const uint8_t *l, int ol, int rate_scaling)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        e->rates[i] = r[i];
        e->levels[i] = l[i];
    }
    e->outlevel = (int16_t)ol;
    e->rate_scaling = (int8_t)rate_scaling;
    e->level = 0;
    e->down = 1;
    dx7_env_advance(e, 0);
}

/* Env::update: new parameters for a sounding note (an edit); held notes restart at L3 */
static void dx7_env_update(dx7_env_t *e, const uint8_t *r, const uint8_t *l, int ol, int rate_scaling)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        e->rates[i] = r[i];
        e->levels[i] = l[i];
    }
    e->outlevel = (int16_t)ol;
    e->rate_scaling = (int8_t)rate_scaling;
    if (e->down) {
        int actual = dx7_scaleoutlevel(e->levels[2]) >> 1;
        actual = (actual << 6) - 4256;
        actual = actual < 16 ? 16 : actual;
        e->target = actual << 16;
        dx7_env_advance(e, 2);
    }
}

static int32_t dx7_env_tick(dx7_env_t *e)
{
    if (e->statics) {
        e->statics -= DX7_N;
        if (e->statics <= 0) {
            e->statics = 0;
            dx7_env_advance(e, e->ix + 1);
        }
    }
    if (e->ix < 3 || (e->ix < 4 && !e->down)) {
        if (e->statics) {
            ;
        } else if (e->rising) {
            const int32_t jump = 1716 << 16;
            if (e->level < jump)
                e->level = jump;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                dx7_env_advance(e, e->ix + 1);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                dx7_env_advance(e, e->ix + 1);
            }
        }
    }
    return e->level;
}

static void dx7_env_keyup(dx7_env_t *e)
{
    if (e->down) {
        e->down = 0;
        dx7_env_advance(e, 3);
    }
}

/* ------------------------------------------------- the pitch envelope --- */
typedef struct {
    int32_t level, target, inc;
    uint8_t ix, rising, down;
} dx7_penv_t;

static const uint8_t DX7_PENV_RATE[100] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17,
    18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49,
    51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115,
    120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int8_t DX7_PENV_TAB[100] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10,
    -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73,
    82, 92, 103, 115, 127};

static void dx7_penv_advance(dx7_penv_t *e, const uint8_t *patch, int ix)
{
    e->ix = (uint8_t)ix;
    if (ix < 4) {
        e->target = DX7_PENV_TAB[patch[DX7_PL + ix] % 100u] * (1 << 19);
        e->rising = e->target > e->level;
        e->inc = DX7_PENV_RATE[patch[DX7_PR + ix] % 100u] * DX7_PENV_UNIT;
    }
}

static int32_t dx7_penv_tick(dx7_penv_t *e, const uint8_t *patch)
{
    if (e->ix < 3 || (e->ix < 4 && !e->down)) {
        if (e->rising) {
            e->level += e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                dx7_penv_advance(e, patch, e->ix + 1);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                dx7_penv_advance(e, patch, e->ix + 1);
            }
        }
    }
    return e->level;
}

/* ------------------------------------------------------------- the LFO --- */
typedef struct {                        /* one per instrument (a DX7 has one LFO for all voices) */
    uint32_t phase, delta, delaystate, delayinc, delayinc2;
    uint8_t wave, rnd, sync;
} dx7_lfo_t;

static void dx7_lfo_reset(dx7_lfo_t *l, const uint8_t *patch)
{
    int a = 99 - patch[DX7_LFD] % 100u;
    dx7_tables_init();
    l->delta = DX7_LFO_DELTA[patch[DX7_LFS] % 100u];
    if (a == 99) {
        l->delayinc = l->delayinc2 = ~0u;
    } else {
        a = (16 + (a & 15)) << (1 + (a >> 4));
        l->delayinc = DX7_LFO_UNIT * (uint32_t)a;
        a &= 0xFF80;
        a = a > 0x80 ? a : 0x80;
        l->delayinc2 = DX7_LFO_UNIT * (uint32_t)a;
    }
    l->wave = patch[DX7_LFW];
    l->sync = patch[DX7_LFKS] != 0;
}

static int32_t dx7_lfo_tick(dx7_lfo_t *l)               /* 0..1 in Q24 */
{
    int32_t x;
    l->phase += l->delta;
    switch (l->wave) {
    case 0:                                            /* triangle */
        x = (int32_t)(l->phase >> 7);
        x ^= -(int32_t)(l->phase >> 31);
        return x & ((1 << 24) - 1);
    case 1:                                            /* saw down */
        return (int32_t)((~l->phase ^ (1u << 31)) >> 8);
    case 2:                                            /* saw up */
        return (int32_t)((l->phase ^ (1u << 31)) >> 8);
    case 3:                                            /* square */
        return (int32_t)(((~l->phase) >> 7) & (1u << 24));
    case 4:                                            /* sine */
        return (1 << 23) + (dx7_sin((int32_t)(l->phase >> 8)) >> 1);
    case 5:                                            /* sample and hold */
        if (l->phase < l->delta)
            l->rnd = (uint8_t)((l->rnd * 179 + 17) & 0xFF);
        x = l->rnd ^ 0x80;
        return (x + 1) << 16;
    default:
        return 1 << 23;
    }
}

static int32_t dx7_lfo_delay(dx7_lfo_t *l)              /* the delay ramp, 0..1 in Q24 */
{
    uint32_t delta = l->delaystate < (1u << 31) ? l->delayinc : l->delayinc2;
    uint64_t d = (uint64_t)l->delaystate + delta;
    if (d > 0xFFFFFFFFu)
        return 1 << 24;
    l->delaystate = (uint32_t)d;
    return d < (1u << 31) ? 0 : (int32_t)((d >> 7) & ((1u << 24) - 1));
}

static void dx7_lfo_keydown(dx7_lfo_t *l)
{
    if (l->sync)
        l->phase = (1u << 31) - 1;
    l->delaystate = 0;
}

/* ----------------------------------------------------- the algorithms --- */
enum { DX7_OB1 = 1, DX7_OB2 = 2, DX7_ADD = 4, DX7_IB1 = 16, DX7_IB2 = 32, DX7_FBI = 64, DX7_FBO = 128 };
/* fm_core.cc: per algorithm, the flags of OP6 .. OP1 (the order they render in) */
static const uint8_t DX7_ALGS[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04},
};

/* an operator that writes the output (no bus): msfa's FmCore::isCarrier tests the ADD flag instead, which
 * also takes a modulator summed into a bus (0x05, 0x25, 0xc5: algorithms 7..18, 26, 27); with such an
 * operator at release rate 0 the note never ended (ROM1A BASS 1, STEEL DRUM, SYN-LEAD 1) and held its slot */
static int dx7_is_carrier(uint32_t alg, uint32_t op) { return (DX7_ALGS[alg & 31u][op] & 3u) == 0u; }

/* ----------------------------------------------------------- a note --- */
typedef struct {
    dx7_env_t env[DX7_NOPS];
    int32_t phase[DX7_NOPS], gain[DX7_NOPS], basepitch[DX7_NOPS], level[DX7_NOPS];
    int32_t fb[2], fb8[2];                       /* feedback histories: the DX7 loop, OP8 */
    dx7_penv_t penv;
    int32_t pmd, ams[DX7_NOPS], amd;             /* pitch / amplitude modulation depths, sensitivities */
    uint8_t pms, alg, fb_shift, fb8_shift, ext, target, nops, mode[DX7_NOPS];
} dx7_note_t;

static const int32_t DX7_COARSEMUL[32] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705,
    58039632, 60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397, 72509921,
    73690858, 74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032, 81503396, 82323963,
    83117622};

static int32_t dx7_osc_freq(int note, const uint8_t *op)
{
    int32_t lf;
    int coarse = op[DX7_COARSE], fine = op[DX7_FINE] % 100, det = op[DX7_DET];
    if (op[DX7_MODE] == 0) {
        note = note < 0 ? 0 : note > 127 ? 127 : note;
        lf = 50857777 + ((1 << 24) / 12) * note;
        lf += (int32_t)(((int64_t)DX7_DETUNE_Q8[note] * (det - 7)) >> 8);
        lf += DX7_COARSEMUL[coarse & 31];
        lf += DX7_FINE_TAB[fine];             /* Dexed: floor(24204406.323123 log(1 + fine / 100) + 0.5) */
    } else {
        lf = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
        lf += det > 7 ? 13457 * (det - 7) : 0;
    }
    return lf;
}

static const uint8_t DX7_VELDATA[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181,
    184, 186, 189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227,
    229, 230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};
static int dx7_scale_vel(int vel, int sens)
{
    int v = vel < 0 ? 0 : vel > 127 ? 127 : vel;
    return ((sens * (DX7_VELDATA[v >> 1] - 239) + 7) >> 3) << 4;
}
static int dx7_scale_rate(int note, int sens)
{
    int x = note / 3 - 7;
    x = x < 0 ? 0 : x > 31 ? 31 : x;
    return (sens * x) >> 3;
}
static const uint8_t DX7_EXPSCALE[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
                                         80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static int dx7_scale_curve(int group, int depth, int curve)
{
    int s;
    if (curve == 0 || curve == 3)
        s = (group * depth * 329) >> 12;
    else
        s = (DX7_EXPSCALE[group > 32 ? 32 : group] * depth * 329) >> 15;
    return curve < 2 ? -s : s;
}
static int dx7_scale_level(int note, const uint8_t *op)
{
    int off = note - op[DX7_BP] - 17;
    return off >= 0 ? dx7_scale_curve((off + 1) / 3, op[DX7_RD], op[DX7_RC])
                    : dx7_scale_curve(-(off - 1) / 3, op[DX7_LD], op[DX7_LC]);
}

static const uint8_t DX7_PMSTAB[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const int32_t DX7_AMSTAB[4] = {0, 4342338, 7171437, 16777216};

/* an operator's output level (microsteps) and rate scaling for a note and velocity */
static void dx7_op_levels(const uint8_t *op, int note, int vel, int *ol, int *rs)
{
    int o = dx7_scaleoutlevel(op[DX7_OL]) + dx7_scale_level(note, op);
    o = o > 127 ? 127 : o;
    o = (o << 5) + dx7_scale_vel(vel, op[DX7_KVS]);
    *ol = o < 0 ? 0 : o;
    *rs = dx7_scale_rate(note, op[DX7_RS]);
}

static uint32_t dx7_nops(const uint8_t *patch) { return patch[DX7_EXT] && patch[DX7_EXT] < DX7_EXT_COUNT ? 8u : 6u; }

static void dx7_note_globals(dx7_note_t *n, const uint8_t *patch)
{
    uint32_t fb = patch[DX7_FB] & 7u, fb8 = patch[DX7_FB8] & 7u;
    n->alg = patch[DX7_ALG] & 31u;
    n->fb_shift = (uint8_t)(fb ? 8u - fb : 16u);
    n->fb8_shift = (uint8_t)(fb8 ? 8u - fb8 : 16u);
    n->pmd = (patch[DX7_LPMD] * 165) >> 6;
    n->pms = DX7_PMSTAB[patch[DX7_LPMS] & 7u];
    n->amd = (patch[DX7_LAMD] * 165) >> 6;
    n->ext = (uint8_t)(patch[DX7_EXT] < DX7_EXT_COUNT ? patch[DX7_EXT] : 0u);
    n->target = (uint8_t)(5u - patch[DX7_EXTT] % 6u);    /* OP1..OP6 -> internal index 5..0 */
    n->nops = (uint8_t)dx7_nops(patch);
}

/* Dx7Note::init. note: the MIDI note (transposed), vel 0..127. keep: a voice taken while sounding keeps
 * its phases and gains (no click; Dexed: no oscSync when stealing) */
static void dx7_note_init(dx7_note_t *n, const uint8_t *patch, int note, int vel, int keep)
{
    uint32_t i;
    dx7_tables_init();
    dx7_note_globals(n, patch);
    for (i = 0; i < DX7_NOPS; i++) {
        const uint8_t *op = patch + DX7_OP(i);
        int ol, rs;
        dx7_op_levels(op, note, vel, &ol, &rs);
        dx7_env_init(&n->env[i], op + DX7_R1, op + DX7_L1, ol, rs);
        n->basepitch[i] = dx7_osc_freq(note, op);
        n->mode[i] = op[DX7_MODE];
        n->ams[i] = DX7_AMSTAB[op[DX7_AMS] & 3u];
        if (!keep && patch[DX7_OKS]) {
            n->phase[i] = 0;
            n->gain[i] = 0;
        }
    }
    if (!keep && patch[DX7_OKS])
        n->fb[0] = n->fb[1] = n->fb8[0] = n->fb8[1] = 0;
    n->penv.level = DX7_PENV_TAB[patch[DX7_PL + 3] % 100u] * (1 << 19);
    n->penv.down = 1;
    dx7_penv_advance(&n->penv, patch, 0);
}

/* Dx7Note::update: an edit while the note sounds */
static void dx7_note_update(dx7_note_t *n, const uint8_t *patch, int note, int vel)
{
    uint32_t i;
    dx7_note_globals(n, patch);
    for (i = 0; i < DX7_NOPS; i++) {
        const uint8_t *op = patch + DX7_OP(i);
        int ol, rs;
        n->basepitch[i] = dx7_osc_freq(note, op);
        n->mode[i] = op[DX7_MODE];
        n->ams[i] = DX7_AMSTAB[op[DX7_AMS] & 3u];
        dx7_op_levels(op, note, vel, &ol, &rs);
        dx7_env_update(&n->env[i], op + DX7_R1, op + DX7_L1, ol, rs);
    }
}

static void dx7_note_keyup(dx7_note_t *n)
{
    uint32_t i;
    for (i = 0; i < DX7_NOPS; i++)
        dx7_env_keyup(&n->env[i]);
    if (n->penv.down) {
        n->penv.down = 0;
        n->penv.ix = 3;                                   /* (advance with the patch: dx7_note_compute) */
        n->penv.rising = 2;                               /* marks "advance to 3 pending" */
    }
}

/* still sounding: a carrier's envelope not finished (Dx7Note::isPlaying; extra carriers of EXT too) */
static int dx7_note_playing(const dx7_note_t *n)
{
    uint32_t i;
    for (i = 0; i < 6u; i++)
        if (dx7_is_carrier(n->alg, i) && (n->env[i].ix < 4 || n->env[i].levels[3] > 0))
            return 1;
    if (n->ext == DX7_EXT_STACK || n->ext == DX7_EXT_PAIR)
        for (i = 6; i < 8u; i++)
            if ((i == 6 || n->ext == DX7_EXT_PAIR) && (n->env[i].ix < 4 || n->env[i].levels[3] > 0))
                return 1;
    return 0;
}

/* ------------------------------------------------------ the kernels --- */
#define DX7_THRESH 1120                                  /* below this gain an operator is not rendered */
static void dx7_op(int32_t *out, const int32_t *in, int32_t phase, int32_t freq, int32_t g1, int32_t g2, int add)
{
    int32_t dg = (g2 - g1 + (DX7_N >> 1)) >> DX7_LG_N, g = g1;
    uint32_t i;
    if (add) {
        for (i = 0; i < DX7_N; i++) {
            g += dg;
            out[i] += (int32_t)(((int64_t)dx7_sin(phase + in[i]) * g) >> 24);
            phase += freq;
        }
    } else {
        for (i = 0; i < DX7_N; i++) {
            g += dg;
            out[i] = (int32_t)(((int64_t)dx7_sin(phase + in[i]) * g) >> 24);
            phase += freq;
        }
    }
}

static void dx7_op_pure(int32_t *out, int32_t phase, int32_t freq, int32_t g1, int32_t g2, int add)
{
    int32_t dg = (g2 - g1 + (DX7_N >> 1)) >> DX7_LG_N, g = g1;
    uint32_t i;
    if (add) {
        for (i = 0; i < DX7_N; i++) {
            g += dg;
            out[i] += (int32_t)(((int64_t)dx7_sin(phase) * g) >> 24);
            phase += freq;
        }
    } else {
        for (i = 0; i < DX7_N; i++) {
            g += dg;
            out[i] = (int32_t)(((int64_t)dx7_sin(phase) * g) >> 24);
            phase += freq;
        }
    }
}

/* feedback operator; in: an extra modulation input (OP7 / OP8 into a DX7 operator), 0 = none */
static void dx7_op_fb(int32_t *out, const int32_t *in, int32_t phase, int32_t freq, int32_t g1, int32_t g2,
                      int32_t *fb, int shift, int add)
{
    int32_t dg = (g2 - g1 + (DX7_N >> 1)) >> DX7_LG_N, g = g1, y0 = fb[0], y = fb[1];
    uint32_t i;
    for (i = 0; i < DX7_N; i++) {
        int32_t m = (y0 + y) >> (shift + 1);
        g += dg;
        y0 = y;
        y = dx7_sin(phase + m + (in ? in[i] : 0));
        y = (int32_t)(((int64_t)y * g) >> 24);
        out[i] = add ? out[i] + y : y;
        phase += freq;
    }
    fb[0] = y0;
    fb[1] = y;
}

/* level (envelope + amplitude modulation) -> the operator's gain for this block */
static int32_t dx7_op_gain(int32_t level, int32_t ams, uint32_t amd)
{
    if (ams) {                                           /* Dexed: pt = exp(sens / 262144 * 0.07 + 12.2) */
        uint32_t sens = (uint32_t)(((uint64_t)amd * (uint32_t)ams) >> 24);
        int64_t l2 = (int64_t)DX7_AM_A + (((int64_t)sens * DX7_AM_B) >> 16);   /* log2(pt), Q24 */
        int32_t ip = (int32_t)(l2 >> 24), fr = (int32_t)(l2 & 0xFFFFFF);
        uint32_t pt = (uint32_t)dx7_exp2(fr) << 6;                            /* 2^fr in Q30 */
        pt = ip >= 30 ? pt : pt >> (30 - ip);
        level -= (int32_t)(((uint64_t)(uint32_t)level * ((uint64_t)pt << 4)) >> 28);
    }
    return level;
}


/* OP8 and OP7 (EXT) into ext[]: returns 1 when ext[] holds something. A carrier stack / pair goes
 * straight into the voice output (buf), a modulator stays in ext[] for the target operator */
static int dx7_ext_render(dx7_note_t *n, const int32_t *fq, int32_t *ext, int32_t *buf)
{
    int32_t g1 = n->gain[7], g2 = dx7_exp2(n->level[7] - (14 << 24));
    int32_t h1 = n->gain[6], h2 = dx7_exp2(n->level[6] - (14 << 24));
    int stack = n->ext == DX7_EXT_STACK || n->ext == DX7_EXT_STACKMOD;
    int on8 = g1 >= DX7_THRESH || g2 >= DX7_THRESH, on7 = h1 >= DX7_THRESH || h2 >= DX7_THRESH, any;
    uint32_t k;
    n->gain[7] = g2;
    n->gain[6] = h2;
    if (on8) {
        if (n->fb8_shift < 16)
            dx7_op_fb(ext, 0, n->phase[7], fq[7], g1, g2, n->fb8, n->fb8_shift, 0);
        else
            dx7_op_pure(ext, n->phase[7], fq[7], g1, g2, 0);
    }
    if (on7) {
        if (stack && on8)
            dx7_op(ext, ext, n->phase[6], fq[6], h1, h2, 0);   /* (in place: each sample read before written) */
        else
            dx7_op_pure(ext, n->phase[6], fq[6], h1, h2, !stack && on8);
    }
    n->phase[7] += fq[7] << DX7_LG_N;
    n->phase[6] += fq[6] << DX7_LG_N;
    any = stack ? on7 : on7 || on8;
    if (any && (n->ext == DX7_EXT_STACK || n->ext == DX7_EXT_PAIR)) {
        for (k = 0; k < DX7_N; k++)
            buf[k] = ext[k];
        return 0;
    }
    return any;
}

/* render one block of the note into buf (cleared here). lfo, lfo_delay: dx7_lfo_tick / dx7_lfo_delay of
 * the instrument; pb: a pitch offset (Q24 octaves: glide, tune, the SLOOP LFO), on every operator */
static void dx7_note_compute(dx7_note_t *n, const uint8_t *patch, int32_t *buf, int32_t lfo, int32_t lfo_delay,
                             int32_t pb)
{
    static int32_t bus[2][DX7_N], ext[DX7_N], tmp[DX7_N];
    const uint8_t *alg = DX7_ALGS[n->alg];
    uint32_t pmd = (uint32_t)n->pmd * (uint32_t)lfo_delay, amd, i;
    int32_t senslfo = n->pms * (lfo - (1 << 23)), pmod, fq[DX7_NOPS];
    int has[3] = {1, 0, 0}, ext_in = 0;
    if (n->penv.rising == 2) {                           /* key up: the pitch envelope's release */
        n->penv.rising = 0;
        dx7_penv_advance(&n->penv, patch, 3);
    }
    /* pitch: the pitch envelope, the LFO (Dx7Note::compute) */
    pmod = (int32_t)(((int64_t)pmd * senslfo) >> 39);
    pmod = dx7_penv_tick(&n->penv, patch) + pmod + pb;
    /* amplitude modulation depth */
    amd = (uint32_t)(((int64_t)n->amd * lfo_delay) >> 8);
    amd = (uint32_t)(((int64_t)amd * ((1 << 24) - lfo)) >> 24);
    for (i = 0; i < n->nops; i++) {
        fq[i] = dx7_freqlut(n->basepitch[i] + (n->mode[i] ? pb : pmod));
        n->level[i] = dx7_op_gain(dx7_env_tick(&n->env[i]), n->ams[i], amd);
    }
    for (i = 0; i < DX7_N; i++)
        buf[i] = 0;
    if (n->nops == 8u)
        ext_in = dx7_ext_render(n, fq, ext, buf);
    for (i = 0; i < 6u; i++) {                           /* FmCore::render: OP6 .. OP1 */
        uint32_t fl = alg[i], inb = (fl >> 4) & 3u, outb = fl & 3u;
        int add = (fl & DX7_ADD) != 0, xin = ext_in && i == n->target;
        int32_t *o = outb == 0 ? buf : bus[outb - 1u];
        int32_t g1 = n->gain[i], g2 = dx7_exp2(n->level[i] - (14 << 24));
        n->gain[i] = g2;
        if (g1 >= DX7_THRESH || g2 >= DX7_THRESH) {
            if (!has[outb])
                add = 0;
            if (inb && has[inb]) {
                const int32_t *in = bus[inb - 1u];
                if (xin) {
                    uint32_t k;
                    for (k = 0; k < DX7_N; k++)
                        tmp[k] = in[k] + ext[k];
                    in = tmp;
                }
                dx7_op(o, in, n->phase[i], fq[i], g1, g2, add);
            } else if ((fl & 0xC0u) == 0xC0u && n->fb_shift < 16) {
                dx7_op_fb(o, xin ? ext : 0, n->phase[i], fq[i], g1, g2, n->fb, n->fb_shift, add);
            } else if (xin) {
                dx7_op(o, ext, n->phase[i], fq[i], g1, g2, add);
            } else {
                dx7_op_pure(o, n->phase[i], fq[i], g1, g2, add);
            }
            has[outb] = 1;
        } else if (!add) {
            has[outb] = 0;
        }
        n->phase[i] += fq[i] << DX7_LG_N;
    }
}

/* ----------------------------------------------------------- sysex --- */
/* a voice's bytes inside their ranges (an imported voice may hold anything) */
static void dx7_voice_clamp(uint8_t *v)
{
    uint32_t i, k;
    for (i = 0; i < DX7_NOPS; i++)
        for (k = 0; k < 21u; k++)
            if (v[DX7_OP(i) + k] > DX7_OPMAX[k])
                v[DX7_OP(i) + k] = DX7_OPMAX[k];
    for (k = 0; k < 19u; k++)
        if (v[126 + k] > DX7_GLMAX[k])
            v[126 + k] = DX7_GLMAX[k];
    for (k = 0; k < 10u; k++)
        if (v[DX7_NAME + k] < 32 || v[DX7_NAME + k] > 126)
            v[DX7_NAME + k] = ' ';
    if (v[DX7_EXT] >= DX7_EXT_COUNT)
        v[DX7_EXT] = 0;
    v[DX7_EXTT] %= 6u;
    v[DX7_FB8] &= 7u;
}

/* OP7 / OP8 of a plain DX7 voice: silent (level 0), ratio 1, the usual envelope; EXT off */
static void dx7_ext_default(uint8_t *v)
{
    static const uint8_t OPI[21] = {99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7};
    uint32_t i, k;
    for (i = 6; i < DX7_NOPS; i++)
        for (k = 0; k < 21u; k++)
            v[DX7_OP(i) + k] = OPI[k];
    v[DX7_EXT] = 0;
    v[DX7_EXTT] = 0;
    v[DX7_FB8] = 0;
}

/* a 128-byte VMEM voice -> the voice (Dexed PluginData unpackProgram); OP7 / OP8 default */
static void dx7_unpack(uint8_t *v, const uint8_t *b)
{
    uint32_t op, k;
    for (op = 0; op < 6u; op++) {
        const uint8_t *s = b + op * 17u;
        uint8_t *d = v + op * 21u;
        for (k = 0; k < 11u; k++)
            d[k] = s[k] & 0x7Fu;
        d[DX7_LC] = s[11] & 3u;
        d[DX7_RC] = (s[11] >> 2) & 3u;
        d[DX7_RS] = s[12] & 7u;
        d[DX7_DET] = (s[12] >> 3) & 15u;
        d[DX7_AMS] = s[13] & 3u;
        d[DX7_KVS] = (s[13] >> 2) & 7u;
        d[DX7_OL] = s[14] & 0x7Fu;
        d[DX7_MODE] = s[15] & 1u;
        d[DX7_COARSE] = (s[15] >> 1) & 31u;
        d[DX7_FINE] = s[16] & 0x7Fu;
    }
    for (k = 0; k < 9u; k++)
        v[126 + k] = b[102 + k] & 0x7Fu;                /* PR1..4 PL1..4 ALG */
    v[DX7_FB] = b[111] & 7u;
    v[DX7_OKS] = (b[111] >> 3) & 1u;
    for (k = 0; k < 4u; k++)
        v[DX7_LFS + k] = b[112 + k] & 0x7Fu;            /* LFS LFD LPMD LAMD */
    v[DX7_LFKS] = b[116] & 1u;
    v[DX7_LFW] = (b[116] >> 1) & 7u;
    v[DX7_LPMS] = (b[116] >> 4) & 7u;
    for (k = 0; k < 11u; k++)
        v[DX7_TRNSP + k] = b[117 + k] & 0x7Fu;          /* TRNSP, NAME */
    dx7_ext_default(v);
    dx7_voice_clamp(v);
}

/* the voice -> 128 bytes of a VMEM bank (OP7 / OP8 are not part of a DX7 voice) */
static void dx7_pack(uint8_t *b, const uint8_t *v)
{
    uint32_t op, k;
    for (op = 0; op < 6u; op++) {
        const uint8_t *s = v + op * 21u;
        uint8_t *d = b + op * 17u;
        for (k = 0; k < 11u; k++)
            d[k] = s[k];
        d[11] = (uint8_t)((s[DX7_RC] & 3u) << 2 | (s[DX7_LC] & 3u));
        d[12] = (uint8_t)((s[DX7_DET] & 15u) << 3 | (s[DX7_RS] & 7u));
        d[13] = (uint8_t)((s[DX7_KVS] & 7u) << 2 | (s[DX7_AMS] & 3u));
        d[14] = s[DX7_OL];
        d[15] = (uint8_t)((s[DX7_COARSE] & 31u) << 1 | (s[DX7_MODE] & 1u));
        d[16] = s[DX7_FINE];
    }
    for (k = 0; k < 9u; k++)
        b[102 + k] = v[126 + k];
    b[111] = (uint8_t)((v[DX7_OKS] & 1u) << 3 | (v[DX7_FB] & 7u));
    for (k = 0; k < 4u; k++)
        b[112 + k] = v[DX7_LFS + k];
    b[116] = (uint8_t)((v[DX7_LPMS] & 7u) << 4 | (v[DX7_LFW] & 7u) << 1 | (v[DX7_LFKS] & 1u));
    for (k = 0; k < 11u; k++)
        b[117 + k] = v[DX7_TRNSP + k];
}

static uint8_t dx7_checksum(const uint8_t *d, uint32_t n)   /* DX7 bulk checksum: the sum + it = 0 (7 bit) */
{
    uint32_t s = 0;
    while (n--)
        s += *d++;
    return (uint8_t)(-s & 0x7Fu);
}

/* a SysEx frame (without F0 / F7): 43 0n 00 01 1B <155> <sum> = a single voice (VCED). Returns 1 and
 * the voice in v (OP7 / OP8 default), 0 if it is not one or the checksum is wrong */
static int dx7_parse_vced(const uint8_t *f, uint32_t n, uint8_t *v)
{
    uint32_t k;
    if (n != 5u + DX7_VCED + 1u || f[0] != 0x43 || (f[1] & 0xF0u) != 0x00 || f[2] != 0x00 || f[3] != 0x01 ||
        f[4] != 0x1B || dx7_checksum(f + 5, DX7_VCED) != f[5 + DX7_VCED])
        return 0;
    for (k = 0; k < DX7_VCED; k++)
        v[k] = f[5 + k] & 0x7Fu;
    dx7_ext_default(v);
    dx7_voice_clamp(v);
    return 1;
}

/* the voice as a VCED frame (without F0 / F7): 161 bytes, channel 1 */
static uint32_t dx7_make_vced(uint8_t *f, const uint8_t *v)
{
    uint32_t k;
    f[0] = 0x43;
    f[1] = 0x00;
    f[2] = 0x00;
    f[3] = 0x01;
    f[4] = 0x1B;
    for (k = 0; k < DX7_VCED; k++)
        f[5 + k] = v[k];
    f[5 + DX7_VCED] = dx7_checksum(f + 5, DX7_VCED);
    return 6u + DX7_VCED;
}

/* a 32-voice bank frame (without F0 / F7): 43 0n 09 20 00 <4096> <sum>. Returns 1 if it is one with a
 * good checksum (the 4096 bytes are at f + 5) */
static int dx7_is_vmem(const uint8_t *f, uint32_t n)
{
    return n == 5u + 4096u + 1u && f[0] == 0x43 && (f[1] & 0xF0u) == 0x00 && f[2] == 0x09 && f[3] == 0x20 &&
           f[4] == 0x00 && dx7_checksum(f + 5, 4096) == f[5 + 4096];
}
