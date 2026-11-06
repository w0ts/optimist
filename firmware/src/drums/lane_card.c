/* SPDX-License-Identifier: GPL-3.0-only */
/* A drum lane on a sample of the SD card (FELUCCA_SD_LANES; docs/SD-CARD-DESIGN.md 7 and 9): its SRC DL_CARD, its sample
 * the project's (storage/sd/sd_mode.c: the lane's hash in the samples' record, given to the streams' plan as slot
 * SST_NPART + lane while the lane is on CARD), played as a one-shot through SAMPLE's reader (storage/sd/sd_stream.h
 * sst_frame): the hit starts at once from the head (the pre-buffer), the ring follows. Included by drums.c before
 * drum_on; the ISR's.
 *   the hit    cuts the lane's own hit before (its ring back for this one: the plan counts a ring a card lane), takes a
 *              drum voice and a stream; TUNE (an octave up at most, as SAMPLE's CARD: each octave doubles the reads),
 *              DECAY, CUT, LEVEL as a flash sample's (dl_smp_fx). No source (MISSING, the plan refused or not placed yet):
 *              silent; no ring free: the oldest card hit's tail gives its ring; held by the parts: silent, counted (sst_nostream)
 *   the voice  drums_mix's sampled loop, dlc_next its next frame: ph[0] the frame, ph[1] the fraction, s[0] the source,
 *              s[2] / s[3] the interpolator's two samples, s[4] SMP_STREAM | the stream, s[5] the step (Q16), s[7] its last
 *              output. Its end, a dry stream (counted, sst_dry) or its source let go (a project change, the plan placed
 *              again): the voice ends, its last output faded by the drums' declick tail (no click) */

/* a card voice's stream over one block of drums_mix (dlc_open before the voice's loop, dlc_step a frame). What the
 * frames read of the stream and its source holds for the whole block: the audio alone changes it (sst_start, sst_stop
 * at a hit or in dlc_step itself; the main loop's sd_stream.c never runs inside a render, the card's interrupt writes
 * only the slots and their tags), so it is looked at once a block, not once a frame */
typedef struct {
    sst_t *st;
    const sst_src_t *s;
    uint32_t k, frames, fsh;
    int32_t ok;                                      /* 1 its stream, on, its source ready; 0 its stream, stopped or the source
                                                      * not ready (the next frame stops it, ends); -1 not its stream (ends) */
    uint8_t m16;                                     /* mono 16-bit: a frame in the window is a plain read */
    int32_t *tail;                                   /* where its end's last value goes (the declick): drums.tail, or the
                                                      * drum half's that renders it (drums_mix) */
} dlc_blk_t;
AINL void dlc_open(voice_t *v, dlc_blk_t *b)
{
    uint32_t k = (uint32_t)v->s[4] & 0xFFu;
    const sst_src_t *s = &sst_src[(uint32_t)v->s[0] < SST_NSRC ? (uint32_t)v->s[0] : 0u];
    b->k = k;
    b->st = &sst[k < SST_N ? k : 0u];
    b->s = s;
    b->ok = -1;
    b->frames = s->frames;
    b->fsh = s->fsh;
    b->m16 = (uint8_t)(s->bits == SST_F16 && s->chans == 1u);
    b->tail = &drums.tail;
    if (k < SST_N && b->st->owner == &v->active) {
        b->st->beat = b->st->beat + 1u;              /* (rendered: the main loop keeps it; once a block is enough, it
                                                      * looks for a change over SST_DEAD_MS) */
        b->ok = b->st->on && s->ready;
    }
}
/* card voice v's next frame into s[3] (b: its block's, dlc_open); 0 = it ended (declicked). A frame inside the
 * stream's window of a mono 16-bit source is read in place (sst_at_go's compare, then the two bytes: the same value
 * sst_frame gives); leaving the window, another format, the end: sst_frame, as before */
AINL int dlc_step(voice_t *v, const dlc_blk_t *b)
{
    sst_t *st = b->st;
    int32_t x;
    if (b->ok > 0) {
        uint32_t f = v->ph[0] + 1u, off, d;
        v->ph[0] = f;
        if (f < b->frames) {
            off = f << b->fsh;
            d = off - st->wlo;
            if (b->m16 && d < st->wn) {
                const uint8_t *p = st->win + d;
                st->pos = off;
                v->s[3] = (int32_t)(int16_t)(p[0] | p[1] << 8);
                return 1;
            }
            if (sst_frame(st, b->s, f, &x)) {
                v->s[3] = x;
                return 1;
            }
            sst_ran_dry(b->k);                       /* (the card late: counted) */
        }
        sst_stop(b->k);
    } else if (!b->ok) {
        sst_stop(b->k);
    }
    v->active = 0;
    *b->tail += v->s[7];
    return 0;
}
/* card voice v's next frame into s[3]; 0 = it ended (declicked). One frame alone (tests/sd_kit_card_test.c); drums_mix
 * opens the block once and steps */
