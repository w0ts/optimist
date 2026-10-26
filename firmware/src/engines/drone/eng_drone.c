/* SPDX-License-Identifier: GPL-3.0-only */
/* DRONE: a coupled network of four generators for drones and ambient, with binaural beats (engine 16,
 * FELUCCA_ENG_DRONE, off by default).
 *
 * The network after Caravel 0.3.3's DRONE (bonszaimusic, GPL-3.0-only, github.com/bonszaimusic/caravel
 * firmware/src/eng_drone.c, its TOPO 0..2; docs/drone-engine.md there): "original DSP inspired by the idea of a coupled,
 * slowly moving drone instrument" (the LYRA-8's cross-modulated generators), not a clone nor a circuit model. Rewritten
 * here without its divides, with our BEAT (two ears) and HOLD; its cell modes (TOPO 3..5) not ported.
 *
 * A note plays four sines; each one's phase is modulated (bounded: under a quarter turn, never its accumulated phase)
 * by another's previous sample, through a soft odd cubic (CHAR); the four are averaged at equal weight, a DC block,
 * a one-pole low-pass (TONE), a soft knee. One voice a part (.poly 1): a note transposes the whole network.
 *
 * NETWORK page:
 *   SPREAD the generators' ratios 1 : 1 + s/4 : 1 + s/2 : 1 + s, s 0..1: unison (0) opens to 1 : 5/4 : 3/2 : 2
 *   COUPLE how far each generator's phase moves with its source (0 .. a quarter turn); the track's SHP moves it
 *   TOPO   who modulates whom: PAIRS 0<->1 2<->3, CHAIN 0->1->2->3, RING 3->0->1->2->3; a change crossfades the
 *          sources (PAIRS to RING passes CHAIN)
 *   CHAR   sine -> the soft cubic 1.5 x - 0.5 x^3 (more harmonics into the coupling)
 * MOVE page:
 *   DRIFT  each generator's slow triangle wander, up to +-6.8 cents, its own phase and rate (0.015 .. 0.11 Hz,
 *          faster as DRIFT rises: Caravel's RATE folded in)
 *   TONE   dark -> open, the one-pole; the track's FLT moves it
 *   BEAT   binaural beat: OFF, else the left ear's generators BEAT / 2 below their pitch and the right ear's BEAT / 2
 *          above, 0.05 .. 40 Hz (the LFO RATE's curve, F_LFOHZ). Headphones; on speakers the ears mix (slow beating)
 *   HOLD   ON: a key latches its note, the same key again lets it go; HOLD OFF lets the latched notes go (voice.c)
 * The generators' detune is the track's DTUNE (VOICE 2; unused by a one-voice engine otherwise): {-3 -1 1 3} x up to
 * ~6.7 cents.
 *
 * Stereo: a part is mono up to its pan (fx.c mix_part), so with BEAT the network runs twice (an ear each: twice the
 * cost) and writes the mid (L + R) / 2 to the block and the side (L - R) / 2 to drone_side, which mix_part adds after
 * the level as WIDE's side (insfx.c ifx_side_mix). BEAT OFF: one network, the right ear's state kept on the left's.
 *
 * State: the part's (drone_pt; one voice), kept across notes; a note on a silent voice starts it afresh (drone_note_on). */
static const char *const N_DRONE_TOPO[] = {"PAIRS", "CHAIN", "RING"};
/* = edit[6] at 0: an F_INT with a 0-terminated name list reads OFF on any value of its range (params.c param_format; an
 * F_ENUM would read names 1..127 that are not there: a lock's value on STEP, the editor's DESC, the list tests) */
static const char *const N_DRONE_OFF[] = {"OFF", 0};
static const param_desc_t DRONE_BEAT_OFF = PN("BEAT", F_INT, 0, 127, 0, N_DRONE_OFF);

/* BEAT's increment: LFO_HZ_X100 (Hz x 100) x 2^32 / (100 FS), rounded */
#define DRONE_BEAT_K ((uint32_t)((4294967296ull + 50ull * FS) / (100ull * FS)))
#define DRONE_MAXINC 0x60000000                 /* (a generator's increment at most: 0.375 of FS) */
enum { DR_CPL, DR_TOPO, DR_CHR, DR_DRF, DR_TONE, DR_NC };

/* each part's network: the ears' phases and last outputs, their DC blocks and low-passes; the shared drift phases,
 * smoothed increments and controls; its latched voices (HOLD) */
