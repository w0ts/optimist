/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * CZ engine: Kerem Kilic (Ellic Studio) (Melodee 0.11, github.com/keremimo/melodee, eng_cz.c, cz_patch.h,
 * cz_legacy.h and the CZ envelopes of eng_phase.c at v0.11), GPL-3.0-only; the native playback (cz_native.c)
 * after Devin Acker's uPD933 model (MAME), BSD-3-Clause. Ported to SLOOP, behind FELUCCA_ENG_CZ */
/* CZ: Casio CZ-style phase distortion with the CZ-1's own tone structure, played natively (the uPD933's phase
 * functions, windows, 8-step envelope rate law and logarithmic DCA). Two lines, each with its own 8-step
 * pitch (DCO), timbre (DCW) and volume (DCA) envelopes; line select 1 / 2 / 1+1' / 1+2', ring and noise
 * modulation, detune, vibrato, key follow, line levels and velocity sensitivity.
 *
 * In SLOOP (a first version): a part plays one of the tones below, our own (no Casio factory tone, nor
 * anything taken from one), built into the part's native 128 bytes (Casio's MIDI layout, as Melodee keeps
 * them) when TONE changes. The 8 EDIT values change the tone as it plays without storing it: LINE, MOD,
 * detune, DCW depth, the DCW and DCA envelope times, vibrato depth. A project stores the 8 values (P_E0..P_E7),
 * nothing more. Not ported: Melodee's 38 CZ pages, the 8 CZ banks, .syx, Casio SysEx, Casio's 64 tones.
 * The envelopes are the engine's own: ATK DEC SUS REL do not shape a CZ voice; it ends when its DCA envelopes
 * end (cz_amp). Velocity is the tone's (V.AMP / V.WAV / V.PIT). */
#define CZ_GAIN 1                                  /* out << CZ_GAIN (Melodee: * 4; levels measured, eng_cz tests) */
static const uint8_t CZ_ENV_BASE[2][3] = {{55,38,21},{112,95,78}};   /* DCO DCW DCA steps, lines 1 and 2 */
static const uint8_t CZ_ENV_END[2][3] = {{54,37,20},{111,94,77}};    /* their END (low 3 bits), velocity (high 4) */

/* Eight rate/target points, with explicit sustain/end. Rates are Q24 per
 * control tick. No exponential asymptote and no extra release after END. (Melodee eng_phase.c) */
typedef struct { uint32_t rate[8]; int32_t level[8]; uint8_t sustain, end; } cz_env_def_t;
typedef struct { int32_t level; uint8_t stage, gate; } cz_env_t;
typedef struct { cz_env_t eg[2][3]; uint32_t vib_phase, vib_ticks, noise; } cz_voice_t;
/* the EDIT values of a part, as the render takes them (cz_block) */
typedef struct { uint8_t line, mod, vib, tone; int32_t dcw, det16, detf, wtim, atim; } cz_ed_t;

static cz_voice_t cz_v[NPART][NVOICE];
static uint8_t cz_tone[NPART][128];                /* the part's tone: Casio's 128 synthesis bytes */
static cz_env_def_t cz_defs[NPART][2][3];          /* its envelope points (cz_part_defs), for its EDIT times */
static cz_ed_t cz_ed[NPART];
static uint32_t cz_key[NPART];                     /* what cz_tone / cz_defs were built for, 0 = nothing yet */

static cz_voice_t *cz_voice(track_t *t, voice_t *v)
{
    return &cz_v[(uint32_t)(t - trk) % NPART][(uint32_t)(v - t->v) % NVOICE];
}
AINL int32_t cz_cos(uint32_t ph16)                 /* -cos, Q15, from a 16-bit phase (eng_phase.c pd_cos) */
{
    return -sine_i(((ph16 & 0xFFFFu) << 16) + 0x40000000u);
}

