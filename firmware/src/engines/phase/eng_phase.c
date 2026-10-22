/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Phase distortion. The waveforms are a C port of the oscillator of
 * CrispyZebra (Leo Kuroshita, GPL-3.0, github.com/hugelton/CrispyZebra), the
 * author's own phase-distortion core: a -cos table read through a bent phase whose
 * bend is DCW. WAVE2 alternates with WAVE every other cycle. A second
 * line (DTN) can be mixed or ring-modulated; SUB adds a sine an octave down.
 * Envelopes are Felucca's own (ADSR, ENV -> DCW).
 * FOLD (E7, builder item PHASE_FOLD; Felucca-Engines 04ffe90 src/eng_phase.c, MIT, Leo Kuroshita): a sine wavefolder on
 * the voice, after the lines and before SUB and the amp. 0 is off (the loop and the sound without it). Up from 0 a soft
 * sine saturation, then folds: the gain rises to ~8x (four folds of a full-scale wave). Alias-aware: the sine fold has
 * no corners, and above A4 the gain added past the saturation falls with the pitch (1/f), so the top notes fold less. */
static const char *const N_PD_WAVE[] = {"SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_WAVE2[] = {"-", "SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_LINE[] = {"MIX", "RING"};

#define pd_cos neg_cos16                        /* -cos, Q15, from the 16-bit PD phase (dsp.c) */

/* one wave's bend for a block: break points and the slopes of the bent phase
 * (Q16 reciprocals), so a sample costs a multiply instead of a divide (the
 * bent phase comes out the same or 1 lower in 65536) */
typedef struct {
    uint32_t w, x, peak, rez;
    uint32_t k0, k1;
} pd_t;

AINL uint32_t pd_slope(uint32_t span, uint32_t len) { return (span << 16) / (len ? len : 1u); }

static HOT void pd_setup(pd_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x;
    b->w = w;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, 65535u - x);
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, x);
        break;
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->k0 = pd_slope(65535u, x);
        break;
    case 3:                                                  /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        b->k0 = pd_slope(65535u, x);
        b->k1 = pd_slope(65535u, 65535u - x);
        break;
    case 4:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        b->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!b->peak)
            b->peak = 1;
        b->k0 = pd_slope(32768u, b->peak);
        b->k1 = pd_slope(32767u, x - b->peak);
        break;
    default:                                                 /* RESONANCE */
        x = 0;
        b->rez = (dcw * 44000u) >> 16;
        break;
    }
    b->x = x;
}

/* one sample at phase ph (16 bit): bipolar Q15 */
static HOT int32_t pd_wave(const pd_t *b, uint32_t ph)
{
    uint32_t pd, x = b->x;
    switch (b->w) {
    case 0:                                                  /* SAW */
        pd = ph < x ? (ph * b->k0) >> 16 : 32768u + (((ph - x) * b->k1) >> 16);
        break;
    case 1:                                                  /* SQUARE */
        if (ph < x)
            pd = (ph * b->k0) >> 16;
        else if (ph < 32768u)
            pd = 32768u;
        else if (ph < 32768u + x)
            pd = 32768u + (((ph - 32768u) * b->k1) >> 16);
        else
            pd = 65535u;
        break;
    case 2:                                                  /* PULSE */
        pd = ph < x ? (ph * b->k0) >> 16 : 65535u;
        break;
    case 3:                                                  /* DOUBLE SINE */
        pd = ph < x ? (ph * b->k0) >> 16 : ((ph - x) * b->k1) >> 16;
        break;
    case 4:                                                  /* SAW-PULSE */
        if (ph >= x)
            pd = 65535u;
        else if (ph < b->peak)
            pd = (ph * b->k0) >> 16;
        else
            pd = 32768u + (((ph - b->peak) * b->k1) >> 16);
        break;
    default: {                                               /* RESONANCE: windowed sine, rez = pitch of the core */
        uint32_t rp = (ph + ((ph * b->rez) >> 16) * 7u) & 0xFFFFu, win;
        int32_t core = (pd_cos(rp) * 5) >> 3;
        if (b->w == 5)
            win = 65535u - ph;                               /* saw window */
        else if (b->w == 6)
            win = ph < 32768u ? ph << 1 : (65535u - ph) << 1; /* triangle window */
        else
            win = ph < 16384u ? ph << 2 : ph > 49152u ? (65535u - ph) << 2 : 65535u;   /* trapezoid */
        if (win > 65535u)
            win = 65535u;
        return (core * (int32_t)win) >> 16;                  /* |core| <= 20479: fits 32 bits */
    }
    }
    return pd_cos(pd);
}

#if FELUCCA_PHASE_FOLD
/* s[4] at FOLD 0: the last block had no fold (phase_render: a FOLD going on or off crossfades a block) */
#define PD_UNFOLDED (-2147483647 - 1)
#endif

