/* SPDX-License-Identifier: GPL-3.0-only */
/* Card samples read in any order (docs/SD-CARD-DESIGN.md 7, "Random access"; the user, 2026-10-29: GRAIN and STRETCH
 * play SD card mode's sets from the card): the audio's side, included at the end of sd_stream.h. GRAIN jumps through a
 * zone (a grain's start, its spread, reversed grains), STRETCH's window moves slowly forward and wraps: a linear stream
 * cannot follow either, so each part playing a card set that way has a CACHE of nk slots of one 512-byte card
 * sector each, any sector of any of the part's sources in any slot (a TAG a slot: source << 24 | the file's sector + 1,
 * 0 empty), filled by the main loop's runs (sd_stream_rc.c) in the order the audio ASKS:
 *   - WANTS (wlo .. whi, tags of one source, by wdue on the audio's clock `now`, in samples): what the engine will read
 *     soon. GRAIN knows its next grains some ms early (eng_grain.c gr_plan: their random numbers are the part's
 *     generator's next ones), STRETCH its next window a frame (46 ms) early. A want past its due by SST_RC_STALE is no
 *     longer served.
 *   - PINS (plo .. phi): what a sounding grain or STRETCH's last window reads; the main loop never takes their slots.
 *     The audio pins first, then looks at the tags; the main loop clears a slot's tag first, then looks at the pins
 *     (and puts the tag back when it is pinned): either way a slot read is never written.
 *   - the REGION (rlo .. rhi): where the part's next grains can start at all (POS and SPRD), filled from slots no one
 *     used for SST_RC_COLD samples while the bus has nothing else to do.
 * The audio reads through a WINDOW (sst_rw_t: one slot's data, or the head) as sst_at does: one compare a sample; a
 * sector not there (never for a pinned range) reads as 0 (the reader then fades: the engine's). The slots' tags and
 * their last use (LRU: the main loop's victims) are in the arena block with the data; the rest here. The readers are
 * GRAIN's and STRETCH's code's neighbours (not HOT: their callers are not in the code RAM; a call from flash to it is
 * out of the pi32v2's jump range). */
#ifndef SD_STREAM_RC_H
#define SD_STREAM_RC_H

#ifndef SST_RAND_KB
#ifdef FELUCCA_SD_RAND_KB
#define SST_RAND_KB ((uint32_t)FELUCCA_SD_RAND_KB)
#else
/* a GRAIN part's cache, KB: 8 of the builder's SD_RING, 16 to 32 KB (measured, tests/sd_grain_test.c: the factory
 * presets on PIANO / VIBES / FLUTE, a note's grains still sounding in its release while the next note's start in
 * another zone, need 32 KB to sound as the flash's; 24 misses some, 16 many) */
#define SST_RAND_KB (8u * (uint32_t)FELUCCA_SD_RING_KB < 16u ? 16u : 8u * (uint32_t)FELUCCA_SD_RING_KB > 32u ? 32u \
                     : 8u * (uint32_t)FELUCCA_SD_RING_KB)
#endif
#endif
#define SST_STRETCH_KB 16u                           /* STRETCH's: its window (8 KB), the last one's and the next one's */
#define SST_RK 64u                                   /* a cache's slots at most (32 KB) */
#define SST_RC_BYTES(kb) ((uint32_t)(kb) * 2u * (512u + 8u))   /* a cache in the arena block: slots, their tags and uses */
#define SST_RW 20u                                   /* wants a cache (GRAIN: one a grain, GR_NG 12, then its plan's) */
#define SST_RP 16u                                   /* pins a cache (GRAIN: one a grain, GR_NG 12; STRETCH: 4) */
#define SST_RC_STALE (20u * 441u / 10u)              /* (samples: 20 ms) */
#define SST_RC_COLD (300u * 441u / 10u)              /* (samples: 300 ms) */
_Static_assert(SST_RAND_KB >= 16u && SST_RAND_KB * 2u <= SST_RK, "sd_stream_rc.h: a cache of 16 to 32 KB");
_Static_assert(SST_NSRC <= 16u, "sd_stream_rc.h: a tag's source is 4 bits");
#define SST_FENCE() __asm__ volatile("" ::: "memory")

