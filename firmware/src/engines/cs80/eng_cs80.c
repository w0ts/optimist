/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 twochannels (CS-1), the Optimist contributors */
/* CS80 (engine 26, FELUCCA_ENG_CS80, off by default): a Yamaha CS-80 style voice, after twochannels' CS-1 for the
 * FM-1 (CS-1 0.3.4 5dce8fa, GPL-3.0-only: firmware/src/cs1_voice.h). Its voice's design here, in our controls: two
 * channels a voice (I and II), each a VCO (saw, pulse, both, saw with the sine after the filters, noise) through a
 * high-pass and a low-pass in series (12 dB each, resonant, never self-oscillating), the MIX lever between them, channel
 * II at its own footage and detuned by DTUNE, and the ring modulator. The track's ENV is both channels' VCA, its ENV >
 * FLT the filter envelope (moving both filters, as the CS-80's VCF envelope and BRILLIANCE do), its LFO the sub
 * oscillator, its SHP the pulse width; velocity opens the filters a little (INITIAL BRILLIANCE). Not taken from CS-1:
 * its panel and patch format, its presets (18 of them converted from a commercial CS-80 emulation's) and its RANDOM
 * model (statistics of commercial presets); the presets here are our own. Optimist is not affiliated with or endorsed
 * by Yamaha.
 *   EDIT 1 (VCO)  I     channel I's wave: SAW, PULSE, BOTH, SAW+SIN (the sine added after the filters), NOISE
 *                 II    channel II's, the same list
 *                 II FT channel II's footage against I's 8': 16' 8' 5.3' 4' 2.7' 2'
 *                 MIX   -64 I alone, 0 both, 63 II alone
 *   EDIT 2 (VCF)  HPF LPF  the two filters' cutoffs (key tracking 1:1; the track's FLT and ENV > FLT move both)
 *                 RES   both filters' resonance
 *                 RING  the ring modulator: 0 off, then its sine's rate from ~8 Hz to ~2.6 kHz
 * The track's DTUNE (VOICE 2) detunes channel II up to ~20 cents (and the unison voices as on every engine).
 * Voice state: ph[0] channel I, ph[1] channel II, ph[2] the ring modulator's sine; s[0..1] I's high-pass, s[2..3] I's
 * low-pass, s[4..5] II's high-pass, s[6..7] II's low-pass. */
#define CS80_POLY 8u
enum { CS_SAW, CS_PULSE, CS_BOTH, CS_SSIN, CS_NOISE, CS_NWAVE };
static const char *const N_CS80_W[CS_NWAVE] = {"SAW", "PULSE", "BOTH", "SAW+SIN", "NOISE"};
static const char *const N_CS80_FT[6] = {"16'", "8'", "5.3'", "4'", "2.7'", "2'"};
static const int8_t CS80_FT[6] = {-12, 0, 7, 12, 19, 24};   /* semitones against 8' */

static void cs80_note_on(track_t *t, voice_t *v)
{
    uint32_t i;
    (void)t;
    if (!v->env && !v->env_out) {                     /* from silence: the channels a third of a turn apart (no comb
                                                       * when they meet), the filters at rest */
        v->ph[0] = 0;
        v->ph[1] = 0x55555555u;
        v->ph[2] = 0;
        for (i = 0; i < 8u; i++)
            v->s[i] = 0;
    }
}

/* the damping (Q12) of RES: 2.0 (none) .. ~0.33 (a ~+10 dB peak; the CS-80's filters never self-oscillate) */
AINL int32_t cs80_k(int32_t res) { return 8192 - clamp(res, 0, 127) * 54; }

/* white noise from the phase alone (no state: an integer hash of it) */
AINL int32_t cs80_noise(uint32_t ph)
{
    uint32_t x = ph ^ (ph >> 15);
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    return (int32_t)(x >> 16) - 32768;
}

