/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0: per-step chance,
 * hugelton/Felucca 727f272, core.h step_chance / seq.c seq_step) */
/* Per-step chance (FELUCCA_CHANCE, backports.h): a synth step plays with a probability, set on SEQ > STEP 2
 * (KNOB 2, 0..100 % in 5 % steps). Included by seq.c.
 *
 * Felucca keeps a byte per step (0 = 100 %, 1..100 %, 101 = never). Our step_t has no byte to spare (10 B,
 * project format FUN8 is full), so the chance lives in the unused bits 2..6 of step_t.flags: 0 = always (every
 * step written before, every pattern), k = 1..20 -> 100 - 5k % (20 = never). Nothing else reads those bits; a
 * build without the switch keeps them (projects, undo, copies) and plays every step.
 *
 * The roll: a NOTE step (not a TIE: it holds what sounds) whose chance fails plays as a REST and its ratchet hits
 * stay silent. A step at 100 % calls no random number: a pattern without chance plays (and renders) exactly as
 * before. Drum steps have no free bits (dstep_t: 16 lanes x on / level / ratchet): their chance is an event of the
 * automation store (auto.h AUTO_CHANCE, phase 3), on synth steps too, where it wins over the bits; the Optimist UI
 * writes events only, SLOOP's UI the bits as before. */
#define SF_CH_SHIFT 2u
#define SF_CH_MASK (31u << SF_CH_SHIFT)
#define CH_STEP 5u                                 /* % per detent */
#define CH_MAX 20u                                 /* 100 / CH_STEP: never */

/* the step's chance in percent (0..100) */
static uint32_t step_chance(const step_t *s)
{
    uint32_t k = ((uint32_t)s->flags & SF_CH_MASK) >> SF_CH_SHIFT;
    return k >= CH_MAX ? 0u : 100u - k * CH_STEP;
}

/* set it (percent, rounded to the 5 % grid) */
static void step_set_chance(step_t *s, uint32_t pct)
{
    uint32_t k = pct >= 100u ? 0u : (100u - pct + CH_STEP / 2u) / CH_STEP;
    if (k > CH_MAX)
        k = CH_MAX;
    s->flags = (uint8_t)(((uint32_t)s->flags & ~SF_CH_MASK) | k << SF_CH_SHIFT);
}

/* the roll for a NOTE step of track t about to play: 1 = it stays silent (no random number at 100 %). The step's
 * chance event (the automation store, auto_step: auto_ch) wins over its chance bits */
static int chance_drop(const track_t *t, const step_t *s)
{
    uint32_t c = auto_ch[trk_index(t) % NTRK];
    if (s->time != ST_NOTE)
        return 0;
    if (c >= 100u) {
        if (!((uint32_t)s->flags & SF_CH_MASK))
            return 0;
        c = step_chance(s);
    }
    return !c || rng() % 100u >= c;
}
/* a drum step's chance (an event only: the drum step has no free bit), every lane together: 1 = the step stays
 * silent, its ratchets too (seq_ratchets) */
static uint8_t chance_out[NTRK];
static int chance_drum_drop(const track_t *t, const dstep_t *s)
{
    uint32_t k = trk_index(t) % NTRK, c = auto_ch[k];
    chance_out[k] = (uint8_t)(c < 100u && dstep_mask(s) && (!c || rng() % 100u >= c));
    return chance_out[k];
}
