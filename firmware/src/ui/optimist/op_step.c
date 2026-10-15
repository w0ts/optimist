/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP (docs/UI-OPTIMIST-DESIGN.md section 4.2, phase 2): one screen for drums and synths. SEQ tapped opens it; the
 * 16 white keys are the 16 steps of the window with nothing held (the ISR's ly_lock LY_STEP: seq.c sends the keys
 * to lk_q, they play nothing). The window is 1-16, 17-32, 33-48, 49-64 up to the track's LEN; it follows the
 * playhead while playing (FOLLOW, the default); PRESETS and HOME + OCT- / OCT+ scroll it and stop the follow, HOME + OCT- +
 * OCT+ together resumes it. OCT on a synth's roll is the keyboard octave and moves the roll's view with it, ONE value (the user,
 * 2026-10-12: "the oct up / down keys in piano roll should actually shift the piano roll"; 2026-10-13: "can we make them
 * one value?"; the view is the pattern's fit moved by song.octave octaves; both OCT: octave 0, the fit); on the drum track ghost / hard, which a tapped drum step
 * takes too; with a synth step held it moves the step's notes an octave instead.
 *   a step key tapped     an empty step: set with the pick; a set step: cleared. Both when the key is let go, and
 *                         only after a tap: a press held past the hold time (op_hold_ms) or one during which a knob
 *                         turned, a value or a lock was edited or a card page changed leaves the step as it was
 *                         (the user, 2026-10-11: "long press on step to open plock, should not toggle the step")
 *   a step key held       the cards are the step's (an empty step held: its cards, no note made): drums LEVEL RATCHET, synths NOTE LEVEL RATCHET LENGTH (its
 *                         ties); several held edit together. SELECT pages the cards: the step, its extras (CHANCE NUDGE FILL COND, as
 *                         built), then the track's sound pages as p-locks; PRESETS is the sound as everywhere, SAVE the fill condition (FELUCCA_FILLS),
 *                         HOME the clear
 *   + a page button       ENV LFO FX SCL ARP: that page's cells become the steps' events (FELUCCA_PLOCK; the
 *                         automation store, seq/auto.h): a turn writes a step-only event (a lock; a hold event there:
 *                         its value), a cell with one shows a mark (a padlock; a hold event: an arrow); YES toggles
 *                         the hot cell's between step-only and HOLD; HOME + the knob clears it; the button again: the
 *                         family's next page, past the last the first
 *   SHIFT held + a step   the step's p-locks cleared (the notes, nudge, fill, chance and motion stay; SHIFT is OCT- held,
 *                         and SHIFT + the knob its default). A step held, HOME tapped: the step cleared (held_clear).
 *                         Drum track: SEQ + a step clears it, HOME + a key is the pick below
 *   HOME held + keys      the drum track's pick: the keys play (ly_lock is let go while HOME is down, the sound only
 *                         stopped); a key picks its lane (lane_sel), the last upper key the ACCENT lane
 *   SEQ held + keys       a synth's notes become the next steps' (the core's pen: seq.c key_down)
 *   SEQ tapped again      the keys play with the screen up (the window keeps following); again: steps
 * With no step held the cards are the track's PATTERN row (and TOOLS, EUCLID). Every step key pressed opens an undo level (undo_mark), as SLOOP's step entry. The logic
 * of the edits is SLOOP's (ui/sloop/ui_layers.c step_down / steps_held_*), copied: nothing in ui/sloop is called. */
#define STEP_LY_BIT (1u << 31)          /* seq.c ly_bit[LY_STEP]: no button holds it; STEP locks the layer (ly_lock) */

static struct {
    uint8_t page;                       /* the window: steps 16 page + 1 .. 16 page + 16 */
    uint8_t follow;                     /* the window follows the playhead while playing */
    uint8_t play;                       /* SEQ tapped on STEP: the keys play, the screen stays */
    uint8_t lock_pg;                    /* the held step's p-lock page: a PAGES row (a page button, or SELECT), LOCK_NONE else */
    uint8_t hp;                         /* the held step's card page: 0 the step, 1 its extras, then the p-lock pages */
    uint16_t held;                      /* the step keys down (white key w: step 16 page + w) */
    uint16_t pend;                      /* the step keys down that toggle their step when let go: a short press, nothing edited */
    uint32_t sess;                      /* the undo session of the step key pressed last */
    uint32_t t_down;                    /* fm1_ms when the first of the keys held went down (step_held_long) */
    uint32_t t_key[16];                 /* fm1_ms when each step key went down (a tap: let go before op_hold_ms) */
    uint8_t ph_drawn;                   /* the playhead strip as drawn (op_stepdraw.c) */
} st = {.follow = 1, .lock_pg = 0xFF, .ph_drawn = 0xFF};
#define LOCK_NONE 0xFFu
static void hp_sync(void);
static void hp_save(void);
static void hp_reopen(void);

static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};   /* the levels, softest first */
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }
static int step_on(const step_t *s) { return s->time == ST_NOTE && s->n; }
static void step_clear(step_t *s)
{
    memset(s, 0, sizeof *s);
    s->time = ST_REST;
}
static uint32_t step_pages(const track_t *t) { return (trk_len(t) + 15u) / 16u; }

/* the drum track's lane pick on STEP: SHIFT (OCT-) held, no step held, as on every page (so OCT- no longer makes a ghost hit on STEP) */
static int step_pick(uint32_t held)
{
    return ui.scr == SCR_STEP && is_drum(TSEL) && op_shift(held) && !st.held;
}
/* the keys are steps now: STEP shown, not toggled to playing, and not a pick: the drum track's HOME (step_pick), a
 * synth track's SEQ (the pen). The drum track's SEQ + a step clears it (step_key) */
