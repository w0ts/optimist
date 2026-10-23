/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Stephane Schmitz
 * Based on reSID 0.16 (a MOS 6581 / 8580 SID emulator, Copyright (C) 2004 Dag Lem <resid@nimrod.no>, GPL-2.0-or-later;
 * github.com/simonowen/resid), the combined-waveform and filter-curve data in sid_tab.h come from it as they are */
/* SID: one voice of reSID in integer arithmetic: the chip's waveform generator, its multimode filter and its external
 * high-pass, at the engine's own sample rate (the chip's 985248 Hz PAL clock is 22.3 cycles a sample). The voice's
 * amplitude is the part's ADSR, not the chip's envelope generator (so no ADSR delay bug), and the three voices of a
 * part are three notes of the part, each with its own copy of the filter and the modulator that is its SYNC / RING MOD
 * partner (the chip has one filter for its three voices and sync / ring between them).
 *
 * EDIT 1 (OSC):    WAVE  TRI  SAW  PULSE  NOISE, and the combined waveforms TRI+SAW, TRI+PUL, SAW+PUL, T+S+P. These
 *                        are not the AND of the waves: reSID plays back what the combined waves of real chips measured
 *                        (4096 samples each, the 6581's thin and dark, the 8580's fuller), with the pulse gating them
 *                  PW    pulse width, 0..127 = 0..100 % (the track's ENV / LFO -> SHP and the matrix's SHP move it: PWM)
 *                  MOD   OFF, SYNC (the oscillator restarts when the modulator's MSB rises), RING (the triangle's MSB
 *                        is XORed with the modulator's: TRI and the combined waves with TRI), or both
 *                  RATIO the modulator's pitch against the note, -24..+24 semitones
 * EDIT 2 (FILTER): MODE  OFF (bypass), LP, BP, LP+BP, HP, NOTCH (LP+HP), BP+HP, ALL: the chip's three mode bits
 *                  CUT   the cutoff, 0..127 across the chip's 11-bit FC register: the 6581's curve is flat and lazy at
 *                        the bottom, has a step in the middle (FC 1023 -> 1024) and runs up to 18 kHz; the 8580's is
 *                        linear to 12.5 kHz
 *                  RES   resonance, in the chip's 16 steps (Q 0.7 .. 1.7)
 *                  MODEL 6581 or 8580: the waveform tables, the cutoff curve and the DC level of the voices
 *
 * The noise is the chip's 23-bit shift register (taps 22 and 17, the 8 bits read from 22 20 16 13 11 7 4 2) clocked when
 * the oscillator's bit 19 rises: low notes crackle, high notes hiss. All waveforms are the chip's naive ones, aliasing
 * included, as in reSID. The filter is reSID's (two integrators, Euler steps), 4 steps a sample, cutoff limited to 12 kHz
 * (reSID's 8-cycle steps stop at 4 kHz).
 *
 * Voices: three per part (engine_t.poly), as many as the chip has. The voice state is in the voice: ph[0] the
 * oscillator's 24-bit accumulator (in the top of the word), ph[1] the modulator's, s[0] s[1] s[4] the filter's Vbp Vlp
 * Vhp, s[2] the noise register, s[5] the external filter's high-pass (Q8), s[6] the DC level of the waveform. */
