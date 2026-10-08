/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAMPLE: IMA ADPCM sample sets. */
static const char *const N_ONOFF_E[] = {"OFF", "ON"};
/* IMA ADPCM sample sets (tools/gen_samples.py), native rates, zones across
 * the keyboard, loops with the ADPCM state stored at the loop start.
 * SET is P_E0 (EDIT 1, KNOB 1). */
typedef struct {
    uint32_t off, n, ls, le;     /* byte offset, samples, loop start / end (sample index) */
    uint32_t rate;               /* source rate / 44100, Q16 */
    int16_t root16;              /* root pitch in 1/16 semitone */
    int16_t pred;                /* ADPCM state at the loop start */
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
typedef struct {
    const char *name;
    uint16_t z0, nz;
} smp_set_t;
#include "felucca_samples.h"

/* (IMA_STEP, IMA_IDX, ima_nibble, lerp16, smp_lp_k: dsp.c) */
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) */
AINL uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = PITCH_INC[1600 + u % 192u] / (PITCH_INC[1600] >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}

/* ---- user sample slots (loaded from the web editor into flash, see web/EDITOR_PROTOCOL.md)
 * 3 slots of 80 KiB at flash 0xA0000.. (Felucca data region), read through the plain XIP
 * window. Slot = header (magic, count, name, data length, CRC32) + up to 16 zones in the
 * smp_zone_t layout (off relative to the slot's data at +512) + IMA ADPCM data. */
#include "../hal/fm1_xip.h"   /* relative: hostsim includes this file too */
#define SMP_USER_SLOTS 3
#define SMP_USER_BASE 0xA0000u
#define SMP_USER_SIZE 0x14000u
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u                  /* "FSMP" */
/* what slot k may hold: USR3 gives its last 16 KiB to the banks (in every build: the flash map does not
 * depend on the build): 0xD8000..0xD9FFF the FM6 user bank (fm6_store.c), 0xDA000..0xDBFFF the user drum
 * kit bank (storage.c ST_UKIT_SECTOR). USR3 holds 64 KiB; a longer USR3 written before reads as empty. With
 * FELUCCA_SNAPSHOTS the snapshot area (SN_SECTORS x 4 KiB, snap_store.c) ends USR3 below the banks: USR3 holds
 * that much less (32 KiB with 4 snapshots) */
#define SMP_BANKS 0xD8000u
#define SMP_USR3_END (SMP_BANKS - (SN_SECTORS + NB_SECTORS) * 0x1000u)   /* (and the CZ collection: cz_bank.c) */
#define SMP_USER_CAP(k) ((k) == 2u ? SMP_USR3_END - (SMP_USER_BASE + 2u * SMP_USER_SIZE) : SMP_USER_SIZE)
#define SMP_NALL (SMP_NSETS + SMP_USER_SLOTS)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;                                   /* 32 + 16 x 28 = 480 B, data at +512 */
static smp_zone_t usr_zone[SMP_USER_SLOTS][16];     /* RAM copy, off rebased onto SMP_DATA */
static uint8_t usr_nz[SMP_USER_SLOTS];
static const char *const SMP_ALL_NAMES[SMP_NALL] = {SMP_SET_NAMES_INIT, "USR1", "USR2", "USR3"};

#ifndef SMP_USER_XIP                                /* host tests: a RAM image of the slots */
#define SMP_USER_XIP(k) fm1_xip_ptr(SMP_USER_BASE + (k) * SMP_USER_SIZE)
#endif
static const uint8_t *smp_user_xip(uint32_t k) { return SMP_USER_XIP(k); }
#if FELUCCA_SLICE
static void slc_user_scan(uint32_t k, int valid);   /* eng_slice.c: SLICE's slice table of the slot */
#else
#define slc_user_scan(k, valid) ((void)0)
#endif
static uint32_t smp_user_gen;                       /* + 1 per slot scan (GRAIN: its seek index follows uploads) */

/* (re)read slot k from flash: valid header -> zones usable; call after boot and after an upload
 * (main loop: SLICE scans the slot's audio here) */
/* a zone of a slot header usable (inside the slot's data, well formed) */
static int smp_zone_ok(const smp_zone_t *z, uint32_t data_len)
{
    return z->off <= data_len && z->n <= 2u * SMP_USER_SIZE && z->off + (z->n + 1u) / 2u <= data_len &&
           z->idx <= 88u && z->rate && z->rate <= 4u << 16 && z->ls <= z->le && z->le < z->n && z->lo <= z->hi;
}
static void smp_user_scan(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    static smp_zone_t zs[16];
    uint32_t i, base;
    smp_user_gen++;
    usr_nz[k] = 0;                                  /* (from here no new voice takes the slot) */
    slc_user_scan(k, 0);
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_CAP(k) - SMP_USER_DATA)
        return;
    base = (uint32_t)(uintptr_t)(smp_user_xip(k) + SMP_USER_DATA) - (uint32_t)(uintptr_t)SMP_DATA;
    for (i = 0; i < h->nz; i++) {                   /* checked in a copy: a voice still playing the slot */
        zs[i] = h->zone[i];                         /* never reads a zone that is not valid */
        if (!smp_zone_ok(&zs[i], h->data_len))
            return;                                 /* zone outside the data or malformed: slot unusable */
        zs[i].off += base;
    }
    fm1_irq_off();
    for (i = 0; i < h->nz; i++)
        usr_zone[k][i] = zs[i];
    usr_nz[k] = h->nz;
    fm1_irq_on();
    slc_user_scan(k, 1);
}

