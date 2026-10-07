/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Devin Acker
 * Modifications Copyright (c) 2026 Kerem Kilic (Ellic Studio)
 * License: LICENSES/BSD-3-Clause-uPD933.txt
 * Native CZ-1 tone playback. Parameter encoding: Casio MIDI specification.
 * Chip envelope rate, phase functions and logarithmic DCA law follow the
 * independently documented uPD933 model by Devin Acker (MAME, BSD-3-Clause).
 *
 * From Melodee 0.11 (github.com/keremimo/melodee, firmware/src/cz_native.c at v0.11), adapted for SLOOP's
 * CZ engine (eng_cz.c): the tone is the part's (cz_tone, built from eng_cz.c's own tone table, no Casio
 * data), the envelope points are figured once a block per part (cz_part_defs), the EDIT values change the
 * line select, ring / noise, detune, DCW depth, the DCW / DCA times and the vibrato depth (cz_ed_t); the
 * phase function is inlined into the sample loop; the output is scaled as SLOOP's other engines. */
static const uint16_t CZ_AMP[128] = {
    0,4,5,5,5,6,6,7,7,8,8,9,9,10,11,12,
    12,13,14,15,16,18,19,20,22,23,25,27,29,31,33,36,
    38,41,44,47,51,54,58,63,67,72,77,83,89,96,103,110,
    118,127,136,146,157,168,180,194,208,223,239,257,275,296,317,340,
    365,392,421,451,484,520,558,598,642,689,739,793,851,914,980,1052,
    1129,1212,1300,1395,1497,1606,1724,1850,1985,2130,2286,2453,2632,2824,3031,3252,
    3490,3745,4019,4313,4628,4966,5329,5718,6136,6585,7066,7582,8136,8731,9369,10054,
    10789,11577,12423,13331,14305,15351,16473,17676,18968,20355,21842,23438,25151,26990,28962,31079
};
/* time scale: 2^(k/16), Q16, k 0..15 (cz_scale) */
static const uint32_t CZ_POW16[16] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752,
                                      92682, 96785, 101070, 105545, 110218, 115098, 120194, 125515};
