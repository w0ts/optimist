/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' motion and locks: the calls both UIs (and the mixer's lane rows) make, on the drum track's list (auto.h
 * AUTO_LANE: lane l's value v, v a drum_edit.c DE_* index, TUNE .. LEVEL). Included by auto.c (DLA_ON). The main loop;
 * the IRQ off where they say so, as the other edits of the list (auto.c). Values are offsets as dl.ofs keeps them,
 * clamped to DE_MIN..DE_MAX. The user's rulings (2026-10-12): a lock needs its lane's hit on the step (D4: lane_lock_set
 * refuses LANE_NOHIT; a lane's hit removed: lane_step_drop); every hit of the lane during the step hears the lock (D5);
 * a lane's value is heard at its hits through the overlay, never written into dl.ofs (D2). */
#define LANE_NOHIT (-1)                  /* lane_lock_set: the lane has no hit on that step (nothing written) */
/* the lane values' ranges (drum_edit.c DE_MIN / DE_MAX; dsnd_desc.c DSD's first 8 show them): the editor's AUTO_SET
 * clamps an event's value to them (ed_stepx.c); tests/auto_int8_check.c checks they fit a byte and match */
static const param_desc_t DLA_DESC[DE_N] = {
    PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_BIPCT, -64, 63, 0), PD("SNAP", F_BIPCT, -64, 63, 0),
    PD("CLICK", F_BIPCT, -64, 63, 0), PD("BEND", F_SEMI, -24, 24, 0), PD("CUT", F_BIPCT, -64, 63, 0),
    PD("DRIVE", F_BIPCT, -64, 63, 0), PD("LEVEL", F_INT, -24, 6, 0)};

/* lane l hits on step idx of track t's pattern (the drum track only) */
static int lane_hits(const track_t *t, uint32_t idx, uint32_t l)
{
    return is_drum(t) && l < DRUM_LANES && ((dstep_mask(&t->dstep[idx % NSTEP]) >> l) & 1u);
}
static int lane_ok(const track_t *t, uint32_t l, uint32_t v) { return is_drum(t) && l < DRUM_LANES && v < DE_N; }

/* lane l's lock of value v on step idx: 1 there is one, its value into *x */
static int lane_lock_get(const track_t *t, uint32_t idx, uint32_t l, uint32_t v, int32_t *x)
{
    return lane_ok(t, l, v) && auto_get(AL(t), (idx % NSTEP) | AUTO_ONLY, AUTO_LANE(l, v), x);
}
/* ... set (or made) at x, clamped; callers hold the IRQ off (as seq24.c lock_set). -> 1 done, 0 the list is full or no
 * lock here (not the drum track, not a lane value, a build without locks), LANE_NOHIT the lane has no hit there */
static int lane_lock_set(track_t *t, uint32_t idx, uint32_t l, uint32_t v, int32_t x)
{
#if FELUCCA_PLOCK
    if (!lane_ok(t, l, v))
        return 0;
    if (!lane_hits(t, idx, l))
        return LANE_NOHIT;
    return auto_put(AL(t), (idx % NSTEP) | AUTO_ONLY, AUTO_LANE(l, v), clamp(x, DE_MIN[v], DE_MAX[v])) >= 0;
#else
    (void)t, (void)idx, (void)l, (void)v, (void)x;
    return 0;
#endif
}
/* ... gone (callers hold the IRQ off) */
static void lane_lock_drop(track_t *t, uint32_t idx, uint32_t l, uint32_t v)
{
    int i = lane_ok(t, l, v) ? auto_find(AL(t), (idx % NSTEP) | AUTO_ONLY, AUTO_LANE(l, v)) : -1;
    if (i >= 0)
        auto_del(AL(t), (uint32_t)i);
}
/* lane l's hold event (motion) of value v on step idx: 1 there is one, its value into *x */
static int lane_hold_get(const track_t *t, uint32_t idx, uint32_t l, uint32_t v, int32_t *x)
{
    return lane_ok(t, l, v) && auto_get(AL(t), idx % NSTEP, AUTO_LANE(l, v), x);
}
/* ... gone (callers hold the IRQ off); 1 there was one */
static int lane_hold_drop(track_t *t, uint32_t idx, uint32_t l, uint32_t v)
{
    int i = lane_ok(t, l, v) ? auto_find(AL(t), idx % NSTEP, AUTO_LANE(l, v)) : -1;
    if (i >= 0)
        auto_del(AL(t), (uint32_t)i);
    return i >= 0;
}
/* lane l's step-only events of step idx gone (its hit there removed: D4; callers hold the IRQ off) -> how many */
static uint32_t lane_step_drop(track_t *t, uint32_t idx, uint32_t l)
{
    auto_list_t *a = AL(t);
    uint32_t i, n = 0, k = 0, pl = (idx % NSTEP) | AUTO_ONLY;
    if (!is_drum(t) || l >= DRUM_LANES)
        return 0;
    for (i = 0; i < a->n; i++)
        if (a->ev[i].place == pl && AUTO_IS_LANE(a->ev[i].param) && AUTO_LANE_L(a->ev[i].param) == l)
            k++;
        else
            a->ev[n++] = a->ev[i];
    a->n = (uint8_t)n;
    return k;
}
/* the values v of lane l with an event of the kinds which (AUTO_HOLDS, AUTO_ONLYS) on step idx: bit v (the marks) */
static uint32_t lane_step_values(const track_t *t, uint32_t idx, uint32_t l, uint32_t which)
{
    const auto_list_t *a = AL(t);
    uint32_t i, m = 0, s = idx % NSTEP;
    if (!is_drum(t))
        return 0;
    for (i = 0; i < a->n && i < AUTO_MAX; i++) {
        const auto_ev_t *e = &a->ev[i];
        if ((e->place & AUTO_STEP) == s && AUTO_IS_LANE(e->param) && AUTO_LANE_L(e->param) == l &&
            (which & (e->place & AUTO_ONLY ? AUTO_ONLYS : AUTO_HOLDS)))
            m |= 1u << AUTO_LANE_V(e->param);
    }
    return m;
}
/* the event of lane l's value v on step idx changes kind (YES with a step held and the lane's cell hot): a lock becomes
 * a hold event (its PLAY bit on), a hold event a lock (the lane must hit there: D4); one of each there: the lock goes,
 * the hold takes its value. -> 1 now a hold, 2 now a lock, 0 none there or the other kind not allowed (IRQ off) */
