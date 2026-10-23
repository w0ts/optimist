/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesised drum kits (drums.c kits DRUM_SAMPLED..): analogue-style models in fixed point, in the
 * spirit of the classic drum machines. Every sound of a kit is one dsnd_t, written in musical units
 * by tools/gen_drumkits.py (felucca_drumkits.h). A sound is up to four layers into one filter:
 *   tone   SINE / TRI / SQUARE / FM / BELL (two squares 1 : 1.48), with an exponential pitch drop
 *          (BEND semitones over BTIME); its envelope HOLDs at full, then decays. A second partial
 *          (T2: a sine at RATIO x the tone, decaying twice as fast): the second mode of a drum head
 *   click  the attack: a 1-2 ms burst of bright noise (the beater, the stick)
 *   noise  WHITE / METAL (six squares at the 808 cymbal ratios) / CYM (metal + white) / CHIP
 *          (15-bit LFSR at its own clock); its own hold and decay. CLAP: three bursts, then the tail
 *   filter a resonant state-variable filter (LP / BP / HP) on the noise, or on everything; its
 *          cutoff follows the pitch envelope (FENV) and the velocity (softer = darker); a
 *          12 dB/oct high-pass (HPF) on the noise before it
 *   out    drive (tanh), the kit's crush (bit depth, sample-and-hold), the sound's level
 * The envelopes, the pitch and the filter run at the control rate (CTL samples) and are ramped
 * per sample. Cost: ~50-60 integer ops per voice and sample (METAL: +6 adds).
 * KAWA extras (builder item DS_KAWA; after Kawa Tracker's kit engine, Guizmoh/Kawa-Tracker-Fm1 kawa_drums.c, GPL-3.0):
 * four METAL tunings (the 808's, a wide cymbal, a tight cluster, a ride's bell), the second partial mixed, ring-
 * modulating the tone or modulating its phase (FM), re-strikes (a flam, a clap's hands, a guiro's ridges: HITS more
 * hits GAP apart, the click and the noise struck again, the tone too if asked) and a RISE (the tone and the noise
 * swell in: shakers, brushes, a rumble). PLAITS (builder item DS_PLAITS, drum_plaits.c): two Plaits models as waves. */
enum { DW_NONE, DW_SINE, DW_TRI, DW_SQUARE, DW_FM, DW_BELL, DW_PKICK, DW_PSNARE };
enum { DN_NONE, DN_WHITE, DN_METAL, DN_CYM, DN_CHIP, DN_CLAP = 0x10 };
enum { DF_OFF, DF_LP, DF_BP, DF_HP, DF_ALL = 4 };   /* flt: mode | DF_ALL (the tone too) | res << 3 */
typedef struct {
    uint8_t wave, src;           /* DW_*, DN_* (| DN_CLAP) */
    uint8_t pitch, fine;         /* the tone: MIDI note + 1/16 semitones (METAL / CYM: their base) */
    uint8_t bend, btime;         /* pitch drop: semitones at the hit, DECAY_K index of its fall */
    uint8_t hold, decay, tlev;   /* tone: hold (2 ms units), DECAY_K index, level 0..127 */
    uint8_t t2, t2lev;           /* second partial: ratio x32 (0 = none), level 0..127 */
    uint8_t click;               /* attack burst level 0..127 */
    uint8_t nlev, nhold, ndec;   /* noise: level 0..127, hold (2 ms units), DECAY_K index */
    uint8_t flt, fcut;           /* filter: DF_* | res (0..31) << 3; CUTOFF_HZ index */
    int8_t fenv;                 /* cutoff offset at the hit (CUTOFF_HZ steps, follows the pitch drop) */
    uint8_t hpf;                 /* CUTOFF_HZ index of the noise high-pass, 0 = off */
    uint8_t chip;                /* CHIP noise: clock note */
    uint8_t drive, level;        /* drive 0..127; level: 128 + 4 x dB (-32 .. +31.75 dB) */
#if FELUCCA_DS_KAWA
    uint8_t kx;                  /* KAWA: METAL tuning (bits 0..1), the partial's mode 0 MIX 1 RING 2 FM (2..3), re-strikes
                                  * (4..6), the tone struck again too (7) */
    uint8_t kgap, krise;         /* the re-strikes' gap (blocks of CTL samples, +0 / +1 at random); RISE: DECAY_K index, 0 none */
#endif
} dsnd_t;
#define DK_MSET(d) ((d)->kx & 3u)
#define DK_T2MODE(d) (((d)->kx >> 2) & 3u)
#define DK_HITS(d) (((d)->kx >> 4) & 7u)
#define DK_HTONE 0x80u
enum { DK_MIX, DK_RING, DK_FM };
#if FELUCCA_DS_PLAITS
#include "drum_plaits.c"         /* KPUNC, SCRAK (DW_PKICK, DW_PSNARE) */
#endif
#define DS_LANES 16              /* kick snare clap hat open-hat tom-lo tom-hi crash ride shaker conga
                                  * rim cowbell clave kick-2 snare-2 */
typedef struct {                 /* (its name and style: drums.c DRUM_KIT_NAMES / DRUM_KIT_STYLES, DS_KIT_NAME_LIST) */
    uint8_t crush;               /* low nibble: bits dropped, high nibble: sample-and-hold - 1 */
    uint16_t si[DS_LANES];       /* each lane's sound: DS_SNDS index (a sound several kits share is kept once) */
} dkit_t;
/* a sound as the kit tables keep it (tools/gen_drumkits.py, DSC_NP + 1 = 12 B; 17 B until 2026-10-23, the whole dsnd_t,
 * 25 B, before: 1.3 KB more of flash): the 16 fields most sounds set, packed as bit fields low bit first (DSC_W the width
 * of each, in this order: wave src pitch fine bend btime hold decay tlev click nlev nhold ndec flt fcut level; a column
 * with few distinct values holds an index into its sorted values, DSC_MV from DSC_MO[i], 255 = the value itself), then
 * the index of a row of DS_RARE holding the rest (t2, t2lev, fenv, hpf, chip, drive, the KAWA extras), which most
 * sounds leave at 0; row 0 is all zeros. ds_unpack makes the dsnd_t a voice plays (its own copy, dsv_t snd, at a
 * note-on; the UI's pictures and values make one on the stack) */
#define DSC_NP 11
typedef struct {
    uint8_t p[DSC_NP], rare;
} dsndc_t;
static const uint8_t DSC_OFS[16] = {   /* where each packed field goes in a dsnd_t */
    __builtin_offsetof(dsnd_t, wave), __builtin_offsetof(dsnd_t, src), __builtin_offsetof(dsnd_t, pitch), __builtin_offsetof(dsnd_t, fine),
    __builtin_offsetof(dsnd_t, bend), __builtin_offsetof(dsnd_t, btime), __builtin_offsetof(dsnd_t, hold), __builtin_offsetof(dsnd_t, decay),
    __builtin_offsetof(dsnd_t, tlev), __builtin_offsetof(dsnd_t, click), __builtin_offsetof(dsnd_t, nlev), __builtin_offsetof(dsnd_t, nhold),
    __builtin_offsetof(dsnd_t, ndec), __builtin_offsetof(dsnd_t, flt), __builtin_offsetof(dsnd_t, fcut), __builtin_offsetof(dsnd_t, level)};
typedef struct {
    uint8_t t2, t2lev;
    int8_t fenv;
    uint8_t hpf, chip, drive;
#if FELUCCA_DS_KAWA
    uint8_t kx, kgap, krise;
#endif
} dsrare_t;
#if FELUCCA_DS_KAWA
#define DS_KX(a, b, c) , a, b, c
#else
#define DS_KX(a, b, c)
#endif
#include "felucca_drumkits.h"    /* DS_SNDS[], DS_RARE[], DS_KITS[], DS_NKITS; DS_XKITS[], DS_XSNDS[], DS_NXKITS (tools/gen_drumkits.py) */
static void ds_unpack(dsnd_t *o, const dsndc_t *c)
{
    const dsrare_t *r = &DS_RARE[c->rare];
    const uint8_t *s = c->p;
    uint32_t acc = 0, n = 0, i;
    for (i = 0; i < 16u; i++) {                 /* the packed fields, low bit first, DSC_W[i] bits each */
        uint32_t w = DSC_W[i], v;
        while (n < w) {
            acc |= (uint32_t)*s++ << n;
            n += 8;
        }
        v = acc & ((1u << w) - 1u);
        acc >>= w;
        n -= w;
        ((uint8_t *)o)[DSC_OFS[i]] = DSC_MO[i] == 255u ? (uint8_t)v : DSC_MV[DSC_MO[i] + v];
    }
    o->t2 = r->t2, o->t2lev = r->t2lev, o->fenv = r->fenv, o->hpf = r->hpf, o->chip = r->chip, o->drive = r->drive;
#if FELUCCA_DS_KAWA
    o->kx = r->kx, o->kgap = r->kgap, o->krise = r->krise;
#endif
}
/* a lane's sound by its index: DS_SNDS', or (DS_XSI: bit 15) its own in an extra kit's table (kit UIDs 47..: DS_XKITS, each
 * in a builder item of its own; a table not built is never read: its kit plays a stand-in, drums.c drum_kit_of) */
#define DS_XSI 0x8000u
#if DS_NXKITS
AINL const dsndc_t *ds_snd(uint32_t si) { return si & DS_XSI ? &DS_XSNDS[(si >> 8) & 127u][si & 255u] : &DS_SNDS[si]; }
#else
#define ds_snd(si) (&DS_SNDS[si])
#endif
#define DS_SND(k, lane) ds_snd((k)->si[lane])   /* kit k's lane: its sound */

/* GM note -> synth lane and a pitch offset in semitones */
static const struct { uint8_t lane; int8_t semi; } DS_MAP[128 - 35] = {
    /* 35 */ {14, 0}, {0, 0}, {11, 0}, {1, 0}, {2, 0}, {15, 0}, {5, -3}, {3, 0}, {5, 0}, {3, -1},
    /* 45 */ {5, 3}, {4, 0}, {6, -3}, {6, 0}, {7, 0}, {6, 3}, {8, 0}, {7, 3}, {8, 6}, {9, 4},
    /* 55 */ {7, 5}, {12, 0}, {7, 1}, {9, -5}, {8, 1}, {10, 9}, {10, 6}, {10, 3}, {10, 0}, {10, -5},
    /* 65 */ {6, 5}, {6, 1}, {12, 7}, {12, 3}, {9, -3}, {9, 0}, {13, 9}, {13, 6}, {9, -7}, {9, -9},
    /* 75 */ {13, 0}, {13, 4}, {13, -1}, {10, 12}, {10, 8}, {8, 24}, {8, 20}, {9, 2},
    /* 83..127: shaker */
};
static uint32_t ds_lane(uint32_t note, int32_t *semi)
{
    *semi = 0;
    if (note < 35u)
        return 0;
    if (note >= 83u)
        return 9;
    *semi = DS_MAP[note - 35u].semi;
    return DS_MAP[note - 35u].lane;
}

typedef struct {
    const dsnd_t *d;             /* the sound it plays: its own copy below (ds_on_snd) */
    dsnd_t snd;
    uint32_t ph, inc, inc_to;    /* tone phase; increment now and at the end of the block */
    uint32_t ph2, ph3, mph[6];   /* BELL / FM second oscillator, the second partial, the metal squares */
    uint32_t minc[6];
    int32_t pe;                  /* pitch envelope Q15 (32767 = BEND semitones) */
    int32_t amp, amp_to;         /* tone envelope Q15, now and at the end of the block */
    int32_t nz, nz_to;           /* noise envelope Q15 */
    int32_t ck, ck_to;           /* click envelope Q15 */
    uint32_t kpe, ka, kn;        /* per-block decay factors Q16 */
    uint32_t t;                  /* samples since the hit (CLAP bursts) */
    uint16_t hold, nhold;        /* blocks left at full level */
    int32_t base16;              /* pitch in 1/16 semitones */
    int32_t cut;                 /* filter cutoff (CUTOFF_HZ index << 8) without the envelope */
    int32_t f1, f2, fk;          /* SVF states, damping (Q12) */
    tsvf_t c;                    /* SVF coefficients (dsp.c tsvf_t: a1, a2, a3 as before) */
    int32_t hp1, hp2, ahp;       /* noise high-pass states, coefficient Q15 (0 = off) */
    int32_t rnd, cprev;          /* white noise state, the click's last noise sample */
    uint32_t lfsr, cacc, cinc;   /* CHIP noise */
    int32_t gain, lg;            /* velocity Q15, the sound's level Q10 */
    int32_t held, holdn;         /* crush: sample-and-hold */
    uint8_t crush, bursts, fmode;
#if FELUCCA_DS_KAWA
    uint8_t hits, gap;           /* re-strikes left, blocks to the next */
    int32_t rr, rr_to;           /* RISE: the envelopes' share, Q15 (0 at the hit, up to 32767) */
    uint32_t kr;                 /* RISE: its per-block factor */
#endif
#if FELUCCA_DS_PLAITS
    dplaits_t pl;                /* a Plaits model's state (DW_PKICK, DW_PSNARE) */
    uint8_t quiet;               /* its quiet blocks in a row */
#endif
} dsv_t;

/* per-sample DECAY_K (Q16) -> per-block (CTL = 32 samples) by squaring five times */
AINL uint32_t ds_k32(uint32_t idx)
{
    uint32_t k = DECAY_K[idx & 127u], i;
    for (i = 0; i < 5u; i++)
        k = (k * k) >> 16;
    return k;
}
static int32_t ds_onepole(uint32_t cut)                 /* CUTOFF_HZ index -> a = g / (1 + g), Q15 */
{
    uint32_t g = SVF_G[cut & 127u];
    return (int32_t)((g << 15) / (4096u + g));
}
/* a lane's sound edits at its hit (drum_edit.c, drum_x0x.c: the same lines): CUT- as a one-pole coefficient
 * (>= ~80 Hz), LEVEL in dB (-24..6) as a Q12 gain 2^(dB / 6.02), 0 = no edit */
AINL int32_t dl_cut_k(int32_t cut) { return ds_onepole((uint32_t)clamp(127 + 2 * cut, 20, 127)); }
AINL int32_t dl_lvl_g(int32_t db) { return db ? (int32_t)(pow2_q16(db * 32) >> 4) : 0; }
AINL uint32_t ds_inc(int32_t p16) { return pitch_inc(clamp(p16, 0, 127 * 16 + 15)); }
AINL uint16_t ds_blocks(uint32_t units2ms) { return (uint16_t)(units2ms * 2u * FS / 1000u / CTL); }

/* the filter's coefficients for this block: the cutoff plus the envelope's share (dsp.c tsvf_coef_k's lines: the call
 * moves drums_mix's RAM code by -40..+58 B by profile, docs/DSP-SHARED.md, so the copy stays) */
AINL void ds_filter(dsv_t *s)
{
    const dsnd_t *d = s->d;
    int32_t cut = s->cut + ((s->pe * (int32_t)d->fenv) >> 7), i, g, den;   /* (pe Q15 x steps << 8) */
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + s->fk)) >> 12);
    s->c.a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    s->c.a2 = (s->c.a1 * g) >> 12;
    s->c.a3 = (s->c.a2 * g) >> 12;
}