static int step_keys(uint32_t held)
{
    if (ui.scr != SCR_STEP || st.play)
        return 0;
    return is_drum(TSEL) ? !step_pick(held) : !(held & (1u << panel.btn[B_SEQ]));
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
    ui.cards_t = 0;                                     /* (the grid is what is looked at: the cards go, the header names the window) */
}
static void step_reset(void)                            /* STEP entered or left: nothing held, the keys are steps */
{
    if (st.held)
        hp_save();                                      /* (left with a step held: its page is the one to reopen) */
    st.held = st.pend = 0;
    st.play = 0;
    st.lock_pg = LOCK_NONE, st.hp = 0;
}

/* ---- the pick: what a tapped step takes (drums the selected lane, synths the core's pen: the notes played last),
 * moved by the octaves OCT- / OCT+ turned since it was played (the user, 2026-10-11: step entry follows the keyboard
 * octave, as live playing does) */
static uint32_t pick_note(uint32_t i)                   /* the pick's note i (0..3) */
{
    int32_t n = pen_n ? pen_note[i & 3u] : last_note;
    return (uint32_t)clamp(n + 12 * ((int32_t)song.octave - pen_oct), 0, 127);
}
static void note_name(char *b, uint32_t n)              /* "C#4" (b holds 5) */
{
    str_cpy(b, N_NOTE[n % 12u], 3);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}