/* a rate times 2^(-v/16): v > 0 slower (a longer time), v < 0 faster; v -64..63 */
static uint32_t cz_scale(uint32_t rate, int32_t v)
{
    uint64_t r = rate;
    if (!v)
        return rate;
    if (v > 0) {                                     /* / 2^(v/16) */
        r = (r << 16) / CZ_POW16[v & 15];
        r >>= (uint32_t)v >> 4;
    } else {
        v = -v;
        r = (r * CZ_POW16[v & 15]) >> 16;
        r <<= (uint32_t)v >> 4;
    }
    return r > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)r;
}
static __attribute__((noinline)) uint32_t cz_hw_rate(uint32_t raw, uint32_t e)
{
    uint32_t chip = (8u | (raw & 7u)) << ((raw & 127u) >> 3);
    /* uPD933 at 40 kHz; convert its three different fixed-point domains to
     * our Q24 control tick. DCO covers 128 semitones, DCW 1024, DCA 512. */
    uint32_t shift = e == 0u ? 3u : e == 1u ? 2u : 1u;
    return (uint32_t)(((uint64_t)chip * CTL * 40000u / FS) >> shift);
}
static int32_t cz_hw_target(uint32_t raw, uint32_t e)
{
    raw &= 127u;
    if (e == 0u) return (int32_t)((raw & 63u) << (raw & 64u ? 18 : 13));
    return (int32_t)(raw << 17); /* DCW: 8 units; DCA: 4 units */
}
/* the part's envelope points (no key follow: cz_native_render), its DCW and DCA times scaled by W.TIM, A.TIM */
static __attribute__((noinline)) void cz_part_defs(const uint8_t *b, cz_env_def_t d[2][3], int32_t wtim, int32_t atim)
{
    for (uint32_t l = 0; l < 2u; l++) for (uint32_t e = 0; e < 3u; e++) {
        cz_env_def_t *p = &d[l][e]; uint32_t base = CZ_ENV_BASE[l][e];
        int32_t sc = e == 1u ? wtim : e == 2u ? atim : 0;
        memset(p, 0, sizeof *p); p->end = b[CZ_ENV_END[l][e]] & 7u; p->sustain = 255;
        for (uint32_t k = 0; k < 8u; k++) {
            uint32_t lev = b[base + 2u*k + 1u];
            p->rate[k] = cz_scale(cz_hw_rate(b[base + 2u*k], e), sc);
            p->level[k] = cz_hw_target(lev, e);
            if ((lev & 128u) && p->sustain == 255u && k < p->end) p->sustain = (uint8_t)k;
        }
    }
}
static __attribute__((noinline)) int32_t cz_native_amp(int32_t level, uint32_t atten, uint32_t sense, uint32_t vel)
{
    /* Interpolate the chip's logarithmic amplitude, not a linear percentage. */
    uint32_t x = (uint32_t)clamp(level, 0, 127<<17), i = x >> 17, f = x & 0x1FFFFu;
    int32_t a = CZ_AMP[i];
    if (i < 127u) a += (int32_t)((uint32_t)(CZ_AMP[i+1u]-CZ_AMP[i]) * f >> 17);
    a = a * (int32_t)(15u-atten) / 15;
    return a * (int32_t)(127u*15u - (127u-vel)*sense) / (127*15);
}
/* Block-prepared slopes of the chip's 11-bit phase functions. */
typedef struct { uint32_t wave[2], window, dcw, pivot, k0, k1; } cz_hw_pd_t;
static __attribute__((noinline)) void cz_native_pd(cz_hw_pd_t *p, uint32_t word, uint32_t dcw)
{
    p->wave[0] = (word >> 13) & 7u;
    p->wave[1] = word & 512u ? (word >> 10) & 7u : p->wave[0];
    p->window = (word >> 6) & 7u; p->dcw = dcw;
    p->pivot = 1024u - dcw;
    p->k0 = (1024u << 16) / p->pivot;
    p->k1 = (1024u << 16) / (2048u - p->pivot);
}
static inline __attribute__((always_inline)) int32_t cz_native_wave(const cz_hw_pd_t *b, uint32_t ph, uint32_t toggle)
{
    uint32_t pos = ph >> 21, pivot = b->pivot, phase = 0, window = 0;
    switch (b->wave[toggle & 1u]) {
    case 0: phase = pos < pivot ? pos*b->k0 >> 16 : 1024u + ((pos-pivot)*b->k1 >> 16); break;
    case 1: phase = (pos & 1023u) < pivot ? ((pos & 1023u)*b->k0 >> 16) : 1023u; phase |= pos & 1024u; break;
    case 2: phase = pos < pivot*2u ? pos*b->k0 >> 16 : 2047u; break;
    case 3: return 0;                                    /* undocumented silent wave */
    case 4: phase = pos < pivot ? pos*b->k0 >> 15 : (pos-pivot)*b->k1 >> 15; break;
    case 5: phase = pos < 1024u ? pos : pos < pivot+1024u ? 1024u+((pos&1023u)*b->k0 >> 16) : 2047u; break;
    case 6: phase = pos + ((pos*b->dcw) >> 6); break;
    default: phase = (pos&1023u) < pivot ? (pos&1023u)*b->k0 >> 16 : 2047u; break;
    }
    phase &= 2047u;
    switch (b->window) {
    case 0: break;
    case 1: window = pos; break;
    case 2: window = (pos&1023u)*2u; if (pos < 1024u) window ^= 2046u; break;
    case 3: if (pos >= 1024u) window = (pos&1023u)*2u; break;
    case 4: window = pos < 1024u ? pos*2u : 2047u; break;
    default: window = (1023u ^ (pos&1023u))*2u; break;
    }
    uint32_t cp = (phase << 5) + (phase >> 6);
    int32_t carrier = (cz_cos(cp) + 32767) >> 1;
    return ((carrier * (int32_t)(2048u-window)) >> 10) - 32767;
}
/* the vibrato's machine depth of a panel DEPTH 0..99 (Casio p. 85; Melodee cz_legacy.h lcz_sx_vdata) */
static uint32_t cz_vib_depth(uint32_t n)
{
    uint32_t group = n < 32u ? 0u : (n - 16u) / 16u, step = 1u << group;
    uint32_t v = group ? ((n - 16u - 16u * group) * step + ((17u << group) - 1u)) : n;
    return n == 99u ? 0x300u : v + step;
}
static __attribute__((noinline)) int32_t cz_native_vibrato(cz_voice_t *c, const uint8_t *b, uint32_t vib)
{
    uint32_t delay = (uint32_t)b[6] | (uint32_t)b[7] << 8, inc = (uint32_t)b[9] | (uint32_t)b[10] << 8;
    uint32_t depth = vib ? cz_vib_depth(vib) : (uint32_t)b[12] | (uint32_t)b[13] << 8;
    int32_t x, wave;
    if (!vib && !b[11])
        return 0;                                        /* DEPTH 0 (EDIT VIB 0: the tone's) */
    /* Approximate control clock; needs verification against a CZ-1. */
    c->vib_ticks++;
    if (c->vib_ticks < delay * FS / (200u*CTL)) return 0;
    c->vib_phase += inc * (uint32_t)((uint64_t)CTL*200u*65536u/FS);
    x = (int32_t)(c->vib_phase >> 16);
    if (b[4] & 32u) wave = 32767-x;
    else if (b[4] & 8u) wave = x < 32768 ? x*2-32768 : 98303-x*2;
    else if (b[4] & 4u) wave = x-32768;
    else wave = x < 32768 ? -32768 : 32767;
    /* 1/16-semitone pitch, toward zero: DEPTH 0 is machine depth 1 (Casio p. 85), which must not bend
     * the negative half of the wave down a step */
    return (wave * (int32_t)depth) / (1 << 19);
}
/* LINE (EDIT): 0 the tone's, else 1, 2, 1+1', 1+2' -> the line select bits; MOD: 0 the tone's, OFF, RING, NOISE */
static uint32_t cz_lines(const uint8_t *b, const cz_ed_t *ed) { return ed->line ? ed->line - 1u : b[0] & 3u; }
static uint32_t cz_mod(const uint8_t *b, const cz_ed_t *ed)
{
    static const uint8_t M[4] = {0, 0, 4, 3};
    return ed->mod ? M[ed->mod & 3u] : (b[15] >> 3) & 7u;
}
static void cz_native_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t part = (uint32_t)(t - trk) % NPART;
    const uint8_t *b = cz_tone[part];
    const cz_ed_t *ed = &cz_ed[part];
    cz_voice_t *c = cz_voice(t, v); cz_env_def_t (*defs)[3] = cz_defs[part], dca; cz_hw_pd_t pd[2];
    uint32_t inc[2], ph[2] = {v->ph[0],v->ph[1]}, tg[2] = {(uint32_t)v->s[0],(uint32_t)v->s[1]};
    int32_t amp[2][2], dc[2] = {v->s[2],v->s[3]}, step[2];
    uint32_t ls = cz_lines(b, ed), first = ls == 1u ? 1u : 0u, last = ls >= 2u ? 1u : first;
    int32_t vib = cz_native_vibrato(c,b,ed->vib), oct = (b[0]>>2) == 1u ? 192 : (b[0]>>2) == 2u ? -192 : 0;
    uint32_t kfnote = v->note > 36u ? v->note-36u : 0u, modulation = cz_mod(b, ed);
    for (uint32_t l=0;l<2u;l++) {
        uint32_t src = ls == 2u ? 0u : l, off = src*57u;
        const cz_env_def_t *da = &defs[src][2];
        uint32_t av = 15u-(b[20u+off]>>4), al = b[16u+off]>>4, akf = b[16u+off] & 15u;
        if (akf) {                                       /* Native DCA key-follow is rate scaling; higher keys */
            dca = *da;                                   /* run faster */
            for (uint32_t k = 0; k < 8u; k++)
                dca.rate[k] += (uint32_t)(((uint64_t)dca.rate[k] * akf * kfnote) / 96u);
            da = &dca;
        }
        amp[l][0] = cz_native_amp(c->eg[l][2].level,al,av,v->vel);
        int32_t pitch = cz_env_tick(&c->eg[l][0],&defs[src][0],v->gate);
        int32_t depth = cz_env_tick(&c->eg[l][1],&defs[src][1],v->gate);
        cz_env_tick(&c->eg[l][2],da,v->gate);
        amp[l][1] = cz_native_amp(c->eg[l][2].level,al,av,v->vel);
        step[l] = (amp[l][1]-amp[l][0])*256/(int32_t)n;
        uint32_t pv = 15u-(b[54u+off]>>4), wv = 15u-(b[37u+off]>>4);
        if (pv) pitch = mulq16(pitch, (127u*15u-(127u-v->vel)*pv)*65536u/(127u*15u));
        if (wv) depth = mulq16(depth, (127u*15u-(127u-v->vel)*wv)*65536u/(127u*15u));
        uint32_t kf = b[18u+off];
        if (kf) depth = depth*96/(int32_t)(96u+kf*kfnote);
        int32_t det = l ? ((int32_t)b[3]*16+(b[2]>>2)*16/64)*(b[1] ? -1 : 1) + ed->det16 : 0;
        int32_t nt = clamp(m->pitch16+oct+vib+(pitch>>13)+det,0,2047);
        inc[l] = PITCH_INC[nt];
        inc[l] = fine_inc(inc[l], m->fine + (l ? ed->detf : 0));   /* (Optimist: dsp.c, the same) */
        uint32_t word = (uint32_t)b[14u+off]<<8 | b[15u+off];
        uint32_t dep = (uint32_t)clamp((depth>>14)+ed->dcw+((m->cutoff+m->shape-(64<<8))>>5),0,1023);
        cz_native_pd(&pd[l],word,dep);
    }
    for (uint32_t i=0;i<n;i++) {
        int32_t line[2] = {0,0};
        for (uint32_t l=first;l<=last;l++) {
            uint32_t old=ph[l], delta=inc[l];
            if (l && (modulation==3u)) {
                xorshift32(&c->noise);   /* (Optimist: dsp_common.h, the same xorshift32) */
                if (c->noise&1u) delta = delta > 0x1428A2F9u ? 0x7FFFFFFFu : (delta>>8)*1625u;
            }
            int32_t raw=cz_native_wave(&pd[l],ph[l],tg[l]); ph[l]+=delta;
            if (ph[l]<old) tg[l]^=1u;
            dc[l]+=raw-(dc[l]>>10); raw=(raw-(dc[l]>>10))>>1;
            line[l]=mulq15(raw,amp[l][0]+((step[l]*(int32_t)i)>>8));
        }
        int32_t sample=first==last ? line[first] : (modulation==4u) ? mulq15(line[0],line[1])*2 : (line[0]+line[1])>>1;
        out[i]+=mulq15(mulq15(sample, amp_at(m, i)), VOICE_FS) << CZ_GAIN;
    }
    v->ph[0]=ph[0];v->ph[1]=ph[1];v->s[0]=(int32_t)tg[0];v->s[1]=(int32_t)tg[1];v->s[2]=dc[0];v->s[3]=dc[1];
}
