/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (X0X: charlesvestal/fm1-x0x 80b7d40; dsp/bass303.c, seq/tb3po.c) */
/* ACID (FELUCCA_ENG_ACID, backports.h): EXPERIMENTAL. X0X's TB-303 voice as an engine of its own, engine number 12,
 * monophonic, one per synth part, with X0X's TB-3PO line generator. Ported from X0X by Charles Vestal (GPL-3.0):
 *   - the voice: acid/bass303.c as X0X has it (Open303 by Robin Schmidt, MIT; Devilfish ranges after jc303;
 *     Soft / RAT drive after schwung-303 and dm-Rat), float, in its own translation unit (acid/acid_dsp.c,
 *     built with X0X's FPU flags): this file talks to it through acid_* with integers only;
 *   - the generator: X0X's seq/tb3po.c (itself after schwung-tb3po and the Phazerville Hemisphere Suite's
 *     TB_3PO applet by djphazer and contributors, GPL-3.0) rewritten for our steps, its float draws as the same
 *     integer comparisons (x / 2^24 < p / 100 as 100 x < p 2^24).
 * NOT X0X's break player (dsp/breaks*: no licence of its own, used by X0X by permission): nothing of it here.
 *
 * EDIT 1: CUT RESO ENV DEC, EDIT 2: ACC WAVE DRV SLD (the 303's pots 0..127; WAVE saw / square). A step's ACCENT
 * (velocity 127; MIDI: velocity >= 120) is the 303's accent; a SLIDE into the next step slides (the voice's legato,
 * voice.c), as on the 303. The track's ENV / LFO / pitch modulation and TUNE do not reach it (the 303 has its own
 * envelopes and tuning); the ADSR gates it (the presets: SUS 127). ACID GEN (an EDIT page on an ACID track): DENS,
 * ACC %, SLD %, GO (twice): a new line of TB-3PO into the pattern (LEN steps, the track's ROOT, its own minor
 * scale), undoable. */
void acid_pots(int k, const int16_t *e);           /* acid/acid_dsp.c (float, its own unit) */
void acid_note(int k, int note, int accent, int slide);
void acid_off(int k);
void acid_render(int k, int32_t *out, int n, int32_t gain);
#if FELUCCA_CPU_GUARD
void acid_lite(int k, int on);
#endif
#ifndef __PI32V2__
#define blep acid_blep                             /* (the host tests: one unit; names the firmware also has) */
#include "acid_dsp.c"
#undef blep
#endif

static const char *const N_ACID_WAVE[] = {"SAW", "SQR"};

static int acid_k(const track_t *t) { return (int)(t - trk); }

static void acid_note_on(track_t *t, voice_t *v)
{
    if (t >= &trk[NPART])
        return;
    acid_pots(acid_k(t), &t->p[P_E0]);
    acid_note(acid_k(t), v->note, v->vel >= 120u, 0);
}

static void acid_legato(track_t *t, voice_t *v)    /* a SLIDE: the 303 slides to the note */
{
    if (t >= &trk[NPART])
        return;
    acid_note(acid_k(t), v->note, v->vel >= 120u, 1);
}

static void acid_render_v(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)m;
    if (t >= &trk[NPART] || v != &t->v[0])
        return;                                    /* (one 303 per part: its first voice) */
    acid_pots(acid_k(t), &t->p[P_E0]);
    if (!v->gate)
        acid_off(acid_k(t));
#if FELUCCA_CPU_GUARD
    acid_lite(acid_k(t), CG_LEVEL >= CG_L_QUALITY);
#endif
    acid_render(acid_k(t), out, (int)n, VOICE_FS * 2);
}

/* ---- TB-3PO (after X0X seq/tb3po.c) ---- */
static const int8_t TB_MINOR[7] = {0, 2, 3, 5, 7, 8, 10};

