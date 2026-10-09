/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP (docs/UI-OPTIMIST-DESIGN.md section 4.2, phase 2): one screen for drums and synths. SEQ tapped opens it; the
 * 16 white keys are the 16 steps of the window with nothing held (the ISR's ly_lock LY_STEP: seq.c sends the keys
 * to lk_q, they play nothing). The window is 1-16, 17-32, 33-48, 49-64 up to the track's LEN; it follows the
 * playhead while playing (FOLLOW, the default); HOME + OCT- / OCT+ scroll it and stop the follow, HOME + OCT- +
 * OCT+ together resumes it. OCT alone keeps its job (the octave; on the drum track ghost / hard, which a tapped drum
 * step takes too).
 *   a step key tapped     an empty step: set with the pick; a set step: cleared (when let go, unless edited)
 *   a step key held       the cards are the step's: drums LEVEL RATCHET, synths NOTE LEVEL RATCHET LENGTH (its
 *                         ties); several held edit together. SELECT the nudge (FELUCCA_MICRO), PRESETS the chance
 *                         (drum and synth steps, FELUCCA_CHANCE: an event), SAVE the fill condition (FELUCCA_FILLS),
 *                         HOME the clear
 *   + a page button       ENV LFO FX SCL ARP: that page's cells become the steps' events (FELUCCA_PLOCK; the
 *                         automation store, seq/auto.h): a turn writes a step-only event (a lock; a hold event there:
 *                         its value), a cell with one shows a mark (a padlock; a hold event: an arrow); YES toggles
 *                         the hot cell's between step-only and HOLD; HOME + the knob clears it; the button again: the
 *                         family's next page, stopping at the last
 *   HOME held + a step    the step cleared: its notes and every event of it (locks, motion, nudge, fill, chance)
 *   SEQ held + keys       the pick: the keys play (ly_lock is let go while SEQ is down); a drum key picks the lane
 *                         (lane_sel), a synth's notes become the next steps' (the core's pen: seq.c key_down)
 *   SEQ tapped again      the keys play with the screen up (the window keeps following); again: steps
 * With no step held the cards are the drum lane's LEVEL TUNE DECAY REV (SELECT the lane) or the synth track's
 * PATTERN and ARP rows. Every step key pressed opens an undo level (undo_mark), as SLOOP's step entry. The logic
 * of the edits is SLOOP's (ui/sloop/ui_layers.c step_down / steps_held_*), copied: nothing in ui/sloop is called. */
#define STEP_LY_BIT (1u << 31)          /* seq.c ly_bit[LY_STEP]: no button holds it; STEP locks the layer (ly_lock) */

static struct {
    uint8_t page;                       /* the window: steps 16 page + 1 .. 16 page + 16 */
    uint8_t follow;                     /* the window follows the playhead while playing */
    uint8_t play;                       /* SEQ tapped on STEP: the keys play, the screen stays */
    uint8_t lock_pg;                    /* a page button with a step held: that PAGES row as the steps' locks */
    uint16_t held;                      /* the step keys down (white key w: step 16 page + w) */
    uint16_t pend;                      /* set steps tapped: cleared when let go, unless edited meanwhile */
    uint32_t sess;                      /* the undo session of the step key pressed last */
    uint8_t ph_drawn;                   /* the playhead strip as drawn (op_stepdraw.c) */
} st = {.follow = 1, .lock_pg = 0xFF, .ph_drawn = 0xFF};
#define LOCK_NONE 0xFFu

static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};   /* the levels, softest first */
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }
static int step_on(const step_t *s) { return s->time == ST_NOTE && s->n; }
static void step_clear(step_t *s)
{
    memset(s, 0, sizeof *s);
    s->time = ST_REST;
}
static uint32_t step_pages(const track_t *t) { return (trk_len(t) + 15u) / 16u; }

/* the keys are steps now: STEP shown, not toggled to playing, SEQ not held (the pick) */
static int step_keys(uint32_t held)
{
    return ui.scr == SCR_STEP && !st.play && !(held & (1u << panel.btn[B_SEQ]));
}

