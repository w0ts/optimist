/* SPDX-License-Identifier: GPL-3.0-only */
/* Slot-relative automation (builder item SLOT_AUTO, 2026-10-18, the user: "reference the modulation by track, the device
 * emplacements then param id ... the ids are not global and relative to the project"): a track's locks and motion on a
 * parameter of the device in its insert slot S1 / S2, by (slot, the device's parameter number), not by a global value id.
 * The parameter is the device's own (sa_vp: EQ3's LO MID HI FREQ L FREQ H today; a new device adds its own here), so a
 * device's settings need no track value of their own and a new device needs no new id. Only for a device value that is
 * not a track value; the ones that are keep their stored ids (auto.c MOT_TAIL, MOT_TAIL2). Included by auto.c (SLOT_AUTO).
 *   the id      auto.h AUTO_SLOTP(s, i): 0xDC + s x 16 + i, i < 16, in the track's own list (relative to the track)
 *   playback    a lock: its step, then back (to the hold's value or the base); a hold (motion): until the pass ends or
 *               STOP; the base is the device's value found the first time (sa_base), as motion_base for track values
 *   a swap      another type loaded into the slot (fx_slots.c ins_load): its events go and its values come back (ruled:
 *               cleared), so a parameter number never lands on another device's parameter
 * The ISR (auto_step: sa_event, sa_locks_off) and the main loop with the interrupts off change the masks. */
#define SA_N 32u                                             /* (2 slots x 16 parameters) */
#define SA_NI static __attribute__((noinline))            /* (out of line: inlined into each caller it cost ~4 KB) */
static int16_t sa_base[NTRK][SA_N], sa_hv[NTRK][SA_N];     /* the device's value before the automation, a hold's value */
static uint32_t sa_hm[NTRK], sa_lm[NTRK];                   /* the holds and the locks in force, bit s x 16 + i */

/* slot s's parameter i of track k: its value and descriptor (0: none there, or a track value: those keep their ids) */
SA_NI int16_t *sa_vp(uint32_t k, uint32_t s, uint32_t i, const param_desc_t **d)
{
    *d = 0;
    if (k >= NTRK || s >= 2u)
        return 0;
    if (ins_slot[k][s] == FXT_EQ3 && i < EQV_N) {          /* EQ3: LO MID HI FREQ L FREQ H (eq3_v, the track's own) */
        *d = &EQ_D[i];
        return &EQ3_V(k)[i];
    }
    return 0;
}
/* the id an event has for value pointer vp of track k's slots (the UIs' cards), -1 none: a slot's parameter whose value
 * is not a track value (those keep their stored ids) */
SA_NI int32_t sa_find(uint32_t k, const int16_t *vp)
{
    const param_desc_t *d;
    uint32_t s, i;
    if (k >= NTRK || (vp >= trk[k].p && vp < trk[k].p + P_COUNT))
        return -1;
    for (s = 0; s < 2u; s++)
        for (i = 0; i < 16u; i++)
            if (sa_vp(k, s, i, &d) == vp)
                return (int32_t)(s << 4 | i);
    return -1;
}
static void sa_take(uint32_t k, uint32_t b, const int16_t *vp)   /* its base, the first time the automation moves it */
{
    if (!(((sa_hm[k] | sa_lm[k]) >> b) & 1u))
        sa_base[k][b] = *vp;
}
/* track k's event e of the step entered (auto_step, the ISR); mo: its motion plays */
SA_NI void sa_event(uint32_t k, const auto_ev_t *e, uint32_t mo)
{
    const param_desc_t *d;
    uint32_t b = AUTO_SLOTP_B(e->param);
    int16_t *vp = sa_vp(k, b >> 4, b & 15u, &d);
    int32_t x;
    if (!vp)
        return;
    x = clamp(e->value, d->min, d->max);
    if (e->place & AUTO_ONLY) {
        sa_take(k, b, vp);
        sa_lm[k] |= 1u << b;
    } else if (mo) {
        sa_take(k, b, vp);
        sa_hm[k] |= 1u << b;
        sa_hv[k][b] = (int16_t)x;
    } else {
        return;
    }
    *vp = (int16_t)x;
}
/* the last step's locks let go (auto_step, the ISR): to the hold's value, else the base */
SA_NI void sa_locks_off(uint32_t k)
{
    const param_desc_t *d;
    uint32_t m = sa_lm[k], b;
    for (b = 0; m; b++, m >>= 1) {
        int16_t *vp;
        if (!(m & 1u) || !(vp = sa_vp(k, b >> 4, b & 15u, &d)))
            continue;
        *vp = (sa_hm[k] >> b) & 1u ? sa_hv[k][b] : sa_base[k][b];
    }
    sa_lm[k] = 0;
}
/* every value the automation moved back to its base, nothing in force (STOP, a pass's start, a load: as motion_restore) */
SA_NI void sa_restore(uint32_t k)
{
    const param_desc_t *d;
    uint32_t m = sa_hm[k % NTRK] | sa_lm[k % NTRK], b;
    for (b = 0; m; b++, m >>= 1) {
        int16_t *vp;
        if ((m & 1u) && (vp = sa_vp(k % NTRK, b >> 4, b & 15u, &d)) != 0)
            *vp = sa_base[k % NTRK][b];
    }
    sa_hm[k % NTRK] = sa_lm[k % NTRK] = 0;
}
/* another type into track k's slot s (fx_slots.c ins_load; the main loop): its values back, its events gone (ruled: a
 * swap clears). Before the type changes: the values are the old device's */