/* zone index in a voice: < 0x8000 built-in, else 0x8000 | slot << 5 | zone */
AINL const smp_zone_t *smp_zone(uint32_t zi)
{
    return zi < 0x8000u ? &SMP_ZONES[zi] : &usr_zone[(zi >> 5) & 3u][zi & 31u];
}

/* voice: ph[0] position (samples), ph[1] fraction Q16, s[0] predictor, s[1] step
 * index, s[2] previous sample, s[3] current sample, s[4] zone, s[5] step Q16 */
static inline HOT int32_t sample_next(const smp_zone_t *z, voice_t *v, int loop)
{
    uint32_t pos = v->ph[0], b = SMP_DATA[z->off + (pos >> 1)];
    uint32_t code = (pos & 1u) ? (b >> 4) : (b & 15u);
    ima_nibble(code, IMA_STEP[(uint32_t)v->s[1] <= 88u ? v->s[1] : 88], &v->s[0], &v->s[1]);   /* (dsp.c) */
    pos++;
    if (pos > z->le && z->looped && loop) {
        pos = z->ls;
        v->s[0] = z->pred;
        v->s[1] = z->idx;
    }
    v->ph[0] = pos;
    return v->s[0];
}

static void sample_note_on(track_t *t, voice_t *v)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu;
    if (si < SMP_NSETS) {
        const smp_set_t *set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (v->note >= SMP_ZONES[set->z0 + i].lo && v->note <= SMP_ZONES[set->z0 + i].hi)
                zi = set->z0 + i;
        v->s[4] = (int32_t)(zi == 0xFFFFu ? set->z0 : zi);
    } else {                                        /* user slot: silent if empty */
        uint32_t k = si - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++)
            if (v->note >= usr_zone[k][i].lo && v->note <= usr_zone[k][i].hi)
                zi = 0x8000u | k << 5 | i;
        v->s[4] = (int32_t)(zi == 0xFFFFu ? 0 : zi);
    }
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[6] = zi == 0xFFFFu;     /* 1 = sample ended (one-shot); a kit key with no sound stays silent */
    v->s[7] = 0;                 /* lo-pass state */
}

static HOT void sample_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = smp_zone((uint32_t)v->s[4]);
    uint32_t i, frac = v->ph[1];
    int32_t d16 = clamp(m->pitch16 + p[P_E1] * 16 - z->root16, -1536, 576);   /* <= 3 octaves up: bounded decode load */
    uint32_t r = fine_inc(pow2_q16(d16), m->fine), stepq = (r >> 8) * (z->rate >> 8);   /* Q16 samples per output */
    int32_t bits = p[P_E2], lp = smp_lp_k((p[P_E4] << 8) + m->cutoff);
    int32_t drv = p[P_E6], sh = bits / 10;
    if (v->s[6] || !z->n) {
        v->active = 0;
        return;
    }
    if (v->ph[0] == 0 && v->ph[1] == 0 && v->s[3] == 0)
        v->s[3] = sample_next(z, v, p[P_E3]);         /* prime the interpolator */
    for (i = 0; i < n; i++) {
        int32_t s;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[2] = v->s[3];
            if (v->ph[0] >= z->n) {                   /* one-shot reached its end */
                v->s[6] = 1;
                v->s[3] = 0;
                break;
            }
            v->s[3] = sample_next(z, v, p[P_E3]);
        }
        s = lerp16(v->s[2], v->s[3], frac);
        if (sh)                                       /* BITS: 0 = clean, up to 12 bits removed */
            s = (s >> sh) << sh;
        if (drv)
            s = softclip(s + (((s >> 2) * (drv * 150)) >> 11));   /* = s * drv * 600 / 32768, no overflow */
        v->s[7] += mulq15(s - v->s[7], lp);           /* gentle tone control (CUT) */
        out[i] += mulq15(mulq15(v->s[7], amp_at(m, i)), VOICE_FS) << 1;
        if (v->s[6])
            break;
    }
    v->ph[1] = frac;
}


static const engine_t ENG_SAMPLE = {
    "SAMPLE", {"SET", "TONE"},
    {
        {"SET", F_ENUM, 0, SMP_NALL - 1, 0, SMP_ALL_NAMES, 0},
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"BITS", F_INT, 0, 127, 0, 0, 0},
        {"LOOP", F_ENUM, 0, 1, 1, N_ONOFF_E, 0},
        {"CUT", F_INT, 0, 127, 127, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    SMP_PRESET_TABLE, sizeof(SMP_PRESET_TABLE) / sizeof(SMP_PRESET_TABLE[0]), -1, sample_note_on, sample_render,
    0xFFFF, {P_E0, P_E4, P_ATK, P_REL},
};
