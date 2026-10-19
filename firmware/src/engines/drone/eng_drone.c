/* SPDX-License-Identifier: GPL-3.0-only */
/* DRONE: continuous tones for drones and ambient, with binaural beats (engine 16, FELUCCA_ENG_DRONE, off by default).
 *
 * TONE page:
 *   WAVE  morphs sine (0) -> triangle (64) -> saw (127); the track's SHP moves it
 *   STAK  the oscillators of a note: SOLO one; UNI three, detuned; OCT, 5TH, SUB, 4TH, MIN, MAJ the key and two
 *         intervals above or below it (DRONE_STAK)
 *   DTUN  the two stacked oscillators' detune, up and down: 0 .. 30 cents (squared: fine at the bottom), slow beating
 *   BEAT  binaural beat: OFF, else the left ear BEAT / 2 below each oscillator and the right ear BEAT / 2 above it,
 *         0.05 .. 40 Hz (the LFO RATE's curve, F_LFOHZ). Heard on headphones only; on speakers the ears mix (a slow
 *         beating in the room)
 * MOVE page:
 *   CUT   a low-pass (the trapezoidal SVF, a little resonance) on each ear; the track's FLT moves it
 *   DRFT  a slow random wander of the part (all its voices together): pitch up to +-20 cents, level down to -4 dB,
 *         CUT up to about an octave; a new target every 0.7 .. 3 s, followed with a 0.7 s lag
 *   AIR   soft noise (white through a one-pole at ~900 Hz) under the tone, before CUT: breath, wind
 *   HOLD  ON: a key latches its note, the same key again lets it go (the release); HOLD OFF lets the latched notes go
 *         (a key still down plays on until it is let go).
 *         The sequencer's notes toggle the same way: a sequenced drone keeps HOLD OFF and holds with TIEs
 *
 * Stereo: a part is mono up to its pan (fx.c mix_part), so with BEAT the voices write the mid (L + R) / 2 to the
 * block and the side (L - R) / 2 to drone_side, which mix_part adds after the level as WIDE's side (insfx.c
 * ifx_side_mix: + to the left, - to the right). The part's inserts, sends, meters and USB stem get the mid only.
 * BEAT OFF: no side, the right ear not computed.
 *
 * State: ph[0..2] the left oscillators' phases, s[0..2] the right's, s[3] s[4] the left filter, s[5] s[6] the right,
 * s[7] AIR's pole. A note on a sounding voice (retrigger, steal) keeps all of it: no click (drone_note_on). */
static const char *const N_DRONE_STAK[] = {"SOLO", "UNI", "OCT", "5TH", "SUB", "4TH", "MIN", "MAJ"};
static const char *const N_DRONE_OFF[] = {"OFF"};
static const param_desc_t DRONE_BEAT_OFF = {"BEAT", F_ENUM, 0, 127, 0, N_DRONE_OFF, 0};   /* = edit[3] at 0 */

/* the second and third oscillators: semitones from the key, levels (Q15; 0 = not played); the gain of the stack, Q15:
 * 1 / (1 + the levels), peaks at the level of one oscillator */
static const struct { int8_t iv[2]; int16_t lv[2]; int16_t g; } DRONE_STAK[8] = {
    {{0, 0}, {0, 0}, 32767},              /* SOLO: one sine is a clean binaural tone */
    {{0, 0}, {26214, 26214}, 12603},      /* UNI: three copies, 0.8 each */
    {{12, 0}, {16384, 19661}, 15603},     /* OCT: the octave 0.5, a copy 0.6 */
    {{7, 12}, {19661, 11469}, 16804},     /* 5TH: the fifth 0.6, the octave 0.35 */
    {{-12, 7}, {22938, 13107}, 15603},    /* SUB: the octave below 0.7, the fifth 0.4 */
    {{5, 12}, {19661, 11469}, 16804},     /* 4TH */
    {{3, 7}, {18022, 18022}, 15603},      /* MIN: the minor third and the fifth, 0.55 */
    {{4, 7}, {18022, 18022}, 15603},      /* MAJ */
};

