/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* TERRAIN (engine 20, FELUCCA_ENG_TERRAIN, off by default). From isod89/clipper-fm1 firmware/src/eng_terrain.c and
 * terrain_tables.h (Clipper 1.1, GPL-3.0-only; the tables are tools/gen_terrain.py's output there). Changes: our
 * engine_t's designated fields; the DSP, the surfaces and the presets are Clipper's. SHAPE below is the track's ENV and
 * LFO on SHP. */
/* TERRAIN: wave terrain synthesis, "lite" (Clipper 0.5; Felucca discussion #146). Instead of a waveform read along
 * one axis, an orbit circles over a surface: the surface's height under it is the sound. One turn of the orbit is
 * one period of the note, so it is pitched; where the orbit lies on the surface and how wide it is choose the
 * harmonics, and moving it (X, Y, DRIFT, the matrix) makes the timbre evolve as no single wave can.
 * The surfaces: 8 tables of 32 x 32 (terrain_tables.h, tools/gen_terrain.py), periodic in x and y (a torus: the
 * orbit crosses an edge without a step), each of a few low spatial harmonics so the output stays band-limited; read
 * with bilinear interpolation (4 reads, 3 multiplies). The orbit: an ellipse of radius RAD around (X, Y), squeezed
 * flat by ORB below 64 and turned into a figure eight above it (its second harmonic: brighter, hollow). DRIFT moves
 * its centre round a slow circle (~0.03 .. 1 Hz, each note its own), SHAPE (LFO / ENV DEST SHP, MW / AT SHP, the
 * matrix) widens it. High notes: the radius shrinks an octave at a time above C6 (a wide orbit would only alias).
 * EDIT 1  TERR  RAD  X  Y        EDIT 2  ORB  DRIFT  CUT  RES
 * Voice state: ph[0] the orbit, ph[1] the drift; s[0..1] the SVF; s[2] the DC blocker. */
#include "terrain_tables.h"

/* the surface's height at (x, y): 2^32 = one period of the table (32 cells), bilinear */
AINL int32_t ter_at(const int16_t *T, uint32_t x, uint32_t y)
{
    uint32_t xi = x >> 27, yi = y >> 27, xj = (xi + 1u) & 31u, yj = ((yi + 1u) & 31u) << 5;
    int32_t fx = (int32_t)((x >> 12) & 32767u), fy = (int32_t)((y >> 12) & 32767u);
    int32_t a = T[(yi << 5) + xi], b = T[(yi << 5) + xj], c = T[yj + xi], d = T[yj + xj];
    int32_t ab = a + mulq15(b - a, fx), cd = c + mulq15(d - c, fx);
    return ab + mulq15(cd - ab, fy);
}

static void terrain_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {
        v->ph[0] = 0;
        v->ph[1] = (uint32_t)v->note * 0x9E3779B9u;  /* (each note's drift from its own place: a chord moves apart) */
        v->s[0] = v->s[1] = v->s[2] = 0;
    }
}

