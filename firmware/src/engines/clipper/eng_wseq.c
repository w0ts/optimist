/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* WSEQ (engine 21, FELUCCA_ENG_WSEQ, off by default). From isod89/clipper-fm1 firmware/src/eng_wseq.c (Clipper 1.1,
 * GPL-3.0-only). Changes: our engine_t's designated fields, the waves computed (wseq_wave, not FMX's tables), the
 * pitch from pitch_inc and the voice's fine_all (Clipper's hy_inc), RATE in our DUR_LIST indices; the sequences, the
 * stepping and the presets are Clipper's. */
/* WSEQ: wave sequencing (Clipper 0.6, after the Korg Wavestation and wavestate: the idea, not any code). Each note
 * plays a sequence of steps, each its own wave, pitch and level, one after the other at the song's tempo, the next
 * one fading in over the end of the one before (XFADE): rhythmic, stepping timbres from a single key, or (long steps,
 * full crossfade) a sound that keeps changing.
 *   SEQ    one of 16 sequences (WSEQ_SEQ below: up to 16 steps of {wave, semitones, level})
 *   RATE   a step's length: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4, 1/2, a bar (the song's tempo, MIDI clock too)
 *   XFADE  the share of a step the next one fades in over (0: a cut, 127: the whole step)
 *   LOOP   FWD, BACK, PING (back and forth), ONCE (stops on its last step)
 *   START  the first step (each note starts there)
 *   PITCH  how far the steps' pitches go (64: as the sequence has them, 0: every step at the note, 127: twice)
 *   CUT RES  a 12 dB low-pass after them (keyboard ~40 %)
 * The waves: FMX's ten (SINE TRI SAW SQR HALF ABS QUART PULSE SIN8 NOISE). Here computed, not FMX's 26 KB of tables:
 * SAW SQR PULSE (25 %) are dsp.c's polyBLEP oscillators, HALF ABS QUART the rectified sines with their DC taken off and
 * their peak at full scale (as tools/gen_fmx.py makes them), SIN8 a sine on 16 steps.
 * Voice state: ph[0] the step playing, ph[1] the next one (from its fade in), ph[2] samples into the step; s[0..1] the
 * SVF, s[2] the step, s[3] the direction (PING) and 0x100 = stopped (ONCE), s[4] NOISE's state, s[5] the level glided
 * (a cut between two levels: no click). */
