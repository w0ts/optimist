/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GM drum part (track 4): the SAMPLE engine's KIT set (General MIDI percussion map,
 * tools/gen_samples.py GM_KIT), played by its step pattern, the keys when track 4
 * is selected, and its own MIDI channel (GLO -> DRUMS, default 10). Its own voices
 * (outside the parts' voice budget). One-shots: note-offs are ignored; a closed or
 * pedal hi-hat chokes the open one. LEVEL: GLO > DRUMS (G_DRLVL); the sends: each lane's (drum_sends.c);
 * PAN and MUTE: the drum track's P_PAN / P_MUTE. Rendered from the audio ISR. */
#define NDRUM 6
#include "drum_synth.c"       /* synthesised kits (DS_KITS) */
/* P_E0 was unused on the drum track: it holds the kit. 0..4: the GM sample kit and its four
 * treatments (as before: old projects keep their kit), 5..36: the synthesised kits, 37 / 38: X0X's 909 and 808
 * (drum_x0x.c; registry.h). Every build knows every kit UID, built or not. */
#define DRUM_SAMPLED 5u
#define DRUM_SYNTH_END (DRUM_SAMPLED + DS_NKITS)   /* the synthesised kits: DRUM_SAMPLED .. DRUM_SYNTH_END - 1 */
#define DRUM_KITS (DRUM_UID_XSTYLE + DRUM_NXSTYLE)   /* every kit UID */
_Static_assert(DRUM_SYNTH_END == DRUM_UID_X909, "kit UIDs: the X0X kits follow the synthesised ones");
/* the drum lanes' sources by name (ui_drums.c SRC: KIT, the track's; USR1..3; then every kit UID; every build has the
 * lanes' pages, drum_edit.c DL_ANY: SOUND 3, their sends, at least); the kits' names (DRUM_KIT_NAMES) are its tail:
 * one table for both */
#define DRUM_SRC_HEAD 4u
static const char *const DRUM_SRC_NAMES[] = {
    "KIT", "USR1", "USR2", "USR3",
    "ACOUSTIC", "DEEP", "TIGHT", "BRIGHT", "DUST", DS_KIT_NAME_LIST, "X0X 909", "X0X 808",
    "X9 TECH", "X9 HOUSE", "X9 UKG", "X9 ACID", "X8 TRAP", "X8 BOOM", "X8 ELEC", "X8 MIAMI",   /* (their styles) */
#if FELUCCA_DRUM_KITS && DRUM_SRC_HEAD                /* Optimist: then the X0X voices a lane can play (drum_edit.c */
#if FELUCCA_DRUM_X909                                 /* DL_X909 / DL_X808), those of the machines built */
    "X9 BD", "X9 SD", "X9 LT", "X9 MT", "X9 HT", "X9 RS", "X9 CP", "X9 CH", "X9 OH",
#if FELUCCA_X909_CYM
    "X9 CR", "X9 RD",
#endif
#endif
#if FELUCCA_DRUM_X808
    "X8 BD", "X8 SD", "X8 LT", "X8 MT", "X8 HT", "X8 LC", "X8 MC", "X8 HC", "X8 RS", "X8 CL",
    "X8 MA", "X8 CP", "X8 CB", "X8 CH", "X8 OH", "X8 CY",
#endif
#endif
};
#define DRUM_KIT_NAMES (DRUM_SRC_NAMES + DRUM_SRC_HEAD)
/* the X0X voices in DRUM_SRC_NAMES after the kits: the 909's (CR RD with its cymbal samples), then the 808's */
#define DRUM_SRC_X909N (FELUCCA_DRUM_KITS && DRUM_SRC_HEAD && FELUCCA_DRUM_X909 ? (FELUCCA_X909_CYM ? 11u : 9u) : 0u)
#define DRUM_SRC_X808N (FELUCCA_DRUM_KITS && DRUM_SRC_HEAD && FELUCCA_DRUM_X808 ? 16u : 0u)
static const char *const DRUM_KIT_STYLES[] = {"STUDIO", "SOFT", "PUNCHY", "BRIGHT", "DUSTY", DS_KIT_STYLE_LIST
#if DRUM_X0X                                      /* (read through drum_kit(): only a built kit's) */
                                              , "TR-909 MODEL", "TR-808 MODEL", "909 TECHNO", "909 HOUSE", "909 GARAGE",
                                              "909 ACID", "808 TRAP", "808 BOOM", "808 ELECTRO", "808 MIAMI"
#endif
};
_Static_assert(sizeof DRUM_SRC_NAMES / sizeof DRUM_SRC_NAMES[0] ==
               DRUM_SRC_HEAD + DRUM_KITS + DRUM_SRC_X909N + DRUM_SRC_X808N, "a name per kit UID, then the X0X voices");
/* the kits of this build (registry.h): a kit UID not built plays a stand-in (the parameter keeps the UID: a project
 * goes back to a full build as it was). Sampled <-> synthesised: the other source's first kit; X0X 909 / 808: the
 * synthesised 909 / 808, else the first sampled kit */
#define DRUM_SFIRST (DRUM_SMASK & 1 ? 0u : DRUM_SMASK & 2 ? 1u : DRUM_SMASK & 4 ? 2u : DRUM_SMASK & 8 ? 3u : 4u)
static int drum_kit_built(uint32_t k)
{
    if (k < DRUM_SAMPLED)
        return (DRUM_SMASK >> k) & 1;
    if (k < DRUM_SYNTH_END)
        return FELUCCA_DRUM_SYNTH;
    k = DRUM_UID_XMACH(k);                         /* (a style kit: its machine's) */
    return k == DRUM_UID_X909 ? FELUCCA_DRUM_X909 : k == DRUM_UID_X808 && FELUCCA_DRUM_X808;
}
static __attribute__((noinline)) uint32_t drum_kit_of(int32_t v)   /* (one copy: the X0X stand-ins) */
{
    uint32_t k = (uint32_t)clamp(v, 0, DRUM_KITS - 1);
    /* an X0X kit not built: the synthesised 909 / 808 (37 -> 6, 38 -> 5; a style kit: its machine's), else sampled */
    if (k >= DRUM_SYNTH_END && !drum_kit_built(k))
        k = FELUCCA_DRUM_SYNTH ? DRUM_UID_X909 + DRUM_SAMPLED + 1u - DRUM_UID_XMACH(k) : DRUM_SFIRST;
    if ((DRUM_SMASK == 31 && FELUCCA_DRUM_SYNTH) || drum_kit_built(k))   /* (every other kit built: as before) */
        return k;
    return k < DRUM_SAMPLED && FELUCCA_DRUM_SYNTH ? DRUM_SAMPLED : DRUM_SMASK ? DRUM_SFIRST : DRUM_SAMPLED;
}
static uint32_t drum_kit(void) { return drum_kit_of(TDRUM->p[P_E0]); }
#define DRUM_DEFAULT_KIT (FELUCCA_DRUM_SYNTH ? DRUM_SAMPLED : DRUM_SFIRST)   /* power-on: 808 */

static struct {
    voice_t v[NDRUM];
    uint32_t age;
    int16_t set;                 /* SMP_SETS index of "PERC" (GM map), -1 = none */
    int32_t tail;                /* declick: the last output of cut voices, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    uint8_t kit[NDRUM];
    int32_t filter[NDRUM], env[NDRUM];
    uint8_t synth[NDRUM];        /* the voice plays a synthesised kit (ds[]) */
    dsv_t ds[NDRUM];
    volatile uint16_t hits;      /* bit per lane hit since the UI last looked (pads, key LEDs) */
    volatile uint8_t kick;       /* a kick was hit (fx.c DUCK) */
    int32_t a0, a1;              /* the drum track's mute / solo attenuation over this block (fx.c), Q15, 0 = heard */
} drums = {.set = -2};

static int32_t ds_buf[CTL];

static int32_t drum_set(void)
{
    uint32_t i;
    if (drums.set == -2) {
        drums.set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC") && SMP_SETS[i].nz)   /* (a set left out: nz 0) */
                drums.set = (int16_t)i;
    }
    return drums.set;
}

/* ------------------------------------------------------------ lanes --- */
/* The drum track's 16 sounds, one per white key, F3 (kick) .. G5 (cowbell): kicks, snare and clap,
 * the hi-hats, rim and a second snare, the toms, the cymbals, the percussion. Each plays a GM note
 * (the sampled kits: their samples; the synthesised kits: drum_synth.c DS_MAP). A black key plays
 * the lane of the white key left of it (two fingers on one sound). */
static const uint8_t LANE_NOTE[DRUM_LANES] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
static const char *const LANE_NAME[DRUM_LANES] = {
    "KICK", "KICK 2", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "PEDAL HAT", "RIM",
    "SNARE 2", "LOW TOM", "HI TOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"};
static const char *const LANE_SHORT[DRUM_LANES] = {           /* 5 characters: tiles, dials */
    "kick", "kick2", "snare", "clap", "c.hat", "o.hat", "p.hat", "rim",
    "snr 2", "tom l", "tom h", "crash", "ride", "shake", "conga", "bell"};
/* a GM note (MIDI in, old projects) -> its lane: the nearest sound of the 16 (35..81; below: kick, above: shaker) */
static const uint8_t LANE_OF_GM[81 - 35 + 1] = {
    /* 35 */ 1, 0, 7, 2, 3, 8, 9, 4, 9, 6,
    /* 45 */ 9, 5, 10, 10, 11, 10, 12, 11, 12, 13,
    /* 55 */ 11, 15, 11, 13, 12, 14, 14, 14, 14, 14,
    /* 65 */ 14, 14, 15, 15, 13, 13, 15, 15, 13, 13,
    /* 75 */ 7, 7, 7, 14, 14, 15, 15};
static uint32_t lane_of_note(uint32_t note)
{
    return note < 35u ? 0u : note > 81u ? 13u : LANE_OF_GM[note - 35u];
}
/* the lane of key k (0 = F3 .. 26 = G5): white keys in order, a black key the white key left of it */
static uint32_t lane_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    if (i < 0)
        i = W[(k - 1u) % 12u];
    return (uint32_t)((int32_t)(k / 12u) * 7 + i) & 15u;
}
/* the white key of lane l (key index), for the key LEDs */
static uint32_t key_of_lane(uint32_t l)
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    return (l / 7u) * 12u + K[l % 7u];
}