typedef struct {
    uint32_t ph[2][4];
    int32_t prev[2][4];
    int32_t low[2], dci[2], dco[2];
    uint32_t dph[4];
    int32_t inc[4], ctl[DR_NC];
    uint32_t ready;                       /* 0: the next render installs the pitch and the controls (no glide in) */
    uint32_t latched;                     /* HOLD: the voices whose key went up (voice.c drone_latch_off), bit i voice i */
    uint8_t lnote[NVOICE];                /* (and the note each latched: MONO may move the voice to another key since) */
} drone_pt_t;
static drone_pt_t drone_pt[NPART];
/* a part's side this block (BEAT: (L - R) / 2), mixed by fx.c mix_part; set: written this block (drone_block clears
 * it, the voice zeroes the block) */
static int32_t drone_side[NPART][CTL];
static uint8_t drone_sd[NPART];

static const param_desc_t *drone_desc(const track_t *t, uint32_t k)
{
    return k == 6u && t->p[P_E6] == 0 ? &DRONE_BEAT_OFF : 0;
}

/* once a block, before the voices: HOLD OFF lets the latched notes go (only those: a key still down, or a note
 * started since, plays on; a latched note out of the MONO stack, so a key let go later does not return to it), the
 * side cleared */
static void drone_block(track_t *t)
{
    uint32_t k, i;
    drone_pt_t *d;
    if (t >= &trk[NPART])
        return;
    k = (uint32_t)(t - trk);
    d = &drone_pt[k];
    drone_sd[k] = 0;
    if (d->latched && !t->p[P_E7]) {                    /* HOLD OFF: the latched notes into their release */
        for (i = 0; i < NVOICE; i++)
            if ((d->latched >> i) & 1u) {
                uint32_t nt = d->lnote[i], j, n = 0;
                for (j = 0; j < t->nmono; j++)
                    if (t->mono_stack[j] != nt)
                        t->mono_stack[n++] = t->mono_stack[j];
                t->nmono = (uint8_t)n;
                if (t->v[i].gate && t->v[i].note == nt) {
                    if (t->mono_note == nt)
                        t->mono_note = 0;
                    t->v[i].gate = 0;
                    t->v[i].stage = 3;
                }
            }
        d->latched = 0;
    }
}

static void drone_note_on(track_t *t, voice_t *v)
{
    drone_pt_t *d;
    if (t >= &trk[NPART])
        return;
    d = &drone_pt[t - trk];
    d->latched &= ~(1u << ((uint32_t)(v - t->v) % NVOICE));   /* a new note on this voice: not latched (yet) */
    if (!v->env)                                        /* silent: afresh (sounding: retrigger, steal, the network on) */
        d->ready = 0;
}

/* the controls' targets (Q15; TOPO 0 .. 2 x 32768) */
static void drone_targets(const int16_t *p, const vmod_t *m, int32_t *c)
{
    c[DR_CPL] = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 32512);
    c[DR_TOPO] = clamp(p[P_E2], 0, 2) << 15;
    c[DR_CHR] = clamp(p[P_E3], 0, 127) * 258;
    c[DR_DRF] = clamp(p[P_E4], 0, 127) * 258;
    c[DR_TONE] = clamp((p[P_E5] << 8) + m->cutoff, 0, 32512);
}

/* a ~23 ms follower (2^10 samples), at least a step a sample: it ends on the target */
AINL int32_t drone_follow(int32_t a, int32_t b)
{
    int32_t d = b - a, s = d >> 10;
    return a + (s ? s : (d > 0) - (d < 0));
}

/* generator g's source, from the previous sample's x[]: PAIRS -> CHAIN (topo 0 .. 32768) -> RING (.. 65536) */
AINL int32_t drone_src(const int32_t *x, uint32_t g, int32_t topo)
{
    static const uint8_t PAIRS[4] = {1, 0, 3, 2}, RING[4] = {3, 0, 1, 2};
    int32_t a = x[PAIRS[g]], b = g ? x[g - 1u] : 0, c = x[RING[g]];
    return topo <= 32768 ? a + mulq15(b - a, topo) : b + mulq15(c - b, topo - 32768);
}

/* one ear's network, one sample: the four generators at ph[] (each stepped by st[] + off), their outputs into
 * prev[], the averaged, DC-blocked, low-passed, soft-kneed result */
