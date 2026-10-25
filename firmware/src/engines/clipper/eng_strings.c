/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* STRINGS (engine 18, FELUCCA_ENG_STRINGS, off by default): Clipper's string machine. From isod89/clipper-fm1
 * firmware/src/eng_strings.c (Clipper 1.1, GPL-3.0-only). Changes in the glue only: our engine_t's designated fields,
 * osc_saw for Clipper's hy_saw (the same polyBLEP saw), P_LEG_SHP for P_LD_SHP; the DSP and the presets are Clipper's.
 *
 * A string machine in the manner of the 1970s ensembles (Solina, ARP, Elka). Divide-down registers: per tap one phase
 * at the 16' pitch, the 8' and the 4' its double and quadruple, so the registers stay locked as on the originals
 * (polyBLEP saws, or towards a square with SHP). The ensemble is three taps, each a copy of the registers whose pitch
 * two LFOs move, 120 degrees apart from tap to tap (the slow ~0.6 Hz sweep and the fast ~6 Hz shimmer of the three BBD
 * lines), the depth ENS, the speed RATE. Then a 12 dB low-pass.
 * EDIT 1  16'  8'  4'  ENS        EDIT 2  CUT  RATE  SHP  RES
 * Voice state: ph[0..2] the three taps (16' phase), s[0..1] the SVF. Part state: the ensemble LFOs (strings_block). */
static uint32_t str_lfo[NPART][2];               /* the ensemble's slow and fast LFO, per part (strings_block) */

static OVLX(strings) void strings_block(track_t *t)
{
    uint32_t part = (uint32_t)(t - trk) % NPART;
    int32_t rate = t->p[P_E5];                       /* x 0.25 .. x 2 */
    uint32_t k = 8192u + (uint32_t)rate * 448u;      /* Q15 */
    str_lfo[part][0] += (uint32_t)(((uint64_t)(58000u * CTL) * k) >> 15);      /* ~0.6 Hz at RATE 50 */
    str_lfo[part][1] += (uint32_t)(((uint64_t)(584000u * CTL) * k) >> 15);     /* ~6 Hz */
}

static void strings_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {
        v->ph[0] = v->ph[1] = v->ph[2] = 0;          /* the taps start together (as the BBD lines share one input:
                                                      * set apart a third of a turn they would cancel all but every
                                                      * third harmonic); the LFOs pull them apart */
        v->s[0] = v->s[1] = 0;
    }
}

/* a register: a saw, towards a square with SHP (x Q15: 0 saw .. 32767 square) */
AINL int32_t str_reg(uint32_t ph, uint32_t inc, int32_t x)
{
    int32_t a = osc_saw(ph, inc);
    return x ? a + mulq15(((a - osc_saw(ph + 0x80000000u, inc)) >> 1) - a, x) : a;
}