/* drum steps: a lane's bit, level, ratchet */
static int dstep_has(const dstep_t *s, uint32_t l) { return (s->on[(l >> 3) & 1u] >> (l & 7u)) & 1u; }
static uint32_t dstep_lvl(const dstep_t *s, uint32_t l) { return (s->lvl[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_rat(const dstep_t *s, uint32_t l) { return (s->rat[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_mask(const dstep_t *s) { return (uint32_t)s->on[0] | (uint32_t)s->on[1] << 8; }
static void dstep_set(dstep_t *s, uint32_t l, uint32_t lvl, uint32_t rat)   /* lane on, with its level and ratchet */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] |= (uint8_t)(1u << (l & 7u));
    s->lvl[b] = (uint8_t)((s->lvl[b] & ~(3u << sh)) | (lvl & 3u) << sh);
    s->rat[b] = (uint8_t)((s->rat[b] & ~(3u << sh)) | (rat & 3u) << sh);
}
static void dstep_clr(dstep_t *s, uint32_t l)                                /* lane off */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] &= (uint8_t)~(1u << (l & 7u));
    s->lvl[b] &= (uint8_t)~(3u << sh);
    s->rat[b] &= (uint8_t)~(3u << sh);
}

/* the velocity of a level: as played (LV_NORM: vel), ghost, soft, hard */
static uint32_t lvl_vel(uint32_t lvl, uint32_t vel)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    return lvl & 3u ? V[lvl & 3u] : vel;
}
/* a MIDI velocity -> the level it records as (keys play 100: LV_NORM) */
static uint32_t vel_lvl(uint32_t vel)
{
    return vel < 56u ? LV_GHOST : vel < 88u ? LV_SOFT : vel < 116u ? LV_NORM : LV_HARD;
}

/* every drum voice stops now, faded by the declick tail (MIDI All Sound Off) */
#if DRUM_X0X
static void x0x_all_off(void);                  /* drum_x0x.c */
#endif
static void drums_off(void)
{
    uint32_t i;
#if DRUM_X0X
    x0x_all_off();
#endif
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active) {
            drums.v[i].active = 0;
            drums.tail += drums.v[i].s[7];
        }
}