/* ---- the window */
static void step_tick(void)                             /* once a frame: FOLLOW, the window inside LEN */
{
    const track_t *t = TSEL;
    uint32_t n = step_pages(t);
    if (st.follow && song.playing && !st.held)
        st.page = (uint8_t)((t->seq_idx % trk_len(t)) / 16u);
    if (st.page >= n)
        st.page = (uint8_t)(n - 1u);
}
static void step_scroll(int32_t d)                      /* HOME + OCT- / OCT+ */
{
    st.page = (uint8_t)clamp((int32_t)st.page + d, 0, (int32_t)step_pages(TSEL) - 1);
    st.follow = 0;
}
static void step_reset(void)                            /* STEP entered or left: nothing held, the keys are steps */
{
    st.held = st.pend = 0;
    st.play = 0;
    st.lock_pg = LOCK_NONE;
}

/* ---- the pick: what a tapped step takes (drums the selected lane, synths the core's pen: the notes played last) */
static void note_name(char *b, uint32_t n)              /* "C#4" (b holds 5) */
{
    str_cpy(b, N_NOTE[n % 12u], 3);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}
static void step_pick_name(char *b, uint32_t n)         /* "KICK", "C4 E4+" (b holds n >= 14) */
{
    uint32_t i, k = pen_n ? pen_n : 1u;
    if (is_drum(TSEL)) {
        str_cpy(b, LANE_NAME[lane_selected()], n);
        return;
    }
    b[0] = 0;
    for (i = 0; i < k && i < 2u; i++) {
        if (i)
            str_cpy(b + str_len(b), " ", n - str_len(b));
        note_name(b + str_len(b), pen_n ? pen_note[i] : last_note);
    }
    if (k > 2u)
        str_cpy(b + str_len(b), "+", n - str_len(b));
}

/* ---- set and clear (callers hold the IRQ off where they say so) */
static uint32_t step_note_len(const track_t *t, uint32_t start)   /* a note and its ties, in steps */
{
    uint32_t len = trk_len(t), n = 1;
    while (n < len && t->step[(start + n) % len].time == ST_TIE)
        n++;
    return n;
}
/* the length of the note at start d steps longer / shorter, as TIE steps after it (from Melodee 0dbe626, Kerem
 * Kilic, as SLOOP's ui_layers.c): longer through empty steps up to the next note or tie, shorter clears its ties */
static void step_note_resize(track_t *t, uint32_t start, int32_t d)
{
    uint32_t len = trk_len(t), old = step_note_len(t, start), limit = old, n, i;
    while (limit < len) {
        const step_t *s = &t->step[(start + limit) % len];
        if (step_on(s) || s->time == ST_TIE || t->step[(start + limit + 1u) % len].time == ST_TIE)
            break;
        limit++;
    }
    n = (uint32_t)clamp((int32_t)old + d, 1, (int32_t)limit);
    for (i = n; i < old; i++)
        step_clear(&t->step[(start + i) % len]);
    for (i = old; i < n; i++) {
        step_clear(&t->step[(start + i) % len]);
        t->step[(start + i) % len].time = ST_TIE;
    }
}
static void step_set(track_t *t, uint32_t idx)          /* an empty step takes the pick (IRQ off) */
{
    if (is_drum(t)) {                                   /* (OCT- / OCT+ held: a ghost / hard hit) */
        uint32_t lv = fm1_in.buttons & dyn_bit[0] ? LV_GHOST : fm1_in.buttons & dyn_bit[1] ? LV_HARD : LV_NORM;
        dstep_set(&t->dstep[idx], lane_selected(), lv, 0);
    } else {
        step_t *s = &t->step[idx];
        uint32_t i, n = pen_n ? pen_n : 1u;
        memset(s, 0, sizeof *s);
        for (i = 0; i < n && i < 4u; i++)
            s->note[i] = pen_n ? pen_note[i] : last_note;
        s->n = (uint8_t)(t->p[P_VOICE] == V_POLY ? n : 1u);
        s->time = ST_NOTE;
        s->vel = 100;
    }
    t->seq_active = 1;
}
/* step idx cleared (IRQ off): all = the whole step (HOME), else the drum lane's hit or the synth note; a synth
 * note's ties go with it; a step left empty keeps no nudge, lock or fill */
static void step_wipe(track_t *t, uint32_t idx, uint32_t all)
{
    if (is_drum(t)) {
        dstep_t *d = &t->dstep[idx];
        if (all)
            memset(d, 0, sizeof *d);
        else
            dstep_clr(d, lane_selected());
        if (dstep_mask(d))
            return;
    } else {
        if (step_on(&t->step[idx]))
            step_note_resize(t, idx, -(int32_t)NSTEP);
        step_clear(&t->step[idx]);
    }
#if FELUCCA_AUTO
    (void)auto_step_clear(t, idx, all ? AUTO_HOLDS | AUTO_ONLYS : AUTO_ONLYS);   /* (HOME: its motion too) */
#endif
}
static int step_has(const track_t *t, uint32_t idx)     /* a tap would clear it: the lane's hit, the note */
{
    return is_drum(t) ? dstep_has(&t->dstep[idx], lane_selected()) : step_on(&t->step[idx]);
}