static uint32_t tb_rng(uint32_t *r)                /* xorshift32 (dsp_common.h), a zero state taken as 1 */
{
    if (!*r)
        *r = 1u;
    return xorshift32(r);
}
static int tb_below(uint32_t *r, uint32_t pct)     /* rng_f(r) < pct / 100 */
{
    return (uint64_t)(tb_rng(r) & 0xFFFFFFu) * 100u < (uint64_t)pct << 24;
}
static uint32_t tb_pick(uint32_t *r, uint32_t n)   /* (int)(rng_f(r) * n) */
{
    return (uint32_t)(((uint64_t)(tb_rng(r) & 0xFFFFFFu) * n) >> 24);
}

/* a new line into track t's pattern: dens / acc / sld %, seed */
static void acid_generate(track_t *t, uint32_t dens, uint32_t acc, uint32_t sld, uint32_t seed)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, i, any = 0, r = seed ? seed : 1u;   /* (trk_len) */
    int32_t root = 24 + t->p[P_ROOT];              /* (TB-3PO's base: note 24 + root) */
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        s->n = 0;
        s->time = ST_REST;
        s->flags = s->vel = s->lvl = s->rat = 0;
    }
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        uint32_t deg, oct;
        if (!tb_below(&r, dens))
            continue;                              /* rest */
        deg = i % 4u == 0u && tb_below(&r, 35) ? 0u : tb_pick(&r, 7);
        oct = tb_pick(&r, 2);
        s->note[0] = (uint8_t)clamp(root + TB_MINOR[deg] + 12 * (int32_t)oct, 1, 127);
        s->n = 1;
        s->time = ST_NOTE;
        if (tb_below(&r, acc))
            s->flags = SF_ACCENT;
        if (tb_below(&r, sld))
            s->flags = SF_SLIDE;                   /* (TB-3PO: a SLIDE step is not also an ACCENT) */
    }
    for (i = 0; i < len; i++) {                    /* a slide into a rest does nothing */
        if ((t->step[i].flags & SF_SLIDE) && t->step[(i + 1u) % len].time == ST_REST)
            t->step[i].flags = 0;
        any |= t->step[i].time == ST_NOTE;
    }
    if (!any) {
        t->step[0].note[0] = (uint8_t)root;
        t->step[0].n = 1;
        t->step[0].time = ST_NOTE;
    }
}

static const preset_t ACID_PRESETS[] = {
    /* name, {CUT, RESO, ENV, DEC, ACC, WAVE, DRV, SLD}, {A D S R}, fenv, mono */
    {"ACID LINE", {40, 100, 70, 50, 80, 0, 0, 21}, {0, 0, 127, 12}, 0, 1, FX(0, 0, 20, 10)},
    {"ACID SQR", {30, 90, 60, 40, 70, 1, 0, 21}, {0, 0, 127, 12}, 0, 1, FX(0, 0, 20, 10)},
    {"ACID RAGE", {55, 115, 90, 60, 100, 0, 70, 30}, {0, 0, 127, 12}, 0, 1, FX(0, 0, 10, 0)},
    {"ACID DUB", {20, 70, 40, 90, 40, 0, 0, 60}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 50, 30)},
};

static const engine_t ENG_ACID = {
    .name = "ACID",
    .page_title = {"FILT", "VOICE"},
    .edit = {
        {"CUT", F_PCT, 0, 127, 64, 0, 0},
        {"RESO", F_PCT, 0, 127, 64, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DEC", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 64, 0, 0},
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"SLD", F_PCT, 0, 127, 21, 0, 0},
    },
    .presets = ACID_PRESETS,
    .npresets = sizeof(ACID_PRESETS) / sizeof(ACID_PRESETS[0]),
    .fil_page = 0,
    .note_on = acid_note_on,
    .render = acid_render_v,
    .color = 0xE7E0,
    .macro = {P_E0, P_E1, P_E2, P_E3},
    .poly = 1,
    .vel_own = 1,
    .legato = acid_legato,
};