AINL int32_t drone_ear(drone_pt_t *d, uint32_t e, const uint32_t *st, uint32_t off, int32_t g1)
{
    int32_t nx[4], sum = 0, x, hp;
    uint32_t g;
    for (g = 0; g < 4u; g++) {
        int32_t pm = mulq15(drone_src(d->prev[e], g, d->ctl[DR_TOPO]), d->ctl[DR_CPL]);
        int32_t y = sine_i(d->ph[e][g] + (uint32_t)pm * 32768u);   /* (|pm| < 2^15: under a quarter turn) */
        int32_t cube = mulq15(mulq15(y, y), y);
        nx[g] = y + mulq15((y - cube) >> 1, d->ctl[DR_CHR]);
        sum += nx[g];
        d->ph[e][g] += st[g] + off;
    }
    for (g = 0; g < 4u; g++)
        d->prev[e][g] = nx[g];
    x = sum >> 2;
    hp = x - d->dci[e] + d->dco[e] - (d->dco[e] >> 9);   /* the DC block, ~14 Hz */
    d->dci[e] = x;
    d->dco[e] = hp;
    d->low[e] += (int32_t)(((int64_t)(hp - d->low[e]) * g1) >> 15);
    return soft_knee(d->low[e], 16000);
}

static void drone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint32_t IV[4] = {0, 16384, 32768, 65536};   /* SPREAD's ratios - 1 at 127, Q16 */
    static const int8_t DT[4] = {-3, -1, 1, 3};
    const int16_t *p = t->p;
    uint32_t k = (uint32_t)(t - trk) % NPART, i, g, e;
    drone_pt_t *d = &drone_pt[k];
    uint32_t bi = p[P_E6] > 0 ? LFO_HZ_X100[p[P_E6] & 127] * DRONE_BEAT_K : 0u, st[4];
    uint32_t spr = (uint32_t)clamp(p[P_E0], 0, 127), det = (uint32_t)clamp(p[P_DETUNE], 0, 127);
    int32_t tg[DR_NC], fq[4], *sd;
    (void)v;
    if (n > CTL)
        n = CTL;
    drone_targets(p, m, tg);
    for (g = 0; g < 4u; g++) {                          /* the generators' pitches: the key x SPREAD's ratio x DTUNE */
        uint64_t hz = ((uint64_t)m->inc * (65536u + IV[g] * spr / 127u)) >> 16;
        hz = (hz * (uint32_t)(65536 + DT[g] * (int32_t)det * 252 / 127)) >> 16;
        fq[g] = (int32_t)(hz > DRONE_MAXINC ? DRONE_MAXINC : hz);
    }
    if (!d->ready) {                                    /* afresh: the pitch and the controls installed, apart phases */
        memset(d, 0, (uint32_t)((char *)&d->latched - (char *)d));   /* (the network; HOLD's latched notes kept) */
        for (g = 0; g < 4u; g++) {
            d->ph[0][g] = d->ph[1][g] = g * 0x19234567u;
            d->dph[g] = g * 0x39123457u;
            d->inc[g] = fq[g];
        }
        for (i = 0; i < DR_NC; i++)
            d->ctl[i] = tg[i];
        d->ready = 1;
    }
    sd = drone_side[k] + (CTL - n);                     /* (a voice from inside the block: out is the block + CTL - n) */
    if (bi && !drone_sd[k]) {                           /* the part's side, zeroed once this block */
        for (i = 0; i < CTL; i++)
            drone_side[k][i] = 0;
        drone_sd[k] = 1;
    }
    for (i = 0; i < n; i++) {
        int32_t a = mulq15(amp_at(m, i), VOICE_FS), g1, yl;
        uint32_t rate;
        for (e = 0; e < DR_NC; e++)
            d->ctl[e] = drone_follow(d->ctl[e], tg[e]);
        rate = 1461u + (((uint32_t)d->ctl[DR_DRF] * 9u) >> 5);   /* (Caravel's RATE at DRIFT / 2: 0.015 .. 0.11 Hz) */
        for (g = 0; g < 4u; g++) {                      /* the drifts: distinct triangles, no accumulated detune */
            uint32_t q = d->dph[g] >> 16;
            int32_t tri = q < 32768u ? (int32_t)q * 2 - 32768 : 98303 - (int32_t)q * 2;
            int32_t s;
            d->inc[g] = drone_follow(d->inc[g], fq[g]);
            s = d->inc[g] + (((d->inc[g] >> 16) * mulq15(tri, d->ctl[DR_DRF])) >> 7);
            st[g] = (uint32_t)clamp(s, 0, DRONE_MAXINC);
            d->dph[g] += (rate * (8u + g)) >> 3;
        }
        g1 = 128 + ((d->ctl[DR_TONE] * 30236) >> 15);  /* TONE's one-pole, Q15: 128 .. ~30128 */
        if (bi) {                                       /* BEAT: two ears, the mid to the block, the side aside */
            int32_t yr;
            yl = drone_ear(d, 0, st, 0u - (bi >> 1), g1);
            yr = drone_ear(d, 1, st, bi >> 1, g1);
            sd[i] += mulq15(yl - yr, a);
            out[i] += mulq15(yl + yr, a);
        } else {
            yl = drone_ear(d, 0, st, 0u, g1);
            out[i] += mulq15(yl * 2, a);
        }
    }
    if (!bi) {                                          /* BEAT OFF: the right ear with the left (in phase when it starts) */
        memcpy(d->ph[1], d->ph[0], sizeof d->ph[0]);
        memcpy(d->prev[1], d->prev[0], sizeof d->prev[0]);
        d->low[1] = d->low[0], d->dci[1] = d->dci[0], d->dco[1] = d->dco[0];
    }
}