static void step_pick_name(char *b, uint32_t n)         /* "KICK", "C4 E4+" (b holds n >= 14) */
{
    uint32_t i, k = pen_n ? pen_n : 1u;
    if (is_drum(TSEL)) {
        str_cpy(b, lane_name(lane_selected()), n);
        return;
    }
    b[0] = 0;
    for (i = 0; i < k && i < 2u; i++) {
        if (i)
            str_cpy(b + str_len(b), " ", n - str_len(b));
        note_name(b + str_len(b), pick_note(i));
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
#if FELUCCA_PLOCK
    if (n != old)                                   /* whole steps now: a recorded fraction goes */
        lock_drop(t, (start + old - 1u) % len, P_SGATE);
#endif
    if (n != old && t->step[(start + old - 1u) % len].time == ST_NOTE)   /* (and the notes' own ends there) */
        memset(t->step[(start + old - 1u) % len].ngate, 0, sizeof t->step[0].ngate);
}
/* the note at start (n steps with its ties) as it plays, in tenths of a step, when its last step has a GATE lock (a
 * live-recorded length: seq.c rec_len_end; a TIE ends half a step + GATE in), else -1 */
static int32_t step_note_len10(const track_t *t, uint32_t start, uint32_t n)
{
#if FELUCCA_PLOCK
    int32_t g;
    if (lock_get(t, (start + n - 1u) % trk_len(t), P_SGATE, &g))
        return (int32_t)(n - 1u) * 10 + (n > 1u ? 5 : 0) + (g * 10 + 64) / 128;
#endif
    (void)t;
    (void)start;
    (void)n;
    return -1;
}
static void step_set(track_t *t, uint32_t idx)          /* an empty step takes the pick (IRQ off) */
{
    if (is_drum(t)) {                                   /* (OCT+ held: a hard hit; OCT- is SHIFT) */
        uint32_t lv = fm1_in.buttons & dyn_bit[0] ? LV_GHOST : fm1_in.buttons & dyn_bit[1] ? LV_HARD : LV_NORM;
        dstep_set(&t->dstep[idx], lane_selected(), lv, 0);
    } else {
        step_t *s = &t->step[idx];
        uint32_t i, n = pen_n ? pen_n : 1u;
        memset(s, 0, sizeof *s);
        for (i = 0; i < n && i < 4u; i++)
            s->note[i] = (uint8_t)pick_note(i);
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
        if (all) {
            memset(d, 0, sizeof *d);
        } else {
            dstep_clr(d, lane_selected());
#if FELUCCA_PLOCK
            if (lane_acc())
                lock_drop(t, idx, P_DACC);              /* (the ACCENT hit gone: its amount's lock there too) */
#endif
#if DLA_ON
            (void)lane_step_drop(t, idx, lane_selected());   /* (the lane's hit gone: its locks there, D4) */
#endif
        }
        if (dstep_lanes(d))
            return;                                 /* (the ACCENT lane's hit is a hit of the step) */
    } else {
        if (step_on(&t->step[idx]))
            step_note_resize(t, idx, -(int32_t)NSTEP);
        step_clear(&t->step[idx]);
    }
#if FELUCCA_AUTO
    (void)auto_step_clear(t, idx, all ? AUTO_HOLDS | AUTO_ONLYS : AUTO_ONLYS);   /* (HOME: its motion too) */
#endif
}
/* a synth step tapped off keeps its pitch, tapped on again it comes back at that pitch (the user, 2026-10-12: "click a
 * step remove the note, clicking again add the note at the same pitch for that step"): the cleared step is a rest
 * (silent, seq.c plays no rest) that keeps note[] and n; everything else of it goes as step_wipe says (IRQ off) */
static void step_off_keep(track_t *t, uint32_t idx)
{
    step_t *s = &t->step[idx];
    uint8_t nt[4], n = s->n;
    memcpy(nt, s->note, sizeof nt);
    step_wipe(t, idx, 0);
    memcpy(s->note, nt, sizeof nt);
    s->n = n;
}
static int step_kept(const track_t *t, uint32_t idx)     /* a rest that remembers a note */
{
    return !is_drum(t) && t->step[idx].time == ST_REST && t->step[idx].n;
}
static void step_back(track_t *t, uint32_t idx)          /* the remembered note again, as step_set makes one (IRQ off) */
{
    step_t *s = &t->step[idx];
    if (t->p[P_VOICE] != V_POLY)
        s->n = 1;
    s->time = ST_NOTE;
    s->vel = 100;
    t->seq_active = 1;
}
static int step_has(const track_t *t, uint32_t idx)     /* a tap would clear it: the lane's hit, the note */
{
    return is_drum(t) ? dstep_has(&t->dstep[idx], lane_selected()) : step_on(&t->step[idx]);
}
/* the keys' view of a step: the selected lane's hit; the drum track with SEQ held (SEQ + a key clears the whole step): any lane's */
static int step_shown(const track_t *t, uint32_t idx)
{
    if (is_drum(t) && (fm1_in.buttons & (1u << panel.btn[B_SEQ])))
        return dstep_lanes(&t->dstep[idx]) != 0u;
    return step_has(t, idx);
}

/* step idx's p-locks go (IRQ off): the step-only events of a parameter, a drum lane's values included; the step's notes,
 * its nudge, fill, chance and condition (the pseudo-parameters) and its motion stay; -> how many */
static uint32_t step_unlock(track_t *t, uint32_t idx)
{
    uint32_t n = 0;
#if FELUCCA_AUTO
    auto_list_t *l = AL(t);
    uint32_t i, m = 0;
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        const auto_ev_t *e = &l->ev[i];
        if ((e->place & AUTO_STEP) == idx && (e->place & AUTO_ONLY) && !AUTO_PSEUDO(e->param))
            n++;
        else
            l->ev[m++] = *e;
    }
    l->n = (uint8_t)m;
    if (n)
        auto_touch(trk_index(t));
#else
    (void)t, (void)idx;
#endif
    return n;
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
        if ((st.pend & bit) && fm1_ms - st.t_key[w] < op_hold_ms() && idx < trk_len(t)) {   /* tapped: toggled */
            undo_mark(t, st.sess);
            fm1_irq_off();
            if (step_has(t, idx) && is_drum(t) && lane_selected() != DRUM_ACC && dstep_has(&t->dstep[idx], lane_selected()) &&
                dstep_lvl(&t->dstep[idx], lane_selected()) <= LV_HARD && dstep_lvl(&t->dstep[idx], lane_selected()) != LV_GHOST &&
                dstep_lvl(&t->dstep[idx], lane_selected()) != LV_SOFT)   /* a drum hit tapped: normal -> hard -> ghost -> off (the user, 2026-10-14) */
                dstep_set(&t->dstep[idx], lane_selected(),
                          dstep_lvl(&t->dstep[idx], lane_selected()) == LV_NORM ? LV_HARD : LV_GHOST,
                          dstep_rat(&t->dstep[idx], lane_selected()));
            else if (step_has(t, idx))
                is_drum(t) ? step_wipe(t, idx, 0) : step_off_keep(t, idx);
            else if (step_kept(t, idx))
                step_back(t, idx);                      /* (the pitch it had) */
            else
                step_set(t, idx);
            fm1_irq_on();
            sync_reload = 1;
        }
        st.pend &= (uint16_t)~bit;
        if (!st.held) {
            hp_save();                                  /* (the last step let go: its page is the next hold's) */
            st.lock_pg = LOCK_NONE, st.hp = 0;
        }
        return;
    }
    if (ui.scr != SCR_STEP || op_armed() || idx >= trk_len(t))
        return;                                         /* (a question waits: the keys do nothing) */
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    st.sess = (undo_sess += 4u) | 3u;                   /* every step key: an undo level */
    if (op_shift(held) && !(is_drum(t) && !step_has(t, idx))) {   /* SHIFT + a step: its p-locks cleared, the step stays (a drum
                                                         * step not hit yet: OCT- is its ghost hit, seq.c) */
        uint32_t n;
        undo_mark(t, st.sess);
        fm1_irq_off();
        n = step_unlock(t, idx);
        fm1_irq_on();
        sync_reload = 1;
        ui_message(n ? "LOCKS CLEARED" : "NO LOCKS");
        return;
    }
    if (is_drum(t) && (held & (1u << panel.btn[B_SEQ]))) {   /* the drum track's SEQ + the step: cleared, everything of it */
        undo_mark(t, st.sess);
        fm1_irq_off();
        step_wipe(t, idx, 1);
        fm1_irq_on();
        sync_reload = 1;
        return;
    }
    if (!st.held) {
        st.t_down = fm1_ms;                             /* (the press that may become a hold) */
        hp_reopen();                                    /* (its cards open on the page last used on this track) */
    }
    st.held |= (uint16_t)bit;
    st.t_key[w] = fm1_ms;
    st.pend |= (uint16_t)bit;                           /* toggled when let go, if a tap (nothing changes on the press) */
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
/* a step held past the hold time (op_hold_ms, SYSTEM HOLD: what makes it a held step): only then do the cards show
 * by themselves; a quick tap toggles the step and shows none (the user, 2026-10-10: "in seq, we should show the plock
 * overlay only after some time"). An edit or a page change while it is held shows them at once (step_cards_show) */
static int step_held_long(void) { return st.held && fm1_ms - st.t_down >= op_hold_ms(); }
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
/* a step held + OCT- / OCT+ (the user, 2026-10-11): every note of the steps held an octave down / up, each clamped
 * to 0..127; the keyboard octave and FM6's paging stay. The cards come to the step's page, NOTE lit */
static void held_octave(int32_t d)
{
    if (held_first() < 0)
        return;
    st.hp = 0;
    st.lock_pg = LOCK_NONE;
    held_edit(0, 12 * d);
    step_cards_show();
    ui.hot = 0;
    ui.hot_lit = 1;
}