static int32_t cz_env_tick(cz_env_t *e, const cz_env_def_t *d, uint32_t gate)
{
    uint32_t left = 65536u;
    if (e->gate && !gate && d->sustain <= d->end && e->stage <= d->sustain)
        e->stage = d->sustain + 1u;                    /* release even during attack */
    e->gate = (uint8_t)gate;
    /* Carry unused tick time across points, including zero-distance points.
     * A short segment must not acquire an extra 32-sample delay. */
    for (uint32_t k = 0; k < 8u && e->stage <= d->end; k++) {
        uint32_t st = e->stage, rate = d->rate[st];
        int32_t target = st == d->end ? 0 : d->level[st];
        int32_t delta = target - e->level;
        uint32_t dist = (uint32_t)(delta < 0 ? -delta : delta);
        uint32_t step = (uint32_t)mulq16((int32_t)rate, left);
        if (dist > step) {
            e->level += delta < 0 ? -(int32_t)step : (int32_t)step;
            break;
        }
        e->level = target;
        if (gate && st == d->sustain) break;
        e->stage++;
        if (dist && rate) {
            uint32_t used = (uint32_t)(((uint64_t)dist << 16) / rate);
            left = used < left ? left - used : 0;
        }
        if (!left) break;
    }
    return e->level;
}

#include "cz_native.c"

/* ------------------------------------------------------------------ tones --- */
/* A tone in panel values (the CZ-1's: rates and levels 0..99), our own. An envelope: 8 rates, 8 levels, the
 * SUS point (8 = none) and END; a line: its waves (W2 0 = none, else wave + 1), window, key follows, level
 * 1..15, velocity (pitch, wave, amp: 0..15). line: 0 = 1, 1 = 2, 2 = 1+1', 3 = 1+2'; mod: 0 off, 1 ring,
 * 2 noise; oct: 0, 1 = +1, 2 = -1; detune: sign, octave, note, fine 0..60; vibrato: wave (TRI, SAW UP,
 * SAW DN, SQR), rate, depth, delay */
typedef struct { uint8_t r[8], l[8], sus, end; } cz_pe_t;
typedef struct { uint8_t w1, w2, win, kw, ka, level, vp, vw, va; cz_pe_t env[3]; } cz_pl_t;   /* env: DCO DCW DCA */
typedef struct {
    uint8_t line, mod, oct, sign, doct, note, fine, vwave, vrate, vdep, vdelay;
    cz_pl_t ln[2];
} cz_panel_t;