/* a voice for a hit: free, else the oldest (stolen: its last value fades, the declick tail); a closed or
 * pedal hat chokes the open one */
static voice_t *drum_voice(uint32_t note, uint32_t vel)
{
    voice_t *v = &drums.v[0];
    uint32_t i;
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];          /* fade what it was playing, not a step */
            }
    for (i = 0; i < NDRUM; i++) {                   /* free voice, else the oldest */
        if (!drums.v[i].active) {
            v = &drums.v[i];
            break;
        }
        if (drums.v[i].age < v->age)
            v = &drums.v[i];
    }
    if (v->active)
        drums.tail += v->s[7];                      /* stolen voice: fade its last value */
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->ofs = ev_ofs;                                /* a sequenced hit inside the block: from its sample */
    v->s[7] = 0;
    v->age = ++drums.age;
    return v;
}

#include "drum_edit.c"        /* the lanes' own sounds: edits, user samples, other kits' sounds */
#include "drum_sends.c"       /* the lanes' REV / DLY / CHO sends */
#if DRUM_X0X
#include "drum_x0x.c"         /* the X0X 909 / 808 kits */
#endif

static void drum_on(uint32_t note, uint32_t vel)
{
    int32_t si = drum_set(), shift;
    const smp_set_t *set;
    voice_t *v;
    uint32_t i, vi, zi = 0xFFFFu, kit = drum_kit(), lane = note == 76u || note == 77u ? DRUM_LANES : lane_of_note(note);
    if (lane < DRUM_LANES)                          /* the pads and key LEDs (not the click's wood block) */
        drums.hits |= (uint16_t)(1u << lane);
    if (note == 35u || note == 36u)
        drums.kick = 1;                             /* (DUCK) */
#if DL_ANY
    kit = dl_kit_of(lane, kit);                     /* (the lane may play another kit's sound, a user sample) */
#if FELUCCA_DRUM_USR
    if ((i = dl_usr_of(lane)) != 0u) {
        dl_usr_hit(lane, i, note, vel);
        return;
    }
#endif
#endif
#if DRUM_X0X
    if (kit >= DRUM_UID_X909) {                     /* an X0X kit (built: drum_kit_of) */
        if (x0x_on(kit, note, vel, lane))
            return;
        kit = x0x_standin(kit);                     /* a sound the machine lacks */
    }
#endif
    if (FELUCCA_DRUM_SYNTH && kit >= DRUM_SAMPLED) {   /* synthesised kit */
        v = drum_voice(note, vel);
        vi = (uint32_t)(v - drums.v);
        drums.synth[vi] = 1;
        drums.kit[vi] = (uint8_t)kit;
        dl_ds_on(&drums.ds[vi], vi, &DS_KITS[kit - DRUM_SAMPLED], note, vel, lane);
        return;
    }
    if (si < 0)
        return;
    set = &SMP_SETS[si];
    for (i = 0; i < set->nz; i++)
        if (note >= SMP_ZONES[set->z0 + i].lo && note <= SMP_ZONES[set->z0 + i].hi)
            zi = set->z0 + i;
    if (zi == 0xFFFFu)
        return;
    v = drum_voice(note, vel);
    vi = (uint32_t)(v - drums.v);
    v->s[4] = (int32_t)zi;
    v->ph[0] = v->ph[1] = 0;
    v->ph[2] = ~0u;                                 /* (no end before the sample's) */
    v->s[0] = v->s[1] = v->s[2] = 0;
    v->s[3] = FAR(sample_next)(&SMP_ZONES[zi], v, 0);  /* (a hit: XIP calls the RAM code) */
    shift = kit == 1u ? (note <= 36u ? -5 : -2) : kit == 3u ? 2 : kit == 4u ? -1 : 0;
    shift += dl_tune(lane);
    v->s[5] = (int32_t)((pow2_q16((int32_t)note * 16 + shift * 16 - SMP_ZONES[zi].root16) >> 8) * (SMP_ZONES[zi].rate >> 8));
    drums.kit[vi] = (uint8_t)kit;
    drums.synth[vi] = 0;
    drums.filter[vi] = 0;
    drums.env[vi] = 32767;
    dl_smp_fx(vi, lane);
}

