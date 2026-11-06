/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE's material (engines/slice/eng_slice.c, the top): the slice table, the ADPCM decoder over it, the onset scan, and
 * a CARD's encoder. Split out (plugins/samples, 2026-11-06) so that a firmware whose SLICE is a plugin still reads and
 * encodes a part's card sample in its main loop (storage/sd/sd_slice.c, SLC_CARD_SVC: engines.c), and the plugin plays
 * it (the table through plug_api_t slc_crd). The same code, moved. */
#ifndef SLC_CARD_C
#define SLC_CARD_C
#define SLC_GRID 128u                /* grid points: decoder states at k * len / SLC_GRID */
#define SLC_GRID_LOG2 7
#define SLC_AUTO 32u                 /* AUTO slices at most */
#define SLC_SEGS 16u                 /* zones of a user slot */
#define SLC_RB 64u                   /* reverse: samples decoded per window */
#define SLC_BASE 60                  /* note of slice 0 (+ the track's ROOT) */
#define SLC_HOP 32u                  /* AUTO detector: samples per hop */

typedef struct {
    uint32_t off, at, n;             /* data (SMP_DATA index), material position of its first sample, samples */
} slc_seg_t;
typedef struct {
    uint32_t len, rate, nseg, nauto; /* material samples (0 = none), rate (as smp_zone_t), segments, AUTO slices */
    slc_seg_t seg[SLC_SEGS];
    uint32_t grid[SLC_GRID];         /* decoder state before sample k * len / SLC_GRID: */
                                     /* (uint16_t)predictor | step index << 16 | segment << 24 */
    uint32_t apos[SLC_AUTO], ast[SLC_AUTO];   /* AUTO slice starts (apos[0] = 0) and their states */
    const uint8_t *dat;              /* the segments' data: 0 SMP_DATA (BREAK, a user slot), else a CARD's (in RAM) */
} slc_src_t;

#define SLC_SRC_CARD 4u
#if FELUCCA_SD_STREAM
#ifdef PLUG_BUILD
#define slc_crd ((const slc_src_t *const volatile *)plugin_api->slc_crd)   /* (a plugin: the firmware's) */
#else
static const slc_src_t *volatile slc_crd[SST_NPART];   /* each part's CARD material, 0 none (storage/sd/sd_slice.c) */
#endif
#endif

/* ---- the ADPCM decoder over the material (segments one after the other, each from 0 / 0) */
typedef struct {
    int32_t pred, idx;
    uint32_t pos, seg;
    const uint8_t *dat;              /* (slc_dat) */
} slc_dec_t;
AINL const uint8_t *slc_dat(const slc_src_t *s) { return s->dat ? s->dat : SMP_DATA; }
static inline void slc_dec_at(slc_dec_t *d, uint32_t pos, uint32_t st)
{
    d->pos = pos;
    d->pred = (int16_t)(st & 0xFFFFu);
    d->idx = (int32_t)((st >> 16) & 127u);
    d->seg = st >> 24;
}
static inline uint32_t slc_dec_st(const slc_dec_t *d)
{
    return (uint32_t)(uint16_t)d->pred | (uint32_t)d->idx << 16 | d->seg << 24;
}
/* the sample at d->pos (< len), then the next position */
static inline int32_t slc_dec_next(const slc_src_t *s, slc_dec_t *d)
{
    const slc_seg_t *g = &s->seg[d->seg];
    uint32_t rel = d->pos - g->at, b, code;
    if (rel >= g->n && d->seg + 1u < s->nseg) {         /* into the next zone: its data starts from 0 / 0 */
        d->seg++;
        g++;
        rel = d->pos - g->at;
        d->pred = 0;
        d->idx = 0;
    }
    b = d->dat[g->off + (rel >> 1)];
    code = (rel & 1u) ? (b >> 4) : (b & 15u);
    ima_nibble(code, IMA_STEP[d->idx], &d->pred, &d->idx);   /* (dsp.c) */
    d->pos++;
    return d->pred;
}