/* BEAT's increment: LFO_HZ_X100 (Hz x 100) x 2^32 / (100 FS), rounded */
#define DRONE_BEAT_K ((uint32_t)((4294967296ull + 50ull * FS) / (100ull * FS)))

/* each part's drift (drone_block), its random generator (AIR, the drift's targets), its latched voices */
typedef struct {
    int32_t dr, tg, cnt, rng;
    int32_t cents, lvl, cut;              /* the drift now: pitch (cents), level (Q15), CUT (SVF units) */
    uint32_t latched;                     /* HOLD: the voices whose key went up (voice.c drone_latch_off), bit i voice i */
} drone_pt_t;
static drone_pt_t drone_pt[NPART];
/* a part's side this block (BEAT: (L - R) / 2 of its voices), mixed by fx.c mix_part; set: written this block
 * (drone_block clears it, the first voice zeroes the block) */
static int32_t drone_side[NPART][CTL];
static uint8_t drone_sd[NPART];

static const param_desc_t *drone_desc(const track_t *t, uint32_t k)
{
    return k == 3u && t->p[P_E3] == 0 ? &DRONE_BEAT_OFF : 0;
}

/* once a block, before the voices: HOLD OFF lets the latched voices go (only those: a key still down, or a note
 * started since, plays on), the drift, the side cleared */
static void drone_block(track_t *t)
{
    uint32_t k, i;
    drone_pt_t *d;
    int32_t drft;
    if (t >= &trk[NPART])
        return;
    k = (uint32_t)(t - trk);
    d = &drone_pt[k];
    drone_sd[k] = 0;
    if (d->latched && !t->p[P_E7]) {                    /* HOLD OFF: the latched notes into their release */
        for (i = 0; i < NVOICE; i++)
            if ((d->latched >> i) & 1u && t->v[i].gate) {
                if (t->mono_note == t->v[i].note)
                    t->nmono = 0, t->mono_note = 0;
                t->v[i].gate = 0;
                t->v[i].stage = 3;
            }
        d->latched = 0;
    }
    if (!d->rng)
        d->rng = (int32_t)(0x2545F491u + k * 0x9E3779B9u);
    drft = t->p[P_E5];
    if (--d->cnt <= 0) {                                /* a new target every 1024 .. 4095 blocks: 0.7 .. 3 s */
        uint32_t r = noise32(&d->rng);
        d->tg = (int32_t)(r >> 16) - 32768;
        d->cnt = 1024 + (int32_t)(r & 3071u);
    }
    d->dr += (d->tg - d->dr) >> 10;                     /* a 0.7 s lag */
    {
        int32_t dr8 = d->dr >> 8, dg = (32767 - d->dr) >> 1;   /* (-128..127; 0..32767: how far down) */
        d->cents = (dr8 * drft * drft * 21) >> 21;      /* +-20 cents at 127 */
        d->lvl = 32767 - ((((dg >> 7) * drft * 95) >> 8)); /* down to 0.63 (-4 dB) */
        d->cut = (d->dr * drft) >> 10;                  /* +-4064: +-16 SVF indices */
    }
}

static void drone_note_on(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[NPART])                                /* a new note on this voice: not latched (yet) */
        drone_pt[t - trk].latched &= ~(1u << ((uint32_t)(v - t->v) % NVOICE));
    if (v->env)                                         /* sounding (retrigger, steal): its phases and filters on */
        return;
    for (i = 0; i < 3u; i++)
        v->ph[i] = (uint32_t)i * 0x55555555u;           /* (the stack apart: no stacked peak at the start) */
    for (i = 0; i < 3u; i++)
        v->s[i] = (int32_t)v->ph[i];
    for (i = 3; i < 8u; i++)
        v->s[i] = 0;
}

