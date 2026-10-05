/* SPDX-License-Identifier: GPL-3.0-only */
/* DX7: a six-operator FM engine compatible with the Yamaha DX7, plus two optional operators (7 and 8).
 * The sound is msfa (the core of Dexed), ported to C in dx7_core.c (Apache-2.0); DX7 voices load into
 * operators 1..6 and sound as in Dexed. This file fits it into SLOOP:
 *  - each synth part has a DX7 voice of its own (dx7_part[].v, the edit buffer): VOICE (EDIT 1 KNOB 1)
 *    loads a factory voice or one of the 32 of the DX7 cartridge into it; the operator editor
 *    (ui_dx7.c: ENV held + the black keys OP1..OP6, PIT, GLO, OP7 / OP8) edits it;
 *  - four macros change the voice without editing it (MOD: the modulators' levels, ATK: the attack
 *    rates, REL: the release rates, FB: the feedback); what notes play is the voice with the macros
 *    (dx7_part[].eff, rebuilt when either changes; sounding notes follow, as in Dexed);
 *  - a DX7 note needs ~0.5 KiB of state: notes live in DX7_NSLOT slots taken by the sounding voices
 *    (SLOOP's budget is 8 sounding voices; a few more cover the voices fading out);
 *  - the DX7's own envelopes shape the sound: SLOOP's ADSR is not used (amp hook), a voice ends when
 *    its carriers' envelopes have finished; the LFO is the DX7's (one per part, as a DX7 has one).
 * The DX7 cartridge: a user sample slot (USR1..3) can hold 32 DX7 voices instead of samples (header
 * magic "FDX7"); the first such slot gives the voices U01..U32. Nothing writes one yet: loading a .syx
 * bank from the web editor (and STORE from the operator editor) is still to come. */
#include "dx7_core.c"
#include "dx7_voices.c"
#ifndef FELUCCA_DX7_ROM
#define FELUCCA_DX7_ROM 1        /* the classic DX7 ROM1A voices (Yamaha data: assets/dx7/PROVENANCE.md) */
#endif
#if FELUCCA_DX7_ROM
#include "dx7_rom1a.h"           /* build/gen: tools/gen_dx7_rom.py */
#else
#define DX7_NROM 0u
#define DX7_ROM_NAMES
#endif

#define DX7_NSLOT 12u
#define DX7_NUSER 32u
#define DX7_NFIXED (DX7_NFACTORY + DX7_NROM)            /* VOICE: the factory voices, the ROM bank, */
#define DX7_NVOICES (DX7_NFIXED + DX7_NUSER)            /* then the cartridge's U01..U32 */
#define DX7_CART_MAGIC 0x37584446u                      /* "FDX7" */
#define DX7_CART_VOICES 4096u                           /* voice k at slot + 4096 + k * 256 */
#define DX7_CART_STRIDE 256u
#define DX7_REC 208u                                    /* a stored voice: DX7_VSIZE bytes, its hash, 0 */

typedef struct {
    dx7_note_t n;
    const voice_t *owner;                               /* the SLOOP voice it plays, 0 = free */
    uint32_t last;                                      /* its part's block count when last rendered */
    uint8_t part, note, vel, keyup;
} dx7_slot_t;

typedef struct {
    uint8_t v[DX7_VSIZE];                               /* the edit buffer: VOICE loaded, then edits */
    uint8_t eff[DX7_VSIZE];                             /* v with the macros: what the notes play */
    int16_t loaded1;                                    /* the VOICE in v + 1, 0 = none yet */
    uint8_t edited;                                     /* v was edited since it was loaded */
    uint32_t gen, eff_gen;                              /* edits (the UI bumps gen), the one eff has */
    int16_t mac[4];                                     /* the macros eff has */
    uint32_t blk;                                       /* blocks rendered */
    dx7_lfo_t lfo;
    int32_t lfo_val, lfo_delay;
} dx7_part_t;

static dx7_slot_t dx7_slot[DX7_NSLOT];
static dx7_part_t dx7_part[NPART];
static int8_t dx7_cart = -1;                            /* user sample slot holding the cartridge, -1 none */
static uint32_t dx7_cart_gen = ~0u;                     /* smp_user_gen it was looked up at */