static void terrain_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int16_t *T = TERRAIN[(uint32_t)p[P_E0] % TER_COUNT];
    int32_t r = p[P_E1] * 129 + (m->shape - (64 << 8)), orb = p[P_E4], ry, a8 = 0;
    int32_t ic1 = v->s[0], ic2 = v->s[1], dc = v->s[2], over = m->pitch16 - 84 * 16;
    uint32_t i, ph = v->ph[0], inc = m->inc, cx = (uint32_t)p[P_E2] << 25, cy = (uint32_t)p[P_E3] << 25;
    tsvf_t flt;
    if (p[P_E5]) {                                   /* DRIFT: the centre round a circle an eighth of the surface wide */
        uint32_t dinc = (uint32_t)p[P_E5] * (uint32_t)p[P_E5] * 6u + 2400u;   /* per sample: ~0.03 .. ~1 Hz */
        v->ph[1] += dinc * n;
        cx += (uint32_t)(osc_sine(v->ph[1]) * 16384);
        cy += (uint32_t)(osc_sine(v->ph[1] + 0x40000000u) * 16384);
    }
    r = clamp(r, 0, 16383);                          /* the radius, 1/65536 of the surface: up to a quarter of it */
    if (over > 0) {                                  /* high notes: an octave narrower an octave up */
        uint32_t oc = (uint32_t)over / 192u, ro = oc >= 15u ? 0u : (uint32_t)r >> oc;
        r = (int32_t)(ro - ro * ((uint32_t)over % 192u) / 384u);
    }
    if (orb <= 64) {
        ry = r * orb >> 6;                           /* an ellipse, flat at 0 */
    } else {
        ry = r;
        a8 = (orb - 64) * 4;                         /* towards a figure eight (Q8) */
    }
    tsvf_coef(&flt, (p[P_E6] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), p[P_E7]);
    for (i = 0; i < n; i++, ph += inc) {
        int32_t sy = osc_sine(ph), y, x;
        if (a8)
            sy += ((osc_sine(ph << 1) - sy) * a8) >> 8;
        y = ter_at(T, cx + ((uint32_t)(osc_sine(ph + 0x40000000u) * r) << 1), cy + ((uint32_t)(sy * ry) << 1));
        dc += y - (dc >> 8);                         /* DC blocker (~27 Hz): an orbit off the middle has an offset */
        x = tsvf_lp(&flt, (y - (dc >> 8)) * 3 >> 2, &ic1, &ic2);
        if (x > 16000 || x < -16000)
            x = soft_knee(x, 16000);
        out[i] += mulq15(mulq15(x << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = dc;
}

static const preset_t TERRAIN_PRESETS[] = {
    /* {TERR, RAD, X, Y, ORB, DRIFT, CUT, RES}, {A D S R}, ENV -> FILTER, mono */
    {"TERRAIN", {0, 50, 0, 0, 64, 20, 110, 0}, {60, 100, 120, 90}, 0, 0, FX(0, 20, 10, 60)},
    {"RIPPLE PAD", {1, 70, 20, 40, 64, 30, 100, 10}, {80, 100, 120, 100}, 0, 0, FX(0, 30, 10, 70)},
    {"CRYSTAL", {2, 60, 10, 30, 64, 10, 120, 0}, {0, 80, 40, 70}, 0, 0, FX(0, 10, 20, 50)},
    {"BUMPY BASS", {3, 70, 0, 0, 40, 0, 84, 20}, {0, 70, 100, 30}, 10, 1, FX(10, 0, 0, 10)},
    {"SADDLE LEAD", {4, 70, 30, 20, 90, 0, 110, 10}, {0, 80, 110, 40}, 0, 1, FX(0, 0, 20, 30), XP(P_GLIDE + 1, 20)},
    {"FOLD DRONE", {5, 90, 0, 64, 100, 60, 100, 20}, {90, 100, 125, 110}, 0, 0, FX(0, 20, 20, 80)},
    {"TWISTER", {6, 80, 40, 10, 120, 40, 110, 10}, {20, 90, 110, 60}, 0, 0, FX(0, 10, 20, 50)},
    {"CHAOS FX", {7, 110, 0, 0, 127, 100, 127, 30}, {40, 100, 110, 90}, 0, 0, FX(10, 0, 30, 70)},
    {"ORBIT KEYS", {2, 75, 20, 44, 50, 15, 104, 0}, {0, 80, 70, 50}, 0, 0, FX(0, 20, 10, 40)},
    {"TERRA PLUCK", {1, 60, 40, 0, 80, 0, 115, 10}, {0, 74, 20, 56}, 0, 0, FX(0, 0, 20, 40), XP(P_ED_SHP + 1, 30)},
};

static const engine_t ENG_TERRAIN = {
    .name = "TERRAIN",
    .page_title = {"ORBIT", "MOTION"},
    .edit = {
        PN("TERR", F_ENUM, 0, TER_COUNT - 1, 0, N_TERRAIN),
        PU("RAD", F_PCT, 0, 127, 50, 0),
        PU("X", F_PCT, 0, 127, 0, 0),
        PU("Y", F_PCT, 0, 127, 0, 0),
        PU("ORB", F_PCT, 0, 127, 64, 0),
        PU("DRIFT", F_PCT, 0, 127, 20, 0),
        PU("CUT", F_CUTOFF, 0, 127, 110, 0),
        PU("RES", F_PCT, 0, 127, 0, 0),
    },
    .presets = PRESETS_PTR(TERRAIN_PRESETS),
    .npresets = sizeof(TERRAIN_PRESETS) / sizeof(TERRAIN_PRESETS[0]),
    .fil_page = 1,
    .note_on = terrain_note_on,
    .render = terrain_render,
    .color = 0x4CED,                             /* #4f9d69 (tools/colors.json) */
    .macro = {P_E1, P_E2, P_E3, P_E5},
};