static HOT int dlc_next(voice_t *v)
{
    dlc_blk_t b;
    dlc_open(v, &b);
    return dlc_step(v, &b);
}

/* no ring free for voice v's hit: the oldest card hit's tail gives its ring (cut, its last value faded by the tail, counted
 * sst_cut). A hit is never silent for a tail: rings are fewer than the lanes, the kit's files last 0.25 to 1.6 s and a dense
 * pattern on 3 rings silenced 2 hits of 3 (tests/sd_kit_card_test.c; the user's NO RING 400, 2026-11-05) */
static void dlc_ring_free(const voice_t *v)
{
    voice_t *o = 0;
    uint32_t i;
    for (i = 0; i < SST_N; i++)
        if (sst[i].ring && (!sst[i].on || (sst[i].owner && !*sst[i].owner)))
            return;
    for (i = 0; i < NDRUM; i++)
        if (&drums.v[i] != v && drums.v[i].active && dlc_is(&drums.v[i]) && (!o || drums.v[i].age < o->age))
            o = &drums.v[i];
    if (!o)
        return;                                      /* (the parts hold them all: silent, counted) */
    o->active = 0;
    drums.tail += o->s[7];
    dlc_let_go(o);
    sst_cut++;
}

/* lane l's hit on card source si, d16 sixteenths of a semitone off its rate (an octave up at most), kit its treatment
 * (drums_mix: a sampled kit's, 0 none): the lane's hit before cut, a drum voice and a stream (drum_on: dlc_hit, a lane on
 * CARD; kit_card.c kc_hit, a sampled kit's sound on the card) */
static void dlc_go(uint32_t l, uint32_t si, uint32_t note, uint32_t vel, int32_t d16, uint32_t kit)
{
    uint32_t i, vi, stepq;
    const sst_src_t *s = &sst_src[si < SST_NSRC ? si : 0u];
    voice_t *v;
    int32_t k;
    if (si >= SST_NSRC || !s->ready)
        return;                                      /* (MISSING, refused, not placed yet: silent) */
    for (i = 0; i < NDRUM; i++)                      /* the lane's hit before: cut, its ring for this one */
        if (drums.v[i].active && drums.lane[i] == l && dlc_is(&drums.v[i])) {
            drums.v[i].active = 0;
            drums.tail += drums.v[i].s[7];
            dlc_let_go(&drums.v[i]);
        }
    stepq = (pow2_q16(clamp(d16, -1536, SST_UP16)) >> 8) * (s->rate >> 8);
    v = FAST_CALL(seq, drum_voice)(note, vel, l);   /* (flash code: the RAM code's with SEQ_RAM 2) */
    dlc_ring_free(v);
    k = sst_start(si, 0, 0, 0, sst_rate(s, stepq), &v->active);
    if (k < 0) {
        v->active = 0;                               /* (no ring free: counted) */
        return;
    }
    vi = (uint32_t)(v - drums.v);
#if DINS
    FAST_CALL(seq, dins_restart)(vi);
#endif
    v->s[4] = (int32_t)(SMP_STREAM | (uint32_t)k);
    v->s[0] = (int32_t)si;
    v->ph[0] = (uint32_t)-1;                         /* (frame 0 read by the mix's first pass, dlc_next: RAM code;
                                                      * the hit here runs from flash, the reader's slow path from RAM) */
    v->ph[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[5] = (int32_t)stepq;
    drums.kit[vi] = (uint8_t)kit;
    drums.synth[vi] = 0;
    drums.filter[vi] = 0;
    drums.env[vi] = 32767;
    FAST_CALL(seq, dl_smp_fx)(vi, l);
}

/* lane l's hit on its card sample (drum_on: SRC CARD) */
static void dlc_hit(uint32_t l, uint32_t note, uint32_t vel)
{
    dlc_go(l, sst_part_src[SST_NPART + l], note, vel, dl_tune_play(l) * 16, 0);
}
#if KITS_CARD
#include "kit_card.c"          /* the sampled kits' sounds on the card (SD card mode) */
#endif
