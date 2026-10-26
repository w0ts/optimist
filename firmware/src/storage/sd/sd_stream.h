/* SPDX-License-Identifier: GPL-3.0-only */
/* Samples streamed from the SD card (docs/SD-CARD-DESIGN.md 7, "Streaming"; FELUCCA_SD_STREAM: full FatFs, the builder's
 * SD_RING). The audio's side: the types the main loop's scheduler (sd_stream.c) and the voices share, and the reads a
 * voice makes. Included early (engines/sample/eng_sample.c), before the card's code; nothing here touches the bus.
 *
 * A SOURCE (sst_src[]) is a sample on the card the project plays: its WAV data's place in the file, its format, the
 * file's contiguous runs on the card (from FatFs' fast-seek map, made once when the project loads), and its HEAD: the
 * first hlen bytes of its data kept in RAM (the pre-buffer, sst_pre_ms), so a note starts at once while the card's first
 * block comes. A STREAM (sst[]) is a playing voice's lookahead: a RING of SST_K slots of one 512-byte card sector each,
 * filled in play order by the main loop (sd_stream.c sst_service, short CMD18 runs between the screen's strips), read
 * by the audio. Each slot carries a TAG, gen << 24 | the file's sector + 1 (0: empty), written by the main loop after
 * the slot's data; the audio reads a slot only when its tag is the sector it wants under its own generation.
 *
 * The audio (the voice, sst_at): the bytes at a data offset, from the current WINDOW (the head, or the ring slot being
 * read) in a compare; leaving the window it gives the slot back (rd + 1) and takes the next one in the ring, or the head
 * (a loop back into it); a slot not there yet (its tag not the one wanted) is DRY: sst_at returns 0, the voice fades out
 * (eng_sample.c, no click) and sst_dry counts it. A stream is started by the audio (sst_start, at a note: the tags
 * cleared, the generation + 1) and ended by it (sst_stop); a voice that ends without its engine knowing (a release's
 * end, a steal) frees it with its active flag (owner), and one no longer rendered at all stops its BEAT: the main loop
 * takes the stream back (sd_stream.c). Single
 * producer (the main loop), single consumer (the core that renders the voice); every field one of them writes is a
 * 32-bit or 8-bit store. */
#ifndef SD_STREAM_H
#define SD_STREAM_H
#include <stdint.h>

#ifndef HOT
#define HOT                                          /* (core.h: the audio's code in RAM; a host test: none) */
#endif
#ifndef IN_POOL_AUDIO
#define IN_POOL_AUDIO                                /* (core.h: the audio reads it, in the pool with TAB_IN_POOL) */
#endif
#ifndef SST_N
#define SST_N 8u                                     /* streams (rings): one a sounding voice (core.h NVOICE) */
#endif
#ifndef SST_K
#define SST_K ((uint32_t)FELUCCA_SD_RING_KB * 2u)    /* a ring's slots (512 B each): the builder's SD_RING, KB a voice */
#endif
#ifndef SST_NSRC
#define SST_NSRC 16u                                 /* the card samples a project plays at most */
#endif
_Static_assert(SST_K >= 2u && SST_K <= 64u, "sd_stream.h: a ring of 1 to 32 KB");

enum { SST_F8 = 0, SST_F16 = 1 };                    /* sst_src_t.bits: 8-bit unsigned, 16-bit signed (little-endian) */

typedef struct {
    uint32_t fsec, lba, n;                           /* a run: the file's sector fsec on, n sectors at the card's lba */
} sst_run_t;

typedef struct {                                     /* a card sample (the main loop writes it while ready is 0) */
    const uint8_t *head;                             /* the data's first hlen bytes */
    const sst_run_t *run;                            /* nrun runs, in the file's order */
    uint32_t hlen;                                   /* the head's bytes (a multiple of 4, <= dlen) */
    uint32_t dofs;                                   /* the data's offset in the file (even: RIFF chunks are) */
    uint32_t dlen;                                   /* the data's bytes */
    uint32_t frames;                                 /* dlen / the frame's bytes */
    uint32_t rate;                                   /* its rate / 44100, Q16 */
    uint64_t hash;                                   /* sd_hash.c: the data's XXH64 (its identity in a project) */
    uint32_t lps, lpe;                               /* the file's loop (smpl): frames lps .. lpe; lpe 0 none */
    uint16_t nrun;
    int8_t root;                                     /* the file's unity note (smpl) - C4: the note it plays at its rate */
    uint8_t bits, chans;                             /* SST_F8 / SST_F16; 1 or 2 */
    uint8_t fsh;                                     /* log2 of a frame's bytes */
    volatile uint8_t ready;                          /* 1: the audio may play it */
} sst_src_t;

