/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's per-step extras: micro timing, fills (and, with PLOCK, the parameter locks). Ported from
 * SLOOP 2.4 by isod89 (isod89/sloop-fm1 v2.4, 8d3823f, seq.c micro_units / step_fill / step_plays, GPL-3.0-only; on
 * Felucca by Leo Kuroshita). Included by auto.c (backports24seq.h: FELUCCA_MICRO, FELUCCA_FILLS, FELUCCA_PLOCK).
 *
 * The data are the automation store's (auto.h, phase 3): a nudge is a step-only AUTO_NUDGE event (-32 early .. 31
 * late, in 1/64 of a step; auto_nudge keeps each step's for micro_units), a fill condition a step-only AUTO_FILL
 * event, a lock any other step-only event. SLOOP 2.4's record (stepx.h: micro[64], lock[24], fill[16]) is their
 * import / export form only. */

#if FELUCCA_MICRO
/* the nudge of grid step abs of track t, in units of a step slen long: where in its own step it fires
 * (micro >= 0), or how far before its step (micro < 0, as a negative number) */
static int32_t micro_units(const track_t *t, uint32_t abs, uint32_t slen)
{
    int32_t m = auto_nudge[trk_index(t) % NTRK][TRK_IDX(t, abs, trk_len(t)) % NSTEP];
    return (int32_t)(slen / 64u) * m;                    /* |m| <= 32: fits */
}
#endif

#if FELUCCA_FILLS
static volatile uint8_t fill_held;           /* GLO + key 9 down (the UI) */
static volatile uint8_t fill_arm;            /* GLO + key 10: the next bar is a fill (the UI; the ISR clears it) */
static uint8_t fill_bar_on;                  /* that bar, while it plays (the ISR) */
static uint8_t fill_now;                     /* this block is a fill: fill_held || fill_bar_on, read once a block */
static uint32_t fill_last_bar = 0xFFFFFFFFu; /* clk_beat / 4 of the last bar seen (events_block) */
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

#if FELUCCA_PLOCK
/* ---- parameter locks (SLOOP 2.4 seq.c p_lockable / lock_step / lock_set, isod89, GPL-3.0-only). A lock: on its step
 * the track's p[param] takes its value; the parameter goes back at the next step without a lock on it (Elektron
 * style: notes still ringing follow, the engines read p[] every block). The locks in force, their values to go back
 * to and the base under them are auto_base.c's (one base with the motion): a lock lets go to the base, or to what a
 * hold event set under it (motion.c motion_put); a knob turned while not recording lets it go at once (auto_knob_base)
 * and is the new patch. Beside motion recording a step's hold events apply first (auto_step), so a lock wins on its
 * step. Ids: ours (P_*) here, the stored ids in the list (auto.c mot_sid), 2.4's converted at import / export. What
 * may be locked: auto_base.c auto_param, the motion's list too. */

/* the range of p[id] on track t (params.c track_desc: the engine asked for, the drum track's kit and ACCENT) */
#define lock_desc track_desc
static void lock_write(track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = lock_desc(t, id);
    t->p[id % P_COUNT] = (int16_t)clamp(v, d->min, d->max);
}
static void locks_restore(track_t *t)        /* every lock in force let go (a cleared pattern) */
{
    uint32_t i, k = trk_index(t) % NTRK;
    for (i = 0; i < lk_n[k] && i < NLOCK; i++)
        lock_write(t, lk_param[k][i], lk_ret[k][i]);
    lk_n[k] = 0;
#if DLA_ON
    if (is_drum(t))
        dla_locks_off();                     /* (the lanes' locks too: drum_edit.c dla) */
#endif
}
/* the sequencer enters a step (auto_step, the ISR) whose step-only events set our ids lp[] to lv[] (m of them; a
 * fill-skipped step: none): they take hold, the last step's that it does not share let go */
static void lock_apply(track_t *t, const uint8_t *lp, const int8_t *lv, uint32_t m)
{
    uint32_t i, j, n = 0, k = trk_index(t) % NTRK;
    for (i = 0; i < lk_n[k] && i < NLOCK; i++) {     /* in force, no lock here: back */
        uint32_t p = lk_param[k][i] % P_COUNT, has = 0;
        for (j = 0; j < m; j++)
            has |= lp[j] == p;
        if (has)
            lk_param[k][n] = (uint8_t)p, lk_ret[k][n++] = lk_ret[k][i];
        else
            lock_write(t, p, lk_ret[k][i]);
    }
    lk_n[k] = (uint8_t)n;
    for (j = 0; j < m; j++) {                        /* this step's locks */
        uint32_t p = lp[j] % P_COUNT;
        for (i = 0; i < lk_n[k] && lk_param[k][i] != p; i++)
            ;
        if (i == lk_n[k]) {
            if (i >= NLOCK)
                continue;
            auto_take(t, k, p);                      /* (the base, the first time the automation changes it) */
            lk_param[k][i] = (uint8_t)p;
            lk_ret[k][i] = t->p[p];
            lk_n[k]++;
        }
        lock_write(t, p, lv[j]);
    }
}
/* set (or make) the step-only event of (step, our id param) at v, clamped to its range; 0 = the list is full or it is
 * not lockable. Callers hold the IRQ off */
static int lock_set(track_t *t, uint32_t step, uint32_t param, int32_t v)
{
    const param_desc_t *d;
    if (step >= NSTEP || !lock_ok(t, param))
        return 0;
    d = lock_desc(t, param);
    return auto_put(AL(t), step | AUTO_ONLY, mot_sid(param), clamp(v, d->min, d->max)) >= 0;
}
#endif