/* the 808 cymbal oscillators (205.3 304.4 369.6 522.7 540 800 Hz) as 1/16 semitones above the first; KAWA: three more
 * tunings (kawa_drums.c METAL_R): a wider cymbal, a tight cluster (jingles, bright hats), wide apart (a ride's ping, bells) */
#if FELUCCA_DS_KAWA
static const int16_t DS_METALS[4][6] = {{0, 109, 163, 259, 268, 377}, {0, 96, 130, 169, 212, 265},
                                        {0, 7, 18, 46, 108, 167}, {0, 304, 404, 462, 509, 654}};
#define DS_METAL DS_METALS[DK_MSET(d)]
#else
static const int16_t DS_METAL[6] = {0, 109, 163, 259, 268, 377};
#endif

/* sound d (crush: its kit's) hit at note (semi: the note's offset from the sound, ds_lane) */
static void ds_on_snd(dsv_t *s, const dsnd_t *d, uint32_t crush, int32_t semi, uint32_t note, uint32_t vel)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    s->snd = *d;                                        /* (d may be a caller's temporary: ds_unpack, de_synth) */
    d = s->d = &s->snd;
    s->crush = (uint8_t)crush;
    s->gain = (int32_t)vel * 258;
    {   /* level: quarter dB from 128; 24 quarters = 6 dB = x2 (0.02 dB off per 6 dB) */
        static const uint16_t QDB[24] = {1024, 1054, 1085, 1116, 1149, 1182, 1217, 1252, 1289, 1326, 1365, 1405,
                                         1446, 1488, 1532, 1576, 1622, 1669, 1718, 1768, 1820, 1873, 1928, 1984};
        int32_t e = (int32_t)d->level - 128 + 192, q = e / 24 - 8;   /* (+192: positive for the divide) */
        uint32_t g = QDB[e % 24];
        s->lg = (int32_t)(q >= 0 ? g << q : g >> -q);
    }
    s->base16 = ((int32_t)d->pitch + semi) * 16 + d->fine;
    s->pe = d->bend || d->fenv ? 32767 : 0;
    s->kpe = ds_k32(d->btime);
    s->inc = s->inc_to = ds_inc(s->base16 + ((s->pe * (int32_t)d->bend * 16) >> 15));
    s->amp = s->amp_to = d->wave ? (int32_t)d->tlev * 258 : 0;
    s->ka = ds_k32(d->decay);
    s->hold = ds_blocks(d->hold);
    s->nz = s->nz_to = (d->src & 15u) ? (int32_t)d->nlev * 258 : 0;
    s->kn = (d->src & DN_CLAP) ? ds_k32(12) : ds_k32(d->ndec);  /* CLAP: short bursts first */
    s->nhold = (d->src & DN_CLAP) ? 0 : ds_blocks(d->nhold);
    s->ck = s->ck_to = (int32_t)d->click * 258;
    s->bursts = 1;
    s->fmode = d->flt & 7u;
    s->fk = 8192 - (int32_t)(d->flt >> 3) * 245;        /* damping 2.0 .. ~0.15 (Q12), as tsvf_coef */
    /* softer hits are darker: about an octave from the hardest to a ghost note */
    s->cut = ((int32_t)d->fcut << 8) + ((int32_t)vel - 110) * 40;
    if (s->fmode & 3u)
        ds_filter(s);
    s->ahp = d->hpf ? ds_onepole(d->hpf) : 0;
    s->rnd = (int32_t)(0x9E3779B9u ^ (note * 2654435761u) ^ rng());
    s->lfsr = 0x4001u;
    {   /* CHIP clock: 8x that note; saturated (the top notes would wrap 32 bits: a dull noise) */
        uint32_t ci = pitch_inc(clamp((int32_t)d->chip, 0, 127) * 16);
        s->cinc = ci >= 0x20000000u ? 0xFFFFFFFFu : ci << 3;
    }
    for (i = 0; i < 6u; i++)
        s->minc[i] = ds_inc(s->base16 + DS_METAL[i]);