/* one sample of a channel's VCO, Q15 */
AINL int32_t cs80_vco(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw)
{
    int32_t x;
    if (w == CS_NOISE)
        return cs80_noise(ph) >> 1;
    x = osc_saw(ph, inc);
    if (w == CS_PULSE || w == CS_BOTH) {             /* the pulse from the same saw: one blep saved on BOTH */
        int32_t pl = (x - osc_saw(ph + pw, inc)) >> 1;
        x = w == CS_PULSE ? pl : (x + pl) >> 1;
    }
    return x;
}

/* channel c of voice v, n samples ADDED to acc (Q15 x its MIX gain): VCO, high-pass, low-pass (+ the sine) */
AINL void cs80_chan(voice_t *v, uint32_t c, uint32_t w, uint32_t inc, uint32_t pw, int32_t gain, const tsvf_t *hp,
                    const tsvf_t *lp, int32_t k, int32_t *acc, uint32_t n)
{
    int32_t h1 = v->s[4u * c], h2 = v->s[4u * c + 1u], l1 = v->s[4u * c + 2u], l2 = v->s[4u * c + 3u];
    uint32_t ph = v->ph[c], i;
    for (i = 0; i < n; i++, ph += inc) {
        int32_t x = cs80_vco(w, ph, inc, pw), bp, lo, hi, y;
        lo = tsvf_tick(hp, x, &h1, &h2, &bp);
        hi = x - ((k * bp) >> 12) - lo;                /* the high-pass out */
        y = tsvf_tick(lp, clamp(hi, -65535, 65535), &l1, &l2, &bp);
        y = soft_knee(y, 24000);
        if (w == CS_SSIN)
            y -= osc_sine(ph) >> 1;                    /* the rising saw's fundamental is -sin: in phase with it */
        acc[i] += mulq15(clamp(y, -65535, 65535), gain);
    }
    v->ph[c] = ph;
    v->s[4u * c] = h1;
    v->s[4u * c + 1u] = h2;
    v->s[4u * c + 2u] = l1;
    v->s[4u * c + 3u] = l2;
}

static void cs80_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)clamp(p[P_E0], 0, CS_NWAVE - 1), w2 = (uint32_t)clamp(p[P_E1], 0, CS_NWAVE - 1);
    int32_t mix = clamp(p[P_E3], -64, 63), ring = clamp(p[P_E7], 0, 127), k = cs80_k(p[P_E6]);
    int32_t g1 = mix <= 0 ? 32767 : (63 - mix) * 520, g2 = mix >= 0 ? 32767 : (64 + mix) * 512;   /* the MIX lever */
    int32_t pwq = clamp(16384 - ((m->shape - (64 << 8)) >> 1), 1638, 16384);   /* Q15 of a turn: 50 % .. 5 % (SHP) */
    int32_t mod = m->cutoff + ((m->pitch16 - 60 * 16) << 4) + (((int32_t)v->vel - 100) * 24);   /* FLT, keys 1:1, velocity */
    int32_t acc[CTL];
    uint32_t pw = (uint32_t)pwq << 17, i;
    tsvf_t hp, lp;
    if (n == 0 || n > CTL)
        return;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    tsvf_coef_k(&hp, (p[P_E4] << 8) + mod, k);
    tsvf_coef_k(&lp, (p[P_E5] << 8) + mod, k);
    if (g1)
        cs80_chan(v, 0, w1, m->inc, pw, g1, &hp, &lp, k, acc, n);
    if (g2) {
        int32_t det = (int32_t)t->p[P_DETUNE] * 20 / 127;   /* cents */
        uint32_t inc2 = det_inc(m->pitch16 + CS80_FT[clamp(p[P_E2], 0, 5)] * 16, det, m->fine_all);
        cs80_chan(v, 1, w2, inc2, pw, g2, &hp, &lp, k, acc, n);
    }
    if (ring) {                                        /* the ring modulator: the sum times a sine, 3/4 wet */
        uint32_t rinc = pitch_inc((uint32_t)((ring - 1) * 1600 / 126)), rph = v->ph[2];
        for (i = 0; i < n; i++, rph += rinc)
            acc[i] = mulq15(acc[i], 8192) + mulq15(mulq15(acc[i], osc_sine(rph)), 24576);
        v->ph[2] = rph;
    }
    for (i = 0; i < n; i++)
        out[i] += mulq15(mulq15(soft_knee(acc[i] >> 1, 24000), amp_at(m, i)), VOICE_FS) << 1;
}