typedef struct {
    uint8_t *ring;                                   /* SST_K x 512 B, 4-byte aligned (the DMA) */
    const volatile uint8_t *owner;                   /* the voice's active flag: 0, the stream is free */
    volatile uint32_t tag[SST_K];                    /* gen << 24 | file sector + 1; 0 empty */
    /* the audio's */
    const uint8_t *win;                              /* the window: data bytes wlo .. wlo + wn at win */
    uint32_t wlo, wn;
    uint32_t lpend;                                  /* looped: the data byte after the loop's end (0: one-shot) */
    uint32_t lpstart;                                /* looped: the loop's first data byte */
    volatile uint32_t rd;                            /* the slots given back */
    volatile uint32_t pos;                           /* the data byte read last (the scheduler's priority) */
    volatile uint32_t bpms;                          /* the data bytes it reads a millisecond (its pitch, its rate) */
    volatile uint32_t beat;                          /* + 1 a render: the main loop sees a voice taken away */
    volatile uint8_t gen;                            /* + 1 a start */
    volatile uint8_t on;                             /* 1 from sst_start to sst_stop */
    volatile uint8_t dry;                            /* it ran dry (the voice fades out) */
    uint8_t src;                                     /* sst_src[] */
    uint8_t in_ring;                                 /* the window is slot rd % SST_K */
    /* the main loop's (sd_stream.c) */
    uint8_t pgen;                                    /* the generation it fills for */
    uint8_t fails;                                   /* a block's CRC failures (sd_crc_retry) */
    uint8_t done;                                    /* nothing more to read in play order */
    uint32_t wr;                                     /* the slots filled */
    uint32_t next;                                   /* the next file sector in play order */
    uint32_t beat_seen, beat_t;                      /* the beat it saw last, and when (SD_NOW) */
} sst_t;

#ifndef SST_NPART
#define SST_NPART 3u                                 /* the synth parts (core.h NPART) */
#endif
#ifndef SST_NLANE
#define SST_NLANE 0u                                 /* the drum lanes that play card samples (eng_sample.c: DRUM_LANES with
                                                      * FELUCCA_SD_LANES), slots SST_NPART + lane after the parts */
#endif
#define SST_NSLOT (SST_NPART + SST_NLANE)
static sst_src_t sst_src[SST_NSRC] IN_POOL_AUDIO;
static uint8_t sst_part_src[SST_NSLOT] = {[0 ... SST_NSLOT - 1u] = 0xFF};   /* the source each part's CARD (each card
                                                                             * lane) plays, 0xFF none (sd_stream_plan.c) */
static sst_t sst[SST_N] IN_POOL_AUDIO;
static volatile uint32_t sst_dry;                    /* streams that ran dry (the console's sd stream) */
static volatile uint32_t sst_nostream;               /* notes on a card sample with no ring free (silent) */

AINL uint32_t sst_tag(uint32_t gen, uint32_t sec) { return gen << 24 | (sec + 1u); }

/* the audio: data byte off (one sample: 1 byte at 8 bits, 2 at 16, never across a slot: the data's offset is even);
 * 0 = dry. Leaving the window gives its slot back. */
static HOT const uint8_t *sst_at_slow(sst_t *st, const sst_src_t *s, uint32_t off)
{
    uint32_t sec, slot, lo, hi;
    if (st->in_ring) {                               /* (leaving the slot: given back) */
        st->in_ring = 0;
        st->rd = st->rd + 1u;
    }
    if (off < s->hlen) {                             /* the head: the start, or a loop back into it */
        st->win = s->head;
        st->wlo = 0;
        st->wn = s->hlen;
        return s->head + off;
    }
    if (off >= s->dlen)
        return 0;
    sec = (s->dofs + off) >> 9;
    slot = st->rd % SST_K;
    if (st->tag[slot] != sst_tag(st->gen, sec)) {
        st->wn = 0;
        return 0;                                    /* dry */
    }
    lo = sec << 9;
    hi = lo + 512u;
    lo = lo > s->dofs ? lo - s->dofs : 0;            /* (the data's first sector holds the header's end too) */
    hi = hi - s->dofs;
    if (hi > s->dlen)
        hi = s->dlen;
    st->in_ring = 1;
    st->win = st->ring + slot * 512u + ((s->dofs + lo) & 511u);
    st->wlo = lo;
    st->wn = hi - lo;
    return st->win + (off - lo);
}