/* ---- the slice table (main loop) */
/* the grid states and the AUTO slices of s (len samples; s->len stays 0 meanwhile): see the top */
static void slc_scan(slc_src_t *s, uint32_t len)
{
    slc_dec_t d;
    uint32_t pos = 0, k = 0, gap = ((s->rate >> 4) * 44100u >> 12) / 20u, last = 0, h0 = 0, st0 = 0;
    int32_t prev = 0, pkd = 0, pkr = 0, envd = 0, envr = 0, thd, thr;
    d.dat = slc_dat(s);
    slc_dec_at(&d, 0, 0);
    while (pos++ < len) {                               /* pass 1: the largest step and sample */
        int32_t x = slc_dec_next(s, &d), a = x - prev;
        a = a < 0 ? -a : a;
        pkd = a > pkd ? a : pkd;
        a = x < 0 ? -x : x;
        pkr = a > pkr ? a : pkr;
        prev = x;
    }
    thd = pkd / 12;
    thr = pkr / 8;
    s->nauto = 1;
    s->apos[0] = 0;
    s->ast[0] = 0;
    slc_dec_at(&d, 0, 0);
    prev = 0;
    for (pos = 0; pos < len;) {                         /* pass 2: grid states, onsets */
        uint32_t hs = pos, hst = slc_dec_st(&d), e = len - pos > SLC_HOP ? pos + SLC_HOP : len;
        int32_t hd = 0, hr = 0;
        for (; pos < e; pos++) {
            int32_t x, a;
            while (k < SLC_GRID && ((k * len) >> SLC_GRID_LOG2) == pos)
                s->grid[k++] = slc_dec_st(&d);
            x = slc_dec_next(s, &d);
            a = x - prev;
            a = a < 0 ? -a : a;
            hd = a > hd ? a : hd;
            a = x < 0 ? -x : x;
            hr = a > hr ? a : hr;
            prev = x;
        }
        /* a rise of the high-passed peak (hats, snares, clicks) or of the peak (low hits: a slow
         * follower, so a low tone's cycles do not look like rises) */
        if (hs && ((hd > thd && hd * 4 > envd * 7) || (hr > thr && hr * 4 > envr * 7)) && h0 >= last + gap &&
            s->nauto < SLC_AUTO) {
            s->apos[s->nauto] = h0;                     /* from the hop before: pre-roll */
            s->ast[s->nauto++] = st0;
            last = h0;
        }
        envd = hd > envd ? hd : envd - envd / 16;
        envr = hr > envr ? hr : envr - envr / 64;
        h0 = hs;
        st0 = hst;
    }
}

#if FELUCCA_SD_STREAM
/* ---- a CARD's material (main loop: storage/sd/sd_slice.c): its table, then its ADPCM, in one block of memory.
 * slc_enc_bytes the block for a sample of frames; slc_enc_begin, slc_enc_put a mono sample (Q15) frame by frame,
 * slc_enc_end: the table scanned (slc_scan, as a user slot's), its length set last; the table, or 0 */
typedef struct {
    slc_src_t *s;
    uint8_t *d;
    uint32_t n, frames;
    int32_t pred, idx;
} slc_enc_t;
#define SLC_TAB_BYTES (((uint32_t)sizeof(slc_src_t) + 7u) & ~7u)
static uint32_t slc_enc_bytes(uint32_t frames) { return SLC_TAB_BYTES + (((frames + 1u) / 2u + 7u) & ~7u); }

static void slc_enc_begin(slc_enc_t *e, void *mem, uint32_t frames, uint32_t rate)   /* (mem 8-byte aligned) */
{
    e->s = (slc_src_t *)mem;
    e->d = (uint8_t *)mem + SLC_TAB_BYTES;
    e->n = 0;
    e->frames = frames;
    e->pred = e->idx = 0;                               /* (a segment's data starts from 0 / 0) */
    memset(e->s, 0, sizeof *e->s);
    e->s->rate = rate;
    e->s->dat = e->d;
}

static void slc_enc_put(slc_enc_t *e, int32_t x)       /* IMA ADPCM: the code nearest x, decoded as the voice will */
{
    int32_t step = IMA_STEP[e->idx], dx = x - e->pred;
    uint32_t c = 0;
    if (e->n >= e->frames)
        return;
    if (dx < 0) {
        c = 8u;
        dx = -dx;
    }
    if (dx >= step) {
        c |= 4u;
        dx -= step;
    }
    if (dx >= step >> 1) {
        c |= 2u;
        dx -= step >> 1;
    }
    if (dx >= step >> 2)
        c |= 1u;
    ima_nibble(c, step, &e->pred, &e->idx);
    if (e->n & 1u)
        e->d[e->n >> 1] |= (uint8_t)(c << 4);
    else
        e->d[e->n >> 1] = (uint8_t)c;
    e->n++;
}

static const slc_src_t *slc_enc_end(slc_enc_t *e)
{
    slc_src_t *s = e->s;
    if (!e->n)
        return 0;
    s->nseg = 1;
    s->seg[0].off = 0;
    s->seg[0].at = 0;
    s->seg[0].n = e->n;
    slc_scan(s, e->n);
    s->len = e->n;
    return s;
}
#endif

#endif