/* the extras: the nudge, the fill condition, the chance (the held step's second card page) */
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
#endif
#if FELUCCA_CHANCE
/* the chance as an event of the automation store, on drum and synth steps alike (this UI writes events only) */
static void edit_chance(track_t *t, uint32_t idx, int32_t s)
{
    if (step_has(t, idx) || (is_drum(t) && dstep_lanes(&t->dstep[idx % NSTEP])))
        (void)step_chance_set(t, idx, s == OP_RESET ? 100u : (uint32_t)clamp((int32_t)step_chance_of(t, idx) + s * (int32_t)CH_STEP, 0, 100));
}
#endif
#if FELUCCA_COND
/* a step's condition (Felucca 1.5.6, seq/auto.c cond_miss): -, then a:b, plays on loop a of every b */
static const char *const COND_NAME[AUTO_NCOND + 1u] = {"-", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4"};
static void edit_cond(track_t *t, uint32_t idx, int32_t v) { (void)step_cond_set(t, idx, (uint32_t)v); }
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
/* page pg's cells are locks on this track: its sound pages; on the drum track the selected lane's SOUND, SOUND 2 and
 * ENV too (their values TUNE .. LEVEL: lane locks, DLA_ON), not SOUND 3, SOURCE, KIT nor the lane's LFO */
static int lock_pg_ok(const page_t *pg)
{
    if (!sound_page(pg))
        return 0;
    return pg->scope != SC_DSND || (DLA_ON && pg->id[0] < DE_N);
}
static void step_lock_page(uint32_t fam)                /* the family's first page shown on this track, again: its next */
{
#if FELUCCA_PLOCK
    uint32_t i, first = LOCK_NONE, next = LOCK_NONE;
    for (i = 0; i < NPAGES; i++) {
        if (PAGES[i].fam != fam || !lock_pg_ok(&PAGES[i]))
            continue;
        if (first == LOCK_NONE)
            first = i;
        if (st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].fam == fam && i > st.lock_pg && next == LOCK_NONE)
            next = i;
    }
    st.lock_pg = (uint8_t)(next != LOCK_NONE ? next : first);   /* (past the family's last page: its first) */
    st.pend &= (uint16_t)~st.held;                      /* (a page changed: no tap) */
    hp_sync();
    step_cards_show();                                  /* (a page button: the p-lock page) */
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
/* page pg's cell k as a lockable value of the selected track: its P_* id and descriptor, -1 when it is not one. On the
 * drum track's SOUND, SOUND 2 and ENV pages the selected lane's value v: LOCK_LANE | v (seq/auto_lane.c, the lane's
 * locks and motion events, heard at its hits) */
#define LOCK_LANE 0x100
static int32_t lock_param(const page_t *pg, uint32_t k, const param_desc_t **d)
{
    int16_t *vp;
    *d = page_desc(pg, k, &vp);
    if (!*d || !vp || PARAM_HIDDEN(*d) || is_go(*d) || (*d)->max <= (*d)->min)
        return -1;
#if DLA_ON
    if (pg->scope == SC_DSND)
        return page_id(pg, k) < DE_N ? (int32_t)(LOCK_LANE | page_id(pg, k)) : -1;
#endif
    if (vp < TSEL->p || vp >= TSEL->p + P_COUNT)
        return -1;
    return lock_ok(TSEL, (uint32_t)(vp - TSEL->p)) ? (int32_t)(vp - TSEL->p) : -1;
}
/* the first step held's event of cell k: 1 step-only (a lock), 2 a hold event (motion), 0 none; its value into *v */
static uint32_t lock_kind(uint32_t id, int32_t i, int32_t *v)
{
#if DLA_ON
    if (id & LOCK_LANE)
        return lane_lock_get(TSEL, (uint32_t)i, lane_selected(), id & 7u, v) ? 1u :
               lane_hold_get(TSEL, (uint32_t)i, lane_selected(), id & 7u, v) ? 2u : 0u;
#endif
    return lock_get(TSEL, (uint32_t)i, id, v) ? 1u : hold_get(TSEL, (uint32_t)i, id, v) ? 2u : 0u;
}
/* the cell of lockable id (lock_param's) with descriptor d: the event of the first step held, else the track's value (a
 * lane's: its patch) */
static void lock_cell_id(int32_t id, const param_desc_t *d, cell_t *c)
{
    int32_t i = held_first(), v;
    uint32_t q;
    cell_clear(c);
    if (!d)
        return;
    c->label = d->label;
    if (id < 0 || i < 0) {
        str_cpy(c->val, "-", sizeof c->val);            /* (not a lockable value: the mixer's, a GO button) */
        return;
    }
    q = lock_kind((uint32_t)id, i, &v);
    c->kind = CK_VAL;
    c->d = d;
#if DLA_ON
    if (id & LOCK_LANE) {
        if (!q)
            v = dl.ofs[lane_selected()][id & 7];
        c->vp = &dsv[id & 7];
    } else
#endif
    {
        if (!q)
            v = TSEL->p[id];
        c->vp = &TSEL->p[id];
    }
    c->mark = (uint8_t)q;                               /* (op_draw.c: 1 a padlock, 2 a hold's arrow) */
    c->col = q == 1u ? C_WARN : q ? C_WARN : 0;
    param_format(d, v, c->val, &c->unit);
    cell_gauge_d(c, d, v);
}
static void lock_cell(uint32_t k, cell_t *c)
{
    const param_desc_t *d;
    int32_t id = lock_param(&PAGES[st.lock_pg], k, &d);
    lock_cell_id(id, d, c);
}
/* step idx's event of lockable id set a turn of step on (OP_RESET: its events of id gone, both kinds); r: bit 0 the
 * list full, bit 1 the lane has no hit there (D4: no lock), bit 2 written (IRQ off) */
static void lock_step_put(track_t *t, uint32_t idx, uint32_t id, const param_desc_t *d, int32_t step, uint32_t *r)
{
    int32_t v;
#if DLA_ON
    if (id & LOCK_LANE) {
        uint32_t l = lane_selected(), dv = id & 7u;
        int rc;
        if (step == OP_RESET) {
            lane_lock_drop(t, idx, l, dv);
            (void)lane_hold_drop(t, idx, l, dv);
            return;
        }
        if (!lane_lock_get(t, idx, l, dv, &v) && lane_hold_get(t, idx, l, dv, &v)) {
            (void)auto_put(AL(t), idx % NSTEP, AUTO_LANE(l, dv), clamp(param_step(d, v, step), DE_MIN[dv], DE_MAX[dv]));
            *r |= 4u;                                   /* (the hold event's value) */
            return;
        }
        if (!lane_lock_get(t, idx, l, dv, &v))
            v = dl.ofs[l][dv];
        rc = lane_lock_set(t, idx, l, dv, param_step(d, v, step));
        *r |= rc == LANE_NOHIT ? 2u : rc ? 4u : 1u;
        return;
    }
#endif
    if (step == OP_RESET) {
        lock_drop(t, idx, id);
        (void)auto_step_hold_drop(t, idx, id);
        return;
    }
    if (is_drum(t) && id == P_DACC && !dstep_has(&t->dstep[idx % NSTEP], DRUM_ACC)) {
        *r |= 2u;                                       /* (the ACCENT amount's lock needs the accent hit on the step, as a lane's: D4) */
        return;
    }
    if (!lock_get(t, idx, id, &v) && hold_get(t, idx, id, &v)) {
        (void)auto_put(AL(t), idx, mot_sid(id), param_step(d, v, step));   /* (the hold event's value) */
        *r |= 4u;
        return;
    }
    if (!lock_get(t, idx, id, &v))
        v = t->p[id];
    *r |= lock_set(t, idx, id, param_step(d, v, step)) ? 4u : 1u;
}
/* a turn of knob k writes lockable id's step-only event on every step held (a hold event alone there: its value);
 * OP_RESET (HOME + the knob) drops the step's events of the cell, both kinds */
static void lock_turn_id(int32_t id, const param_desc_t *d, uint32_t k, int32_t s, int fine)
{
    track_t *t = TSEL;
    int32_t step;
    uint32_t w, r = 0;
    if (id < 0) {
        if (d)
            ui_message("NOT LOCKABLE");
        return;
    }
    step = fine || s == OP_RESET ? s : accel(EN_K1 + k, s, accel_range(d));
    st.pend &= (uint16_t)~st.held;
    undo_mark(t, st.sess);                              /* (a lock written or dropped: undone with the step, undo.c) */
    fm1_irq_off();
    for (w = 0; w < 16u; w++)
        if (((st.held >> w) & 1u) && st.page * 16u + w < trk_len(t))
            lock_step_put(t, st.page * 16u + w, (uint32_t)id, d, step, &r);
    fm1_irq_on();
    sync_reload = 1;
    if (r & 1u)
        ui_message("NO LOCK LEFT");
    else if ((r & 2u) && !(r & 4u))
        ui_message("NO HIT ON THIS STEP");               /* (the lane does not play there: a lock would never be heard) */
}
static void lock_turn(uint32_t k, int32_t s, int fine)
{
    const param_desc_t *d;
    int32_t id = lock_param(&PAGES[st.lock_pg], k, &d);
    lock_turn_id(id, d, k, s, fine);
}
/* the held drum step's first page, KNOB 3 and 4 (the user, 2026-10-12, D3: LEVEL RATCHET TUNE DECAY): the selected
 * lane's TUNE and DECAY on the steps held, as a lock page's cells */
static int32_t held_lane_param(uint32_t k, const param_desc_t **d)
{
#if DLA_ON
    int16_t *vp;
    uint32_t v = k == 2u ? DE_TUNE : DE_DECAY;
    if (lane_acc()) {                                   /* the ACCENT lane: no sound, no TUNE or DECAY; its amount is the third card */
        *d = k == 2u ? &DRUM_ACC_DESC : 0;
        return k == 2u ? (int32_t)P_DACC : -1;
    }
    *d = dsnd_desc_lane(lane_selected(), v, &vp);
    return *d ? (int32_t)(LOCK_LANE | v) : -1;
#else
    (void)k;
    *d = 0;
    return -1;
#endif
}
#endif
/* YES with a step held: the lock page's hot cell (the held drum step's TUNE, DECAY too), its event's kind on every step
 * held (step-only <-> HOLD); without a lock page, or no event there: the fill condition (FELUCCA_FILLS) */
static void held_yes(void)
{
    step_cards_show();
#if FELUCCA_PLOCK
    const param_desc_t *d;
    int32_t id = -1, v, i = held_first();
    if (st.lock_pg != LOCK_NONE)
        id = lock_param(&PAGES[st.lock_pg], ui.hot & 3u, &d);
    else if (st.hp == 0u && is_drum(TSEL) && (ui.hot & 3u) >= 2u)
        id = held_lane_param(ui.hot & 3u, &d);
    if (i >= 0 && id >= 0 && lock_kind((uint32_t)id, i, &v)) {
        track_t *t = TSEL;
        uint32_t w, r = 0;
        undo_mark(t, st.sess);
        st.pend &= (uint16_t)~st.held;
        fm1_irq_off();
        for (w = 0; w < 16u; w++)
            if (((st.held >> w) & 1u) && st.page * 16u + w < trk_len(t)) {
#if DLA_ON
                if ((uint32_t)id & LOCK_LANE) {
                    r |= lane_kind_toggle(t, st.page * 16u + w, lane_selected(), (uint32_t)id & 7u);
                    continue;
                }
#endif
                r |= auto_kind_toggle(t, st.page * 16u + w, (uint32_t)id);
            }
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

/* ---- the held step's card pages (the user, 2026-10-10: "on held step we can record plock and select allow then to page
 * between pages of plock"): SELECT pages them, stopping at the first and the last; KNOB 1..4 edit the page's values.
 *   page 1   the step: NOTE LEVEL RATCHET LENGTH (drums LEVEL RATCHET)
 *   page 2   the extras, as built: CHANCE, NUDGE, FILL, COND (none built: no page)
 *   then     the track's sound pages (the SOUND screen's rows, a drum lane's own pages left out): a turn writes the
 *            step's p-lock (FELUCCA_PLOCK; none: no pages)
 * A page button with a step held still jumps to its family's page (step_lock_page) */
#define EX_N ((FELUCCA_CHANCE != 0) + (FELUCCA_MICRO != 0) + (FELUCCA_FILLS != 0) + (FELUCCA_COND != 0))
static int ex_kind(uint32_t k)                          /* extras cell k: 0 chance, 1 nudge, 2 fill, 3 cond; -1 none */
{
    uint32_t n = 0;
#if FELUCCA_CHANCE
    if (n++ == k)
        return 0;
#endif
#if FELUCCA_MICRO
    if (n++ == k)
        return 1;
#endif
#if FELUCCA_FILLS
    if (n++ == k)
        return 2;
#endif
#if FELUCCA_COND
    if (n++ == k)
        return 3;
#endif
    (void)n;
    (void)k;
    return -1;
}
static uint32_t hp_locks(uint8_t *ix)                   /* the p-lock pages of this track (PAGES indexes) */
{
    uint32_t i, n = 0;
#if FELUCCA_PLOCK
    for (i = 0; i < NPAGES && n < OP_MAXROWS; i++)
        if (lock_pg_ok(&PAGES[i]))
            ix[n++] = (uint8_t)i;
#else
    (void)ix;
    (void)i;
#endif
    return n;
}
#define HP_BASE (1u + (EX_N ? 1u : 0u))                 /* the first p-lock page */
static uint32_t hp_count(void)
{
    uint8_t ix[OP_MAXROWS];
    return HP_BASE + hp_locks(ix);
}
static void hp_set(uint32_t v)                          /* page v of the held step */
{
    uint8_t ix[OP_MAXROWS];
    uint32_t n = hp_locks(ix);
    v = v >= HP_BASE + n ? HP_BASE + n - 1u : v;
    st.hp = (uint8_t)v;
    st.lock_pg = v >= HP_BASE ? ix[v - HP_BASE] : LOCK_NONE;
    step_cards_show();                                  /* (a page change) */
    ui.hot = 0;
    ui.hot_lit = 0;
}
static void hp_sync(void)                               /* the page of st.lock_pg (a page button set it) */
{
    uint8_t ix[OP_MAXROWS];
    uint32_t i, n = hp_locks(ix);
    if (st.lock_pg == LOCK_NONE)
        return;
    for (i = 0; i < n; i++)
        if (ix[i] == st.lock_pg)
            st.hp = (uint8_t)(HP_BASE + i);
}
/* the held step's card page is remembered per track (the user, 2026-10-13: "screen should remember its page, including
 * plocks etc."): saved when the last step is let go (or STEP left, SEQ held), reopened by the next step held on that track:
 * 0 STEP (never used: as before), 1 EXTRA, 2 + a PAGES row for a p-lock page. A page the build or the track no longer has
 * opens STEP. RAM only */
static uint8_t hp_mem[NTRK];
static void hp_save(void)
{
    hp_mem[song.sel % NTRK] = (uint8_t)(st.hp == 0u ? 0u : st.hp < HP_BASE ? 1u : st.lock_pg == LOCK_NONE ? 0u : 2u + st.lock_pg);
}
static void hp_reopen(void)
{
    uint8_t ix[OP_MAXROWS];
    uint32_t m = hp_mem[song.sel % NTRK], i, n;
    st.hp = 0;
    st.lock_pg = LOCK_NONE;
    if (EX_N ? m == 1u : 0)
        st.hp = 1;
    for (i = 0, n = m >= 2u ? hp_locks(ix) : 0u; i < n; i++)
        if (ix[i] == m - 2u) {
            st.hp = (uint8_t)(HP_BASE + i);
            st.lock_pg = ix[i];
        }
}
static void hp_title(char *t, uint32_t n)               /* the held step's card page: "STEP", "EXTRA", "LFO" */
{
    uint8_t ix[OP_MAXROWS];
    const page_t *pg = st.hp >= HP_BASE ? &PAGES[hp_locks(ix) ? ix[(st.hp - HP_BASE) % OP_MAXROWS] : 0] : 0;
    str_cpy(t, st.hp == 0u ? "STEP" : !pg ? "EXTRA" : dr_alias(pg) ? dr_alias(pg) : pg->title, n);   /* (the drum's: "BODY") */
}
static void extra_cell(uint32_t k, cell_t *c)           /* the first step held's extras */
{
    static const char *const FN[3] = {"NORMAL", "FILL", "NO FILL"};
    int32_t i = held_first(), kd = ex_kind(k), v = 0;
    cell_clear(c);
    if (i < 0 || kd < 0)
        return;
    c->kind = CK_VAL;
#if FELUCCA_CHANCE
    if (kd == 0) {
        v = (int32_t)step_chance_of(TSEL, (uint32_t)i);
        c->label = "CHANCE";
        c->unit = "%";
        fmt_int(c->val, v);
        cell_gauge(c, 0, 0, 100, v);
        return;
    }
#endif
#if FELUCCA_MICRO
    if (kd == 1) {
        v = step_micro(TSEL, (uint32_t)i);
        c->label = "NUDGE";
        fmt_int(c->val, v);
        cell_gauge(c, 0, -32, 31, v);
        return;
    }
#endif
#if FELUCCA_FILLS
    if (kd == 2) {
        v = (int32_t)(step_fill(TSEL, (uint32_t)i) % 3u);
        c->label = "FILL";
        str_cpy(c->val, FN[v], sizeof c->val);
        cell_gauge(c, 1, 0, 2, v);
        return;
    }
#endif
#if FELUCCA_COND
    if (kd == 3) {
        v = (int32_t)step_cond(TSEL, (uint32_t)i);
        c->label = "COND";
        str_cpy(c->val, COND_NAME[v % (AUTO_NCOND + 1u)], sizeof c->val);
        cell_gauge(c, 1, 0, AUTO_NCOND, v);
        return;
    }
#endif
    (void)FN;
    (void)v;
}
static void extra_turn(uint32_t k, int32_t s)
{
    int32_t kd = ex_kind(k), i = held_first();
    if (kd < 0 || i < 0)
        return;
    (void)s;
#if FELUCCA_CHANCE
    if (kd == 0)
        held_each(edit_chance, s);
#endif
#if FELUCCA_MICRO
    if (kd == 1)
        held_each(edit_nudge, s);
#endif
#if FELUCCA_FILLS
    if (kd == 2)
        held_each(edit_fill, s == OP_RESET ? 0 : (int32_t)clamp((int32_t)step_fill(TSEL, (uint32_t)i) + (s > 0 ? 1 : -1), 0, 2));
#endif
#if FELUCCA_COND
    if (kd == 3)                                        /* (- 1:2 .. 4:4, stopping at both ends; HOME + the knob: -) */
        held_each(edit_cond, s == OP_RESET ? 0 : clamp((int32_t)step_cond(TSEL, (uint32_t)i) + s, 0, (int32_t)AUTO_NCOND));
#endif
}

/* ---- the screen: rows, cells, turns, YES */
/* STEP's pages (the user: "the first SEQ menu should have the length of the pattern"): PATTERN (LEN DIV SWING GATE) on
 * every track, then TOOLS and EUCLID. No ARP rows (ARP has its own button and page) and no drum LANE page (LEVEL TUNE
 * DECAY REV: sound values, not the sequencer's; the user, 2026-10-14: "why do we have an arpeggio section in the SEQ",
 * "on the sequencer pages i have items to edit the engine sound"); the sound pages stay on a held step as p-locks. SELECT pages the four knobs only and
 * never changes the lane; the lane is ALGORITHM's (DR, then the lanes) or HOME + a key's (the user, 2026-10-12: "in step
 * seq: select should always select the page for the 4 knobs, and algo always the track/drum sub track"). STEP opens on
 * PATTERN the first time, then on the page it was left on (2026-10-13) */
static uint32_t stp_pattern(void)                       /* the PATTERN page (FAM_SEQ, the track's: LEN first) */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_SEQ && PAGES[i].scope == SC_TRACK && PAGES[i].id[0] == P_SLEN)
            return i;
    return 0;
}
static uint32_t step_rows0(void) { return 1u; }   /* the pages before TOOLS, EUCLID: PATTERN */
#if FELUCCA_SEQTOOLS
#define STP_TL 2u                                       /* the last two pages: TOOLS, EUCLID (op_tools.c) */
#else
#define STP_TL 0u
#endif
static uint32_t step_rows(void) { return step_rows0() + STP_TL; }
static int stp_tl(uint32_t r) { return STP_TL && r >= step_rows0() ? (int)(r - step_rows0()) : -1; }   /* 0 TOOLS, 1 EUCLID */
static void step_name(uint32_t r, char *b)
{
    if (stp_tl(r) >= 0)
        str_cpy(b, stp_tl(r) ? "EUCLID" : "TOOLS", 12);
    else
        str_cpy(b, PAGES[stp_pattern()].title, 12);
}
static void held_cell(uint32_t k, cell_t *c)            /* the first step held: its values */
{
    static const char *const LV_NAME[4] = {"GHOST", "SOFT", "NORM", "HARD"};
    static const char *const RT_NAME[4] = {"x1", "x2", "x3", "x4"};
    const track_t *t = TSEL;
    int32_t i = held_first();
    uint32_t lv, rt;
    cell_clear(c);
    if (i >= 0 && is_drum(t) && k > 1u) {               /* (the drum step: LEVEL RATCHET TUNE DECAY, the lane's locks: D3) */
#if FELUCCA_PLOCK
        const param_desc_t *d;
        int32_t id = held_lane_param(k, &d);
        if (id >= 0 && lane_acc() && !dstep_has(&t->dstep[i], DRUM_ACC)) {
            c->label = d ? d->label : 0;                /* (the ACCENT amount's lock needs the accent hit on the step: */
            str_cpy(c->val, "-", sizeof c->val);        /* its card says "-") */
        }
        else if (id >= 0)
            lock_cell_id(id, d, c);
#endif
        return;
    }
    if (i < 0)
        return;
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
        if (k == 0u || k == 3u) {                       /* (an empty step: NOTE and LENGTH say "-", no value: audit #7) */
            c->label = k ? "LENGTH" : "NOTE";
            if (c->kind == CK_NONE) {
                str_cpy(c->val, "-", sizeof c->val);
                return;
            }
        }
        if (k == 0u) {
            note_name(c->val, s->note[0]);
            if (s->n > 1u)
                str_cpy(c->val + str_len(c->val), "+", sizeof c->val - str_len(c->val));
            cell_gauge(c, 0, 0, 127, s->note[0]);
            return;
        }
        if (k == 3u) {
            uint32_t n = step_note_len(t, (uint32_t)i), sp = note_span(t, (uint32_t)i, 0);
            int32_t f = step_note_len10(t, (uint32_t)i, n);   /* (a recorded length: 1.8) */
            if (sp != n * 128u)                         /* (its own end, or on through carry steps: seq.c note_span) */
                f = (int32_t)((sp * 10u + 64u) / 128u);
            fmt_int(c->val, f < 0 ? (int32_t)n : f / 10);
            if (f >= 0) {
                char d[3] = {'.', (char)('0' + f % 10), 0};
                str_cpy(c->val + str_len(c->val), d, sizeof c->val - str_len(c->val));
            }
            c->unit = "steps";
            cell_gauge(c, 0, 0, (int32_t)trk_len(t) * 10, f < 0 ? (int32_t)n * 10 : f);
            return;
        }
        k--;
    }
    c->label = k ? "RATCHET" : "LEVEL";
    if (c->kind == CK_NONE) {                           /* (an empty step: its LEVEL and RATCHET say "-", not a blank card) */
        str_cpy(c->val, "-", sizeof c->val);
        return;
    }
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
        if (EX_N ? st.hp == 1u : 0) {
            extra_cell(k, c);
            return;
        }
        held_cell(k, c);
        return;
    }