#define WSEQ_NSEQ 16u
#define WSEQ_MAXST 16u
enum { FXW_SINE, FXW_TRI, FXW_SAW, FXW_SQR, FXW_HALF, FXW_ABS, FXW_QUART, FXW_PULSE, FXW_SIN8, FXW_NOISE };
/* one sample of wave w at phase ph (FMX's waves: eng_fmx.c fmx_wave, tools/gen_fmx.py) */
AINL int32_t wseq_wave(uint32_t w, uint32_t ph, uint32_t inc, uint32_t *rnd)
{
    int32_t s;
    switch (w) {
    case FXW_TRI:
        return osc_tri(ph - 0x40000000u);           /* (from 0 rising, as the additive one) */
    case FXW_SAW:
        return osc_saw(ph, inc);
    case FXW_SQR:
        return osc_pulse(ph, inc, 0x80000000u);
    case FXW_PULSE:
        return osc_pulse(ph, inc, 0x40000000u);
    case FXW_HALF:                                  /* max(sin, 0) - 1/pi, its peak 1 - 1/pi */
        s = osc_sine(ph);
        return (((s > 0 ? s : 0) - 10430) * 24010) >> 14;
    case FXW_ABS:                                   /* |sin(x / 2)| - 2/pi, one arch a period, its peak 2/pi */
        return ((osc_sine(ph >> 1) - 20860) * 25736) >> 14;
    case FXW_QUART:                                 /* sin(2x) the first quarter, then 0, - 1/(2 pi), its peak 1 - 1/(2 pi) */
        s = ph < 0x40000000u ? osc_sine(ph << 1) : 0;
        return ((s - 5215) * 19484) >> 14;
    case FXW_SIN8: {                                /* floor(8 sin) on -8..7, + 0.5, / 7.5 */
        int32_t q = osc_sine(ph) >> 12;
        q = clamp(q, -8, 7);
        return (2 * q + 1) * 32767 / 15;
    }
    case FXW_NOISE:
        *rnd = *rnd * 1664525u + 1013904223u;
        return (int32_t)(*rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}
/* the increment of pitch16 (1/16 semitone) with the voice's fine part (Clipper's hy_inc) */
AINL uint32_t wseq_inc(int32_t pitch16, const vmod_t *m)
{
    return fine_inc(pitch_inc((uint32_t)clamp(pitch16, 0, 2047)), m->fine_all);
}
typedef struct {
    uint8_t wave;                /* FXW_* */
    int8_t semi;                 /* -24..24 */
    uint8_t lvl;                 /* 0..15 */
} wseq_step_t;
typedef struct {
    uint8_t n;                   /* steps, 1..16 */
    wseq_step_t st[WSEQ_MAXST];
} wseq_seq_t;
#define S(w, p, l) {FXW_##w, p, l}
static const wseq_seq_t WSEQ_SEQ[WSEQ_NSEQ] = {
    {8, {S(SAW, 0, 15), S(SQR, 12, 12), S(PULSE, 7, 13), S(TRI, 0, 11), S(SAW, 12, 14), S(SQR, 0, 12), S(PULSE, 19, 11), S(TRI, 7, 12)}},
    {8, {S(SAW, 0, 15), S(SAW, 0, 6), S(SQR, 0, 12), S(SAW, 0, 6), S(PULSE, 0, 14), S(SAW, 0, 6), S(SQR, 0, 12), S(HALF, 0, 9)}},
    {6, {S(SINE, 0, 14), S(TRI, 0, 14), S(ABS, 0, 13), S(PULSE, 0, 12), S(SQR, 0, 12), S(SAW, 0, 12)}},
    {4, {S(TRI, 0, 15), S(TRI, 7, 13), S(SINE, 12, 13), S(TRI, 19, 11)}},
    {8, {S(SAW, 0, 15), S(SAW, 3, 13), S(SAW, 7, 13), S(SAW, 10, 12), S(SAW, 12, 14), S(SAW, 10, 12), S(SAW, 7, 13), S(SAW, 3, 13)}},
    {8, {S(SQR, 0, 15), S(SQR, 4, 13), S(SQR, 7, 13), S(SQR, 11, 12), S(SQR, 12, 14), S(SQR, 11, 12), S(SQR, 7, 13), S(SQR, 4, 13)}},
    {6, {S(PULSE, 0, 15), S(PULSE, 12, 12), S(SQR, 0, 14), S(PULSE, 12, 12), S(PULSE, 24, 10), S(SQR, 12, 12)}},
    {8, {S(SAW, 0, 15), S(SAW, 0, 0), S(SAW, 0, 15), S(SAW, 0, 0), S(SAW, 0, 15), S(SAW, 0, 15), S(SAW, 0, 0), S(SAW, 0, 12)}},
    {4, {S(SINE, 0, 15), S(HALF, 12, 12), S(ABS, 19, 10), S(SINE, 24, 9)}},
    {8, {S(SIN8, 0, 15), S(SQR, 12, 11), S(SIN8, 7, 13), S(SQR, 5, 11), S(SIN8, 0, 14), S(SQR, 12, 11), S(SIN8, 3, 13), S(SQR, 7, 11)}},
    {4, {S(SINE, 0, 15), S(ABS, 0, 14), S(HALF, 0, 14), S(TRI, 0, 14)}},
    {6, {S(QUART, 0, 15), S(HALF, 0, 13), S(ABS, 12, 12), S(QUART, 7, 13), S(HALF, 5, 12), S(ABS, 0, 13)}},
    {8, {S(SAW, 0, 15), S(NOISE, 0, 5), S(SQR, 0, 13), S(NOISE, 0, 7), S(SAW, 0, 15), S(NOISE, 0, 4), S(SQR, 0, 13), S(NOISE, 0, 8)}},
    {4, {S(SQR, 0, 15), S(SQR, -12, 14), S(SQR, 0, 13), S(SQR, -5, 13)}},
    {8, {S(SAW, 0, 14), S(SAW, 2, 13), S(SAW, 3, 13), S(SAW, 5, 13), S(SAW, 7, 14), S(SAW, 9, 13), S(SAW, 10, 13), S(SAW, 12, 14)}},
    {4, {S(TRI, 0, 15), S(PULSE, 7, 12), S(SAW, 12, 13), S(PULSE, 7, 12)}},
};
#undef S
static const char *const N_WSEQ[WSEQ_NSEQ] = {"STEPS", "RHYTHM", "SWEEP", "FIFTHS", "MINOR", "MAJOR", "OCTAVE", "GATE",
                                              "BELLS", "8BIT", "MORPH", "VOX", "NOISY", "DUB", "DORIAN", "PING"};
static const char *const N_WSEQ_RATE[] = {"1/32", "16T", "1/16", "8T", "1/8", "1/4", "1/2", "BAR", 0};
static const uint8_t WSEQ_DIV[8] = {19, 18, 16, 15, 13, 10, 7, 4};   /* N_WSEQ_RATE -> core.h DUR_LIST (1/32 = 6 48ths ...) */
static const char *const N_WSEQ_LOOP[] = {"FWD", "BACK", "PING", "ONCE"};
enum { WL_FWD, WL_BACK, WL_PING, WL_ONCE };
#define WSEQ_STOP 0x100

/* the step after step k of a sequence of n in loop mode lp (dir: PING's, +1 / -1; 0x100 on it: ONCE ran out) */
static int32_t wseq_next(int32_t k, int32_t n, uint32_t lp, int32_t *dir)
{
    switch (lp) {
    case WL_BACK:
        return k ? k - 1 : n - 1;
    case WL_PING:
        if (n < 2)
            return 0;
        if (k + *dir < 0 || k + *dir >= n)
            *dir = -*dir;
        return k + *dir;
    case WL_ONCE:
        return k + 1 < n ? k + 1 : k;
    default:
        return k + 1 < n ? k + 1 : 0;
    }
}

static void wseq_note_on(track_t *t, voice_t *v)
{
    const wseq_seq_t *q = &WSEQ_SEQ[(uint32_t)clamp(t->p[P_E0], 0, (int32_t)WSEQ_NSEQ - 1)];
    v->ph[0] = v->ph[1] = 0;
    v->ph[2] = 0;
    v->s[2] = clamp(t->p[P_E4], 0, 127) * q->n / 128;  /* START: each note from there */
    v->s[3] = 1;
    v->s[5] = q->st[v->s[2]].lvl * 2184;            /* (the level smoothed: from the first step's) */
    if (!v->env && !v->env_out)
        v->s[0] = v->s[1] = 0;
    if (!v->s[4])
        v->s[4] = (int32_t)(0x2545F491u ^ ((uint32_t)v->note * 2654435761u));
}

static void wseq_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const wseq_seq_t *q = &WSEQ_SEQ[(uint32_t)clamp(p[P_E0], 0, (int32_t)WSEQ_NSEQ - 1)];
    uint32_t lp = (uint32_t)clamp(p[P_E3], 0, 3), i, rate = (uint32_t)clamp(p[P_E1], 0, 127) * 8u / 128u;
    uint32_t len = div_units(WSEQ_DIV[rate]) / (uint32_t)(song.g[G_BPM] > 0 ? song.g[G_BPM] : 120);   /* samples a step */
    uint32_t xf, xinv = 0, pos = v->ph[2], into;
    int32_t k = v->s[2], dir = v->s[3] & 0x80 ? -1 : 1, stopped = (v->s[3] & WSEQ_STOP) != 0;
    int32_t pscale = clamp(p[P_E5], 0, 127), ic1 = v->s[0], ic2 = v->s[1], nk, gs = v->s[5];
    uint32_t pa = v->ph[0], pb = v->ph[1], ia, ib, rnd = (uint32_t)v->s[4];
    tsvf_t flt;
    if (len < 64u)
        len = 64u;
    xf = len * (uint32_t)clamp(p[P_E2], 0, 127) / 128u;
    if (xf)
        xinv = (256u << 16) / xf;                    /* (the fade's position: one multiply a sample) */
    into = len - xf;
    if (k >= q->n || k < 0)
        k = 0;
    {
        int32_t d2 = dir;
        nk = stopped ? k : wseq_next(k, q->n, lp, &d2);
    }
    /* the two steps' increments (the step's pitch scaled by PITCH: 64 = as written) */
    ia = wseq_inc(m->pitch16 + q->st[k].semi * 16 * pscale / 64, m);
    ib = wseq_inc(m->pitch16 + q->st[nk].semi * 16 * pscale / 64, m);
    tsvf_coef(&flt, (p[P_E6] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), p[P_E7]);
    for (i = 0; i < n; i++) {
        int32_t ya = wseq_wave(q->st[k].wave, pa, ia, &rnd), y, x;
        int32_t ga = q->st[k].lvl * 2184;             /* (15 x 2184 = 32760) */
        if (xf && pos >= into && !stopped) {         /* the next step fading in */
            int32_t f = (int32_t)(((pos - into) * xinv) >> 16), gb = q->st[nk].lvl * 2184;   /* 0..255 */
            int32_t yb = wseq_wave(q->st[nk].wave, pb, ib, &rnd);
            f = f > 256 ? 256 : f;
            y = ((ya * ((gs * (256 - f)) >> 8)) >> 15) + ((yb * ((gb * f) >> 8)) >> 15);
            pb += ib;
        } else {
            gs += (ga - gs) >> 5;                    /* (a cut: the level glides ~0.7 ms, no click) */
            y = (ya * gs) >> 15;
        }
        pa += ia;
        if (++pos >= len) {                          /* the next step: its phase from its fade in */
            pos = 0;
            if (!stopped) {
                int32_t d2 = dir, k2 = wseq_next(k, q->n, lp, &d2);
                if (lp == WL_ONCE && k2 == k) {
                    stopped = 1;
                } else {
                    dir = d2;
                    k = k2;
                    if (xf) {
                        pa = pb;
                        gs = q->st[k].lvl * 2184;    /* (faded in: at its level) */
                    }
                    pb = 0;
                    ia = ib;
                    {
                        int32_t d3 = dir;
                        nk = wseq_next(k, q->n, lp, &d3);
                    }
                    ib = wseq_inc(m->pitch16 + q->st[nk].semi * 16 * pscale / 64, m);
                }
            }
        }
        x = tsvf_lp(&flt, y * 3 >> 2, &ic1, &ic2);
        if (x > 16000 || x < -16000)
            x = soft_knee(x, 16000);
        out[i] += mulq15(mulq15(x << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = pa;
    v->ph[1] = pb;
    v->ph[2] = pos;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = k;
    v->s[3] = (dir < 0 ? 0x80 : 0x01) | (stopped ? WSEQ_STOP : 0);
    v->s[4] = (int32_t)rnd;
    v->s[5] = gs;
}

static const preset_t WSEQ_PRESETS[] = {
    /* {SEQ, RATE, XFADE, LOOP, START, PITCH, CUT, RES}, {A D S R}, ENV -> FILTER, mono */
    {"WAVE STEPS", {0, 40, 20, WL_FWD, 0, 64, 110, 10}, {0, 90, 110, 50}, 0, 0, FX(0, 20, 30, 40)},
    {"RHYTHM SEQ", {1, 40, 10, WL_FWD, 0, 64, 104, 20}, {0, 90, 120, 40}, 0, 0, FX(0, 10, 30, 30)},
    {"SWEEP PAD", {2, 110, 127, WL_PING, 0, 64, 100, 10}, {80, 100, 120, 100}, 0, 0, FX(0, 50, 20, 70)},
    {"FIFTH STAB", {3, 40, 0, WL_FWD, 0, 64, 112, 10}, {0, 80, 100, 40}, 0, 0, FX(0, 20, 30, 40)},
    {"MINOR SEQ", {4, 40, 0, WL_FWD, 0, 64, 100, 30}, {0, 90, 110, 40}, 10, 1, FX(0, 10, 40, 30)},
    {"MAJOR SEQ", {5, 40, 0, WL_FWD, 0, 64, 104, 20}, {0, 90, 110, 40}, 0, 0, FX(0, 20, 40, 40)},
    {"OCTAVE BASS", {6, 40, 0, WL_FWD, 0, 64, 84, 30}, {0, 80, 110, 20}, 10, 1, FX(10, 0, 0, 10)},
    {"TRANCE GATE", {7, 40, 8, WL_FWD, 0, 64, 110, 10}, {20, 90, 120, 60}, 0, 0, FX(0, 30, 30, 50)},
    {"BELL STEPS", {8, 40, 20, WL_FWD, 0, 64, 120, 0}, {0, 90, 90, 70}, 0, 0, FX(0, 10, 40, 60)},
    {"8BIT SEQ", {9, 24, 0, WL_FWD, 0, 64, 127, 0}, {0, 80, 110, 30}, 0, 1, FX(0, 0, 30, 20)},
    {"MORPH PAD", {10, 127, 127, WL_PING, 0, 64, 96, 10}, {90, 100, 120, 110}, 0, 0, FX(0, 50, 20, 80)},
    {"VOX STEPS", {11, 56, 60, WL_FWD, 0, 64, 104, 20}, {10, 90, 110, 60}, 0, 0, FX(0, 30, 30, 60)},
    {"NOISY SEQ", {12, 40, 0, WL_FWD, 0, 64, 110, 10}, {0, 80, 110, 30}, 0, 0, FX(0, 0, 30, 30)},
    {"DUB STEPS", {13, 56, 30, WL_FWD, 0, 64, 90, 40}, {0, 80, 110, 30}, 10, 1, FX(10, 0, 40, 30)},
    {"DORIAN RUN", {14, 24, 0, WL_PING, 0, 64, 108, 10}, {0, 80, 110, 30}, 0, 1, FX(0, 10, 40, 30)},
    {"PING PONG", {15, 40, 30, WL_PING, 0, 64, 110, 10}, {0, 90, 110, 50}, 0, 0, FX(0, 20, 40, 50)},
};

static const engine_t ENG_WSEQ = {
    .name = "WSEQ",
    .page_title = {"SEQUENCE", "TONE"},
    .edit = {
        PN("SEQ", F_ENUM, 0, WSEQ_NSEQ - 1, 0, N_WSEQ),
        PN("RATE", F_INT, 0, 127, 40, N_WSEQ_RATE),
        PU("XFADE", F_PCT, 0, 127, 20, 0),
        PN("LOOP", F_ENUM, 0, 3, 0, N_WSEQ_LOOP),
        PU("START", F_INT, 0, 127, 0, 0),
        PU("PITCH", F_PCT, 0, 127, 64, 0),
        PU("CUT", F_CUTOFF, 0, 127, 110, 0),
        PU("RES", F_PCT, 0, 127, 10, 0),
    },
    .presets = PRESETS_PTR(WSEQ_PRESETS),
    .npresets = sizeof(WSEQ_PRESETS) / sizeof(WSEQ_PRESETS[0]),
    .fil_page = 1,
    .note_on = wseq_note_on,
    .render = wseq_render,
    .color = 0x3DFB,                             /* #3fbfd8 (tools/colors.json) */
    .macro = {P_E0, P_E1, P_E2, P_E6},
};