/* WAVE: sine -> triangle (w 0 .. 64 << 8), triangle -> saw (.. 127 << 8); f the crossfade, Q15 */
AINL int32_t drone_wave(uint32_t ph, uint32_t inc, uint32_t seg, int32_t f)
{
    int32_t a, b;
    if (seg == 0u)
        return osc_sine(ph);
    if (seg == 1u) {
        a = osc_sine(ph), b = osc_tri(ph);
    } else {
        a = osc_tri(ph), b = osc_saw(ph, inc);
    }
    return a + mulq15(b - a, f);
}

/* the stack of one ear, one sample: the oscillators at ph[] (np of them), stepped by inc[], times the stack's gain */
AINL int32_t drone_stack(uint32_t *ph, const uint32_t *inc, uint32_t np, uint32_t seg, int32_t f, int32_t lv1,
                         int32_t lv2, int32_t g)
{
    int32_t x = drone_wave(ph[0], inc[0], seg, f);
    ph[0] += inc[0];
    if (np > 1u) {
        x += mulq15(drone_wave(ph[1], inc[1], seg, f), lv1) + mulq15(drone_wave(ph[2], inc[2], seg, f), lv2);
        ph[1] += inc[1];
        ph[2] += inc[2];
    }
    return mulq15(x, g);
}

static void drone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t k = (uint32_t)(t - trk) % NPART, st = (uint32_t)p[P_E1] & 7u, i, j, np, seg;
    const drone_pt_t *d = &drone_pt[k];
    int32_t wave = clamp((p[P_E0] << 8) + m->shape - (64 << 8), 0, 127 << 8), f, det, air, g, lv1, lv2;
    uint32_t bi = p[P_E3] > 0 ? LFO_HZ_X100[p[P_E3] & 127] * DRONE_BEAT_K : 0u, incl[3], incr[3], phl[3], phr[3];
    int32_t l1 = v->s[3], l2 = v->s[4], r1 = v->s[5], r2 = v->s[6], ap = v->s[7], rng = d->rng;
    int32_t *sd;
    tsvf_t c;
    if (n > CTL)
        n = CTL;
    sd = drone_side[k] + (CTL - n);                     /* (a voice from inside the block: out is the block + CTL - n) */
    seg = wave == 0 ? 0u : wave <= (64 << 8) ? 1u : 2u;
    f = seg == 1u ? clamp(wave << 1, 0, 32767) : clamp(((wave - (64 << 8)) * 520) >> 8, 0, 32767);
    det = p[P_E2] * p[P_E2] * 30 / 16129;
    np = DRONE_STAK[st].lv[0] ? 3u : 1u;
    lv1 = DRONE_STAK[st].lv[0], lv2 = DRONE_STAK[st].lv[1];
    g = mulq15(DRONE_STAK[st].g, d->lvl);
    incl[0] = det_inc(m->pitch16, d->cents, m->fine_all);
    incl[1] = det_inc(m->pitch16 + DRONE_STAK[st].iv[0] * 16, d->cents + det, m->fine_all);
    incl[2] = det_inc(m->pitch16 + DRONE_STAK[st].iv[1] * 16, d->cents - det, m->fine_all);
    for (j = 0; j < 3u; j++) {
        incr[j] = incl[j] + (bi >> 1);
        incl[j] -= bi >> 1;
        phl[j] = v->ph[j];
        phr[j] = (uint32_t)v->s[j];
    }
    tsvf_coef(&c, (p[P_E4] << 8) + m->cutoff + d->cut, 20);
    air = (int32_t)p[P_E6] * p[P_E6] * 2;               /* Q15, squared: 0 .. 0.98 */
    if (bi && !drone_sd[k]) {                           /* the part's first voice with a side this block */
        for (i = 0; i < CTL; i++)
            drone_side[k][i] = 0;
        drone_sd[k] = 1;
    }
    for (i = 0; i < n; i++) {
        int32_t a = mulq15(amp_at(m, i), VOICE_FS), nz = 0, xl, xr;
        if (air) {                                      /* AIR: white noise through a one-pole, ~900 Hz */
            ap += (((int32_t)(noise32(&rng) >> 17) - 16384) - ap) >> 3;
            nz = mulq15(ap << 1, air);
        }
        xl = tsvf_lp(&c, drone_stack(phl, incl, np, seg, f, lv1, lv2, g) + nz, &l1, &l2);
        if (bi) {                                       /* BEAT: the right ear, the mid to the block, the side aside */
            xr = tsvf_lp(&c, drone_stack(phr, incr, np, seg, f, lv1, lv2, g) + nz, &r1, &r2);
            sd[i] += mulq15((xl - xr) >> 1, a);
            xl = (xl + xr) >> 1;
        }
        out[i] += mulq15(xl, a);
    }
    if (!bi) {                                          /* BEAT OFF: the right with the left (in phase when it starts) */
        for (j = 0; j < 3u; j++)
            phr[j] = phl[j];
        r1 = l1, r2 = l2;
    }
    for (j = 0; j < 3u; j++) {
        v->ph[j] = phl[j];
        v->s[j] = (int32_t)phr[j];
    }
    v->s[3] = l1;
    v->s[4] = l2;
    v->s[5] = r1;
    v->s[6] = r2;
    v->s[7] = ap;
    drone_pt[k].rng = rng;
}

