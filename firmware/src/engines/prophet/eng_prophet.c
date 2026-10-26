/* SPDX-License-Identifier: GPL-3.0-only */
/* PROPHET (engine 24, FELUCCA_ENG_PROPHET, off by default): a Prophet-5 Rev 4 style voice. The oscillators, the Poly-Mod
 * and the Curtis filter are Melodee's (p5_dsp.c: float, its own unit); here the SLOOP side, our controls: 8 EDIT values,
 * the track's ENV (the amplitude, and through ENV > FLT the filter envelope) and LFO, our own presets. Optimist is not
 * affiliated with or endorsed by Sequential; no Sequential program is included and none is imported.
 *   EDIT 1 (OSC)  A     oscillator A: SAW, PULSE, BOTH; SAW.S PULS.S BOTH.S the same hard-synced to B
 *                 B     oscillator B: SAW TRI PULSE and their mixes
 *                 B FRQ B's interval against A, -24..+24 semitones
 *                 MIX   the mixer: -64 A alone, 0 both at full, 63 B alone
 *   EDIT 2 (VCF)  CUT RES  the Curtis 4-pole (keyboard tracking half, the track's FLT moves CUT)
 *                 PMOD  Poly-Mod from oscillator B: 1..63 on A's pitch (to an octave either way), -1..-64 on the cutoff
 *                 PW    both oscillators' pulse width, 50 % at 0 to 5 % at 127 (the track's SHP moves it: PWM)
 * A modulator's OSC2 moves A's pitch alone, B staying (the synced oscillator's, as ANALOG's osc 2: a sync sweep; its DEST
 * card names it A PIT here; issue 75, jsmonzani: "separate pitch A from pitch B")
 * Five voices, as the Prophet-5. Voice state: ph[0] A, ph[1] B; s[0..3] the filter's stages, s[4] B's triangle, s[5]
 * the sync's carry-over (floats as bits, p5_dsp.c), s[6] the last block's cutoff (Q8, P5_NOCUT: a fresh note). */
#include "p5_dsp.h"
#if !defined(__PI32V2__)
#include "p5_dsp.c"                                   /* (the host tests: one unit; the target builds it alone, with the FPU) */
#endif
#define P5_POLY 5u
#define P5_NOCUT INT32_MIN
static const char *const N_P5_A[] = {"SAW", "PULSE", "BOTH", "SAW.S", "PULS.S", "BOTH.S"};
static const char *const N_P5_B[] = {"SAW", "TRI", "PULSE", "S+T", "S+P", "T+P", "ALL"};
static const uint8_t P5_BWAVE[7] = {1, 2, 4, 3, 5, 6, 7};   /* bit 0 SAW, 1 TRI, 2 PULSE */

static void prophet_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {                     /* from silence: A at zero, B a third on (no comb when they meet) */
        v->ph[0] = 0;
        v->ph[1] = 0x55555555u;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = 0;   /* (0.0f's bits) */
    }
    v->s[6] = P5_NOCUT;
}

static void prophet_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t a = (uint32_t)clamp(p[P_E0], 0, 5), bw = P5_BWAVE[(uint32_t)clamp(p[P_E1], 0, 6)];
    int32_t mix = clamp(p[P_E3], -64, 63), pm = clamp(p[P_E6], -64, 63), res = clamp(p[P_E5], 0, 127);
    int32_t pw = 16384 - clamp(p[P_E7], 0, 127) * 116 - ((m->shape - (64 << 8)) >> 1);   /* Q15 of a turn: 50 % .. 5 % */
    int32_t cut = (p[P_E4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) << 3), c0 = v->s[6] == P5_NOCUT ? cut : v->s[6];
    p5_args_t g;
    if (n == 0 || n > CTL)
        return;
    pw = clamp(pw, 1024, 31744);
    g.inc_a = m->os2 ? fine_inc(pitch_inc((uint32_t)clamp(m->pitch16 + m->os2, 0, 2047)), m->fine_all) : m->inc;   /* (OSC2: A alone) */
    g.inc_b = fine_inc(pitch_inc((uint32_t)clamp(m->pitch16 + clamp(p[P_E2], -24, 24) * 16, 0, 2047)), m->fine_all);
    g.pw_a = g.pw_b = (uint32_t)pw << 17;
    g.mix = mix;
    g.pmod = pm;
    g.cut0 = c0;
    g.cut1 = cut;
    g.res = res;
    g.scale = 2 * VOICE_FS;
    g.amp0 = m->amp0;
    g.amp1 = m->amp1;
    g.saw_a = a % 3u != 1u, g.pulse_a = a % 3u != 0u, g.sync = a >= 3u;
    g.saw_b = bw & 1u, g.tri_b = (bw >> 1) & 1u, g.pulse_b = (bw >> 2) & 1u;
    FAR(p5_render)(out, n, v->ph, v->s, &g);         /* (p5_dsp.c: flash, its own unit) */
    v->s[6] = cut;
}