AINL const uint8_t *sst_at(sst_t *st, const sst_src_t *s, uint32_t off)
{
    uint32_t d = off - st->wlo;
    st->pos = off;
    if (d < st->wn)
        return st->win + d;
    return sst_at_slow(st, s, off);
}

/* frame f's sample, mono (a stereo frame's two channels averaged), Q15; 0 = dry */
AINL int sst_frame(sst_t *st, const sst_src_t *s, uint32_t f, int32_t *out)
{
    uint32_t off = f << s->fsh;
    const uint8_t *p = sst_at(st, s, off), *q;
    int32_t a, b;
    if (!p)
        return 0;
    a = s->bits == SST_F16 ? (int32_t)(int16_t)(p[0] | p[1] << 8) : ((int32_t)p[0] - 128) << 8;
    if (s->chans == 2) {
        q = sst_at(st, s, off + (1u << (s->fsh - 1u)));
        if (!q)
            return 0;
        b = s->bits == SST_F16 ? (int32_t)(int16_t)(q[0] | q[1] << 8) : ((int32_t)q[0] - 128) << 8;
        a = (a + b) >> 1;
    }
    *out = a;
    return 1;
}

/* the audio, at a note: a free stream for source si (looped: frames ls .. le, else one-shot); its index, or -1 none
 * (sst_nostream counted). bpms: the data bytes the voice reads a millisecond (sst_rate); owner: the voice's active
 * flag (a voice that ends without its engine knowing, a release's end, a steal, frees its stream with it) */
static int32_t sst_start(uint32_t si, uint32_t looped, uint32_t ls, uint32_t le, uint32_t bpms,
                         const volatile uint8_t *owner)
{
    const sst_src_t *s = &sst_src[si];
    uint32_t i, k;
    if (si >= SST_NSRC || !s->ready || !s->frames)
        return -1;
    for (i = 0; i < SST_N; i++) {
        sst_t *st = &sst[i];
        if ((st->on && !(st->owner && !*st->owner)) || !st->ring)
            continue;
        for (k = 0; k < SST_K; k++)
            st->tag[k] = 0;
        st->gen = (uint8_t)(st->gen + 1u);
        st->src = (uint8_t)si;
        st->rd = 0;
        st->in_ring = 0;
        st->dry = 0;
        st->win = s->head;
        st->wlo = 0;
        st->wn = s->hlen;
        st->pos = 0;
        st->bpms = bpms;
        if (looped && le >= ls && le < s->frames) {
            st->lpstart = ls << s->fsh;
            st->lpend = (le + 1u) << s->fsh;
        } else {
            st->lpstart = st->lpend = 0;
        }
        st->owner = owner;
        st->on = 1;                                  /* (last: the main loop fills it from here) */
        return (int32_t)i;
    }
    sst_nostream++;
    return -1;
}

AINL void sst_stop(uint32_t i)                      /* the audio: the voice is over */
{
    if (i < SST_N)
        sst[i].on = 0;
}

AINL void sst_ran_dry(uint32_t i)                   /* the audio: sst_at found nothing; the voice fades out */
{
    if (i < SST_N && sst[i].on) {
        sst[i].dry = 1;
        sst[i].on = 0;
        sst_dry++;
    }
}

/* the data bytes a millisecond a voice reads at stepq (Q16 frames an output sample, 44.1 kHz): the scheduler's urgency */
AINL uint32_t sst_rate(const sst_src_t *s, uint32_t stepq)
{
    return (((stepq >> 4) * 441u / 40960u) << s->fsh) + 1u;   /* (32 bits: stepq < 2^23, 4 x 44.1 kHz 8 x up) */
}
#endif