static const preset_t CS80_PRESETS[] = {
    /* {I, II, II FT, MIX, HPF, LPF, RES, RING}, {A D S R}, ENV -> FILTER, mono */
    {"CS BRASS", {0, 0, 1, 0, 0, 46, 24, 0}, {22, 72, 100, 44}, 40, 0, FX(0, 30, 10, 40), XP(P_DETUNE + 1, 55)},
    {"CS BLADE", {0, 1, 1, 0, 8, 58, 30, 0}, {44, 92, 112, 96}, 26, 0, FX(0, 60, 30, 90),
     XP(P_DETUNE + 1, 70, P_LRATE + 1, 24, P_LEG_PIT + 1, 4)},
    {"CS STRINGS", {1, 0, 1, -10, 16, 70, 10, 0}, {70, 100, 115, 90}, 10, 0, FX(0, 70, 10, 70),
     XP(P_DETUNE + 1, 60, P_LEG_SHP + 1, 24)},
    {"CS PAD", {2, 3, 3, 10, 18, 54, 20, 0}, {90, 100, 120, 100}, 16, 0, FX(0, 60, 20, 80), XP(P_DETUNE + 1, 45)},
    {"CS BELL", {1, 3, 2, 0, 30, 92, 10, 92}, {0, 90, 0, 90}, 0, 0, FX(0, 20, 30, 60)},
    {"CS RING", {0, 1, 1, 0, 0, 80, 40, 60}, {0, 80, 90, 50}, 20, 0, FX(0, 10, 20, 40)},
    {"CS BASS", {0, 1, 0, -20, 0, 34, 50, 0}, {0, 60, 70, 20}, 46, 1, FX(10, 0, 0, 10)},
    {"CS LEAD", {0, 0, 3, 0, 10, 70, 40, 0}, {0, 70, 110, 40}, 20, 1, FX(0, 20, 30, 30),
     XP(P_GLIDE + 1, 25, P_DETUNE + 1, 50)},
    {"CS ORGAN", {1, 1, 4, 0, 20, 100, 0, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 30, 0, 30)},
    {"CS WIND", {4, 4, 1, 0, 40, 70, 90, 0}, {90, 100, 110, 100}, 30, 0, FX(0, 0, 20, 80)},
    {"CS CLAV", {1, 0, 3, -30, 40, 86, 30, 0}, {0, 50, 0, 30}, 40, 0, FX(0, 10, 10, 20)},
    {"CS CHOIR", {3, 3, 1, 0, 30, 50, 60, 0}, {80, 100, 120, 90}, 10, 0, FX(0, 60, 10, 80), XP(P_DETUNE + 1, 50)},
};

static const engine_t ENG_CS80 = {
    .name = "CS80",
    .page_title = {"VCO", "VCF"},
    .edit = {
        PN("I", F_ENUM, 0, CS_NWAVE - 1, 0, N_CS80_W),
        PN("II", F_ENUM, 0, CS_NWAVE - 1, 0, N_CS80_W),
        PN("II FT", F_ENUM, 0, 5, 1, N_CS80_FT),
        PU("MIX", F_BIPCT, -64, 63, 0, 0),
        PU("HPF", F_CUTOFF, 0, 127, 0, 0),
        PU("LPF", F_CUTOFF, 0, 127, 70, 0),
        PU("RES", F_PCT, 0, 127, 20, 0),
        PU("RING", F_PCT, 0, 127, 0, 0),
    },
    .presets = CS80_PRESETS,
    .npresets = sizeof(CS80_PRESETS) / sizeof(CS80_PRESETS[0]),
    .fil_page = 1,
    .note_on = cs80_note_on,
    .render = cs80_render,
    .color = 0xECD5,                             /* #e89aa8 (tools/colors.json) */
    .macro = {P_E5, P_E6, P_E3, P_E4},
    .poly = CS80_POLY,
};