#include "sid_tab.h"
#define SID_POLY 3
#define SID_NSTEP 4                                      /* filter steps a sample */
#define SID_DT256 ((985248 * 256) / FS / SID_NSTEP)      /* a step in chip cycles, Q8 (reSID: 8 cycles at most) */
#define SID_EXT_K ((105 * 985248 / 16) / FS)             /* the external high-pass (16 Hz): w0 * cycles a sample, Q16 */
static const char *const N_SID_WAVE[] = {"TRI", "SAW", "PULSE", "NOISE", "TRI+SAW", "TRI+PUL", "SAW+PUL", "T+S+P"};
static const uint8_t SID_WMASK[8] = {1, 2, 4, 8, 3, 5, 6, 7};      /* bit 0 TRI, 1 SAW, 2 PULSE, 3 NOISE */
static const char *const N_SID_MOD[] = {"OFF", "SYNC", "RING", "SYN+RNG"};
static const char *const N_SID_FMODE[] = {"OFF", "LP", "BP", "LP+BP", "HP", "NOTCH", "BP+HP", "ALL"};   /* 0 bypass, else the mode bits */
static const char *const N_SID_MODEL[] = {"6581", "8580"};
#define SID_LFSR_INIT 0x7FFFF8u
#define SID_OVER 8                                       /* the sampled waves are read at 8 points of the sample, and averaged */
/* make-up gain of the sampled waves (Q4, 16 = 1): the 6581's are spikes on silence, 15 to 25 dB below the 8580's (TRI+SAW
 * mean 1.5 of 255, T+S+P 0.4); the soft knee at the end takes the spikes the gain lifts. [model][TRI+SAW, TRI+PUL, SAW+PUL, T+S+P] */
static const uint8_t SID_TGAIN[2][4] = {{64, 16, 40, 96}, {16, 16, 16, 16}};

AINL uint32_t sid_lfsr(uint32_t r) { return ((r << 1) | (((r >> 22) ^ (r >> 17)) & 1u)) & 0x7FFFFFu; }

/* the 12-bit output of the waveform generator (reSID WaveformGenerator::output): ph0 / ph1 the accumulators (24 bits
 * in the top of the word), pw 12 bits, wt the model's combined waveforms */
AINL uint32_t sid_w12(uint32_t mask, uint32_t ph0, uint32_t ph1, uint32_t pw, uint32_t ring, uint32_t lf,
                               const uint8_t (*wt)[4096])
{
    uint32_t a = ph0 >> 20, tri = (((ring ? ph0 ^ ph1 : ph0) & 0x80000000u ? ~ph0 : ph0) >> 19) & 0xFFFu;
    uint32_t pul = a >= pw ? 0xFFFu : 0u;
    switch (mask) {
    case 1:
        return tri;
    case 2:
        return a;
    case 4:
        return pul;
    case 8:
        return (((lf >> 22) & 1u) << 11) | (((lf >> 20) & 1u) << 10) | (((lf >> 16) & 1u) << 9) | (((lf >> 13) & 1u) << 8) |
               (((lf >> 11) & 1u) << 7) | (((lf >> 7) & 1u) << 6) | (((lf >> 4) & 1u) << 5) | (((lf >> 2) & 1u) << 4);
    case 3:
        return (uint32_t)wt[0][a] << 4;
    case 5:
        return ((uint32_t)wt[1][tri >> 1] << 4) & pul;
    case 6:
        return ((uint32_t)wt[2][a] << 4) & pul;
    default:
        return ((uint32_t)wt[3][a] << 4) & pul;
    }
}

static void sid_note_on(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    uint32_t mask = SID_WMASK[(uint32_t)p[P_E0] & 7u], pw = (uint32_t)clamp(p[P_E1] * 32, 16, 4080), k, sum = 0;
    v->ph[1] = 0;
    if (!v->s[2])
        v->s[2] = (int32_t)SID_LFSR_INIT;
    v->s[0] = v->s[1] = v->s[4] = v->s[5] = 0;        /* (a retrigger of a sounding voice keeps them: voice.c voice_start) */
    if (mask != 8u) {                                 /* the waveform's mean over a cycle: the voice plays around it, as the chip's */
        for (k = 0; k < 64u; k++)                     /* external filter would take it out (a narrow pulse is mostly DC) */
            sum += sid_w12(mask, (k << 26) + (1u << 25), 0, pw, 0, 0, SID_WAVE[p[P_E7] != 0]);
        v->s[6] = (int32_t)(sum >> 6) - 2048;
    } else {
        v->s[6] = 0;
    }
}