static void phase_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = 0;
    v->ph[2] = 0;
    v->s[0] = 0;                 /* WAVE / WAVE2 toggle, line 1 */
    v->s[1] = 0;                 /* line 2 */
#if FELUCCA_PHASE_FOLD
    v->s[4] = t->p[P_E7] > 0 ? 0 : PD_UNFOLDED;   /* FOLD's DC blocker (only FOLD > 0 reads them; kept on a */
    v->s[5] = 0;                                   /* retrigger: core/voice.c voice_start) */
    v->s[6] = 163;               /* FOLD's last gain (phase_xfade): FOLD 1's, PD_FOLD_G0, until a folded block */
#endif
}

#if FELUCCA_PHASE_FOLD
/* FOLD's gain (Q8) for a block: 0.64x at FOLD 1 (sin(pi/2 x) then has a small-signal gain of 1: a soft saturation),
 * ~8x at 127; the part above 0.64x scaled by A4 / the note's pitch above A4 (PD_FOLD_REF: 440 Hz) */
#define PD_FOLD_G0 163u
#define PD_FOLD_REF ((uint32_t)((440ull << 32) / FS))
AINL uint32_t pd_fold_gain(uint32_t fold, uint32_t inc)
{
    uint32_t ext = fold * ((2048u - PD_FOLD_G0) << 4) / 127u;          /* Q12, <= 30160 */
    if (inc > PD_FOLD_REF)
        ext = ext * (PD_FOLD_REF >> 12) / (inc >> 12);              /* < 2^32 */
    return PD_FOLD_G0 + (ext >> 4);
}
#else
#define fg 0u
#endif

/* the loop: fg 0 (a constant: the code as it always was), fg > 0 the fold gain (phase_render_f's own copy: the loop
 * without FOLD gains nothing) */
AINL void phase_loop(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m
#if FELUCCA_PHASE_FOLD
                                , uint32_t fg
#endif
                                )
{
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] & 7u, w2 = (uint32_t)p[P_E1], i;
    int32_t depth = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, p[P_E3] * 256);
    uint32_t dcw, inc = m->inc, inc2;
    int32_t det = p[P_E4], line2 = det != 0 || p[P_E5], ring = p[P_E5], sub = p[P_E6] * 200;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t tg0 = v->s[0], tg1 = v->s[1];                    /* WAVE / WAVE2 toggles */
#if FELUCCA_PHASE_FOLD
    int32_t fx1 = fg ? v->s[4] : 0, fhp = fg ? v->s[5] : 0;  /* FOLD's DC blocker: the last fold, its output */
#endif
    pd_t b1, b2;
    depth = clamp(depth + (m->shape - (64 << 8)), 0, 127 << 8);
    dcw = (uint32_t)depth * 65535u / (127u << 8);
    dcw = (dcw * 56000u) >> 16;                              /* the classic range of the bend */
    inc2 = det_inc(m->pitch16, det, m->fine_all);            /* DTN in cents (dsp.c), from osc 1's fine pitch */
    pd_setup(&b1, w1, dcw);
    pd_setup(&b2, w2 ? w2 - 1u : w1, dcw);                   /* WAVE2 (every other cycle) */
    for (i = 0; i < n; i++) {
        uint32_t old = ph0;
        int32_t s;
        s = pd_wave(tg0 ? &b2 : &b1, ph0 >> 16);
        ph0 += inc;
        if (ph0 < old)
            tg0 ^= 1;
        if (line2) {
            uint32_t old2 = ph1;
            int32_t s2 = pd_wave(tg1 ? &b2 : &b1, ph1 >> 16);
            ph1 += inc2;
            if (ph1 < old2)
                tg1 ^= 1;
            s = ring ? mulq15(s, s2) : (s + s2) >> 1;     /* both within +-32767 */
        }
#if FELUCCA_PHASE_FOLD
        if (fg) {   /* sin(pi/2 s gain): full scale x1 is a quarter turn; then a DC blocker (~14 Hz): folding a wave
                     * that sits off centre (PLS, RING, the RESO windows) moves its DC around */
            int32_t f = sine_i((uint32_t)(s * (int32_t)fg) << 7);
            fhp += f - fx1 - (fhp >> 9);
            fx1 = f;
            s = fhp;
        }
#endif
        if (sub) {
            ph2 += inc >> 1;
            s += mulq15(osc_sine(ph2), sub);
        }
        if (fg)     /* (the blocker's step of a full-scale swing, +-65534, and SUB's 25400 on it: s x amp would wrap
                     * 32 bits past 65535; FOLD 0 stays within 58167) */
            s = clamp(s, -65535, 65535);
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = tg0;
    v->s[1] = tg1;
#if FELUCCA_PHASE_FOLD
    if (fg) {
        v->s[4] = fx1;
        v->s[5] = fhp;
        v->s[6] = (int32_t)fg;
    }
#endif
}

