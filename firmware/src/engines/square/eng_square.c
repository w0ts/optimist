/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors */
/* SQUARE (engine UID 26, FELUCCA_EX_SQUARE, off by default): the example engine of CONTRIBUTING.md, "Add a synth
 * engine". A template, so it is small and every line a new engine must have is marked [ENGINE]. Copy this file to
 * engines/<name>/eng_<name>.c, rename sq_ / SQ_ / SQUARE, and follow CONTRIBUTING.md for the places it is registered.
 *
 * The sound: a pulse oscillator (PW from a square to a thin pulse, the track's SHP moves it), a square an octave down
 * (SUB), a second pulse detuned up to ~50 cents (DTN), white noise (NOIS), then a 12 dB low-pass (CUT, RES, TRK: key
 * tracking; the track's FLT moves CUT) and a soft drive (DRV). The amplitude is the track's own envelope (ENV page:
 * amp_at below): an engine renders a steady tone and the voice code shapes it.
 * EDIT 1 OSC   PW  SUB  DTN  NOIS        EDIT 2 FILT  CUT  RES  TRK  DRV
 *
 * The rules every engine here follows (the reviewers check them):
 *   - integers only (no float, no double: the FM-1's audio path is fixed point; dsp.c has the building blocks);
 *   - no divide by a variable in the audio path (tools/div_audit.py lists every one; this file has none: dsp.c's
 *     tsvf_coef divides once a block and is listed there);
 *   - state lives in the voice (voice_t: ph[3] phases, s[8] integers), never in a static per note;
 *   - render ADDS to out[] (the voices of a part are summed there), n samples, n <= CTL.
 * Voice state: ph[0] the pulse, ph[1] the sub, ph[2] the detuned pulse; s[0] s[1] the filter, s[2] the noise. */

/* [ENGINE] a new note on voice v: put its state where a note starts. A voice is reused, so clear what render reads */
static void sq_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;                 /* the oscillators start together: the sub stays in phase */
    v->s[0] = v->s[1] = 0;                              /* the filter at rest */
    v->s[2] = (int32_t)(0x9E3779B9u * (v->note + 1u));  /* the noise's seed (never 0: xorshift would stay at 0) */
}

/* [ENGINE] render n samples of voice v ADDED to out. t->p[P_E0 .. P_E7] are the EDIT values (the .edit list below);
 * m: what voice.c computed for this block (the pitch as a phase increment, the envelope's ramp, the track's FLT and
 * SHP modulation) */
static void sq_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    /* the values once a block (cheap per sample below). PW: 0 a square (half the turn) .. 127 a ~6 % pulse; + SHP */
    int32_t pwv = clamp((p[P_E0] << 8) + m->shape, 0, 127 << 8);
    uint32_t pw = 0x80000000u - (uint32_t)pwv * 57800u;     /* the pulse's width as a phase offset */
    int32_t g_sub = p[P_E1] << 8, dtn = p[P_E2], g_nz = p[P_E3] << 7, drv = p[P_E7];   /* gains Q15 */
    uint32_t inc = m->inc, inc_sub = inc >> 1, inc_dt = fine_inc(inc, dtn);   /* DTN: + dtn / 4096, ~53 cents at 127 */
    /* the filter: CUT + FLT, + TRK x the note's distance from C4 (127: a semitone a semitone), RES */
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (((m->pitch16 - 60 * 16) * p[P_E6] * 151) >> 10);
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], i;
    int32_t ic1 = v->s[0], ic2 = v->s[1], rnd = v->s[2];  /* (the state in locals, written back after the loop) */
    tsvf_t flt;
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t x = osc_pulse(ph0, inc, pw), y;            /* +-16384: a band-limited pulse (polyBLEP, dsp.c) */
        if (g_sub)
            x += mulq15(osc_pulse(ph1, inc_sub, 0x80000000u), g_sub);
        if (dtn)
            x += osc_pulse(ph2, inc_dt, pw);
        if (g_nz)
            x += mulq15((int32_t)(noise32(&rnd) >> 17) - 16384, g_nz);
        ph0 += inc, ph1 += inc_sub, ph2 += inc_dt;
        y = tsvf_lp(&flt, x, &ic1, &ic2);
        if (drv)
            y += (y * drv) >> 5;                           /* DRV: up to x5 into the knee below */
        y = soft_knee(y, 24000);                           /* bounded, whatever RES and DRV do */
        out[i] += mulq15(mulq15(y, amp_at(m, i)), VOICE_FS);   /* [ENGINE] times the envelope, at a voice's level */
    }
    v->ph[0] = ph0, v->ph[1] = ph1, v->ph[2] = ph2;
    v->s[0] = ic1, v->s[1] = ic2, v->s[2] = rnd;
}

/* [ENGINE] the factory presets: {name, {EDIT 1..8}, {ATK DEC SUS REL}, ENV -> FILTER, MONO, FX(DIST, CHO, DLY, REV)}.
 * Each one also needs its line in core/model.c BANK (the PRESETS list, by kind); tests/preset_cover_test.c checks */
static const preset_t SQ_PRESETS[] = {
    /* {PW, SUB, DTN, NOIS, CUT, RES, TRK, DRV} */
    {"SQ LEAD", {0, 0, 24, 0, 92, 30, 64, 12}, {0, 60, 100, 40}, 20, 1, FX(0, 20, 30, 20)},
    {"SQ BASS", {40, 110, 0, 0, 58, 50, 32, 30}, {0, 50, 70, 20}, 40, 1, FX(0, 0, 0, 0)},
    {"SQ PLUCK", {64, 0, 10, 8, 70, 40, 64, 0}, {0, 84, 0, 60}, 50, 0, FX(0, 20, 40, 40)},
};

/* [ENGINE] the engine: its name (the ENG list, 7 letters at most; past 4, its short form in ui/optimist/op_names.c
 * NM_CELL), the two EDIT pages' titles, the eight EDIT values (PU: label in capitals, 6 letters at most, format, min,
 * max, default, unit), the presets, the functions. (engine_t has more, optional: core.h) */
static const engine_t ENG_SQUARE = {
    .name = "SQUARE",
    .page_title = {"OSC", "FILT"},
    .edit = {
        PU("PW", F_PCT, 0, 127, 0, 0),                  /* the pulse's width: a square at 0 */
        PU("SUB", F_PCT, 0, 127, 0, 0),                 /* a square an octave down */
        PU("DTN", F_PCT, 0, 127, 0, 0),                 /* a second pulse, detuned up (0: none) */
        PU("NOIS", F_PCT, 0, 127, 0, 0),                /* white noise */
        PU("CUT", F_CUTOFF, 0, 127, 100, 0),            /* the low-pass's cutoff */
        PU("RES", F_PCT, 0, 127, 20, 0),                /* its resonance */
        PU("TRK", F_PCT, 0, 127, 64, 0),                /* the cutoff follows the key */
        PU("DRV", F_PCT, 0, 127, 0, 0),                 /* a soft drive after the filter */
    },
    .presets = PRESETS_PTR(SQ_PRESETS),
    .npresets = sizeof(SQ_PRESETS) / sizeof(SQ_PRESETS[0]),
    .note_on = sq_note_on,
    .render = sq_render,
    .fil_page = 1,                                      /* EDIT 2 holds the filter (the ENV > FILTER cards) */
    .color = 0x658A,                                    /* #62b356, RGB565 (ui/colors.c COL_ENG_SQUARE; a real engine's
                                                         * colour is in tools/colors.json) */
};
