/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNO (engine 14, FELUCCA_ENG_JUNO, off by default): the Juno-106 voice of fm-106 by PatttF
 * (github.com/PatttF/fm-106 be74757), a fixed-point port of Ultramaster KR-106 (kayrockscreenprinting/
 * ultramaster_kr106), both GPL-3.0-only. The DSP: juno_dsp.h. Here: the SLOOP side.
 *
 *   EDIT  WAVE (OFF / SAW / PULSE / BOTH: the Juno's two buttons), PW (the manual width, 50..95 %; LFO / ENV move
 *         it through SHAPE), SUB, NOISE | CUT, RES, KEY (key tracking around C4), HPF (BOOST / FLAT / 236 / 754 Hz).
 *   The track's ENV is the Juno's ENV: ENV > FLT is the VCF ENV amount (negative: the inverted polarity), the
 *   amplitude goes through the VCA's measured curve (juno_amp); GATE mode is SUS 127 / REL 0. The track's LFO is
 *   the Juno's LFO (LFO > FLT, PIT, SHP); RANGE is the part's TRANSPOSE; the chorus is the chorus send.
 *   Six voices, as the Juno-106. The HPF runs on the part after its voices (juno_post).
 * Not taken: fm-106's own envelope, LFO, arpeggiator, sequencer, chorus, FX; Roland's 128 factory patches. */
#include "juno_dsp.h"

#define JUNO_POLY 6u
static juno_vs_t juno_v[NPART][NVOICE];
static juno_hpf_t juno_h[NPART];
static struct { juno_rset_t r; uint8_t res1; } juno_rc[NPART];   /* each part's RES (+ 1, 0 = none yet) and what it sets */

AINL juno_vs_t *juno_vs(const track_t *t, const voice_t *v)
{
    return &juno_v[(uint32_t)(t - trk) % NPART][(uint32_t)(v - t->v) % NVOICE];
}

static void juno_note_on(track_t *t, voice_t *v)
{
    juno_vs_t *s = juno_vs(t, v);
    const int16_t *p = t->p;
    if (!v->env && !v->env_out) {                     /* a new voice (a retrigger keeps the phase and the filter) */
        memset(s, 0, sizeof *s);
        s->nseed = 123456789u + (uint32_t)v->note * 987654321u;   /* by note: a render is the same on any voice */
        s->vseed = 123456789u + (uint32_t)v->note * 31u;
    }
    /* a pure resonance patch (every wave off, RES > 80) starts the ladder ringing, as fm-106 */
    if (!p[P_E0] && !p[P_E2] && !p[P_E3] && p[P_E5] > 80 && !s->z[0] && !s->z[1])
        s->z[0] = 50 << 9;
}

/* the VCA: the measured 2SA1015 / BA662 curve on the envelope (once a block; the core's ramp is the RC slew) */
static int32_t juno_amp(track_t *t, voice_t *v, int32_t adsr)
{
    uint32_t i = (uint32_t)adsr >> 7, f = (uint32_t)adsr & 127u;
    int32_t a;
    (void)t, (void)v;
    if (i >= 255u)
        return JUNO_DTAB(VCA_HW, 255u);
    a = JUNO_DTAB(VCA_HW, i);
    return a + (((JUNO_DNEXT(VCA_HW, a, i) - a) * (int32_t)f) >> 7);
}