#if FELUCCA_PHASE_FOLD
/* the loop's two copies in RAM: phase_render's without FOLD (its code as it always was, tests/target_budget.txt) and
 * this one with a FOLD gain g (0: the same samples as without; the switch's crossfade renders both sides with it) */
static HOT __attribute__((noinline)) void phase_loopf(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m,
                                                      uint32_t g)
{
    phase_loop(t, v, out, n, m, g);
}
#define phase_render_f(t, v, out, n, m) phase_loopf(t, v, out, n, m, pd_fold_gain((uint32_t)(t)->p[P_E7], (m)->inc))

/* FOLD going on or off (a knob, LFO / MOD, automation): FOLD 0 has no DC blocker (the sound without FOLD) and the
 * fold's output has none of the wave's DC, so the switch would step (up to half full scale on PLS / RING). That block
 * is rendered both ways and crossfaded, unfolded -> folded or back (going off: at the last FOLD's gain); going on, the
 * blocker starts afresh as on a note-on. Once per switch: not the loop's cost */
static int32_t pd_xa[CTL], pd_xb[CTL];
static HOT __attribute__((noinline)) void phase_xfade(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m,
                                                      uint32_t on)
{
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], i;
    int32_t tg0 = v->s[0], tg1 = v->s[1], k;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        pd_xa[i] = pd_xb[i] = 0;
    phase_loopf(t, v, pd_xa, n, m, 0);                              /* unfolded; then from the same phases: */
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = tg0;
    v->s[1] = tg1;
    if (on) {
        v->s[4] = v->s[5] = 0;
        phase_render_f(t, v, pd_xb, n, m);
    } else {
        phase_loopf(t, v, pd_xb, n, m, (uint32_t)v->s[6]);         /* (s[6]: set with the last folded block) */
        v->s[4] = PD_UNFOLDED;
    }
    for (i = 0; i < n; i++) {
        k = (int32_t)(on ? i + 1u : n - 1u - i);                    /* the folded one's share, of n */
        out[i] += pd_xa[i] + (pd_xb[i] - pd_xa[i]) * k / (int32_t)n;
    }
}

static HOT void phase_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    if (t->p[P_E7] > 0) {
        if (v->s[4] == PD_UNFOLDED)
            phase_xfade(t, v, out, n, m, 1);
        else
            phase_render_f(t, v, out, n, m);
    } else if (v->s[4] != PD_UNFOLDED) {
        phase_xfade(t, v, out, n, m, 0);
    } else {
        phase_loop(t, v, out, n, m, 0);
    }
}
#else
#undef fg
static HOT void phase_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    phase_loop(t, v, out, n, m);
}
#endif

static const preset_t PHASE_PRESETS[] = {
    /* name, {WAVE, WAVE2, DCW, ENV, DTN, LINE, SUB, FOLD}, {A D S R}, fenv, mono */
    {"CZ BASS", {5, 0, 36, 70, 0, 0, 72, 0}, {0, 64, 64, 22}, 0, 1, FX(35, 0, 0, 4), XP(P_GLIDE + 1, 36, P_TRANS + 1, -24)},
    {"SOFT KEYS", {3, 0, 40, 70, 0, 0, 0, 0}, {0, 80, 20, 50}, 0, 0, FX(0, 35, 14, 28)},
    {"CZ STRING", {0, 4, 50, 40, 12, 0, 0, 0}, {50, 90, 105, 75}, 0, 0, FX(0, 50, 20, 55)},
    {"RESO PLUCK", {5, 0, 60, 60, 0, 0, 0, 0}, {0, 70, 30, 60}, 0, 0, FX(0, 0, 30, 30)},
    {"CZ BRASS", {0, 0, 30, 90, 0, 0, 0, 0}, {8, 70, 90, 40}, 0, 0, FX(0, 20, 14, 26)},
};

static const engine_t ENG_PHASE = {
    "PHASE", {"PHS", "LINE"},
    {
        {"WAVE", F_ENUM, 0, 7, 0, N_PD_WAVE, 0},
        {"WAVE2", F_ENUM, 0, 8, 0, N_PD_WAVE2, 0},
        {"DCW", F_PCT, 0, 127, 60, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"LINE", F_ENUM, 0, 1, 0, N_PD_LINE, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
#if FELUCCA_PHASE_FOLD
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},       /* 0 = off */
#else
        {"-", F_INT, 0, 0, 0, 0, 0},
#endif
    },
    PHASE_PRESETS, sizeof(PHASE_PRESETS) / sizeof(PHASE_PRESETS[0]), 0, phase_note_on, phase_render,
    0x05DF, {P_E2, P_E3, P_E4, P_REL},
};