/* ------------------------------------------------------- the cartridge --- */
static uint32_t dx7_hash(const uint8_t *p, uint32_t n)   /* FNV-1a */
{
    uint32_t h = 0x811C9DC5u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static void dx7_cart_find(void)                          /* after a slot scan (boot, upload, store) */
{
    uint32_t k;
    dx7_cart = -1;
    for (k = 0; k < SMP_USER_SLOTS && dx7_cart < 0; k++) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
        if (h->magic == DX7_CART_MAGIC && h->version == 1 && h->nz == DX7_NUSER)
            dx7_cart = (int8_t)k;
    }
    dx7_cart_gen = smp_user_gen;
}

static void dx7_cart_check(void)
{
    if (dx7_cart_gen != smp_user_gen)
        dx7_cart_find();
}

/* cartridge voice k: its record if valid, else 0 */
static const uint8_t *dx7_cart_voice(uint32_t k)
{
    const uint8_t *r;
    dx7_cart_check();
    if (dx7_cart < 0 || k >= DX7_NUSER)
        return 0;
    r = smp_user_xip((uint32_t)dx7_cart) + DX7_CART_VOICES + k * DX7_CART_STRIDE;
    if (dx7_hash(r, DX7_VSIZE) != ((uint32_t)r[200] | (uint32_t)r[201] << 8 | (uint32_t)r[202] << 16 |
                                   (uint32_t)r[203] << 24))
        return 0;
    return r;
}

/* voice i of the VOICE list (factory, ROM, then the cartridge) into v; an empty cartridge voice: INIT */
static void dx7_voice_get(uint32_t i, uint8_t *v)
{
    const uint8_t *s = i < DX7_NFACTORY ? DX7_FACTORY[i] : i >= DX7_NFIXED ? dx7_cart_voice(i - DX7_NFIXED) : 0;
    uint32_t k;
#if FELUCCA_DX7_ROM
    if (i >= DX7_NFACTORY && i < DX7_NFIXED) {
        dx7_unpack(v, DX7_ROM[i - DX7_NFACTORY]);       /* (OP7 / OP8 off: the DX7 voice as it is) */
        return;
    }
#endif
    if (!s)
        s = DX7_FACTORY[0];
    for (k = 0; k < DX7_VSIZE; k++)
        v[k] = s[k];
    dx7_voice_clamp(v);
}

/* ------------------------------------------------------ the voice names --- */
/* the factory voices' names, as in their data (tests/dx7_test.c checks), then the ROM's: VOICE's names
 * before the cartridge is looked at (the editor's DESC reads the engine table: no cartridge voices) */
static const char *const DX7_FNAMES[] = {"INIT VOICE", "E.PIANO", "BASS PLUCK", "BRASS SECT", "BELLS", "STRINGS",
                                         "ORGAN", "MARIMBA", "CLAV", "SYN LEAD", "SOFT PAD", "TUBULAR",
                                         "8OP KEYS", "8OP BASS", DX7_ROM_NAMES};
_Static_assert(sizeof DX7_FNAMES / sizeof DX7_FNAMES[0] == DX7_NFIXED, "a name per factory / ROM voice");
static char dx7_name_buf[DX7_NUSER][15];
static const char *dx7_names[DX7_NVOICES];
static uint32_t dx7_names_gen = ~0u;
static void dx7_name_of(char *b, const uint8_t *v)       /* the 10 name bytes, trailing blanks cut */
{
    uint32_t k, n = 0;
    for (k = 0; k < 10u; k++) {
        uint8_t c = v[DX7_NAME + k];
        b[k] = c >= 32 && c < 127 ? (char)(c >= 'a' && c <= 'z' ? c - 32 : c) : ' ';
        if (b[k] != ' ')
            n = k + 1u;
    }
    b[n] = 0;
}
static void dx7_names_update(void)
{
    uint32_t i;
    dx7_cart_check();
    if (dx7_names_gen == dx7_cart_gen)
        return;
    for (i = 0; i < DX7_NVOICES; i++) {
        if (i < DX7_NFIXED) {
            dx7_names[i] = DX7_FNAMES[i];
        } else {
            char *b = dx7_name_buf[i - DX7_NFIXED];
            const uint8_t *r = dx7_cart_voice(i - DX7_NFIXED);
            uint32_t u = i - DX7_NFIXED + 1u;
            dx7_names[i] = b;
            b[0] = 'U';
            b[1] = (char)('0' + u / 10u);
            b[2] = (char)('0' + u % 10u);
            b[3] = ' ';
            if (r)
                dx7_name_of(b + 4, r);
            else
                str_cpy(b + 4, "-", 2);
        }
    }
    dx7_names_gen = dx7_cart_gen;
}