static void sid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t mask = SID_WMASK[(uint32_t)p[P_E0] & 7u], md = (uint32_t)p[P_E2] & 3u, fm = (uint32_t)p[P_E4] & 7u, i;
    uint32_t sync = md & 1u, ring = md >> 1, model = p[P_E7] != 0;
    const uint8_t (*wt)[4096] = SID_WAVE[model];
    uint32_t pw = (uint32_t)clamp(p[P_E1] * 32 + ((m->shape - (64 << 8)) >> 3), 16, 4080);
    uint32_t inc = m->inc, inc2 = 0, ph0 = v->ph[0], ph1 = v->ph[1], lf = (uint32_t)v->s[2] & 0x7FFFFFu;
    int32_t vbp = v->s[0], vlp = v->s[1], vhp = v->s[4], ext = v->s[5], dc = v->s[6], q = 0, wd = 0, gain = 16;
    uint32_t tab = mask == 3u || (mask >= 5u && mask != 8u);                 /* a sampled wave: SID_WAVE */
    uint32_t d0 = inc >> 3, d1 = 0;                                      /* (SID_OVER points a sample) */
    if (!lf)
        lf = SID_LFSR_INIT;
    if (sync || ring)
        inc2 = pitch_inc(clamp(m->pitch16 + p[P_E3] * 16, 0, 2047));
    d1 = inc2 >> 3;
    if (tab)
        gain = SID_TGAIN[model][mask == 3u ? 0 : mask - 4u];
    if (fm) {
        int32_t cq8 = clamp((p[P_E5] << 8) + m->cutoff, 0, 127 << 8), fc = (cq8 * 4128) >> 16;     /* 0 .. 2047 */
        wd = (((int32_t)SID_W0[model][fc] << 1) * SID_DT256) >> 14;                                 /* w0 * step / 64 */
        q = SID_Q1024[(p[P_E6] >> 3) & 15];
    }
    for (i = 0; i < n; i++) {
        uint32_t w = sid_w12(mask, ph0, ph1, pw, ring, lf, wt);
        int32_t y;
        if (tab) {                                    /* the table's spikes are narrower than a sample's step: a box average */
            uint32_t j;                               /* keeps them from folding down as noise */
            for (j = 1; j < SID_OVER; j++)
                w += sid_w12(mask, ph0 + j * d0, ph1 + j * d1, pw, ring, lf, wt);
            w >>= 3;
        }
        y = (((int32_t)w - 2048 - dc) * 2 * gain) >> 4;                       /* reSID: * envelope 255 >> 7 */
        uint32_t old = ph0;
        ph0 += inc;
        if (mask == 8u) {                             /* the shift register is clocked as the accumulator's bit 19 rises */
            uint32_t c = (((ph0 - 0x08000000u) >> 28) - ((old - 0x08000000u) >> 28)) & 15u;
            while (c--)
                lf = sid_lfsr(lf);
        }
        if (inc2) {
            uint32_t o1 = ph1;
            ph1 += inc2;
            if (sync && !(o1 & 0x80000000u) && (ph1 & 0x80000000u)) {      /* the modulator's MSB rose: restart, and count */
                uint32_t f = (ph1 - 0x80000000u) / ((inc2 >> 16) | 1u);     /* the part of this sample after it, Q16 */
                ph0 = mulq16((int32_t)inc, f > 65535u ? 65535u : f);
            }
        }
        if (fm) {                                     /* reSID Filter::clock: Vi in, Vhp Vbp Vlp out */
            int32_t j, vi = y;
            for (j = 0; j < SID_NSTEP; j++) {
                int32_t dbp = (wd * vhp) >> 14, dlp = (wd * vbp) >> 14;
                vbp -= dbp;
                vlp -= dlp;
                vhp = ((vbp * q) >> 10) - vlp - vi;
            }
            vbp = clamp(vbp, -100000, 100000);          /* (a runaway is a wrong cutoff / resonance only here) */
            vlp = clamp(vlp, -100000, 100000);
            vhp = clamp(vhp, -100000, 100000);
            y = 0;
            if (fm & 1u)
                y += vlp;
            if (fm & 2u)
                y += vbp;
            if (fm & 4u)
                y += vhp;
        }
        y = clamp(y, -20000, 20000);
        ext += (((y << 8) - ext) * SID_EXT_K) >> 16;  /* the external high-pass: the chip's output stage takes the DC out */
        out[i] += mulq15(mulq15(soft_knee((y - (ext >> 8)) << 3, 32000), amp_at(m, i)), VOICE_FS);
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = vbp;
    v->s[1] = vlp;
    v->s[2] = (int32_t)lf;
    v->s[4] = vhp;
    v->s[5] = ext;
}