#if FELUCCA_GLIDE
/* the drum track's glides (FELUCCA_GLIDE, after X0X 0.10.1: fx.c glide_next): its LEVEL (GLO > DRUMS LEVEL) and PAN
 * ramp per sample; each lane's sends (its REV / DLY / CHO) move a one-pole step a block (a
 * staircase of 0.7 ms steps over ~10 ms). Nothing sounding: they settle at once */
static struct {
    int32_t lv, pl, pr;                          /* where the level and the pan gains are */
    uint8_t on;                                  /* 0: the next block starts at the targets */
    int16_t r[DRUM_LANES + 1], d[DRUM_LANES + 1], c[DRUM_LANES + 1];   /* each lane's sends (+ the click) */
} dgl;
static __attribute__((noinline)) void dgl_lanes(int settle)   /* (XIP) the lanes' sends one block on */
{
    uint32_t l;
    for (l = 0; l <= DRUM_LANES; l++) {
        int32_t r, d, c;
        dsend_lane(l, &r, &d, &c);
        dgl.r[l] = (int16_t)(settle ? r : glide_next(dgl.r[l], r));
        dgl.d[l] = (int16_t)(settle ? d : glide_next(dgl.d[l], d));
        dgl.c[l] = (int16_t)(settle ? c : glide_next(dgl.c[l], c));
    }
}
AINL void dgl_sends(uint32_t note, int32_t *r, int32_t *d, int32_t *c)
{
    uint32_t l = note == 76u || note == 77u ? DRUM_LANES : lane_of_note(note);
    *r = dgl.r[l];
    *d = dgl.d[l];
    *c = dgl.c[l];
}
#endif