#if FELUCCA_SEQTOOLS
    if (stp_tl(r) >= 0) {
        tl_cell((uint32_t)stp_tl(r), k, c);
        return;
    }
#endif
    page_cell(&PAGES[stp_pattern()], k, c);
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
        if (EX_N ? st.hp == 1u : 0) {
            extra_turn(k, s);
        } else if (is_drum(TSEL) && k > 1u) {           /* (the drum step's TUNE, DECAY: the lane's locks) */
#if FELUCCA_PLOCK
            const param_desc_t *d;
            int32_t id = held_lane_param(k, &d);
            if (d)
                lock_turn_id(id, d, k, s, fine);
#endif
        } else {
            held_edit(k, s);
        }
        return;
    }
#if FELUCCA_SEQTOOLS
    if (stp_tl(r) >= 0) {
        tl_turn((uint32_t)stp_tl(r), k, s);
        return;
    }
#endif
    page_turn(&PAGES[stp_pattern()], k, s, fine);
}
static int step_yes(uint32_t r, uint32_t k, uint32_t ok)
{
#if FELUCCA_SEQTOOLS
    if (stp_tl(r) >= 0)
        return tl_yes((uint32_t)stp_tl(r), k, ok);   /* (asks first, as every action card; one undo) */
#endif
    return page_yes(SCR_STEP, r, &PAGES[stp_pattern()], k, ok);
}