/* a step key down / up (seq.c lk_q, LY_STEP): white key w is step 16 page + w; black keys do nothing here */
static void step_key(uint32_t k, uint32_t down, uint32_t held)
{
    track_t *t = TSEL;
    int32_t w = punch_key(k);
    uint32_t idx, bit;
    if (w < 0 || w > 15)
        return;
    bit = 1u << w;
    idx = st.page * 16u + (uint32_t)w;
    if (!down) {
        if (!(st.held & bit))
            return;
        st.held &= (uint16_t)~bit;
        if ((st.pend & bit) && idx < trk_len(t) && step_has(t, idx)) {   /* tapped: cleared */
            undo_mark(t, st.sess);
            fm1_irq_off();
            step_wipe(t, idx, 0);
            fm1_irq_on();
            sync_reload = 1;
        }
        st.pend &= (uint16_t)~bit;
        if (!st.held)
            st.lock_pg = LOCK_NONE;                     /* (the last step let go: the locks' page goes) */
        return;
    }
    if (ui.scr != SCR_STEP || op_armed() || idx >= trk_len(t))
        return;                                         /* (a question waits: the keys do nothing) */
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    st.sess = (undo_sess += 4u) | 3u;                   /* every step key: an undo level */
    if (held & (1u << panel.btn[B_HOME])) {             /* HOME + the step: cleared, everything of it */
        undo_mark(t, st.sess);
        fm1_irq_off();
        step_wipe(t, idx, 1);
        fm1_irq_on();
        sync_reload = 1;
        return;
    }
    st.held |= (uint16_t)bit;
    if (step_has(t, idx)) {
        st.pend |= (uint16_t)bit;                       /* set: cleared when let go, unless edited */
        return;
    }
    undo_mark(t, st.sess);
    fm1_irq_off();
    step_set(t, idx);
    fm1_irq_on();
    sync_reload = 1;
}
/* the steps held, one by one: f(t, idx) for each within LEN (IRQ off, one undo level, edited: kept when let go) */
static void held_each(void (*f)(track_t *t, uint32_t idx, int32_t s), int32_t s)
{
    track_t *t = TSEL;
    uint32_t w;
    undo_mark(t, st.sess);
    st.pend &= (uint16_t)~st.held;
    fm1_irq_off();
    for (w = 0; w < 16u; w++)
        if (((st.held >> w) & 1u) && st.page * 16u + w < trk_len(t))
            f(t, st.page * 16u + w, s);
    fm1_irq_on();
    sync_reload = 1;
}
static int32_t held_first(void)                         /* the first step held (its values on the cards), -1 none */
{
    uint32_t w;
    for (w = 0; w < 16u; w++)
        if (((st.held >> w) & 1u) && st.page * 16u + w < trk_len(TSEL))
            return (int32_t)(st.page * 16u + w);
    return -1;
}
static void held_clear(void)                            /* a step held, HOME tapped: the steps cleared */
{
    uint32_t w, n = 0;
    track_t *t = TSEL;
    undo_mark(t, st.sess);
    fm1_irq_off();
    for (w = 0; w < 16u; w++)
        if (((st.held >> w) & 1u) && st.page * 16u + w < trk_len(t))
            step_wipe(t, st.page * 16u + w, 1), n++;
    fm1_irq_on();
    st.pend = 0;
    sync_reload = 1;
    ui_message(n > 1u ? "STEPS CLEARED" : "STEP CLEARED");
}

/* ---- the held steps' values: KNOB k (drums: 0 LEVEL, 1 RATCHET; synths: 0 NOTE, 1 LEVEL, 2 RATCHET, 3 LENGTH);
 * s OP_RESET: back to normal (NORM, x1, one step) */