#define CZ_E(r1, l1, r2, l2, r3, l3, r4, l4, sus, end) {{r1, r2, r3, r4, 99, 99, 99, 99}, {l1, l2, l3, l4, 0, 0, 0, 0}, sus, end}
#define CZ_FLAT CZ_E(99, 0, 99, 0, 99, 0, 99, 0, 8, 0)
enum { CZT_INIT, CZT_BASS, CZT_RESO, CZT_BELL, CZT_BRASS, CZT_PAD, CZT_BREATH, CZT_KEYS, CZT_N };
static const char *const N_CZ_TONE[CZT_N] = {"INIT", "BASS", "RESO", "BELL", "BRASS", "PAD", "BREATH", "KEYS"};
static const cz_panel_t CZ_TONES[CZT_N] = {
    /* INIT: one saw line, DCW half open, an organ envelope */
    [CZT_INIT] = {0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, {
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_E(99, 50, 50, 0, 0, 0, 0, 0, 0, 1), CZ_E(99, 99, 50, 0, 0, 0, 0, 0, 0, 1)}},
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_E(99, 50, 50, 0, 0, 0, 0, 0, 0, 1), CZ_E(99, 99, 50, 0, 0, 0, 0, 0, 0, 1)}}}},
    /* BASS: saw, the DCW snapping shut from bright to a round sustain */
    [CZT_BASS] = {0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, {
        {0, 0, 0, 2, 0, 15, 0, 4, 3, {CZ_FLAT, CZ_E(99, 90, 55, 22, 60, 0, 0, 0, 1, 2), CZ_E(99, 99, 45, 82, 75, 0, 0, 0, 1, 2)}},
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_FLAT, CZ_FLAT}}}},
    /* RESO: a saw through the falling-saw window (the CZ's resonance), its DCW swept up then back */
    [CZT_RESO] = {0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, {
        {0, 0, 1, 0, 0, 15, 0, 3, 2, {CZ_FLAT, CZ_E(40, 99, 35, 30, 60, 0, 0, 0, 1, 2), CZ_E(90, 99, 40, 85, 60, 0, 0, 0, 1, 2)}},
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_FLAT, CZ_FLAT}}}},
    /* BELL: a double sine and a saw an octave and a fifth up, mixed, both decaying (MOD RING: a harsher bell) */
    [CZT_BELL] = {3, 0, 0, 0, 1, 7, 5, 0, 50, 0, 0, {
        {4, 0, 0, 0, 0, 15, 0, 0, 4, {CZ_FLAT, CZ_E(99, 40, 30, 0, 0, 0, 0, 0, 8, 1), CZ_E(99, 99, 26, 60, 45, 0, 0, 0, 8, 2)}},
        {0, 0, 0, 0, 0, 13, 0, 0, 4, {CZ_FLAT, CZ_E(99, 35, 35, 0, 0, 0, 0, 0, 8, 1), CZ_E(99, 99, 30, 60, 45, 0, 0, 0, 8, 2)}}}},
    /* BRASS: two saws a little apart, the DCW opening with the attack, a late vibrato */
    [CZT_BRASS] = {2, 0, 0, 0, 0, 0, 6, 0, 55, 8, 30, {
        {0, 0, 0, 0, 0, 15, 0, 5, 3, {CZ_FLAT, CZ_E(62, 80, 50, 60, 55, 0, 0, 0, 1, 2), CZ_E(72, 99, 50, 90, 62, 0, 0, 0, 1, 2)}},
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_FLAT, CZ_FLAT}}}},
    /* PAD: two detuned squares, slow DCW and DCA */
    [CZT_PAD] = {2, 0, 0, 0, 0, 0, 10, 0, 40, 6, 50, {
        {1, 0, 0, 0, 0, 15, 0, 2, 2, {CZ_FLAT, CZ_E(40, 45, 30, 30, 45, 0, 0, 0, 1, 2), CZ_E(45, 99, 40, 92, 45, 0, 0, 0, 1, 2)}},
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_FLAT, CZ_FLAT}}}},
    /* BREATH: a double sine and a saw-pulse line under noise modulation */
    [CZT_BREATH] = {3, 2, 0, 0, 0, 0, 0, 0, 50, 0, 0, {
        {4, 0, 0, 0, 0, 15, 0, 0, 2, {CZ_FLAT, CZ_E(60, 20, 50, 12, 55, 0, 0, 0, 1, 2), CZ_E(50, 99, 45, 86, 55, 0, 0, 0, 1, 2)}},
        {5, 0, 0, 0, 0, 11, 0, 0, 2, {CZ_FLAT, CZ_E(60, 50, 50, 30, 55, 0, 0, 0, 1, 2), CZ_E(55, 99, 45, 80, 55, 0, 0, 0, 1, 2)}}}},
    /* KEYS: a pulse alternating with a saw cycle by cycle, bright on a hard strike */
    [CZT_KEYS] = {0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, {
        {2, 1, 0, 2, 2, 15, 0, 5, 6, {CZ_FLAT, CZ_E(99, 70, 52, 25, 60, 0, 0, 0, 1, 2), CZ_E(99, 99, 45, 82, 62, 0, 0, 0, 1, 2)}},
        {0, 0, 0, 0, 0, 15, 0, 0, 0, {CZ_FLAT, CZ_FLAT, CZ_FLAT}}}},
};

/* panel values -> Casio's bytes (Melodee cz_legacy.h lcz_sx_encode, native: velocity and level in the high
 * nibbles; the name bytes are not kept) */
