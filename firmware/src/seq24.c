/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's per-step extras: micro timing, fills (and, with PLOCK, the parameter locks). Ported from
 * SLOOP 2.4 by isod89 (isod89/sloop-fm1 v2.4, 8d3823f, seq.c micro_units / step_fill / step_plays, GPL-3.0-only; on
 * Felucca by Leo Kuroshita). Included by seq.c (backports24seq.h: FELUCCA_MICRO, FELUCCA_FILLS, FELUCCA_PLOCK).
 *
 * The data model is 2.4's, byte for byte (its FUN5 tail, 176 B a track): micro[64] (a step's nudge in 1/64 of a
 * step, -32 early .. 31 late), lock[24] {step, param, int16 value} and fill[16] (2 bits a step: FC_*). */

#include "stepx.h"          /* the record: SLOOP 2.4's layout and helpers (phase 0) */

/* the working copy of each track's extras: STEPX(k), track k (the persistence, the editor and the UI use it) */
static stepx_t stepx_w[NTRK];
#define STEPX(k) (&stepx_w[(uint32_t)(k) % NTRK])
#define TX(t) STEPX(trk_index(t))

#if FELUCCA_MICRO
/* the nudge of grid step abs of track t, in units of a step slen long: where in its own step it fires
 * (micro >= 0), or how far before its step (micro < 0, as a negative number) */
static int32_t micro_units(const track_t *t, uint32_t abs, uint32_t slen)
{
    int32_t m = TX(t)->micro[abs % trk_len(t) % NSTEP];
    return (int32_t)(slen / 64u) * m;                    /* |m| <= 32: fits */
}
#endif

#if FELUCCA_FILLS
static uint32_t step_fill(const track_t *t, uint32_t idx) { return stepx_fill(TX(t), idx % NSTEP); }
static void step_fill_set(track_t *t, uint32_t idx, uint32_t v) { stepx_fill_set(TX(t), idx % NSTEP, v); }
static volatile uint8_t fill_held;           /* GLO + key 9 down (the UI) */
static volatile uint8_t fill_arm;            /* GLO + key 10: the next bar is a fill (the UI; the ISR clears it) */
static uint8_t fill_bar_on;                  /* that bar, while it plays (the ISR) */
static uint8_t fill_now;                     /* this block is a fill: fill_held || fill_bar_on, read once a block */
static uint32_t fill_last_bar = 0xFFFFFFFFu; /* clk_beat / 4 of the last bar seen (events_block) */
static uint32_t step_plays(const track_t *t, uint32_t idx)   /* its condition holds now (3 reads as normal) */
{
    uint32_t c = step_fill(t, idx);
    return c == FC_FILL ? fill_now : c == FC_NOFILL ? !fill_now : 1u;
}
static void fill_block(void)                 /* once a block (events_block): a new bar takes the armed fill bar */
{
    if (song.playing && !(clk_beat & 3u) && (clk_beat >> 2) != fill_last_bar) {
        fill_last_bar = clk_beat >> 2;
        fill_bar_on = fill_arm;
        fill_arm = 0;
    }
    fill_now = (uint8_t)(fill_held || fill_bar_on);
}
#endif

#if FELUCCA_MICRO
static uint8_t seq_den[NTRK];                /* the DIV each track last played on (a DIV change waits for the next step) */
#endif
#if FELUCCA_FILLS
static uint8_t seq_skip[NTRK];               /* the step playing failed its fill condition: nothing of it sounds */
#endif