#if FELUCCA_DS_KAWA
    if (DK_HITS(d)) {                                   /* re-strikes: short bursts first, the noise's own decay on the
                                                         * last (as CLAP's: the hands, then the room) */
        s->kn = ds_k32(12);
        s->nhold = 0;
    }
    s->hits = (uint8_t)DK_HITS(d);
    s->gap = (uint8_t)(d->kgap ? d->kgap : 1u);
    s->rr = s->rr_to = d->krise ? 0 : 32767;
    s->kr = d->krise ? ds_k32(d->krise) : 0u;
#endif
#if FELUCCA_DS_PLAITS
    if (d->wave >= DW_PKICK) {
        dp_on(&s->pl, d, s->base16, vel, (uint32_t)s->rnd);
        s->fmode = 0;                                   /* (its fcut is its TONE: no filter of the layers; the velocity both
                                                         * the model's accent, its timbre, and the level as every sound's) */
    }
#endif
}
static void ds_on(dsv_t *s, const dkit_t *kit, uint32_t note, uint32_t vel)
{
    int32_t semi;
    uint32_t lane = ds_lane(note, &semi);
    dsnd_t d;
    ds_unpack(&d, DS_SND(kit, lane));
    ds_on_snd(s, &d, kit->crush, semi, note, vel);
}