static uint32_t cz_sx_rate(uint32_t e, uint32_t a) { return e == 0 ? a * 127u / 99u : e == 1 ? 8u + a * 119u / 99u : a * 119u / 99u; }
static uint32_t cz_sx_level(uint32_t e, uint32_t a) { return e == 0 ? a + (a > 63u ? 4u : 0u) : e == 1 ? a * 127u / 99u : a ? a + 28u : 0u; }
static __attribute__((noinline)) void cz_encode(const cz_panel_t *p, uint8_t *d)
{
    static const uint8_t VW[4] = {8, 4, 32, 2};
    static const uint8_t KA[10] = {0, 8, 17, 26, 36, 47, 58, 69, 82, 95}, KW[10] = {0, 31, 44, 57, 70, 83, 96, 111, 146, 255};
    uint32_t j, l, e, v;
    memset(d, 0, 128);
    d[0] = (uint8_t)((p->line & 3u) | (p->oct & 3u) << 2);
    d[1] = p->sign & 1u;
    d[2] = (uint8_t)((p->fine + (p->fine ? (p->fine - 1u) / 15u : 0u)) << 2);
    d[3] = (uint8_t)(p->doct * 12u + p->note);
    d[4] = VW[p->vwave & 3u];
    for (j = 0; j < 3u; j++) {                       /* delay, rate, depth: the display byte, the machine value */
        uint32_t n = j == 0 ? p->vdelay : j == 1 ? p->vrate : p->vdep;
        uint32_t group = n < 32u ? 0u : (n - 16u) / 16u, step = 1u << group;
        if (j == 1) {
            static const uint16_t START[6] = {0x20, 0x460, 0x8e0, 0x11e0, 0x23e0, 0x47e0};
            v = START[group] + 32u * step * (n - (group ? 16u + 16u * group : 0u));
        } else {
            v = group ? ((n - 16u - 16u * group) * step + ((17u << group) - 1u)) : n;
            if (j == 2)
                v = n == 99u ? 0x300u : v + step;
        }
        d[5 + j * 3] = (uint8_t)n;
        d[6 + j * 3] = (uint8_t)v;
        d[7 + j * 3] = (uint8_t)(v >> 8);
    }
    for (l = 0; l < 2u; l++) {
        const cz_pl_t *q = &p->ln[l];
        uint32_t o = l ? 71u : 14u, mod = l ? 0u : p->mod;
        d[o] = (uint8_t)(q->w1 << 5 | (q->w2 ? (q->w2 - 1u) << 2 | 2u : 0u) | q->win >> 2);
        d[o + 1] = (uint8_t)((q->win & 3u) << 6 | (mod == 1u ? 4u : mod == 2u ? 3u : 0u) << 3);
        d[o + 2] = (uint8_t)(q->ka | (15u - q->level) << 4);
        d[o + 3] = KA[q->ka % 10u];
        d[o + 4] = q->kw;
        d[o + 5] = KW[q->kw % 10u];
        for (e = 0; e < 3u; e++) {
            const cz_pe_t *ep = &q->env[e];
            uint32_t off = o + (e == 2u ? 6u : e == 1u ? 23u : 40u), prev = 0;
            uint32_t vel = e == 0u ? q->vp : e == 1u ? q->vw : q->va;
            d[off] = (uint8_t)(ep->end | (15u - vel) << 4);
            for (j = 0; j < 8u; j++) {
                uint32_t val = j == ep->end ? 0u : ep->l[j];
                d[off + 1 + 2 * j] = (uint8_t)(cz_sx_rate(e, ep->r[j]) | (val < prev ? 128u : 0u));
                d[off + 2 + 2 * j] = (uint8_t)(cz_sx_level(e, val) | (ep->sus == j ? 128u : 0u));
                prev = val;
            }
        }
    }
}

/* -------------------------------------------------------------- the part --- */
/* once a block per part (also with no voice): the EDIT values; a new TONE or time builds the tone / points */
static void cz_block(track_t *t)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, tone = (uint32_t)t->p[P_E0] % CZT_N, key;
    const int16_t *p = t->p;
    cz_ed_t *ed = &cz_ed[part];
    int32_t c = p[P_E3], d16 = c * 16 / 100;
    ed->dcw = p[P_E1] * 8;                            /* DCW: +-512 of the 1023 */
    ed->wtim = p[P_E2];
    ed->atim = p[P_E4];
    ed->det16 = d16;                                 /* DTN, cents: whole 1/16 semitones, the rest as fine */
    ed->detf = (c * 16 - d16 * 100) * 2367 / 16000;
    ed->line = (uint8_t)clamp(p[P_E5], 0, 4);
    ed->mod = (uint8_t)clamp(p[P_E6], 0, 3);
    ed->vib = (uint8_t)clamp(p[P_E7], 0, 99);
    key = 1u + tone + ((uint32_t)(ed->wtim + 64) << 4) + ((uint32_t)(ed->atim + 64) << 12);
    if (key != cz_key[part]) {
        if (tone != ed->tone || !cz_key[part])
            cz_encode(&CZ_TONES[tone], cz_tone[part]);
        ed->tone = (uint8_t)tone;
        cz_part_defs(cz_tone[part], cz_defs[part], ed->wtim, ed->atim);
        cz_key[part] = key;
    }
}