SA_NI void sa_slot_clear(uint32_t k, uint32_t s)
{
    auto_list_t *a;
    uint32_t i;
    if (k >= NTRK)
        return;
    fm1_irq_off();
    sa_restore(k);
    a = AUTO_L(k);
    for (i = a->n; i-- > 0u;)
        if (AUTO_IS_SLOTP(a->ev[i].param) && (AUTO_SLOTP_B(a->ev[i].param) >> 4) == (s & 1u))
            auto_del(a, i);
    fm1_irq_on();
}

/* ---- the UIs' calls (the main loop; the IRQ off where they say), as auto_lane.c's: b = s x 16 + i. One call site each
 * of auto.h's inline list helpers (each expands where it is called) */
#define SA_ID(b) AUTO_SLOTP((b) >> 4, (b) & 15u)
SA_NI int sa_get(const track_t *t, uint32_t place, uint32_t b, int32_t *x)   /* (idx % NSTEP, | AUTO_ONLY: a lock) */
{
    return auto_get(AL(t), place, SA_ID(b), x);
}
#define sa_lock_get(t, idx, b, x) sa_get(t, ((idx) % NSTEP) | AUTO_ONLY, b, x)
#define sa_hold_get(t, idx, b, x) sa_get(t, (idx) % NSTEP, b, x)
SA_NI int sa_put(track_t *t, uint32_t place, uint32_t b, int32_t x)   /* (callers hold the IRQ off) -> 1 done, 0 full */
{
    return auto_put(AL(t), place, SA_ID(b), x) >= 0;
}
#define sa_lock_set(t, idx, b, x) (FELUCCA_PLOCK && sa_put(t, ((idx) % NSTEP) | AUTO_ONLY, b, x))
/* the event of (from, b) to place to, its value kept (callers hold the IRQ off): 1 moved, 0 none */
SA_NI uint32_t sa_move(track_t *t, uint32_t from, uint32_t to, uint32_t b)
{
    auto_list_t *a = AL(t);
    int i = auto_find(a, from, SA_ID(b));
    int32_t x;
    if (i < 0)
        return 0;
    x = a->ev[i].value;
    auto_del(a, (uint32_t)i);
    if (to != ~0u)
        (void)sa_put(t, to, b, x);                  /* (a slot freed above: never full) */
    return 1;
}
#define sa_drop(t, idx, b) ((void)sa_move(t, ((idx) % NSTEP) | AUTO_ONLY, ~0u, b), (void)sa_move(t, (idx) % NSTEP, ~0u, b))
/* step-only <-> hold of (idx, b): 1 now a hold, 2 now step-only, 0 none (as lane_kind_toggle) */
SA_NI uint32_t sa_kind_toggle(track_t *t, uint32_t idx, uint32_t b)
{
    uint32_t s = idx % NSTEP;
    if (FELUCCA_MOTION && sa_move(t, s | AUTO_ONLY, s, b)) {
        auto_w.on |= (uint8_t)(1u << (trk_index(t) % NTRK));
        return 1;
    }
    return FELUCCA_PLOCK && sa_move(t, s, s | AUTO_ONLY, b) ? 2u : 0u;
}
/* a card set track k's slot value b to x from was (the main loop). Recording (playing, its REC bit): a hold event at the
 * step playing (as motion_knob), heard at once; not recording: the new value, kept (a lock or hold in force on it lets
 * go, as auto_knob_base). -> 1 recorded, 2 the list is full (motion_full), 0 not recording */
SA_NI int sa_knob(uint32_t k, uint32_t b, int32_t x, int32_t was)
{
    k %= NTRK, b %= SA_N;
#if FELUCCA_MOTION
    if (song.playing && ((song.rec >> k) & 1u)) {
        track_t *t = &trk[k];
        uint32_t into, slen, abs, idx;
        int rc = 1;
        abs = trk_grid(t, &into, &slen);
        idx = TRK_IDX(t, abs, trk_len(t));
        if (into > slen / 2u)
            idx = (idx + 1u) % trk_len(t);
        undo_mark(t, (t->pass << 2) | 1u);
        fm1_irq_off();
        if (!sa_put(t, idx % NSTEP, b, x)) {
            motion_full = 1;
            rc = 2;
        } else {
            auto_w.on |= (uint8_t)(1u << k);
            if (!(((sa_hm[k] | sa_lm[k]) >> b) & 1u))
                sa_base[k][b] = (int16_t)was;               /* (its base: the value before the card wrote it) */
            sa_hm[k] |= 1u << b;                            /* (heard at once: the card wrote the value) */
            sa_hv[k][b] = (int16_t)x;
        }
        fm1_irq_on();
        return rc;
    }
#endif
    (void)x, (void)was;
    fm1_irq_off();
    sa_hm[k] &= ~(1u << b);
    sa_lm[k] &= ~(1u << b);
    fm1_irq_on();
    return 0;
}