static OVLX(strings) void strings_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t acc[CTL];                                /* (on the stack: two cores) */
    uint32_t part = (uint32_t)(t - trk) % NPART, i, k;
    int32_t g16 = p[P_E0] * 34, g8 = p[P_E1] * 34, g4 = p[P_E2] * 34;  /* Q15 / 7.5 (three taps, three
                                                                         * registers: headroom for chords) */
    int32_t ens = p[P_E3], shp = p[P_E6] * 258;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    uint32_t inc16 = m->inc >> 1;
    tsvf_t flt;
    if (n > CTL)
        return;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (k = 0; k < 3u; k++) {
        /* the tap's pitch: the slow and the fast LFO, 120 degrees on; up to ~+-14 cents and ~+-4 cents at ENS 127 */
        int32_t s1 = osc_sine(str_lfo[part][0] + k * 0x55555555u), s2 = osc_sine(str_lfo[part][1] + k * 0x55555555u);
        int32_t dq = ((s1 * 14 + s2 * 4) >> 4) * ens;                   /* cents x 127 x 2048 */
        int32_t fq = dq * 38 / 260096;                                  /* the pitch factor - 1, Q16 (~38 a cent) */
        uint32_t inc = inc16 + (uint32_t)(((int64_t)(int32_t)inc16 * fq) >> 16), ph = v->ph[k];
        for (i = 0; i < n; i++, ph += inc) {
            int32_t s = 0;
            if (g16)
                s += mulq15(str_reg(ph, inc, shp), g16);
            if (g8)
                s += mulq15(str_reg(ph << 1, inc << 1, shp), g8);
            if (g4)
                s += mulq15(str_reg(ph << 2, inc << 2, shp), g4);
            acc[i] += s;
        }
        v->ph[k] = ph;
    }
    tsvf_coef(&flt, (p[P_E4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), p[P_E7]);   /* (keyboard ~40 %) */
    for (i = 0; i < n; i++) {
        int32_t y = tsvf_lp(&flt, acc[i] >> 1, &ic1, &ic2);
        if (y > 16000 || y < -16000)
            y = soft_knee(y, 16000);
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static const preset_t STRINGS_PRESETS[] = {
    /* {16', 8', 4', ENS, CUT, RATE, SHP, RES}, {A D S R}, ENV -> FILTER, mono */
    {"SOLINA", {40, 127, 70, 100, 92, 50, 0, 10}, {50, 100, 120, 80}, 0, 0, FX(0, 10, 10, 50)},
    {"VIOLINS", {0, 127, 100, 90, 100, 60, 20, 15}, {40, 100, 120, 70}, 0, 0, FX(0, 10, 10, 45)},
    {"CELLOS", {127, 80, 0, 80, 76, 45, 30, 20}, {50, 100, 120, 80}, 0, 0, FX(0, 0, 0, 40)},
    {"ENSEMBLE", {90, 110, 90, 127, 96, 55, 10, 10}, {60, 100, 125, 90}, 0, 0, FX(0, 0, 14, 60)},
    {"STR MACHINE", {60, 127, 40, 110, 84, 40, 0, 25}, {30, 100, 120, 60}, 0, 0, FX(0, 20, 0, 45)},
    {"GLASS STR", {0, 90, 127, 120, 110, 70, 50, 30}, {70, 100, 120, 95}, 0, 0, FX(0, 0, 24, 70)},
    {"DARK ENS", {127, 100, 20, 100, 64, 35, 0, 15}, {80, 100, 125, 100}, 0, 0, FX(0, 0, 10, 65)},
    {"PULSE STR", {50, 127, 60, 90, 90, 50, 110, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 10, 50)},
    {"OMNI STR", {60, 127, 90, 110, 96, 55, 10, 15}, {50, 100, 120, 80}, 0, 0, FX(0, 0, 10, 55)},
    {"SLOW STR", {40, 127, 60, 100, 84, 40, 0, 10}, {100, 100, 125, 105}, 0, 0, FX(0, 0, 10, 65)},
    {"STR STAB", {50, 127, 80, 90, 100, 60, 0, 10}, {0, 80, 60, 50}, 20, 0, FX(0, 0, 20, 40)},
    {"CHOIR STR", {20, 127, 90, 127, 80, 30, 60, 30}, {70, 100, 120, 90}, 0, 0, FX(0, 0, 10, 65)},
    {"BRIGHT STR", {0, 127, 127, 100, 112, 60, 0, 20}, {40, 100, 120, 70}, 0, 0, FX(0, 0, 10, 50)},
    {"LOW STR", {127, 60, 0, 90, 70, 40, 10, 10}, {60, 100, 120, 80}, 0, 0, FX(0, 0, 0, 45), XP(P_TRANS + 1, -12)},
    {"80S STR", {40, 127, 60, 60, 96, 80, 40, 10}, {40, 100, 120, 70}, 0, 0, FX(0, 20, 10, 50)},
    {"TAPE STR", {60, 127, 50, 127, 80, 20, 0, 10}, {60, 100, 120, 80}, 0, 0, FX(0, 0, 10, 55),
     XP(P_LRATE + 1, 30, P_LEG_PIT + 1, 2)},
};

static const engine_t ENG_STRINGS = {
    .name = "STRINGS",
    .page_title = {"REG", "TONE"},
    .edit = {
        PU("16'", F_PCT, 0, 127, 40, 0),
        PU("8'", F_PCT, 0, 127, 127, 0),
        PU("4'", F_PCT, 0, 127, 70, 0),
        PU("ENS", F_PCT, 0, 127, 100, 0),
        PU("CUT", F_CUTOFF, 0, 127, 92, 0),
        PU("RATE", F_PCT, 0, 127, 50, 0),
        PU("SHP", F_PCT, 0, 127, 0, 0),
        PU("RES", F_PCT, 0, 127, 10, 0),
    },
    .presets = STRINGS_PRESETS,
    .npresets = sizeof(STRINGS_PRESETS) / sizeof(STRINGS_PRESETS[0]),
    .fil_page = 1,
    .note_on = strings_note_on,
    .render = OVL_FN(strings_render),
    .color = 0xD50E,                             /* #d4a373 (tools/colors.json) */
    .macro = {P_E3, P_E4, P_E1, P_ATK},
    .block = OVL_FN(strings_block),
};
#if FELUCCA_ENG_STRINGS
OVL_ENGINE(strings, ENG_SLOT_STRINGS, strings_render, strings_block, 0, 0)
#endif