static void edit_drum(track_t *t, uint32_t idx, int32_t s)
{
    dstep_t *d = &t->dstep[idx];
    uint32_t l = lane_selected(), lv = dstep_lvl(d, l), rt = dstep_rat(d, l), k = (uint32_t)s >> 16;
    s = (int16_t)(s & 0xFFFF);
    if (!dstep_has(d, l))
        return;
    if (k == 0u)
        lv = s == OP_RESET ? LV_NORM : LV_UP[clamp((int32_t)lvl_rank(lv) + s, 0, 3)];
    else
        rt = s == OP_RESET ? 0u : (uint32_t)clamp((int32_t)rt + s, 0, 3);
    dstep_set(d, l, lv, rt);
}
static void edit_synth(track_t *t, uint32_t idx, int32_t s)
{
    step_t *st_ = &t->step[idx];
    uint32_t i, k = (uint32_t)s >> 16;
    s = (int16_t)(s & 0xFFFF);
    if (!step_on(st_))
        return;
    if (k == 3u) {
        step_note_resize(t, idx, s == OP_RESET ? -(int32_t)NSTEP : s);
        return;
    }
    for (i = 0; i < st_->n && i < 4u; i++) {
        uint32_t sh = 2u * i, lv = (st_->lvl >> sh) & 3u, rt = (st_->rat >> sh) & 3u;
        if (k == 0u) {
            if (s != OP_RESET)
                st_->note[i] = (uint8_t)clamp(st_->note[i] + s, 0, 127);
            continue;
        }
        if (k == 1u)
            lv = s == OP_RESET ? LV_NORM : LV_UP[clamp((int32_t)lvl_rank(lv) + s, 0, 3)];
        else
            rt = s == OP_RESET ? 0u : (uint32_t)clamp((int32_t)rt + s, 0, 3);
        st_->lvl = (uint8_t)((st_->lvl & ~(3u << sh)) | lv << sh);
        st_->rat = (uint8_t)((st_->rat & ~(3u << sh)) | rt << sh);
    }
}
static void held_edit(uint32_t k, int32_t s)
{
    if (s != OP_RESET)
        s = clamp(s, -16, 16);
    held_each(is_drum(TSEL) ? edit_drum : edit_synth, (int32_t)(k << 16) | (uint16_t)(int16_t)s);
}

/* SELECT, PRESETS and SAVE with a step held: the nudge, the chance, the fill condition */
#if FELUCCA_MICRO
static void edit_nudge(track_t *t, uint32_t idx, int32_t s)
{
    (void)step_micro_set(t, idx, s == OP_RESET ? 0 : step_micro(t, idx) + s);
}
#endif
#if FELUCCA_CHANCE
/* a step's chance (0..100 %): its event, else (a synth step) its chance bits (chance.c) */
static uint32_t step_chance_of(const track_t *t, uint32_t idx)
{
    uint32_t c = step_chance_ev(t, idx);
    return c < 100u || is_drum(t) ? c : step_chance(&t->step[idx % NSTEP]);
}
/* the chance as an event of the automation store, on drum and synth steps alike (this UI writes events only) */
static void edit_chance(track_t *t, uint32_t idx, int32_t s)
{
    if (step_has(t, idx) || (is_drum(t) && dstep_mask(&t->dstep[idx % NSTEP])))
        (void)step_chance_set(t, idx, s == OP_RESET ? 100u : (uint32_t)clamp((int32_t)step_chance_of(t, idx) + s * (int32_t)CH_STEP, 0, 100));
}
#endif
#if FELUCCA_FILLS
static void edit_fill(track_t *t, uint32_t idx, int32_t v) { step_fill_set(t, idx, (uint32_t)v); }
static void held_fill(void)                             /* normal -> fill only -> no fill, the first step's next */
{
    static const char *const MSG[3] = {"FILL: NORMAL", "FILL ONLY", "NO FILL"};
    int32_t i = held_first();
    uint32_t v;
    if (i < 0)
        return;
    v = (step_fill(TSEL, (uint32_t)i) + 1u) % 3u;
    held_each(edit_fill, (int32_t)v);
    ui_message(MSG[v]);
}
#endif

/* ---- locks: a page button with a step held makes that page's cells the steps' events (the automation store) */
static void step_lock_page(uint32_t fam)                /* the family's first page shown on this track, again: its next */
{
#if FELUCCA_PLOCK
    uint32_t i, first = LOCK_NONE, next = LOCK_NONE;
    for (i = 0; i < NPAGES; i++) {
        if (PAGES[i].fam != fam || !sound_page(&PAGES[i]) || PAGES[i].scope == SC_DSND)
            continue;
        if (first == LOCK_NONE)
            first = i;
        if (st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].fam == fam && i > st.lock_pg && next == LOCK_NONE)
            next = i;
    }
    if (next == LOCK_NONE && st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].fam == fam)
        next = st.lock_pg;                              /* (the last page: stays) */
    st.lock_pg = (uint8_t)(next != LOCK_NONE ? next : first);
    if (first == LOCK_NONE)
        ui_message("NO LOCKS ON THIS PAGE");
    ui.hot = 0;
    ui.hot_lit = 0;