/* ---- the panel's controls on STEP (op_input.c calls these first; 1 = done) */
static int step_knobs(void)                             /* a step held: SELECT its card pages; 1 turned */
{
    int32_t s, turned = 0;
    if (ui.scr != SCR_STEP || !st.held)
        return 0;
    if ((s = panel_enc(EN_SELECT)) != 0) {              /* SELECT pages the held step's cards */
        turned = 1;
        hp_set((uint32_t)clamp((int32_t)st.hp + s, 0, (int32_t)hp_count() - 1));
    }
    if (panel_enc(EN_ALGO) != 0)                        /* (no other track while a step is held) */
        turned = 1;
    return turned;
}
static void op_row_pick(uint32_t r);                    /* op_input.c */
/* SEQ let go with nothing else done: STEP (on PATTERN); on STEP, tapped again its next page, past the last the first (section 2's
 * paging rule); held alone past HOLD (long), the keys between steps and playing */
static void step_seq_tap(uint32_t lng)
{
    if (ui.scr != SCR_STEP) {
        op_enter(SCR_STEP);
        return;
    }
    if (!lng) {
        uint32_t n = step_rows();
        op_row_pick(ui.row[SCR_STEP] + 1u < n ? ui.row[SCR_STEP] + 1u : 0u);   /* (past the last: the first) */
        return;
    }
    st.play = (uint8_t)!st.play;
    if (st.held)
        hp_save();
    st.held = st.pend = 0;
    st.lock_pg = LOCK_NONE, st.hp = 0;
}
/* the keys while they play on STEP (the pick, or toggled): a drum key picks its lane, the last sound hit */
static void lane_pick_from(uint32_t l, int key);        /* op_input.c: the lane selected (its preview) */
static void step_played(uint32_t notes)
{
    uint32_t k;
    if (ui.scr != SCR_STEP || !is_drum(TSEL))
        return;
    for (k = 0; k < 27u; k++)
        if (((notes >> k) & 1u) && lane_key(k))
            lane_pick_from(lane_of(k), 1);             /* (the key: it sounded stopped, silent playing; key 1: the ACCENT lane) */
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
#if FELUCCA_COND
        if (step_cond(TSEL, (uint32_t)i)) {
            str_cpy(k + str_len(k), "  ", n - str_len(k));
            str_cpy(k + str_len(k), COND_NAME[step_cond(TSEL, (uint32_t)i) % (AUTO_NCOND + 1u)], n - str_len(k));
        }
#endif
        return;
    }
    str_cpy(h, st.play ? "Seq steps  Home back" : "Seq play  Home back", n);
    if (is_drum(TSEL) && (fm1_in.buttons & (1u << panel.btn[B_SEQ])) && !st.play) {
        str_cpy(k, "Keys clear steps", n);              /* (the drum track: SEQ + a key clears its step) */
        return;
    }
    str_cpy(k, step_pick(fm1_in.buttons) || (!is_drum(TSEL) && (fm1_in.buttons & (1u << panel.btn[B_SEQ]))) ? "Pick "
              : st.play ? "Keys play " : "Keys steps ", n);
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