static uint32_t lane_kind_toggle(track_t *t, uint32_t idx, uint32_t l, uint32_t v)
{
    auto_list_t *a = AL(t);
    uint32_t s = idx % NSTEP, sid = AUTO_LANE(l, v);
    int o, h;
    int32_t x;
    if (!lane_ok(t, l, v))
        return 0;
    o = auto_find(a, s | AUTO_ONLY, sid), h = auto_find(a, s, sid);
    if (o >= 0) {
#if FELUCCA_MOTION
        x = a->ev[o].value;
        auto_del(a, (uint32_t)o);
        (void)auto_put(a, s, sid, x);                    /* (a slot freed above: never full) */
        auto_w.on |= (uint8_t)(1u << TRK_DRUM);
        return 1;
#else
        return 0;
#endif
    }
    if (h >= 0) {
#if FELUCCA_PLOCK
        if (!lane_hits(t, s, l))
            return 0;
        x = a->ev[h].value;
        auto_del(a, (uint32_t)h);
        (void)auto_put(a, s | AUTO_ONLY, sid, x);
        return 2;
#else
        return 0;
#endif
    }
    (void)x;
    return 0;
}

/* the value lane l's hits hear for v now, from the automation (a lock of the step playing, else a hold in force), else
 * the patch's (the cards show the patch: this is for a "heard" mark) */
static int32_t lane_heard(uint32_t l, uint32_t v)
{
    if (l >= DRUM_LANES || v >= DE_N)
        return 0;
    if ((dla.lm[l] >> v) & 1u)
        return dla.lv[l][v];
    if ((dla.hm[l] >> v) & 1u)
        return dla.hv[l][v];
    return dl.ofs[l][v];
}

#if FELUCCA_MOTION
/* does the drum track's motion set lane l's value v (PLAY on, a hold event of it)? The SOUND page's mark (as
 * motion_drives). Main loop, at most AUTO_MAX events */
static int lane_motion_drives(uint32_t l, uint32_t v)
{
    const auto_list_t *a = AUTO_L(TRK_DRUM);
    uint32_t i, sid = AUTO_LANE(l, v);
    if (!((auto_w.on >> TRK_DRUM) & 1u))
        return 0;
    for (i = 0; i < a->n; i++)
        if (!(a->ev[i].place & AUTO_ONLY) && a->ev[i].param == sid)
            return 1;
    return 0;
}
/* a lane knob set lane l's value v to x (both UIs' SOUND pages, the mixer's lane rows; the main loop). The drum track
 * recording (playing, its REC bit): a hold event at the step playing (the next past its half, as motion_knob), heard
 * at once through the overlay (not dl.ofs: STOP puts the patch back), undone with the pass (undo.c UNDO_REC). Not
 * recording: a hold in force on (l, v) lets go, so the knob is heard; the caller sets the patch dl.ofs as it did.
 * -> 0 not recording (the caller writes the patch), 1 recorded, 2 the list is full (motion_full), 3 not a lane value */
static int lane_knob(uint32_t l, uint32_t v, int32_t x)
{
    track_t *t = TDRUM;
    uint32_t into, slen, abs, idx, rc = 1;
    if (l >= DRUM_LANES || v >= DE_N)
        return 3;
    x = clamp(x, DE_MIN[v], DE_MAX[v]);
    if (!song.playing || !((song.rec >> TRK_DRUM) & 1u)) {
        fm1_irq_off();
        dla.hm[l] &= (uint8_t)~(1u << v);
        fm1_irq_on();
        return 0;
    }
    abs = trk_grid(t, &into, &slen);
    idx = TRK_IDX(t, abs, trk_len(t));
    if (into > slen / 2u)
        idx = (idx + 1u) % trk_len(t);
    undo_mark(t, (t->pass << 2) | 1u);                       /* (undo.c UNDO_REC: the list is in the pass's level) */
    fm1_irq_off();
    if (auto_put(AL(t), idx % NSTEP, AUTO_LANE(l, v), x) < 0) {
        motion_full = 1;
        rc = 2;
    } else {
        auto_w.on |= (uint8_t)(1u << TRK_DRUM);
        dla.hv[l][v] = (int8_t)x;                            /* (heard at once, until the pass's end) */
        dla.hm[l] |= (uint8_t)(1u << v);
        dla.any |= 1u;
    }
    fm1_irq_on();
    return (int)rc;
}
#endif