#else
    (void)fam;
    ui_message("LOCKS NOT IN THIS BUILD");
#endif
}
#if FELUCCA_PLOCK
/* page pg's cell k as a lockable value of the selected track: its P_* id and descriptor, -1 when it is not one */
static int32_t lock_param(const page_t *pg, uint32_t k, const param_desc_t **d)
{
    int16_t *vp;
    *d = page_desc(pg, k, &vp);
    if (!*d || !vp || vp < TSEL->p || vp >= TSEL->p + P_COUNT || PARAM_HIDDEN(*d) || is_go(*d) || (*d)->max <= (*d)->min)
        return -1;
    return lock_ok(TSEL, (uint32_t)(vp - TSEL->p)) ? (int32_t)(vp - TSEL->p) : -1;
}
/* the first step held's event of cell k: 1 step-only (a lock), 2 a hold event (motion), 0 none; its value into *v */
static uint32_t lock_kind(uint32_t id, int32_t i, int32_t *v)
{
    return lock_get(TSEL, (uint32_t)i, id, v) ? 1u : hold_get(TSEL, (uint32_t)i, id, v) ? 2u : 0u;
}
static void lock_cell(uint32_t k, cell_t *c)            /* the event of the first step held, else the track's value */
{
    const param_desc_t *d;
    int32_t id = lock_param(&PAGES[st.lock_pg], k, &d), i = held_first(), v;
    uint32_t q;
    cell_clear(c);
    if (!d)
        return;
    c->label = d->label;
    if (id < 0 || i < 0) {
        str_cpy(c->val, "-", sizeof c->val);            /* (not a lockable value: the mixer's, a GO button) */
        return;
    }
    if ((q = lock_kind((uint32_t)id, i, &v)) == 0)
        v = TSEL->p[id];
    c->kind = CK_VAL;
    c->d = d;
    c->vp = &TSEL->p[id];
    c->mark = (uint8_t)q;                               /* (op_draw.c: 1 a padlock, 2 a hold's arrow) */
    c->col = q == 1u ? C_WARN : q ? C_AMB : 0;
    param_format(d, v, c->val, &c->unit);
    cell_gauge(c, d->fmt == F_ENUM || d->fmt == F_ONOFF, d->min, d->max, v);
}
/* a turn writes the step-only event on every step held (a hold event alone there: its value); OP_RESET (HOME + the
 * knob) drops the step's events of the cell, both kinds */