static const preset_t PROPHET_PRESETS[] = {
    /* {A, B, B FRQ, MIX, CUT, RES, PMOD, PW}, {A D S R}, ENV -> FILTER, mono */
    {"P5 INIT", {0, 0, 0, -64, 127, 0, 0, 0}, {0, 64, 127, 30}, 0, 0, FX(0, 0, 0, 0)},
    {"P5 BRASS", {0, 0, 0, 0, 52, 20, 0, 0}, {14, 70, 96, 40}, 34, 0, FX(0, 20, 10, 30), XP(P_DETUNE + 1, 12)},
    {"P5 STRINGS", {2, 4, 0, -10, 70, 10, 0, 60}, {64, 80, 110, 80}, 10, 0, FX(0, 50, 10, 50),
     XP(P_LRATE + 1, 30, P_LEG_SHP + 1, 20)},
    {"P5 SYNC LEAD", {3, 0, 12, -64, 90, 30, 0, 0}, {0, 70, 110, 40}, 20, 1, FX(10, 0, 30, 25), XP(P_GLIDE + 1, 20)},
    {"P5 PMOD BELL", {0, 1, 19, -64, 100, 10, 40, 0}, {0, 90, 0, 80}, 0, 0, FX(0, 10, 30, 60)},
    {"P5 BASS", {2, 0, -12, 0, 30, 40, 0, 30}, {0, 60, 60, 20}, 50, 1, FX(10, 0, 0, 10)},
    {"P5 PAD", {0, 1, 7, 0, 60, 15, 0, 0}, {90, 100, 115, 95}, 20, 0, FX(0, 50, 20, 70), XP(P_LRATE + 1, 20, P_LEG_FLT + 1, 6)},
    {"P5 CLAV", {1, 2, 0, 0, 70, 30, 0, 90}, {0, 50, 0, 30}, 40, 0, FX(0, 10, 10, 20)},
    {"P5 GROWL", {0, 0, 7, -40, 45, 70, 30, 0}, {0, 70, 90, 30}, 30, 1, FX(20, 0, 10, 15)},
    {"P5 WAH SWEEP", {2, 0, 0, -20, 30, 90, -40, 40}, {10, 80, 90, 50}, 40, 0, FX(0, 20, 30, 40)},
    {"P5 SYNC SWEEP", {3, 1, 7, -64, 100, 20, 30, 0}, {0, 90, 80, 50}, 10, 1, FX(0, 0, 30, 30), XP(P_ED_PIT + 1, 35)},
    {"P5 HOLLOW", {1, 1, 12, 10, 80, 20, 0, 20}, {20, 80, 100, 70}, 10, 0, FX(0, 30, 20, 50)},
};

static const engine_t ENG_PROPHET = {
    .name = "PROPHET",
    .page_title = {"OSC", "VCF"},
    .edit = {
        PN("A", F_ENUM, 0, 5, 0, N_P5_A),
        PN("B", F_ENUM, 0, 6, 0, N_P5_B),
        PU("B FRQ", F_SEMI, -24, 24, 0, 0),
        PU("MIX", F_BIPCT, -64, 63, 0, 0),
        PU("CUT", F_CUTOFF, 0, 127, 100, 0),
        PU("RES", F_PCT, 0, 127, 0, 0),
        PU("PMOD", F_BIPCT, -64, 63, 0, 0),
        PU("PW", F_PCT, 0, 127, 0, 0),
    },
    .presets = PROPHET_PRESETS,
    .npresets = sizeof(PROPHET_PRESETS) / sizeof(PROPHET_PRESETS[0]),
    .fil_page = 1,
    .note_on = prophet_note_on,
    .render = prophet_render,
    .color = 0xBA67,                             /* #b84d3a (tools/colors.json) */
    .macro = {P_E4, P_E5, P_E6, P_E3},
    .poly = P5_POLY,
};