static void cz_note_on(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    /* a new voice (voice_start cleared env_out): the envelopes from 0, the toggles and DC filters cleared;
     * retriggered while sounding: the envelopes from where they are, the toggles and DC states kept (voice_start
     * keeps the phases), so the new note does not click */
    if (!v->env_out) {
        memset(c, 0, sizeof *c);
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
    } else {
        c->vib_phase = c->vib_ticks = 0;
    }
    for (uint32_t l = 0; l < 2u; l++)
        for (uint32_t e = 0; e < 3u; e++) {
            c->eg[l][e].gate = 1;
            c->eg[l][e].stage = 0;
        }
    c->noise = 0x6D2B79F5u ^ (uint32_t)v->note * 0x9E3779B9u;   /* (by note: a render does not depend on the voice slot) */
}

/* the voice is over: its sounding lines' DCA envelopes are past END (Melodee cz_native_done) */
static int cz_done(track_t *t, voice_t *v)
{
    uint32_t part = (uint32_t)(t - trk) % NPART;
    const uint8_t *b = cz_tone[part];
    cz_voice_t *c = cz_voice(t, v);
    uint32_t ls = cz_lines(b, &cz_ed[part]);
    uint32_t a = ls == 1u ? 1u : 0u, z = ls >= 2u ? 1u : a;
    return c->eg[a][2].stage > (b[CZ_ENV_END[a][2]] & 7u) &&
           c->eg[z][2].stage > (b[CZ_ENV_END[ls == 2u ? 0u : z][2]] & 7u);
}

/* the voice's amplitude is the engine's (its DCA envelopes): the ADSR's release never ends it, the DCA's do;
 * a voice given up for another part (stage 4) fades as any other */
static int32_t cz_amp(track_t *t, voice_t *v, int32_t adsr)
{
    if (v->stage == 4u)
        return adsr;
    if (cz_done(t, v)) {
        v->stage = 0;
        v->active = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static const char *const N_CZ_LINE[] = {"TONE", "1", "2", "1+1'", "1+2'"};
static const char *const N_CZ_MOD[] = {"TONE", "OFF", "RING", "NOISE"};

static const preset_t CZ_PRESETS[] = {
    /* name, {TONE, DCW, W.TIM, DTN, A.TIM, LINE, MOD, VIB}, {A D S R} (unused: the tone's envelopes), fenv, mono */
    {"CZ INIT", {CZT_INIT, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 0, 0)},
    {"PD BASS", {CZT_BASS, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"RESO SWEEP", {CZT_RESO, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 25, 20)},
    {"GLASS BELL", {CZT_BELL, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 20, 20, 45)},
    {"WIRE BRASS", {CZT_BRASS, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 20, 10, 25)},
    {"SOFT PAD", {CZT_PAD, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 45, 15, 55)},
    {"NOISE BREATH", {CZT_BREATH, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 25, 20, 50)},
    {"PULSE KEYS", {CZT_KEYS, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 20, 18, 22)},
};

static const engine_t ENG_CZ = {
    .name = "CZ",
    .page_title = {"TONE", "LINE"},
    .edit = {
        {"TONE", F_ENUM, 0, CZT_N - 1, 0, N_CZ_TONE, 0},
        {"DCW", F_BIPCT, -64, 63, 0, 0, 0},
        {"W.TIM", F_BIPCT, -64, 63, 0, 0, 0},
        {"DTN", F_INT, -64, 63, 0, 0, "ct"},
        {"A.TIM", F_BIPCT, -64, 63, 0, 0, 0},
        {"LINE", F_ENUM, 0, 4, 0, N_CZ_LINE, 0},
        {"MOD", F_ENUM, 0, 3, 0, N_CZ_MOD, 0},
        {"VIB", F_INT, 0, 99, 0, 0, 0},
    },
    .presets = CZ_PRESETS,
    .npresets = sizeof(CZ_PRESETS) / sizeof(CZ_PRESETS[0]),
    .fil_page = 0,
    .note_on = cz_note_on,
    .render = cz_native_render,
    .color = 0x7E5E,                             /* #7ac8f0 (tools/colors.json) */
    .macro = {P_E1, P_E2, P_E3, P_E4},
    .amp = cz_amp,
    .block = cz_block,
    .vel_own = 1,
};