static void lock_turn(uint32_t k, int32_t s, int fine)
{
    const param_desc_t *d;
    track_t *t = TSEL;
    int32_t id = lock_param(&PAGES[st.lock_pg], k, &d), step;
    uint32_t w, full = 0;
    if (id < 0) {
        if (d)
            ui_message("NOT LOCKABLE");
        return;
    }
    step = fine || s == OP_RESET ? s : accel(EN_K1 + k, s, accel_range(d));
    st.pend &= (uint16_t)~st.held;
    undo_mark(t, st.sess);                              /* (a lock written or dropped: undone with the step, undo.c) */
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = st.page * 16u + w;
        int32_t v;
        if (!((st.held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (s == OP_RESET) {
            lock_drop(t, idx, (uint32_t)id);
            (void)auto_step_hold_drop(t, idx, (uint32_t)id);
            continue;
        }
        if (!lock_get(t, idx, (uint32_t)id, &v) && hold_get(t, idx, (uint32_t)id, &v)) {
            (void)auto_put(AL(t), idx, mot_sid((uint32_t)id), param_step(d, v, step));   /* (the hold event's value) */
            continue;
        }
        if (!lock_get(t, idx, (uint32_t)id, &v))
            v = t->p[id];
        full |= !lock_set(t, idx, (uint32_t)id, param_step(d, v, step));
    }
    fm1_irq_on();
    sync_reload = 1;
    if (full)
        ui_message("NO LOCK LEFT");
}
#endif
/* YES with a step held: the lock page's hot cell, its event's kind on every step held (step-only <-> HOLD); without
 * a lock page, or no event there: the fill condition (FELUCCA_FILLS) */
static void held_yes(void)
{
#if FELUCCA_PLOCK
    const param_desc_t *d;
    int32_t id, v, i = held_first();
    if (st.lock_pg != LOCK_NONE && i >= 0 && (id = lock_param(&PAGES[st.lock_pg], ui.hot & 3u, &d)) >= 0 &&
        lock_kind((uint32_t)id, i, &v)) {
        track_t *t = TSEL;
        uint32_t w, r = 0;
        undo_mark(t, st.sess);
        st.pend &= (uint16_t)~st.held;
        fm1_irq_off();
        for (w = 0; w < 16u; w++)
            if (((st.held >> w) & 1u) && st.page * 16u + w < trk_len(t))
                r |= auto_kind_toggle(t, st.page * 16u + w, (uint32_t)id);
        fm1_irq_on();
        sync_reload = 1;
        ui_message(r & 1u ? "HOLD: UNTIL THE NEXT" : r & 2u ? "THIS STEP ONLY" : "NO HOLD HERE");
        return;
    }
#endif
#if FELUCCA_FILLS
    held_fill();
#endif
}

/* ---- the screen: rows, cells, turns, YES */
#if DL_UI
static const page_t STEP_LANE_PG = {"LANE", FAM_EDIT, SC_DSND, GR_DSND, {DE_LEVEL, DE_TUNE, DE_DECAY, 16}};   /* 16: REV */
#endif
static uint8_t stp_ix[OP_MAXROWS];
static int step_row_page(const page_t *pg)              /* a synth track's rows on STEP: PATTERN, ARP, ARP 2 */
{
    return sound_page(pg) && ((pg->fam == FAM_SEQ && pg->scope == SC_TRACK) || pg->fam == FAM_ARP);
}
/* STEP's pages (the user: "the first SEQ menu should have the length of the pattern"): PATTERN first (LEN DIV SWING
 * GATE) on every track, then a synth track's ARP, ARP 2, the drum track's 16 lanes (STP_LANE0 + l: the lane's sound,
 * LEVEL TUNE DECAY REV; SELECT walks them and selects the lane: op_input.c op_row_pick). STEP opens on PATTERN */
#define STP_LANE0 1u
static uint32_t stp_pattern(void)                       /* the PATTERN page (FAM_SEQ, the track's: LEN first) */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_SEQ && PAGES[i].scope == SC_TRACK && PAGES[i].id[0] == P_SLEN)
            return i;
    return 0;
}
static uint32_t step_rows(void)
{
    uint32_t n, i, p;
    if (is_drum(TSEL))
        return STP_LANE0 + DRUM_LANES;
    n = page_rows(step_row_page, stp_ix);
    for (i = 0, p = stp_pattern(); i < n && stp_ix[i] != p; i++)
        ;
    for (; i > 0 && i < n; i--) {                       /* (PATTERN to the front, the others in their order) */
        uint8_t t = stp_ix[i - 1u];
        stp_ix[i - 1u] = stp_ix[i];
        stp_ix[i] = t;
    }
    return n;
}
static int stp_is_lane(uint32_t r) { return is_drum(TSEL) && r >= STP_LANE0; }
static void step_name(uint32_t r, char *b)
{
    uint32_t i;
    if (!is_drum(TSEL) || r < STP_LANE0) {
        str_cpy(b, is_drum(TSEL) ? PAGES[stp_pattern()].title : PAGES[stp_ix[r % OP_MAXROWS]].title, 12);
        return;
    }
    str_cpy(b, LANE_SHORT[(r - STP_LANE0) % DRUM_LANES], 12);   /* "c.hat" -> "C.HAT": as a row's name */
    for (i = 0; b[i]; i++)
        b[i] = (char)(b[i] >= 'a' && b[i] <= 'z' ? b[i] - 32 : b[i]);
}
static void held_cell(uint32_t k, cell_t *c)            /* the first step held: its values */
{
    static const char *const LV_NAME[4] = {"GHOST", "SOFT", "NORM", "HARD"};
    static const char *const RT_NAME[4] = {"x1", "x2", "x3", "x4"};
    const track_t *t = TSEL;
    int32_t i = held_first();
    uint32_t lv, rt;
    cell_clear(c);
    if (i < 0 || (is_drum(t) && k > 1u))
        return;                                         /* (the drum step: LEVEL RATCHET - -) */
    c->kind = CK_VAL;
    if (is_drum(t)) {
        const dstep_t *d = &t->dstep[i];
        lv = dstep_lvl(d, lane_selected());
        rt = dstep_rat(d, lane_selected());
        if (!dstep_has(d, lane_selected()))
            c->kind = CK_NONE;
    } else {
        const step_t *s = &t->step[i];
        lv = s->lvl & 3u;
        rt = s->rat & 3u;
        if (!step_on(s))
            c->kind = CK_NONE;
        if (k == 0u) {
            c->label = "NOTE";
            note_name(c->val, s->note[0]);
            if (s->n > 1u)
                str_cpy(c->val + str_len(c->val), "+", sizeof c->val - str_len(c->val));
            cell_gauge(c, 0, 0, 127, s->note[0]);
            return;
        }
        if (k == 3u) {
            uint32_t n = step_note_len(t, (uint32_t)i);
            c->label = "LENGTH";
            fmt_int(c->val, (int32_t)n);
            c->unit = "ST";
            cell_gauge(c, 0, 0, (int32_t)trk_len(t), (int32_t)n);
            return;
        }
        k--;
    }
    if (c->kind == CK_NONE) {
        str_cpy(c->val, "-", sizeof c->val);
        return;
    }
    c->label = k ? "RATCHET" : "LEVEL";
    str_cpy(c->val, k ? RT_NAME[rt] : LV_NAME[lvl_rank(lv)], sizeof c->val);
    cell_gauge(c, 1, 0, 3, (int32_t)(k ? rt : lvl_rank(lv)));
}
static void step_cell(uint32_t r, uint32_t k, cell_t *c)
{
    if (st.held) {
#if FELUCCA_PLOCK
        if (st.lock_pg != LOCK_NONE) {
            lock_cell(k, c);
            return;
        }
#endif
        held_cell(k, c);
        return;
    }
    if (!stp_is_lane(r)) {
        page_cell(&PAGES[is_drum(TSEL) ? stp_pattern() : stp_ix[r % OP_MAXROWS]], k, c);
        return;
    }
#if DL_UI
    {
        int16_t *vp;
        cell_param(c, dsnd_desc_lane((r - STP_LANE0) % DRUM_LANES, STEP_LANE_PG.id[k & 3u], &vp), vp);
        if (!c->label)                                  /* (a value this lane's sound has not: "-") */
            str_cpy(c->val, "-", sizeof c->val);
    }
#else
    cell_clear(c);
#endif
}
static void step_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (st.held) {
#if FELUCCA_PLOCK
        if (st.lock_pg != LOCK_NONE) {
            lock_turn(k, s, fine);
            return;
        }
#endif
        held_edit(k, s);
        return;
    }
    if (!stp_is_lane(r))
        page_turn(&PAGES[is_drum(TSEL) ? stp_pattern() : stp_ix[r % OP_MAXROWS]], k, s, fine);