/* ------------------------------------------------------------ a part --- */
static dx7_part_t *dx7_of(const track_t *t) { return &dx7_part[(uint32_t)(t - trk) % NPART]; }

/* the macros onto the voice: MOD (modulators' output levels), ATK (R1, + = slower), REL (R4, + = longer),
 * FB (feedback) */
static void dx7_eff_build(dx7_part_t *d, const int16_t *mac)
{
    uint32_t i, alg;
    for (i = 0; i < DX7_VSIZE; i++)
        d->eff[i] = d->v[i];
    alg = d->eff[DX7_ALG] & 31u;
    for (i = 0; i < DX7_NOPS; i++) {
        uint8_t *op = d->eff + DX7_OP(i);
        int carrier = i < 6u ? dx7_is_carrier(alg, i)
                             : (d->eff[DX7_EXT] == DX7_EXT_PAIR || (d->eff[DX7_EXT] == DX7_EXT_STACK && i == 6u));
        if (!carrier)
            op[DX7_OL] = (uint8_t)clamp(op[DX7_OL] + mac[0], 0, 99);
        op[DX7_R1] = (uint8_t)clamp(op[DX7_R1] - mac[1], 0, 99);
        op[DX7_R1 + 3] = (uint8_t)clamp(op[DX7_R1 + 3] - mac[2], 0, 99);
    }
    d->eff[DX7_FB] = (uint8_t)clamp(d->eff[DX7_FB] + mac[3], 0, 7);
    for (i = 0; i < 4u; i++)
        d->mac[i] = mac[i];
    dx7_lfo_reset(&d->lfo, d->eff);
}

static int dx7_slot_sounding(const dx7_slot_t *s)        /* rendered in its part's last block */
{
    return s->owner && dx7_part[s->part % NPART].blk - s->last <= 1u;
}

static dx7_slot_t *dx7_slot_of(const voice_t *v)
{
    uint32_t k = (uint32_t)v->s[0];
    return k < DX7_NSLOT && dx7_slot[k].owner == v ? &dx7_slot[k] : 0;
}

/* once per block and part (ISR): the VOICE asked for, edits and macros into eff, the LFO */
static void dx7_block(track_t *t)
{
    dx7_part_t *d = dx7_of(t);
    uint32_t i, pi = (uint32_t)(t - trk) % NPART;
    int16_t mac[4] = {t->p[P_E1], t->p[P_E2], t->p[P_E3], t->p[P_E4]};
    int changed = 0;
    d->blk++;
    if (d->loaded1 != t->p[P_E0] + 1) {                  /* VOICE changed (knob, preset, project, editor) */
        dx7_voice_get((uint32_t)clamp(t->p[P_E0], 0, (int32_t)DX7_NVOICES - 1), d->v);
        d->loaded1 = (int16_t)(t->p[P_E0] + 1);
        d->edited = 0;
        d->gen++;
    }
    for (i = 0; i < 4u; i++)
        changed |= mac[i] != d->mac[i];
    if (changed || d->gen != d->eff_gen) {
        d->eff_gen = d->gen;
        dx7_eff_build(d, mac);
        for (i = 0; i < DX7_NSLOT; i++) {                /* sounding notes follow (Dx7Note::update) */
            dx7_slot_t *s = &dx7_slot[i];
            if (s->part == pi && dx7_slot_sounding(s) && !s->keyup)
                dx7_note_update(&s->n, d->eff, s->note + d->eff[DX7_TRNSP] - 24, s->vel);
        }
    }
    d->lfo_val = dx7_lfo_tick(&d->lfo);
    d->lfo_delay = dx7_lfo_delay(&d->lfo);
}

