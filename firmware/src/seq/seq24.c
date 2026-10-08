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
    int32_t m = TX(t)->micro[TRK_IDX(t, abs, trk_len(t)) % NSTEP];
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

#if FELUCCA_PLOCK
/* ---- parameter locks (SLOOP 2.4 seq.c p_lockable / lock_step / lock_set, isod89, GPL-3.0-only). A lock: on its step
 * the track's p[param] takes its value; the parameter goes back to what it was at the next step without a lock on
 * it (Elektron style: notes still ringing follow, the engines read p[] every block). The locks in force are listed
 * in lk_* (param, the base to go back to, the value set). A knob turned while a lock is on wins: the value found is
 * kept as the new base. Beside motion recording (motion.c): motion_step runs first on a step, so a lock wins on its
 * step and lets go to the motion value (its base). Ids: ours (P_*), 2.4's converted at import / export.
 * Lockable: the sound values (2.4's list in our ids; ANALOG 2's pages; the track FILTER when built). Not on the drum
 * track: its MAC_ID values, where the macros keep their positions (params.c): a lock there would move a macro. */
static uint8_t lk_n[NTRK], lk_param[NTRK][NLOCK];
static int16_t lk_base[NTRK][NLOCK], lk_set[NTRK][NLOCK];

static int p_lockable(uint32_t id)
{
    return id <= P_LD_AMP || id == P_SGATE || (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
           id == P_DETUNE || (id >= P_SLCR && id <= P_SLDEPTH) || (id >= P_E0 && id <= P_E7)
#if FELUCCA_ANALOG2
           || (id >= P_A2WAVE && id <= P_A2ESDT)
#endif
#if defined(SL24_TP) && FELUCCA_TRK_FILT
           || id == P_TFLT
#endif
           ;
}
static int lock_ok(const track_t *t, uint32_t id)      /* lockable on this track */
{
#if FELUCCA_MACROS
    uint32_t m;
    if (is_drum(t))
        for (m = 0; m < 4u; m++)
            if (MAC_ID[m] == id)
                return 0;
#endif
    (void)t;
    return id < P_COUNT && p_lockable(id);
}
/* the range of p[id] on track t (the engine that renders: t->engine; the drum track's P_E0: the kit) */
static const param_desc_t *lock_desc(const track_t *t, uint32_t id)
{
    if (is_drum(t) && id == P_E0)
        return &DRUM_KIT_DESC;
    if (id >= P_E0 && id <= P_E7)
        return &ENGINES[t->engine % NENGINES]->edit[id - P_E0];
    return &TP[id % P_COUNT];
}
static void lock_write(track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = lock_desc(t, id);
    t->p[id % P_COUNT] = (int16_t)clamp(v, d->min, d->max);
}
static void locks_restore(track_t *t)        /* every lock in force let go (STOP, a load, a cleared pattern) */
{
    uint32_t i, k = trk_index(t) % NTRK;
    for (i = 0; i < lk_n[k] && i < NLOCK; i++)
        if (t->p[lk_param[k][i] % P_COUNT] == lk_set[k][i])
            lock_write(t, lk_param[k][i], lk_base[k][i]);
    lk_n[k] = 0;
}
/* the sequencer enters step idx (NSTEP: a step with no lock): its locks take hold, the last step's that it does
 * not share let go */
static void lock_step(track_t *t, uint32_t idx)
{
    const stepx_t *x = TX(t);
    uint32_t i, j, n = 0, k = trk_index(t) % NTRK;
    for (i = 0; i < lk_n[k] && i < NLOCK; i++) {     /* in force, no lock here: back to the base (or the knob) */
        uint32_t p = lk_param[k][i] % P_COUNT, has = 0;
        for (j = 0; j < NLOCK; j++)
            if (x->lock[j].step == idx && x->lock[j].param == p)
                has = 1;
        if (has) {
            lk_param[k][n] = (uint8_t)p;
            lk_base[k][n] = lk_base[k][i];
            lk_set[k][n] = lk_set[k][i];
            n++;
        } else if (t->p[p] == lk_set[k][i]) {
            lock_write(t, p, lk_base[k][i]);
        }
    }
    lk_n[k] = (uint8_t)n;
    for (j = 0; j < NLOCK; j++) {                    /* this step's locks */
        const plock_t *l = &x->lock[j];
        uint32_t p = l->param;
        if (l->step != idx || !lock_ok(t, p))
            continue;
        for (i = 0; i < lk_n[k] && lk_param[k][i] != p; i++)
            ;
        if (i == lk_n[k]) {
            if (i >= NLOCK)
                continue;
            lk_param[k][i] = (uint8_t)p;
            lk_base[k][i] = t->p[p];
            lk_n[k]++;
        } else if (t->p[p] != lk_set[k][i]) {
            lk_base[k][i] = t->p[p];                 /* turned meanwhile: that is the new base */
        }
        lock_write(t, p, l->val);
        lk_set[k][i] = t->p[p];
    }
}
/* set (or make) the lock of (step, param) at v, clamped; 0 = no slot left or not lockable. Callers hold the IRQ off */
static int lock_set(track_t *t, uint32_t step, uint32_t param, int32_t v)
{
    const param_desc_t *d;
    if (step >= NSTEP || !lock_ok(t, param))
        return 0;
    d = lock_desc(t, param);
    return stepx_lock_set(TX(t), step, param, (int16_t)clamp(v, d->min, d->max)) >= 0;
}
#endif