#if DL_UI
    else
        page_turn(&STEP_LANE_PG, k, s, fine);           /* (the row is the selected lane: op_input.c op_row_pick) */
#endif
}
static int step_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t i, n;
    if (!stp_is_lane(r))
        return page_yes(SCR_STEP, r, &PAGES[is_drum(TSEL) ? stp_pattern() : stp_ix[r % OP_MAXROWS]], k, ok);
    op_enter(SCR_SOUND);                                /* the drum lane: its SOUND rows */
    for (i = 1, n = snd_rows(); i < n; i++)
        if (snd_page(i)->scope == SC_DSND) {
            ui.row[SCR_SOUND] = (uint8_t)i;
            break;
        }
    return 1;
}

/* ---- the panel's controls on STEP (op_input.c calls these first; 1 = done) */
static int step_knobs(void)                             /* a step held: SELECT the nudge, PRESETS the chance; 1 turned */
{
    int32_t s, turned = 0;
    if (ui.scr != SCR_STEP || !st.held)
        return 0;
    if ((s = panel_enc(EN_SELECT)) != 0) {
        turned = 1;
#if FELUCCA_MICRO
        held_each(edit_nudge, s);
#endif
    }
    if (panel_enc(EN_ALGO) != 0)                        /* (no other track while a step is held) */
        turned = 1;
    if (st.lock_pg == LOCK_NONE && (s = panel_enc(EN_PRESET)) != 0) {
        turned = 1;
#if FELUCCA_CHANCE
        held_each(edit_chance, s);                      /* (drums and synths: an event) */
#endif
    }
    return turned;
}
static void op_row_pick(uint32_t r);                    /* op_input.c */
/* SEQ let go with nothing else done: STEP (on PATTERN); on STEP, tapped again its next page, stopping at the last (section 2's
 * paging rule); held alone past HOLD (long), the keys between steps and playing */