/* one CTL block of control: the envelopes' targets at its end */
static HOT void ds_control(dsv_t *s)
{
    const dsnd_t *d = s->d;
    s->amp = s->amp_to;
    s->nz = s->nz_to;
    s->ck = s->ck_to;
    s->inc = s->inc_to;
    if (s->hold)
        s->hold--;
    else
        s->amp_to = decay_q16(s->amp, s->ka);           /* (dsp.c) */
    if ((d->src & DN_CLAP) && s->bursts < 4u && s->t >= s->bursts * 420u) {
        s->nz = (int32_t)d->nlev * 258;                 /* bursts at 0, 9.5, 19 ms; the 4th is the tail */
        if (++s->bursts == 4u) {
            s->kn = ds_k32(d->ndec);
            s->nhold = ds_blocks(d->nhold);
        }
    }
#if FELUCCA_DS_KAWA
    if (s->hits && !--s->gap) {                         /* a re-strike: a flam, a clap's next hand, a guiro's next ridge */
        s->hits--;
        s->gap = (uint8_t)((d->kgap ? d->kgap : 1u) + ((uint32_t)noise32(&s->rnd) >> 31));
        if (!s->hits) {                                 /* the last: the noise's own hold and decay */
            s->kn = ds_k32(d->ndec);
            s->nhold = ds_blocks(d->nhold);
        }
        s->ck = (int32_t)d->click * 258;
        if (d->src & 15u)
            s->nz = (int32_t)d->nlev * 258;
        if (d->kx & DK_HTONE) {
            s->amp = d->wave ? (int32_t)d->tlev * 258 : 0;
            s->pe = d->bend || d->fenv ? 32767 : 0;
            s->hold = ds_blocks(d->hold);
        }
    }
    s->rr = s->rr_to;
    if (s->kr)                                          /* RISE: what is left of it falls; the envelopes' share rises */
        s->rr_to = 32767 - decay_q16(32767 - s->rr, s->kr);
#endif
    if (s->nhold)
        s->nhold--;
    else
        s->nz_to = decay_q16(s->nz, s->kn);
    s->ck_to = (s->ck * 3) >> 3;                        /* ~1 ms: gone in a few blocks */
    s->pe = decay_q16(s->pe, s->kpe);
    s->inc_to = ds_inc(s->base16 + ((s->pe * (int32_t)d->bend * 16) >> 15));
    if ((s->fmode & 3u) && d->fenv)
        ds_filter(s);
}