/* adds the drums into the dry mix and the reverb send; mono != 0: into mono instead, before the
 * pan and the send (the SLICER, slicer.c slicer_drums, does those after it) */
static inline HOT void drums_mix(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *mono, uint32_t n)
{
    uint32_t k, i;
    int32_t on = fx_on(TDRUM), lvl = song.g[G_DRLVL] * 200, pk = drums.peak;
    FAR(dsend_table)(on);                           /* the lanes' send levels this block (drum_sends.c) */
    int32_t pre = mono && FAR(dsend_one)(on) < 0;   /* the SLICER on, the lanes not alike: sends before it */
    int32_t pan = trk[TRK_DRUM].p[P_PAN], gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
#if FELUCCA_GLIDE
    int32_t lv0, gl0, gr0, dlv, dgl_l, dgl_r;
    {
        uint32_t any = drums.tail != 0, j;
        for (j = 0; j < NDRUM; j++)
            any |= drums.v[j].active;
#if DRUM_X0X
        any |= x0x_sounding() != 0;                 /* (the X0X channels: not drums.v voices) */
#endif
        if (!any || !dgl.on) {                      /* nothing sounds (or the first block): at the targets */
            dgl.on = (uint8_t)any;
            dgl.lv = lvl, dgl.pl = gl, dgl.pr = gr;
            FAR(dgl_lanes)(1);
            if (!any)
                return;
        } else {
            FAR(dgl_lanes)(0);
        }
        lv0 = dgl.lv, gl0 = dgl.pl, gr0 = dgl.pr;
        dgl.lv = glide_next(lv0, lvl), dgl.pl = glide_next(gl0, gl), dgl.pr = glide_next(gr0, gr);
        dlv = dgl.lv - lv0, dgl_l = dgl.pl - gl0, dgl_r = dgl.pr - gr0;
    }
#endif
    for (i = 0; i < n && drums.tail; i++) {         /* declick tail, ~0.4 ms */
        if (mono) {
            mono[i] += drums.tail;
        } else {
            ml[i] += drums.tail;
            mr[i] += drums.tail;
#if FELUCCA_USB_AUDIO
            track_capture[i * NTRK + TRK_DRUM] += drums.tail;
#endif
        }
        drums.tail -= drums.tail / 16 + (drums.tail > 0 ? 1 : drums.tail < 0 ? -1 : 0);
    }
    for (k = 0; k < NDRUM; k++) {               /* synthesised voices: render, then as below */
        voice_t *v = &drums.v[k];
        uint32_t m = n < CTL ? n : CTL, o;          /* (the mix runs in blocks of CTL) */
        int32_t r, d, c;                            /* its lane's sends (drum_sends.c) */
        if (!v->active || !drums.synth[k])
            continue;
#if FELUCCA_GLIDE
        dgl_sends(v->note, &r, &d, &c);
#else
        dsend_of(v->note, &r, &d, &c);
#endif
        o = v->ofs < m ? v->ofs : 0u;               /* a hit inside the block: from its sample */
        v->ofs = 0;
        if (!ds_render(&drums.ds[k], ds_buf + o, m - o))
            v->active = 0;
        for (i = o; i < m; i++) {
#if FELUCCA_GLIDE
            int32_t s = mulq15(ds_buf[i], mulq15(lv0 + ((dlv * (int32_t)i) >> CTL_LOG2),
                                                  32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
#else
            int32_t s = mulq15(ds_buf[i], mulq15(lvl, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
#endif
#if FELUCCA_DRUM_EDIT
            if (dv.on[k])                              /* the lane's CUT - */
                s = dl_smp_apply(k, s, v);
#endif
            v->s[7] = s;
            DSEND_KEEP(i, s);                       /* (its delay / chorus sends: after its loop) */
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
#if FELUCCA_GLIDE
            ml[i] += (s * (gl0 + ((dgl_l * (int32_t)i) >> CTL_LOG2))) >> 12;
            mr[i] += (s * (gr0 + ((dgl_r * (int32_t)i) >> CTL_LOG2))) >> 12;
#else
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
#endif
#if FELUCCA_USB_AUDIO
            track_capture[i * NTRK + TRK_DRUM] += s;
#endif
            if (r)
                rev[i] += mulq15(s, r);
        }
        DSEND_POST(o, m, r, d, c, pre);
        if (!v->active) {
            drums.tail += v->s[7];                  /* ended: no step at the end */
            v->s[7] = 0;
        }
    }
    for (k = 0; k < NDRUM; k++) {
        voice_t *v = &drums.v[k];
        const smp_zone_t *z = smp_zone((uint32_t)v->s[4]);   /* (a user sample on a lane: its slot) */
        if (drums.synth[k])
            continue;
        uint32_t frac = v->ph[1], stepq = (uint32_t)v->s[5], i0;   /* Q16 source samples per output (drum_on) */
        int32_t g, r, d, c;
        if (!v->active)
            continue;
#if FELUCCA_GLIDE
        int32_t g1, dg;
        dgl_sends(v->note, &r, &d, &c);
        g = mulq15(lv0, v->vel * 258);
        g += g * 3 >> 2;                           /* x1.75 (+5 dB): as loud as the synthesised kits */
        g1 = mulq15(dgl.lv, v->vel * 258);
        g1 += g1 * 3 >> 2;
        dg = g1 - g;
#else
        dsend_of(v->note, &r, &d, &c);
        g = mulq15(lvl, v->vel * 258);
        g += g * 3 >> 2;                           /* x1.75 (+5 dB): as loud as the synthesised kits */
#endif
        i = v->ofs < n ? v->ofs : 0u;              /* a hit inside the block: from its sample */
        v->ofs = 0;
        i0 = i;
        for (; i < n; i++) {
            int32_t s;
            frac += stepq;
            while (frac >= 65536u) {
                frac -= 65536u;
                v->s[2] = v->s[3];
                if (v->ph[0] >= z->n || v->ph[0] >= v->ph[2]) {   /* (ph[2]: a lane's hit ends there) */
                    v->active = 0;
                    if (v->s[4] >= 0x8000)
                        drums.tail += v->s[7];      /* (cut short: declicked) */
                    break;
                }
                v->s[3] = sample_next(z, v, 0);
            }
            if (!v->active)
                break;
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
#if FELUCCA_GLIDE
            s = mulq15(s, mulq15(g + ((dg * (int32_t)i) >> CTL_LOG2), 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
#else
            s = mulq15(s, mulq15(g, 32767 - drums.a0 - (((drums.a1 - drums.a0) * (int32_t)i) >> CTL_LOG2)));
#endif
            if (drums.kit[k] == 1u || drums.kit[k] == 4u) {
                drums.filter[k] += (s - drums.filter[k]) >> (drums.kit[k] == 1u ? 2 : 1);
                s = drums.filter[k];
                if (drums.kit[k] == 4u) s = (s >> 8) * 256;
            } else if (drums.kit[k] == 2u) {
                s = mulq15(s, drums.env[k]);
                drums.env[k] -= (drums.env[k] >> 11) + 1;
                if (drums.env[k] <= 0) v->active = 0;
            }
#if FELUCCA_DRUM_EDIT
            if (dv.on[k])                          /* the lane's DECAY, CUT, LEVEL */
                s = dl_smp_apply(k, s, v);
#endif
            v->s[7] = s;
            DSEND_KEEP(i, s);                       /* (its delay / chorus sends: after its loop) */
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
#if FELUCCA_GLIDE
            ml[i] += (s * (gl0 + ((dgl_l * (int32_t)i) >> CTL_LOG2))) >> 12;
            mr[i] += (s * (gr0 + ((dgl_r * (int32_t)i) >> CTL_LOG2))) >> 12;
#else
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
#endif
#if FELUCCA_USB_AUDIO
            track_capture[i * NTRK + TRK_DRUM] += s;
#endif
            if (r)
                rev[i] += mulq15(s, r);
        }
        DSEND_POST(i0, i, r, d, c, pre);           /* (i: where it ended) */
        v->ph[1] = frac;
    }
#if DRUM_X0X
    pk = FAR(drums_x0x)(ml, mr, rev, mono, n, on, lvl, pre, gl, gr, pk);   /* the X0X kits' voices */
#endif
    drums.peak = pk;
}
static HOT void drums_render(int32_t *ml, int32_t *mr, int32_t *rev, uint32_t n) { drums_mix(ml, mr, rev, 0, n); }
static HOT void drums_render_mono(int32_t *mono, uint32_t n) { drums_mix(0, 0, 0, mono, n); }
