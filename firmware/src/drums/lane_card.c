/* SPDX-License-Identifier: GPL-3.0-only */
/* A drum lane on a sample of the SD card (FELUCCA_SD_LANES; docs/SD-CARD-DESIGN.md 7 and 9): its SRC DL_CARD, its sample
 * the project's (storage/sd/sd_mode.c: the lane's hash in the samples' record, given to the streams' plan as slot
 * SST_NPART + lane while the lane is on CARD), played as a one-shot through SAMPLE's reader (storage/sd/sd_stream.h
 * sst_frame): the hit starts at once from the head (the pre-buffer), the ring follows. Included by drums.c before
 * drum_on; the ISR's.
 *   the hit    cuts the lane's own hit before (its ring back for this one: the plan counts a ring a card lane), takes a
 *              drum voice and a stream; TUNE (an octave up at most, as SAMPLE's CARD: each octave doubles the reads),
 *              DECAY, CUT, LEVEL as a flash sample's (dl_smp_fx). No source (MISSING, the plan refused or not placed yet):
 *              silent; no ring free: silent and counted (sst_nostream)
 *   the voice  drums_mix's sampled loop, dlc_next its next frame: ph[0] the frame, ph[1] the fraction, s[0] the source,
 *              s[2] / s[3] the interpolator's two samples, s[4] SMP_STREAM | the stream, s[5] the step (Q16), s[7] its last
 *              output. Its end, a dry stream (counted, sst_dry) or its source let go (a project change, the plan placed
 *              again): the voice ends, its last output faded by the drums' declick tail (no click) */

/* card voice v's next frame into s[3]; 0 = it ended (declicked) */
static HOT int dlc_next(voice_t *v)
{
    uint32_t k = (uint32_t)v->s[4] & 0xFFu;
    sst_t *st = &sst[k < SST_N ? k : 0u];
    const sst_src_t *s = &sst_src[(uint32_t)v->s[0] < SST_NSRC ? (uint32_t)v->s[0] : 0u];
    int32_t x;
    if (k < SST_N && st->owner == &v->active) {
        st->beat = st->beat + 1u;                    /* (rendered: the main loop keeps it) */
        if (st->on && s->ready && ++v->ph[0] < s->frames) {
            if (sst_frame(st, s, v->ph[0], &x)) {
                v->s[3] = x;
                return 1;
            }
            sst_ran_dry(k);                          /* (the card late: counted) */
        }
        sst_stop(k);
    }
    v->active = 0;
    drums.tail += v->s[7];
    return 0;
}

/* lane l's hit on its card sample (drum_on) */
static void dlc_hit(uint32_t l, uint32_t note, uint32_t vel)
{
    uint32_t si = sst_part_src[SST_NPART + l], i, vi, stepq;
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
    stepq = (pow2_q16(clamp(dl_tune_play(l) * 16, -1536, SST_UP16)) >> 8) * (s->rate >> 8);
    v = drum_voice(note, vel, l);
    k = sst_start(si, 0, 0, 0, sst_rate(s, stepq), &v->active);
    if (k < 0) {
        v->active = 0;                               /* (no ring free: counted) */
        return;
    }
    vi = (uint32_t)(v - drums.v);
#if DINS
    dins_restart(vi);
#endif
    v->s[4] = (int32_t)(SMP_STREAM | (uint32_t)k);
    v->s[0] = (int32_t)si;
    v->ph[0] = (uint32_t)-1;                         /* (frame 0 read by the mix's first pass, dlc_next: RAM code;
                                                      * the hit here runs from flash, the reader's slow path from RAM) */
    v->ph[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[5] = (int32_t)stepq;
    drums.kit[vi] = 0;
    drums.synth[vi] = 0;
    drums.filter[vi] = 0;
    drums.env[vi] = 32767;
    dl_smp_fx(vi, l);
}