/* {SPREAD, COUPLE, TOPO, CHAR, DRIFT, TONE, BEAT, HOLD}, the track's DTUNE (XP); BEAT: the LFO RATE's steps (57 1 Hz,
 * 70 2 Hz, 91 6 Hz, 101 10 Hz, 127 40 Hz). The first five after Caravel's (its DETUNE in DTUNE, its RATE gone).
 * HOLD OFF in every preset: a sound loaded never latches what is played (the user turns HOLD on) */
#define DRONE_DT(x) XP(P_DETUNE + 1, (x))
static const preset_t DRONE_PRESETS[] = {
    {"STILL AIR", {0, 12, 0, 18, 32, 76, 0, 0}, {82, 90, 112, 96}, 0, 1, FX(0, 0, 20, 60), DRONE_DT(24)},
    {"OPEN FIFTH", {127, 28, 0, 28, 24, 88, 0, 0}, {76, 88, 116, 94}, 0, 1, FX(0, 0, 20, 60), DRONE_DT(18)},
    {"SLOW CHAIN", {72, 54, 1, 48, 64, 62, 0, 0}, {90, 96, 120, 104}, 0, 1, FX(0, 20, 0, 70), DRONE_DT(38)},
    {"DARK RING", {108, 78, 2, 74, 48, 42, 0, 0}, {72, 92, 110, 100}, 0, 1, FX(0, 0, 30, 80), DRONE_DT(28)},
    {"SWARM", {8, 42, 0, 40, 90, 80, 0, 0}, {84, 94, 118, 108}, 0, 1, FX(0, 30, 0, 60), DRONE_DT(90)},
    {"THETA", {0, 0, 0, 0, 0, 100, 91, 0}, {80, 60, 127, 90}, 0, 1, FX(0, 0, 0, 30), DRONE_DT(0)},     /* 6 Hz */
    {"ALPHA", {0, 20, 2, 0, 10, 90, 101, 0}, {80, 60, 127, 90}, 0, 1, FX(0, 0, 0, 40), DRONE_DT(10)},  /* 10 Hz */
    {"DELTA", {64, 10, 1, 10, 10, 70, 70, 0}, {90, 60, 127, 100}, 0, 1, FX(0, 0, 0, 40), DRONE_DT(6)}, /* 2 Hz */
    {"GAMMA", {0, 0, 0, 20, 0, 110, 127, 0}, {60, 60, 127, 80}, 0, 1, FX(0, 0, 0, 20), DRONE_DT(0)},   /* 40 Hz */
};

static const engine_t ENG_DRONE = {
    .name = "DRONE",
    .page_title = {"NETWORK", "MOVE"},
    .edit = {
        PU("SPREAD", F_PCT, 0, 127, 0, 0),
        PU("COUPLE", F_PCT, 0, 127, 12, 0),
        PN("TOPO", F_ENUM, 0, 2, 0, N_DRONE_TOPO),
        PU("CHAR", F_PCT, 0, 127, 18, 0),
        PU("DRIFT", F_PCT, 0, 127, 32, 0),
        PU("TONE", F_PCT, 0, 127, 76, 0),
        PU("BEAT", F_LFOHZ, 0, 127, 0, 0),           /* 0: OFF (drone_desc) */
        PU("HOLD", F_ONOFF, 0, 1, 0, 0),             /* (P_E7: voice.c drone_latch) */
    },
    .presets = PRESETS_PTR(DRONE_PRESETS),
    .npresets = sizeof(DRONE_PRESETS) / sizeof(DRONE_PRESETS[0]),
    .note_on = drone_note_on,
    .render = drone_render,
    .fil_page = 1,
    .color = 0xB4BB,                             /* #b394d9 (tools/colors.json) */
    .macro = {P_E1, P_E3, P_E4, P_E5},           /* COUPLE CHAR DRIFT TONE (Caravel's performance knobs) */
    .poly = 1,                                   /* (one network a part; with BEAT two ears of four generators) */
    .desc = drone_desc,
    .block = drone_block,
};