static void dx7_note_on(track_t *t, voice_t *v)
{
    dx7_part_t *d = dx7_of(t);
    dx7_slot_t *s = dx7_slot_of(v);
    uint32_t i, pi = (uint32_t)(t - trk) % NPART, held = 0;
    int keep;
    if (d->loaded1 != t->p[P_E0] + 1 || d->gen != d->eff_gen)
        dx7_block(t);                                    /* (a note before the part's first block) */
    if (!s) {                                            /* a free slot, else the one heard least recently */
        uint32_t best = 0, age = 0;
        for (i = 0; i < DX7_NSLOT; i++) {
            dx7_slot_t *c = &dx7_slot[i];
            uint32_t a = c->owner ? dx7_part[c->part % NPART].blk - c->last : 0xFFFFFFFFu;
            if (!c->owner || !c->owner->active)
                a = 0xFFFFFFFFu;
            if (a >= age) {
                age = a;
                best = i;
            }
        }
        s = &dx7_slot[best];
        if (s->owner && s->owner != v && s->owner->active)
            ((voice_t *)s->owner)->active = 0;           /* (cannot happen with the budget of 8) */
        s->owner = 0;
    }
    keep = s->owner == v && dx7_slot_sounding(s);
    for (i = 0; i < NVOICE; i++)
        held |= &t->v[i] != v && t->v[i].gate;
    if (!held)
        dx7_lfo_keydown(&d->lfo);                        /* the first key of a phrase (Dexed) */
    s->owner = v;
    s->part = (uint8_t)pi;
    s->note = v->note;
    s->vel = v->vel;
    s->keyup = 0;
    s->last = d->blk;
    v->s[0] = (int32_t)(s - dx7_slot);
    dx7_note_init(&s->n, d->eff, v->note + d->eff[DX7_TRNSP] - 24, v->vel, keep);
}

/* the voice amplitude: the DX7 envelopes do the shaping. Keeps the SLOOP voice alive while the DX7 note
 * sounds, ends it when its carriers are done; undoes SLOOP's velocity (the voice's own KVS has it) */
static int32_t dx7_amp(track_t *t, voice_t *v, int32_t adsr)
{
    dx7_slot_t *s = dx7_slot_of(v);
    (void)t;
    if (!s) {
        v->active = 0;
        v->stage = 0;
        return 0;
    }
    if (v->stage == 4u)
        return adsr;                                     /* given up for another voice: SLOOP's fade */
    if (!v->gate && !s->keyup) {
        dx7_note_keyup(&s->n);
        s->keyup = 1;
    }
    if (s->keyup && !dx7_note_playing(&s->n)) {
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        return 0;
    }
    v->active = 1;
    v->env = 1 << 24;
    v->stage = (uint8_t)(s->keyup ? 3u : 2u);
    return 32767 * 127 / (v->vel ? v->vel : 1);
}

/* SLOOP's pitch of the voice (glide, tune, its LFO and pitch envelope, unison detune) as a pitch offset
 * from the note the DX7 note started with, Q24 octaves */
static int32_t dx7_pitch(const dx7_slot_t *s, const vmod_t *m)
{
    int32_t pb = (m->pitch16 - (int32_t)s->note * 16) * 87381;      /* 1/16 semitone = 2^24 / 192 */
    uint32_t base = PITCH_INC[clamp(m->pitch16, 0, 2047)];
    int32_t unit = (int32_t)(base >> 12);
    if (unit > 0)                                                   /* the fine part: 1/4096 of the ratio */
        pb += ((int32_t)(m->inc - base) / unit) * 5910;             /* = 2^24 / (4096 ln 2) per step */
    return pb;
}

#define DX7_OUT_SHIFT 26                                 /* a carrier at full level = VOICE_FS / 4 */
static void dx7_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static int32_t buf[DX7_N];
    dx7_part_t *d = dx7_of(t);
    dx7_slot_t *s = dx7_slot_of(v);
    int32_t g0, g1;
    uint32_t i;
    if (!s || n != DX7_N)
        return;
    s->last = d->blk;
    dx7_note_compute(&s->n, d->eff, buf, d->lfo_val, d->lfo_delay, dx7_pitch(s, m));
    g0 = mulq15(m->amp0, VOICE_FS);
    g1 = mulq15(m->amp1, VOICE_FS);
    for (i = 0; i < DX7_N; i++) {
        int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> DX7_LG_N);
        out[i] += (int32_t)(((int64_t)buf[i] * g) >> DX7_OUT_SHIFT);
    }
}