static void step_seq_tap(uint32_t lng)
{
    if (ui.scr != SCR_STEP) {
        op_enter(SCR_STEP);
        return;
    }
    if (!lng) {
        uint32_t n = step_rows();
        op_row_pick(ui.row[SCR_STEP] + 1u < n ? ui.row[SCR_STEP] + 1u : n ? n - 1u : 0u);
        return;
    }
    st.play = (uint8_t)!st.play;
    st.held = st.pend = 0;
    st.lock_pg = LOCK_NONE;
}
/* the keys while they play on STEP (the pick, or toggled): a drum key picks its lane, the last sound hit */
static void lane_pick_from(uint32_t l, int key);        /* op_input.c: the lane selected (its preview) */
static void step_played(uint32_t notes)
{
    uint32_t k;
    if (ui.scr != SCR_STEP || !is_drum(TSEL))
        return;
    for (k = 0; k < 27u; k++)
        if (((notes >> k) & 1u) && punch_key(k) >= 0)
            lane_pick_from(lane_of_key(k), 1);         /* (the key: it sounded stopped, silent playing) */
}

/* ---- STEP's lines (no footer: the header's pick, the held step under the grid), in sentence case: the step held
 * (nudge, chance; its number, fill), else the hints and the pick (a lane name as stored) */
static void step_foot(char *h, char *k, uint32_t n)
{
    char b[16];
    int32_t i = held_first();
    if (i >= 0) {
        h[0] = 0;
#if FELUCCA_MICRO
        str_cpy(h, "Nudge ", n);
        if (step_micro(TSEL, (uint32_t)i) > 0)
            str_cpy(h + str_len(h), "+", n - str_len(h));
        fmt_int(h + str_len(h), step_micro(TSEL, (uint32_t)i));
        str_cpy(h + str_len(h), "  ", n - str_len(h));
#endif
#if FELUCCA_CHANCE
        str_cpy(h + str_len(h), "Chance ", n - str_len(h));
        fmt_int(h + str_len(h), (int32_t)step_chance_of(TSEL, (uint32_t)i));
        str_cpy(h + str_len(h), "%", n - str_len(h));
#endif
        str_cpy(k, "Step ", n);
        fmt_int(k + 5, i + 1);
#if FELUCCA_FILLS
        {
            static const char *const F[3] = {"", "  Fill only", "  No fill"};
            str_cpy(k + str_len(k), F[step_fill(TSEL, (uint32_t)i) % 3u], n - str_len(k));
        }
#endif
        return;
    }
    str_cpy(h, st.play ? "Seq steps  Home back" : "Seq play  Home back", n);
    str_cpy(k, fm1_in.buttons & (1u << panel.btn[B_SEQ]) ? "Pick " : st.play ? "Keys play " : "Keys steps ", n);
    step_pick_name(b, sizeof b);
    str_cpy(k + str_len(k), b, n - str_len(k));
}

#if FELUCCA_KEYLIT
/* the keys of the notes a synth track sounds now: its steps', its ARP note, its voices held (the logic of Felucca
 * 1.0.1's play_leds and renebohne's voices, as ui/sloop/keylit.c keylit_play, copied): the lowest key of each */
static uint32_t op_keys_sounding(const track_t *t)
{
    uint8_t s[4 + 1 + NVOICE];
    uint32_t n = 0, i, j, k, used = 0, m = 0;
    for (i = 0; i < t->seq_n && i < 4u; i++)
        s[n++] = t->seq_notes[i];
    if (t->arp_note)
        s[n++] = t->arp_note;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (!v->active || !v->gate || v->stage > 2u)
            continue;
        for (j = 0; j < n && s[j] != v->note; j++)
            ;
        if (j == n)
            s[n++] = v->note;
    }
    for (k = 0; n && k < 27u; k++)
        for (i = 0; i < n; i++)
            if (s[i] == kb_map(t, k) && !((used >> i) & 1u)) {
                used |= 1u << i;
                m |= 1u << k;
            }
    return m;
}
#endif