AINL int ds_alive(const dsv_t *s)
{
#if FELUCCA_DS_KAWA
    if (s->hits)
        return 1;                                       /* re-strikes still to come */
#endif
    if ((s->d->src & DN_CLAP) && s->bursts < 4u)
        return 1;                                       /* bursts still to come */
    return s->hold || s->amp > CG_DTAIL || s->amp_to > CG_DTAIL || s->nz > CG_DTAIL || s->nz_to > CG_DTAIL ||
           s->ck > CG_DTAIL;                            /* (8; the CPU guard's quality level: 327, cpuguard.h) */
}

/* n (<= CTL) samples of voice s into out[] (Q15-ish, peak ~ 32767); returns 0 when it ended */
static HOT int ds_render(dsv_t *s, int32_t *out, uint32_t n)
{
    const dsnd_t *d = s->d;
    uint32_t i, src = d->src & 15u, wave = d->wave, fmode = s->fmode & 3u, fall = s->fmode & DF_ALL;
    /* (SLOOP 2.6, isod89/sloop-fm1 v2.6 d30d655 drum_synth.c ds_render, GPL-3.0-only: a whistle on the kicks) the levels
     * glide to their targets exactly, a0 + r (i + 1) / CTL. A step of r >> CTL_LOG2 a sample fell up to CTL - 1 short
     * and jumped the rest at every block: a click 1378 times a second under the kick's long sine (tones at 1.4 / 2.8
     * kHz). Here as a sum at CTL times the level (an add and a shift a sample, no multiply; a0 CTL is a multiple of CTL,
     * so the floor is 2.6's to the bit). The pitch keeps its step (a frequency, no jump) */
    int32_t ra = s->amp_to - s->amp, rn = s->nz_to - s->nz, rc = s->ck_to - s->ck;   /* (|r| < 2^17: no overflow) */
    int32_t sa = s->amp << CTL_LOG2, sz = s->nz << CTL_LOG2, sc = s->ck << CTL_LOG2;
    int32_t di = (int32_t)(s->inc_to - s->inc) >> CTL_LOG2;
    int32_t a = s->amp, z = s->nz, c = s->ck, inc = (int32_t)s->inc;
    int32_t drive = 16 + d->drive, bits = s->crush & 15, hold = (s->crush >> 4) + 1;
    int32_t t2l = (int32_t)d->t2lev * 258;
#if FELUCCA_DS_KAWA
    uint32_t t2m = d->t2 ? DK_T2MODE(d) : DK_MIX;       /* the partial: MIX (as before), RING, FM */
    int32_t rise = s->kr != 0u, rr = s->rr, sr = s->rr << CTL_LOG2, dr = s->rr_to - s->rr;
    if (rise)
        a = mulq15(a, rr), z = mulq15(z, rr);
#else
#define t2m DK_MIX
#endif
#if FELUCCA_DS_PLAITS
    int32_t px[CTL];
    if (wave >= DW_PKICK) {                             /* a Plaits model: its samples instead of the layers */
        int32_t pk = dp_render(&s->pl, wave, px, n);
        s->quiet = (uint8_t)(pk < DP_QUIET ? (s->quiet < 255u ? s->quiet + 1u : 255u) : 0u);
        wave = 0, src = 0, c = 0;                       /* (no tone, noise or click layer: its samples are the tone) */
    }
#endif
    for (i = 0; i < n; i++) {
        int32_t tone = 0, nz = 0, x;
        if (wave) {
            uint32_t ph = s->ph;
            if (t2m == DK_FM && d->t2) {                /* (KAWA) the partial modulates the tone's phase: its level the index */
                s->ph3 += ((uint32_t)inc >> 5) * d->t2;
                ph += (uint32_t)mulq15(sine_i(s->ph3), mulq15(mulq15(a, a), t2l)) << 17;
            }
            switch (wave) {
            case DW_SINE: tone = sine_i(ph); break;
            case DW_TRI: tone = osc_tri(ph); break;
            case DW_SQUARE: tone = (int32_t)ph < 0 ? -24000 : 24000; break;
            case DW_FM:                                 /* ratio 1.41: metallic, index follows the tone */
                tone = sine_i(ph + ((uint32_t)(sine_i(s->ph2) * a) << 1));
                s->ph2 += (uint32_t)inc + ((uint32_t)inc >> 2) + ((uint32_t)inc >> 3) + ((uint32_t)inc >> 5);
                break;
            default:                                    /* BELL: two squares */
                tone = ((int32_t)ph < 0 ? -12000 : 12000) + ((int32_t)s->ph2 < 0 ? -12000 : 12000);
                s->ph2 += (uint32_t)inc + ((uint32_t)inc >> 1) - ((uint32_t)inc >> 6);
                break;
            }
            s->ph += (uint32_t)inc;
            tone = mulq15(tone, a);
            if (d->t2 && t2m == DK_MIX) {               /* the second mode: a sine, decaying twice as fast */
                s->ph3 += ((uint32_t)inc >> 5) * d->t2;
                tone += mulq15(mulq15(sine_i(s->ph3), mulq15(a, a)), t2l);
            } else if (d->t2 && t2m == DK_RING) {       /* (KAWA) the partial rings the tone: its level the depth */
                s->ph3 += ((uint32_t)inc >> 5) * d->t2;
                tone += mulq15(mulq15(tone, sine_i(s->ph3)) - tone, t2l);
            }
        }
#if FELUCCA_DS_PLAITS
        else if (d->wave >= DW_PKICK)
            tone = px[i];
#endif
        if (src) {
            if (src == DN_WHITE) {
                nz = (int32_t)noise32(&s->rnd) >> 17;
            } else if (src == DN_CHIP) {
                s->cacc += s->cinc;
                if (s->cacc < s->cinc)                  /* wrapped: clock the LFSR (dsp_common.h) */
                    s->lfsr = lfsr15(s->lfsr);
                nz = (s->lfsr & 1u) ? 20000 : -20000;
            } else {                                    /* METAL / CYM: six squares (+ white) */
                s->mph[0] += s->minc[0];              /* unrolled: one sign bit each */
                s->mph[1] += s->minc[1];
                s->mph[2] += s->minc[2];
                s->mph[3] += s->minc[3];
                s->mph[4] += s->minc[4];
                s->mph[5] += s->minc[5];
                nz = (int32_t)(((s->mph[0] >> 31) + (s->mph[1] >> 31) + (s->mph[2] >> 31) +
                                (s->mph[3] >> 31) + (s->mph[4] >> 31) + (s->mph[5] >> 31)) * 9600u) - 28800;
                if (src == DN_CYM)
                    nz += (int32_t)noise32(&s->rnd) >> 18;
            }
            if (wave == DW_BELL)
                nz += tone, tone = 0;
            if (s->ahp) {                               /* two one-pole high-passes: 12 dB / oct */
                s->hp1 += mulq15(nz - s->hp1, s->ahp);
                nz -= s->hp1;
                s->hp2 += mulq15(nz - s->hp2, s->ahp);
                nz -= s->hp2;
            }
            nz = mulq15(nz, z);
            if (d->src & DN_CLAP)
                nz <<= 1;                               /* band-passed: as loud as the others */
        }
        if (c) {                                        /* the click: differentiated noise, bright */
            int32_t w = (int32_t)noise32(&s->rnd) >> 17;
            nz += mulq15(w - s->cprev, c);
            s->cprev = w;
        }
        x = fall ? tone + nz : nz;
        if (fmode) {                                    /* SVF (dsp.c tsvf_tick), with the BP / HP outs */
            int32_t v1, v2 = tsvf_tick(&s->c, x, &s->f1, &s->f2, &v1);
            x = fmode == DF_LP ? v2 : fmode == DF_BP ? v1 : x - ((s->fk * v1) >> 12) - v2;
        }
        x += fall ? 0 : tone;
        if (d->drive)
            x = softclip((x * drive) >> 4);
        if (s->crush) {
            if (--s->holdn <= 0) {
                s->holdn = hold;
                s->held = (x >> bits) << bits;
            }
            x = s->held;
        }
        out[i] = ((mulq15(x, s->gain) >> 2) * s->lg) >> 8;
        a = (sa += ra) >> CTL_LOG2;
        z = (sz += rn) >> CTL_LOG2;
        c = (sc += rc) >> CTL_LOG2;
        inc += di;
#if FELUCCA_DS_KAWA
        if (rise) {                                     /* RISE: the tone and the noise swell in */
            rr = (sr += dr) >> CTL_LOG2;
            a = mulq15(a, rr), z = mulq15(z, rr);
        }
#endif
    }
#ifdef t2m
#undef t2m
#endif
    s->t += n;
    ds_control(s);
#if FELUCCA_DS_PLAITS
    if (d->wave >= DW_PKICK)                            /* a Plaits model: quiet for 46 ms (after 50 ms), or 6 s */
        return !(s->t >= DP_LIFE || (s->t > FS / 20u && s->quiet >= DP_QBLOCKS));
#endif
    return ds_alive(s);
}