/* name, {WAVE, PW, MOD, RATIO, MODE, CUT, RES, MODEL}, {A D S R}, fenv, mono, fx, pattern.
 * CUT is the FC register's 11 bits over 0..127: on the 6581 (MODEL 0) 0..24 is 220..300 Hz, 36 600 Hz, 48 1.6 kHz, 60
 * 4.4 kHz, 80 9.5 kHz, above it opens to 18 kHz; on the 8580 (MODEL 1) it is linear, about 100 Hz a step, so the same
 * patch wants a lower CUT. The first eight are the engine's first sounds, the rest show what reSID adds: the two chips
 * (the same patch on both: GROWL / ACID, the REEDs), the combined waveforms of real chips (PS, ST, PT, PST; the 6581's T+S+P
 * is a table of 7 values, thin and rough: the 8580's is the one to play), every filter
 * mode, SYNC and RING with a RATIO, and the noise register at low notes */
static const preset_t SID_PRESETS[] = {
    {"PULSE LEAD", {2, 40, 0, 0, 1, 48, 50, 0}, {0, 70, 100, 45}, 20, 1, FX(0, 10, 40, 25)},
    {"SAW BASS", {1, 0, 0, 0, 1, 14, 70, 0}, {0, 60, 60, 30}, 60, 1, FX(10, 0, 10, 10)},
    {"TRI FLUTE", {0, 0, 0, 0, 0, 0, 0, 0}, {25, 85, 110, 60}, 0, 0, FX(0, 20, 25, 45)},
    {"SYNC LEAD", {1, 0, 1, 7, 1, 52, 30, 0}, {0, 75, 100, 40}, 25, 1, FX(10, 10, 35, 25)},
    {"RING BELL", {0, 0, 2, 19, 0, 0, 0, 0}, {0, 85, 0, 75}, 0, 0, FX(0, 15, 35, 60)},
    {"NOISE HAT", {3, 0, 0, 0, 4, 80, 20, 0}, {0, 45, 0, 25}, 0, 0, FX(0, 0, 15, 20)},
    {"PWM STRINGS", {2, 64, 0, 0, 1, 36, 20, 0}, {70, 90, 110, 85}, 10, 0, FX(0, 55, 20, 60)},
    {"COMBO BITE", {6, 50, 0, 0, 1, 33, 90, 0}, {0, 62, 50, 30}, 35, 1, FX(20, 0, 20, 15)},
    /* the two chips: one patch each. The 6581's filter is lazy and rough, its resonance sings; the 8580's is clean */
    {"6581 GROWL", {1, 0, 0, 0, 1, 10, 110, 0}, {0, 55, 40, 25}, 70, 1, FX(25, 0, 10, 10)},
    {"8580 ACID", {1, 0, 0, 0, 1, 12, 110, 1}, {0, 55, 40, 25}, 50, 1, FX(25, 0, 10, 10)},
    /* the combined waveforms are samples of real chips, gated by the pulse: the 6581's are rough, the 8580's smooth */
    {"6581 PS REED", {6, 70, 0, 0, 1, 60, 40, 0}, {10, 70, 90, 50}, 20, 0, FX(0, 15, 25, 30)},
    {"8580 PS REED", {6, 70, 0, 0, 1, 35, 40, 1}, {10, 70, 90, 50}, 20, 0, FX(0, 15, 25, 30)},
    {"8580 ST PAD", {4, 0, 0, 0, 1, 40, 30, 1}, {80, 95, 100, 90}, 15, 0, FX(0, 50, 20, 60)},
    {"HOLLOW KEYS", {5, 60, 0, 0, 0, 0, 0, 0}, {0, 75, 20, 45}, 0, 0, FX(0, 15, 25, 40)},
    {"PST VOWEL", {7, 45, 0, 0, 2, 15, 100, 1}, {20, 80, 90, 50}, 20, 1, FX(0, 20, 30, 35)},
    /* SYNC and RING MOD against the modulator RATIO semitones up (the modulator is a second oscillator, as voice 3 is) */
    {"SYNC RIP", {1, 0, 1, 3, 1, 56, 60, 0}, {0, 75, 100, 40}, 30, 1, FX(15, 10, 30, 20)},
    {"RING CLANG", {5, 64, 2, 13, 0, 0, 0, 0}, {0, 70, 0, 70}, 0, 0, FX(0, 20, 30, 55)},
    {"RING SYNC", {0, 0, 3, 5, 1, 70, 40, 0}, {0, 70, 60, 50}, 20, 1, FX(0, 15, 25, 30)},
    /* the filter's modes: NOTCH, BP, HP */
    {"NOTCH PAD", {1, 0, 0, 0, 5, 20, 70, 1}, {85, 95, 100, 90}, 20, 0, FX(0, 45, 20, 55)},
    {"WAH LEAD", {2, 30, 0, 0, 2, 40, 120, 0}, {0, 70, 100, 40}, 45, 1, FX(10, 10, 35, 25)},
    {"HP PLUCK", {2, 32, 0, 0, 4, 28, 60, 0}, {0, 50, 0, 40}, 15, 0, FX(0, 15, 30, 30)},
    /* the 23-bit noise register: low notes crackle in pitch, high notes hiss; BP and fenv make hits and zaps */
    {"NOISE BASS", {3, 0, 0, 0, 1, 25, 80, 0}, {0, 70, 70, 40}, 30, 1, FX(10, 0, 10, 10)},
    {"NOISE SNARE", {3, 0, 0, 0, 2, 70, 30, 0}, {0, 50, 0, 35}, 0, 0, FX(0, 0, 20, 30)},
    {"NOISE ZAP", {3, 0, 0, 0, 2, 100, 90, 0}, {0, 60, 0, 45}, -50, 0, FX(0, 0, 40, 35)},
    {"C64 ARP", {2, 32, 0, 0, 1, 60, 20, 0}, {0, 45, 0, 30}, 0, 0, FX(0, 0, 25, 15)},
    {"8580 PULSE", {2, 50, 0, 0, 1, 30, 40, 1}, {0, 70, 100, 45}, 20, 1, FX(0, 10, 40, 25)},
};

static const engine_t ENG_SID = {
    .name = "SID",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        PN("WAVE", F_ENUM, 0, 7, 2, N_SID_WAVE),
        PU("PW", F_PCT, 0, 127, 64, 0),
        PN("MOD", F_ENUM, 0, 3, 0, N_SID_MOD),
        PU("RATIO", F_SEMI, -24, 24, 7, 0),
        PN("MODE", F_ENUM, 0, 7, 1, N_SID_FMODE),
        PU("CUT", F_PCT, 0, 127, 90, 0),            /* (the FC register's 11 bits, not Hz: the 6581's curve) */
        PU("RES", F_PCT, 0, 127, 40, 0),
        PN("MODEL", F_ENUM, 0, 1, 0, N_SID_MODEL),
    },
    .presets = SID_PRESETS,
    .npresets = sizeof(SID_PRESETS) / sizeof(SID_PRESETS[0]),
    .note_on = sid_note_on,
    .render = sid_render,
    .macro = {P_E0, P_E1, P_E5, P_E6},
    .poly = SID_POLY,
    .fil_page = 1,
    .color = 0x9C9F,                             /* #9e93fc (tools/colors.json) */
};