typedef struct {                                     /* a reader's window: the data bytes wlo .. wlo + wn at win */
    const uint8_t *win;
    uint32_t wlo, wn;
} sst_rw_t;

typedef struct {
    uint8_t *mem;                                    /* nk x 512 B (the arena block; 0: no cache) */
    volatile uint32_t *tag;                          /* nk: source << 24 | file sector + 1, 0 empty (main loop) */
    volatile uint32_t *use;                          /* nk: `now` when last read or filled */
    volatile uint32_t plo[SST_RP], phi[SST_RP];      /* the audio's pins (plo 0: none) */
    volatile uint32_t wlo[SST_RW], whi[SST_RW], wdue[SST_RW];   /* the audio's wants (wlo 0: none) */
    volatile uint32_t rlo, rhi;                      /* the audio's region (rlo 0: none) */
    volatile uint32_t now;                           /* the audio's clock: + CTL a block of the engine's */
    volatile uint32_t hit, late, miss;               /* reads started at once, late (waited for), given up */
    volatile uint32_t gen;                           /* + 1 a placement (the engine's pins and wants start again) */
    volatile uint8_t on;                             /* 1: placed, its tags clear (the main loop) */
    uint8_t hint;                                    /* (the audio's: the slot found last) */
    uint8_t nk;                                      /* its slots (<= SST_RK) */
    uint8_t cold;                                    /* (the main loop's: a region run's slots came from cold ones) */
} sst_rc_t;
static sst_rc_t sst_rc[SST_NPART] IN_POOL_AUDIO;    /* each part's (a part on GRAIN or STRETCH with a card set) */

AINL uint32_t sst_rc_tag(uint32_t si, uint32_t sec) { return si << 24 | (sec + 1u); }
AINL uint32_t sst_rc_sec(const sst_src_t *s, uint32_t f) { return (s->dofs + (f << s->fsh)) >> 9; }
AINL uint32_t sst_rc_esec(const sst_src_t *s, uint32_t f) { return (s->dofs + ((f + 1u) << s->fsh) - 1u) >> 9; }
AINL uint32_t sst_rc_si(uint32_t t) { return t >> 24; }          /* a tag's source */
AINL uint32_t sst_rc_s(uint32_t t) { return (t & 0xFFFFFFu) - 1u; }   /* a tag's file sector */
AINL int sst_rc_live(const sst_rc_t *c) { return c->on && c->mem; }

/* the slot holding tag t, -1 none (the hint, its next slot, then all) */
static int32_t sst_rc_find(sst_rc_t *c, uint32_t t)
{
    uint32_t h = c->hint, k, nk = c->nk;
    if (h >= nk)
        h = 0;                                       /* (no divide: the audio's) */
    if (c->tag[h] == t)
        return (int32_t)h;
    h = h + 1u < nk ? h + 1u : 0u;
    if (c->tag[h] == t) {
        c->hint = (uint8_t)h;
        return (int32_t)h;
    }
    for (k = 0; k < nk; k++)
        if (c->tag[k] == t) {
            c->hint = (uint8_t)k;
            return (int32_t)k;
        }
    return -1;
}

/* the sectors *cur .. end of source si in the cache (or whole in its head) from the cursor on: the cursor moved past
 * those found (a pinned range's stay: a reader waiting for its range looks at each sector once); 1 all of them */
static int sst_rc_scan(sst_rc_t *c, const sst_src_t *s, uint32_t si, uint32_t *cur, uint32_t end)
{
    if (!sst_rc_live(c))
        return 0;
    for (; *cur <= end; (*cur)++)
        if (((*cur + 1u) << 9) > s->dofs + s->hlen && sst_rc_find(c, sst_rc_tag(si, *cur)) < 0)
            return 0;
    return 1;
}

/* pin k: frames f0 .. f1 of source si (before looking at their tags); 0 frames: off */
AINL void sst_rc_pin(sst_rc_t *c, uint32_t k, const sst_src_t *s, uint32_t si, uint32_t f0, uint32_t f1)
{
    if (k >= SST_RP)
        return;
    c->plo[k] = 0;
    SST_FENCE();
    c->phi[k] = sst_rc_tag(si, sst_rc_esec(s, f1));
    SST_FENCE();
    c->plo[k] = sst_rc_tag(si, sst_rc_sec(s, f0));   /* (last: the main loop reads plo first) */
    SST_FENCE();
}
AINL void sst_rc_unpin(sst_rc_t *c, uint32_t k)
{
    if (k < SST_RP)
        c->plo[k] = 0;
}