static const param_desc_t *dx7_desc(const track_t *t, uint32_t k)
{
    static param_desc_t vd;
    (void)t;
    if (k)
        return 0;
    dx7_names_update();
    vd.label = "VOICE";
    vd.fmt = F_ENUM;
    vd.min = 0;
    vd.max = (int16_t)(dx7_cart >= 0 ? DX7_NVOICES - 1u : DX7_NFIXED - 1u);   /* U01..U32 with a cartridge */
    vd.def = 1;
    vd.names = dx7_names;
    vd.unit = 0;
    return &vd;
}

#define DX7_R(k) (DX7_NFACTORY + (k))                   /* VOICE of ROM voice k */
static const preset_t DX7_PRESETS[] = {
    /* name, {VOICE, MOD, ATK, REL, FB, -, -, -}, {A D S R: not used}, fenv, mono, sends */
#if FELUCCA_DX7_ROM                                     /* the classic ones (ROM1A): preset 0 = E.PIANO 1 */
    {"E.PIANO 1", {DX7_R(10), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 30, 14, 26)},
    {"BASS 1", {DX7_R(14), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(0, 0, 0, 6)},
    {"BASS 2", {DX7_R(15), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(0, 0, 0, 6)},
    {"BRASS 1", {DX7_R(0), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 20, 10, 30)},
    {"STRINGS 1", {DX7_R(3), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 30, 14, 44)},
    {"PIANO 1", {DX7_R(7), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 10, 10, 26)},
    {"E.ORGAN 1", {DX7_R(16), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 30, 0, 24)},
    {"HARPSICH 1", {DX7_R(18), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 10, 10, 24)},
    {"CLAV 1", {DX7_R(19), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(6, 10, 6, 10)},
    {"VIBE 1", {DX7_R(20), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 20, 20, 34)},
    {"MARIMBA DX", {DX7_R(21), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 0, 20, 30)},
    {"KOTO", {DX7_R(22), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 0, 20, 34)},
    {"FLUTE 1", {DX7_R(23), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(0, 20, 20, 36)},
    {"TUB BELLS", {DX7_R(25), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 0, 24, 50)},
    {"STEEL DRUM", {DX7_R(26), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 0, 20, 30)},
    {"SYN-LEAD 1", {DX7_R(13), 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(6, 16, 24, 20)},
#endif
    {"DX EPIANO", {1, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 30, 14, 26)},
    {"DX BASS", {2, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(0, 0, 0, 6)},
    {"DX BRASS", {3, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 24, 10, 30)},
    {"DX BELLS", {4, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 10, 30, 44)},
    {"DX STRINGS", {5, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 40, 16, 50)},
    {"DX ORGAN", {6, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 30, 0, 24)},
    {"DX MARIMBA", {7, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 0, 20, 30)},
    {"DX CLAV", {8, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(8, 10, 6, 10)},
    {"DX LEAD", {9, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(6, 16, 24, 20)},
    {"DX PAD", {10, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 50, 20, 60)},
    {"DX TUBULAR", {11, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 0, 24, 50)},
    {"DX 8OP KEY", {12, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 0, FX(0, 30, 18, 30)},
    {"DX 8OP BAS", {13, 0, 0, 0, 0, 0, 0, 0}, {0, 64, 127, 64}, 0, 1, FX(4, 0, 0, 6)},
};

static const engine_t ENG_DX7 = {
    "DX7", {"VOICE", "TONE"},
    {
        {"VOICE", F_ENUM, 0, (int16_t)(DX7_NFIXED - 1u), 1, DX7_FNAMES, 0},
        {"MOD", F_INT, -50, 50, 0, 0, 0},
        {"ATK", F_INT, -50, 50, 0, 0, 0},
        {"REL", F_INT, -50, 50, 0, 0, 0},
        {"FB", F_INT, -7, 7, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    DX7_PRESETS, sizeof(DX7_PRESETS) / sizeof(DX7_PRESETS[0]), -1, dx7_note_on, dx7_render,
    0x07EF, {P_E0, P_E1, P_E2, P_E3}, 0, dx7_amp, dx7_desc, dx7_block,
};