/* {WAVE, STAK, DTUN, BEAT, CUT, DRFT, AIR, HOLD}; BEAT: the LFO RATE's steps (57 1 Hz, 70 2 Hz, 91 6 Hz, 101 10 Hz,
 * 127 40 Hz). HOLD OFF in every preset: a sound loaded never latches what is played (the user turns HOLD on) */
static const preset_t DRONE_PRESETS[] = {
    {"DRONE", {40, 3, 40, 0, 70, 60, 20, 0}, {90, 60, 127, 100}, 0, 0, FX(0, 30, 20, 90)},
    {"THETA", {0, 0, 0, 91, 127, 0, 0, 0}, {80, 60, 127, 90}, 0, 0, FX(0, 0, 0, 30)},       /* 6 Hz, one sine */
    {"ALPHA", {0, 2, 20, 101, 110, 20, 0, 0}, {80, 60, 127, 90}, 0, 0, FX(0, 0, 0, 40)},    /* 10 Hz, the octave */
    {"DELTA", {0, 4, 15, 70, 100, 20, 10, 0}, {90, 60, 127, 100}, 0, 0, FX(0, 0, 0, 40)},   /* 2 Hz, the octave below */
    {"GAMMA", {20, 0, 0, 127, 110, 0, 0, 0}, {60, 60, 127, 80}, 0, 0, FX(0, 0, 0, 20)},      /* 40 Hz */
    {"CHOIR", {90, 7, 60, 0, 55, 80, 30, 0}, {85, 60, 127, 95}, 0, 0, FX(0, 40, 25, 100)},
    {"BREATH", {127, 1, 90, 0, 40, 100, 90, 0}, {95, 60, 127, 105}, 0, 0, FX(0, 20, 30, 100)},
};

static const engine_t ENG_DRONE = {
    .name = "DRONE",
    .page_title = {"TONE", "MOVE"},
    .edit = {
        {"WAVE", F_PCT, 0, 127, 0, 0, 0},
        {"STAK", F_ENUM, 0, 7, 3, N_DRONE_STAK, 0},
        {"DTUN", F_PCT, 0, 127, 30, 0, 0},
        {"BEAT", F_LFOHZ, 0, 127, 0, 0, 0},           /* 0: OFF (drone_desc) */
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"DRFT", F_PCT, 0, 127, 40, 0, 0},
        {"AIR", F_PCT, 0, 127, 0, 0, 0},
        {"HOLD", F_ONOFF, 0, 1, 0, 0, 0},
    },
    .presets = DRONE_PRESETS,
    .npresets = sizeof(DRONE_PRESETS) / sizeof(DRONE_PRESETS[0]),
    .note_on = drone_note_on,
    .render = drone_render,
    .fil_page = 1,
    .color = 0xB4BB,                             /* #b394d9 (tools/colors.json) */
    .macro = {P_E4, P_E5, P_E6, P_REL},
    .poly = 4,                                   /* (two ears of three oscillators a voice) */
    .desc = drone_desc,
    .block = drone_block,
};