/* the level: y * amp >> 12, ANALOG's (juno_test) */
static void juno_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    juno_vs_t *s = juno_vs(t, v);
    juno_ctl_t c;
    int32_t y[CTL], ea = ENV1_FLT_AMT(p), envp = (m->envq15 * ea) >> 8, pw;   /* (ENV on FLT: its slots, AMT 2 = 1 of ENV
                                                                             * DEST FLT of before: voice.c's term taken out) */
    uint32_t wave = (uint32_t)p[P_E0] & 3u, i;
    if (n > CTL)
        n = CTL;                                      /* (never: voice.c renders CTL-sample blocks) */
    c.inc = m->inc;
    c.saw = (uint8_t)(wave & 1u);
    c.pulse = (uint8_t)(wave >> 1);
    c.sub = (uint8_t)clamp(p[P_E2], 0, 127);
    c.noise = (uint8_t)clamp(p[P_E3], 0, 127);
    pw = p[P_E1] + (m->shape >> 8) - 64;              /* SHAPE (LFO / ENV > SHP) moves the width */
    c.pw = (uint8_t)clamp(pw, 0, 105);
    c.res = (uint8_t)clamp(p[P_E5], 0, 127);
    {   /* (the packed resonance tables: decoded when RES changes) */
        uint32_t pi = (uint32_t)(t - trk) % NPART;
        if (juno_rc[pi].res1 != c.res + 1u) {
            juno_res_at(&juno_rc[pi].r, c.res);
            juno_rc[pi].res1 = (uint8_t)(c.res + 1u);
        }
        c.r = juno_rc[pi].r;
    }
    /* the cutoff DAC: the slider, the LFO (and the mod slots) at half the 14-bit range per full swing, the
     * envelope at the Juno's VCF ENV depth (ENV > FLT 63 = 252 of 254), the key tracking */
    c.dac = (uint16_t)juno_dac(clamp(p[P_E4], 0, 127) << 7, (m->cutoff - envp) >> 1, (uint32_t)m->envq15 >> 1, ea * 2,
                               (uint32_t)clamp(p[P_E6], 0, 127) * 2u, (uint32_t)v->pitch16 << 4);
    juno_run(s, &c, y, n);
    {   /* the VCA: the core's amplitude ramp (amp_at, a step a sample), 32-bit products (y < 2^18) */
        int32_t acc = m->amp0 * (int32_t)CTL, step = m->amp1 - m->amp0;
        for (i = 0; i < n; i++, acc += step)
            out[i] += ((y[i] >> 3) * (acc >> CTL_LOG2)) >> 9;
    }
}

static void juno_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    juno_hpf_t *h = &juno_h[(uint32_t)(t - trk) % NPART];
    (void)nr;
    juno_couple(&h->cpl, out, n);
    juno_hpf(h, (uint32_t)t->p[P_E7] & 3u, out, n);
}

static const char *const N_JUNO_WAVE[] = {"OFF", "SAW", "PULSE", "BOTH"};
static const char *const N_JUNO_HPF[] = {"BOOST", "FLAT", "236", "754"};

/* Our own patches (not Roland's factory sounds): {WAVE, PW, SUB, NOISE, CUT, RES, KEY, HPF}, {A D S R}, ENV > FLT,
 * mono, sends (DIST, CHORUS, DELAY, REVERB) */
static const preset_t JUNO_PRESETS[] = {
    {"JUNO INIT", {1, 0, 0, 0, 127, 0, 0, 1}, {0, 64, 127, 30}, 0, 0, FX(0, 0, 0, 0)},
    {"JUNO STRNG", {1, 0, 0, 0, 62, 10, 40, 2}, {70, 80, 110, 80}, 10, 0, FX(0, 60, 10, 40)},
    {"JUNO PWM PD", {2, 40, 30, 0, 58, 18, 30, 1}, {90, 90, 115, 95}, 8, 0, FX(0, 70, 15, 55),
     XP(P_LRATE + 1, 20, P_LEG_SHP + 1, 40)},
    {"JUNO BRASS", {1, 0, 40, 0, 38, 20, 50, 1}, {18, 70, 80, 40}, 40, 0, FX(0, 35, 10, 25)},
    {"JUNO BASS", {3, 60, 90, 0, 30, 40, 60, 0}, {0, 58, 40, 20}, 44, 1, FX(0, 0, 0, 5), XP(P_TRANS + 1, -12)},
    {"JUNO SYNCL", {2, 90, 0, 0, 48, 70, 80, 2}, {0, 50, 30, 30}, 50, 0, FX(0, 30, 35, 30)},
    {"JUNO HOLLOW", {2, 20, 0, 10, 70, 90, 64, 3}, {30, 70, 90, 70}, -20, 0, FX(0, 55, 20, 50)},
    {"JUNO ORGAN", {2, 0, 80, 0, 90, 0, 0, 1}, {0, 64, 127, 12}, 0, 0, FX(0, 50, 0, 25)},
};

static const engine_t ENG_JUNO = {
    .name = "JUNO",
    .page_title = {"DCO", "VCF"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 1, N_JUNO_WAVE, 0},
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 127, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"KEY", F_PCT, 0, 127, 0, 0, 0},
        {"HPF", F_ENUM, 0, 3, 1, N_JUNO_HPF, 0},
    },
    .presets = JUNO_PRESETS,
    .npresets = sizeof(JUNO_PRESETS) / sizeof(JUNO_PRESETS[0]),
    .fil_page = 1,
    .note_on = juno_note_on,
    .render = juno_render,
    .color = 0xCAE7,                             /* #cf5f3f (tools/colors.json) */
    .macro = {P_E4, P_E5, P_E1, P_REL},
    .poly = JUNO_POLY,
    .amp = juno_amp,
    .post = juno_post,
};