/* want k: frames f0 .. f1 of source si by the audio's clock `due` (0 frames: off) */
AINL void sst_rc_want(sst_rc_t *c, uint32_t k, const sst_src_t *s, uint32_t si, uint32_t f0, uint32_t f1, uint32_t due)
{
    if (k >= SST_RW)
        return;
    c->wlo[k] = 0;
    SST_FENCE();
    c->whi[k] = sst_rc_tag(si, sst_rc_esec(s, f1));
    c->wdue[k] = due;
    SST_FENCE();
    c->wlo[k] = sst_rc_tag(si, sst_rc_sec(s, f0));
}
AINL void sst_rc_unwant(sst_rc_t *c, uint32_t k)
{
    if (k < SST_RW)
        c->wlo[k] = 0;
}
AINL void sst_rc_region(sst_rc_t *c, const sst_src_t *s, uint32_t si, uint32_t f0, uint32_t f1)
{
    c->rlo = 0;
    SST_FENCE();
    c->rhi = sst_rc_tag(si, sst_rc_esec(s, f1));
    SST_FENCE();
    c->rlo = sst_rc_tag(si, sst_rc_sec(s, f0));
}
static void sst_rc_reset(sst_rc_t *c)               /* the audio, a new owner: its pins, wants and region off */
{
    uint32_t k;
    for (k = 0; k < SST_RP; k++)
        c->plo[k] = 0;
    for (k = 0; k < SST_RW; k++)
        c->wlo[k] = 0;
    c->rlo = 0;
}

/* data byte off of source si through window w (as sst_at): 0 = not in the cache */
static const uint8_t *sst_rc_slow(sst_rc_t *c, const sst_src_t *s, uint32_t si, sst_rw_t *w, uint32_t off)
{
    uint32_t sec, lo, hi;
    int32_t k;
    if (off < s->hlen) {                             /* the head */
        w->win = s->head;
        w->wlo = 0;
        w->wn = s->hlen;
        return s->head + off;
    }
    if (off >= s->dlen || !sst_rc_live(c))
        return 0;
    sec = (s->dofs + off) >> 9;
    k = sst_rc_find(c, sst_rc_tag(si, sec));
    if (k < 0) {
        w->wn = 0;
        return 0;
    }
    c->use[k] = c->now;
    lo = sec << 9;
    hi = lo + 512u;
    lo = lo > s->dofs ? lo - s->dofs : 0;            /* (the data's first sector holds the header's end too) */
    hi = hi - s->dofs;
    if (hi > s->dlen)
        hi = s->dlen;
    w->win = c->mem + (uint32_t)k * 512u + ((s->dofs + lo) & 511u);
    w->wlo = lo;
    w->wn = hi - lo;
    return w->win + (off - lo);
}

AINL const uint8_t *sst_rc_at(sst_rc_t *c, const sst_src_t *s, uint32_t si, sst_rw_t *w, uint32_t off)
{
    uint32_t d = off - w->wlo;
    if (d < w->wn)
        return w->win + d;
    return sst_rc_slow(c, s, si, w, off);
}

/* frame f's sample, mono (a stereo frame's two channels averaged), Q15 (as sst_frame); 0 = not in the cache */
AINL int sst_rc_frame(sst_rc_t *c, const sst_src_t *s, uint32_t si, sst_rw_t *w, uint32_t f, int32_t *out)
{
    uint32_t off = f << s->fsh;
    const uint8_t *p = sst_rc_at(c, s, si, w, off), *q;
    int32_t a, b;
    if (!p)
        return 0;
    a = s->bits == SST_F16 ? (int32_t)(int16_t)(p[0] | p[1] << 8) : ((int32_t)p[0] - 128) << 8;
    if (s->chans == 2) {
        q = sst_rc_at(c, s, si, w, off + (1u << (s->fsh - 1u)));
        if (!q)
            return 0;
        b = s->bits == SST_F16 ? (int32_t)(int16_t)(q[0] | q[1] << 8) : ((int32_t)q[0] - 128) << 8;
        a = (a + b) >> 1;
    }
    *out = a;
    return 1;
}
#endif
