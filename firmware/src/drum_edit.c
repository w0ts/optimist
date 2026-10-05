/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' own sounds (included by drums.c, before drum_on). Three build switches (core.h), each
 * usable alone:
 *   FELUCCA_DRUM_EDIT  the drum sound editor: 8 offsets per lane over the kit's sound, applied at the hit.
 *                      Synthesised kits: TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL on drum_synth.c's
 *                      dsnd_t (de_synth); sampled sounds (the GM kit, user samples): TUNE DECAY CUT LEVEL
 *   FELUCCA_DRUM_USR   a lane plays a user sample (USR1..USR3, eng_sample.c) instead of the kit: one hit
 *                      (zone) of the slot, from a start to a length (1/1024 of the hit each), so one
 *                      uploaded file holds many hits (the editor's CHOP makes one zone per hit)
 *   FELUCCA_DRUM_KITS  a lane plays another kit's sound for it; user kits (drum_kits.c) store all of it
 * Every build keeps the lanes in the project (format FUN8, project.c): a build without a switch keeps the
 * values and plays the kit as it is. All zero = the kit as it is, sample for sample. */
enum { DE_TUNE, DE_DECAY, DE_SNAP, DE_CLICK, DE_BEND, DE_CUT, DE_DRIVE, DE_LEVEL, DE_N };
#define DL_KIT 0u                /* src: the project's kit */
#define DL_USR 1u                /* src 1..3: USR1..USR3 */
#define DL_KIT0 16u              /* src 16 + k: kit k's sound for this lane */
#define DL_ANY (FELUCCA_DRUM_EDIT || FELUCCA_DRUM_USR || FELUCCA_DRUM_KITS)
typedef struct {                 /* a project's drum lanes (204 bytes; user kits keep the same per lane) */
    int8_t ofs[DRUM_LANES][DE_N];          /* offsets from the kit's sound (DE_*), 0 = as the kit */
    uint8_t src[DRUM_LANES];               /* DL_KIT, DL_USR + k, DL_KIT0 + kit */
    uint8_t ref[DRUM_LANES][3];            /* user sample: hit (zone) 4 bits, start 10 bits, length 10 bits */
    uint8_t ukit;                          /* the user kit these lanes came from (1..16), 0 = none */
    char name[8];                          /* its name (the project shows it without the bank) */
    uint8_t rsv[3];
} dlanes_t;
_Static_assert(sizeof(dlanes_t) == 204u, "drum lanes: 204 bytes in the project");
static dlanes_t dl;                         /* the working project's lanes */
static int16_t dl_e0 = -1;                  /* the kit (P_E0) they were set for: another kit drops a user kit */

/* the reference: hit z, start / length in 1/1024 of the hit (length 0 = 1024: to its end) */
static uint32_t dl_hit(const uint8_t *r) { return r[0] >> 4; }
static uint32_t dl_start(const uint8_t *r) { return (uint32_t)(r[0] & 15u) << 6 | r[1] >> 2; }
static uint32_t dl_len(const uint8_t *r) { uint32_t n = (uint32_t)(r[1] & 3u) << 8 | r[2]; return n ? n : 1024u; }
static void dl_set_ref(uint8_t *r, uint32_t z, uint32_t st, uint32_t len)
{
    st = st > 1023u ? 1023u : st;
    len = len >= 1024u || !len ? 0u : len;
    r[0] = (uint8_t)((z & 15u) << 4 | st >> 6);
    r[1] = (uint8_t)((st & 63u) << 2 | len >> 8);
    r[2] = (uint8_t)len;
}

/* the kit lane l plays (DL_KIT0 + k: kit k; else the project's) */
static uint32_t dl_kit_of(uint32_t l, uint32_t kit)
{
#if FELUCCA_DRUM_KITS
    if (l < DRUM_LANES && dl.src[l] >= DL_KIT0 && dl.src[l] - DL_KIT0 < DRUM_KITS)
        return dl.src[l] - DL_KIT0;
#endif
    (void)l;
    return kit;
}
/* the user slot + 1 lane l plays, 0 = none */
static uint32_t dl_usr_of(uint32_t l)
{
#if FELUCCA_DRUM_USR
    if (l < DRUM_LANES && dl.src[l] >= DL_USR && dl.src[l] < DL_USR + SMP_USER_SLOTS)
        return dl.src[l];
#endif
    (void)l;
    return 0;
}
/* lane l's offsets, 0 = none (or no editor in this build) */
static const int8_t *dl_ofs(uint32_t l)
{
#if FELUCCA_DRUM_EDIT
    uint32_t i;
    if (l < DRUM_LANES)
        for (i = 0; i < DE_N; i++)
            if (dl.ofs[l][i])
                return dl.ofs[l];
#endif
    (void)l;
    return 0;
}
static int32_t dl_tune(uint32_t l) { const int8_t *x = dl_ofs(l); return x ? x[DE_TUNE] : 0; }

/* every value back inside its range (a project, the editor, a user kit) */
static const int8_t DE_MIN[DE_N] = {-24, -64, -64, -64, -24, -64, -64, -24};
static const int8_t DE_MAX[DE_N] = {24, 63, 63, 63, 24, 63, 63, 6};
static void dl_fix(dlanes_t *d)
{
    uint32_t l, i;
    for (l = 0; l < DRUM_LANES; l++) {
        for (i = 0; i < DE_N; i++)
            d->ofs[l][i] = (int8_t)clamp(d->ofs[l][i], DE_MIN[i], DE_MAX[i]);
        if (d->src[l] > DL_USR + SMP_USER_SLOTS - 1u && d->src[l] < DL_KIT0)
            d->src[l] = DL_KIT;                     /* (a kit not in this build is kept: dl_kit_of skips it) */
    }
    d->ukit = d->ukit > 16u ? 0u : d->ukit;
}

/* ---- synthesised sounds: the offsets on a copy of the kit's dsnd_t */
#if FELUCCA_DRUM_EDIT
static void de_synth(dsnd_t *o, const dsnd_t *d, const int8_t *x)
{
    int32_t t = x[DE_TUNE], dc = x[DE_DECAY], sn = x[DE_SNAP], c = x[DE_CUT];
    *o = *d;
    o->pitch = (uint8_t)clamp(d->pitch + t, 0, 127);           /* TUNE: the body (and the metal, the chip) */
    if (d->chip)
        o->chip = (uint8_t)clamp(d->chip + t, 1, 127);
    o->decay = (uint8_t)clamp(d->decay + dc, 0, 127);          /* DECAY: tone and noise, 5.4 % a step */
    o->ndec = (uint8_t)clamp(d->ndec + dc, 0, 127);
    if (dc < 0) {                                              /* (shorter: the holds too) */
        o->hold = (uint8_t)(d->hold * (64 + dc) / 64);
        o->nhold = (uint8_t)(d->nhold * (64 + dc) / 64);
    }
    if (sn > 0) {                                              /* SNAP: noise up, the tone down to half */
        o->tlev = (uint8_t)(d->tlev * (128 - sn) / 128);
        if (d->src & 15u) {
            o->nlev = (uint8_t)(d->nlev + (127 - d->nlev) * sn / 64);
        } else {                                               /* (no noise in it: a white burst) */
            o->src = DN_WHITE;
            o->nlev = (uint8_t)(sn * 2);
            o->ndec = (uint8_t)(d->decay < 50u ? d->decay : 50u);
            o->nhold = 0;
        }
    } else if (sn < 0) {                                       /* tone up, noise down */
        o->nlev = (uint8_t)(d->nlev * (64 + sn) / 64);
        if (d->wave)
            o->tlev = (uint8_t)(d->tlev + (127 - d->tlev) * -sn / 64);
    }
    o->click = (uint8_t)clamp(d->click + 2 * x[DE_CLICK], 0, 127);
    o->bend = (uint8_t)clamp(d->bend + x[DE_BEND], 0, 96);    /* BEND: the pitch drop, semitones */
    if (!d->bend && o->bend && o->btime < 40u)
        o->btime = 40;                                         /* (a new drop: ~40 ms, not a click) */
    if (c > 0 && (d->flt & 3u))                                /* CUT +: its own filter opens (- : dl_smp_fx) */
        o->fcut = (uint8_t)clamp(d->fcut + c, 0, 127);
    o->drive = (uint8_t)clamp(d->drive + 2 * x[DE_DRIVE], 0, 127);
    o->level = (uint8_t)clamp(d->level + 4 * x[DE_LEVEL], 0, 255);   /* LEVEL: dB (quarter dB units) */
}
#endif

/* ---- per drum voice: the edited synth sound; a sampled voice's DECAY, CUT, LEVEL */
static struct {
#if FELUCCA_DRUM_EDIT
    dsnd_t d[NDRUM];
#endif
    uint32_t dec[NDRUM];         /* DECAY: per-sample factor Q16 of the envelope, 0 = none */
    int32_t cut[NDRUM];          /* CUT: one-pole low-pass coefficient Q15, 0 = none */
    int32_t lg[NDRUM];           /* LEVEL: gain Q12, 0 = none */
    int32_t env[NDRUM], flt[NDRUM];
    uint8_t on[NDRUM];           /* any of them */
} dv;

static void dl_smp_fx(uint32_t vi, uint32_t l)
{
    const int8_t *x = dl_ofs(l);
    dv.on[vi] = 0;
    if (!x)
        return;
    dv.dec[vi] = x[DE_DECAY] < 0 ? DECAY_K[clamp(127 + 2 * x[DE_DECAY], 0, 127)] : 0u;
    dv.cut[vi] = x[DE_CUT] < 0 ? ds_onepole((uint32_t)clamp(127 + 2 * x[DE_CUT], 20, 127)) : 0;   /* >= ~80 Hz */
    dv.lg[vi] = x[DE_LEVEL] ? (int32_t)(pow2_q16(x[DE_LEVEL] * 32) >> 4) : 0;   /* 2^(dB / 6.02) */
    dv.env[vi] = 32767;
    dv.flt[vi] = 0;
    dv.on[vi] = (uint8_t)(dv.dec[vi] || dv.cut[vi] || dv.lg[vi]);
}
static void dl_ds_on(dsv_t *s, uint32_t vi, const dkit_t *k, uint32_t note, uint32_t vel, uint32_t l)
{
#if FELUCCA_DRUM_EDIT
    const int8_t *x = dl_ofs(l);
    if (x) {
        int32_t semi;
        de_synth(&dv.d[vi], &k->s[ds_lane(note, &semi)], x);
        ds_on_snd(s, &dv.d[vi], k->crush, semi, note, vel);
        dl_smp_fx(vi, l);                                      /* (CUT -: a low-pass on its output) */
        dv.dec[vi] = 0;
        dv.lg[vi] = 0;
        dv.on[vi] = (uint8_t)(dv.cut[vi] != 0);
        return;
    }
#endif
    dv.on[vi] = 0;
    (void)l;
    ds_on(s, k, note, vel);
}

/* a sampled voice's sample through them (drums_mix); its envelope at 0 ends it */
AINL int32_t dl_smp_apply(uint32_t k, int32_t s, voice_t *v)
{
    if (dv.cut[k]) {
        dv.flt[k] += mulq15(s - dv.flt[k], dv.cut[k]);
        s = dv.flt[k];
    }
    if (dv.dec[k]) {
        s = mulq15(s, dv.env[k]);
        dv.env[k] = (int32_t)(((uint32_t)dv.env[k] * dv.dec[k]) >> 16);
        if (!dv.env[k])
            v->active = 0;
    }
    if (dv.lg[k])
        s = (s * dv.lg[k]) >> 12;
    return s;
}

/* ---- user samples on a lane: the ADPCM state where a hit starts (decoded in the main loop, dl_tick) */
#if FELUCCA_DRUM_USR
static struct {
    uint32_t key, pos;           /* key: slot, hit, start and the slots' generation it was figured for */
    int16_t pred;
    uint8_t idx;
} dl_seek[DRUM_LANES];
static uint32_t dl_seek_key(uint32_t l)
{
    return (dl.src[l] << 24 | (uint32_t)dl.ref[l][0] << 8 | (dl.ref[l][1] & 0xFCu)) ^ (smp_user_gen * 2654435761u);
}
/* where lane l's hit starts and ends (samples of zone *z), 0 = no hit there */
static const smp_zone_t *dl_usr_zone(uint32_t l, uint32_t usr, uint32_t *pos, uint32_t *end)
{
    uint32_t k = usr - 1u, z = dl_hit(dl.ref[l]), n;
    const smp_zone_t *zp;
    if (z >= usr_nz[k])
        return 0;
    zp = &usr_zone[k][z];
    n = zp->n;
    *pos = (n * dl_start(dl.ref[l]) >> 10) & ~1u;     /* (n < 2^18: no overflow; a whole byte: two samples) */
    *end = *pos + (n * dl_len(dl.ref[l]) >> 10);
    if (*end > n)
        *end = n;
    return zp;
}
static void dl_tick(void)                       /* main loop: the start states of the lanes on user samples */
{
    uint32_t l, usr, pos, end, i;
    for (l = 0; l < DRUM_LANES; l++) {
        const smp_zone_t *zp;
        voice_t t;
        uint32_t key;
        if (!(usr = dl_usr_of(l)) || dl_seek[l].key == (key = dl_seek_key(l)))
            continue;
        if (!(zp = dl_usr_zone(l, usr, &pos, &end)))
            pos = 0;
        memset(&t, 0, sizeof t);
        for (i = 0; i < pos; i++)
            sample_next(zp, &t, 0);
        fm1_irq_off();
        dl_seek[l].pos = pos;
        dl_seek[l].pred = (int16_t)t.s[0];
        dl_seek[l].idx = (uint8_t)t.s[1];
        dl_seek[l].key = key;
        fm1_irq_on();
    }
}

/* lane l on user slot usr - 1: a hit of it from its start (stopped there: the state dl_tick decoded) */
static void dl_usr_hit(uint32_t l, uint32_t usr, uint32_t note, uint32_t vel)
{
    uint32_t pos, end, vi;
    const smp_zone_t *zp = dl_usr_zone(l, usr, &pos, &end);
    voice_t *v;
    if (!zp || pos >= end)
        return;
    v = drum_voice(note, vel);
    vi = (uint32_t)(v - drums.v);
    v->s[4] = (int32_t)(0x8000u | (usr - 1u) << 5 | dl_hit(dl.ref[l]));
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    if (pos && dl_seek[l].key == dl_seek_key(l)) {   /* (not decoded yet: from the hit's start) */
        v->ph[0] = dl_seek[l].pos;
        v->s[0] = dl_seek[l].pred;
        v->s[1] = dl_seek[l].idx;
    }
    v->ph[2] = end;
    v->s[3] = FAR(sample_next)(zp, v, 0);
    v->s[5] = (int32_t)((pow2_q16(dl_tune(l) * 16) >> 8) * (zp->rate >> 8));   /* at its own pitch */
    drums.kit[vi] = 0;
    drums.synth[vi] = 0;
    drums.filter[vi] = 0;
    drums.env[vi] = 32767;
    dl_smp_fx(vi, l);
}
#else
#define dl_tick() ((void)0)
#endif
