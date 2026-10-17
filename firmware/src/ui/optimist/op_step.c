/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP (docs/UI-OPTIMIST-DESIGN.md section 4.2, phase 2): one screen for drums and synths. SEQ tapped opens it; the
 * 16 white keys are the 16 steps of the window with nothing held (the ISR's ly_lock LY_STEP: seq.c sends the keys
 * to lk_q, they play nothing). The window is 1-16, 17-32, 33-48, 49-64 up to the track's LEN; it follows the
 * playhead while playing (FOLLOW, the default); PRESETS scrolls it and stops the follow, OCT- (SHIFT) + PRESETS
 * resumes it. OCT on a synth's roll is the keyboard octave and moves the roll's view with it, ONE value (the user,
 * 2026-10-12: "the oct up / down keys in piano roll should actually shift the piano roll"; 2026-10-13: "can we make them
 * one value?"; the view is the pattern's fit moved by song.octave octaves; both OCT: track <-> MASTER); on the drum track ghost / hard, which a tapped drum step
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
 *                         the hot cell's between step-only and HOLD; SHIFT + the knob clears it; the button again: the
 *                         family's next page, past the last the first
 *   SHIFT held + a step   the step's p-locks cleared (the notes, nudge, fill, chance and motion stay; SHIFT is OCT- held,
 *                         and SHIFT + the knob its default). A step held, HOME tapped: the step cleared (held_clear).
 *                         Drum track: SEQ + a step clears it, SHIFT + a key is the pick below
 *   SHIFT held + keys     the drum track's pick: the keys play (ly_lock is let go while SHIFT is down, the sound only
 *                         stopped); a key picks its lane (lane_sel), the last upper key the ACCENT lane
 *   SEQ held + keys       a synth's notes become the next steps' (the core's pen: seq.c key_down)
 *   SEQ tapped again      the keys play with the screen up (the window keeps following); again: steps
 * With no step held the cards are the track's PATTERN row (and TOOLS, GEN, GEN OUT). Every step key pressed opens an undo level (undo_mark), as SLOOP's step entry. The logic
 * of the edits is SLOOP's (ui/sloop/ui_layers.c step_down / steps_held_*), copied: nothing in ui/sloop is called. */
#define STEP_LY_BIT (1u << 31)          /* seq.c ly_bit[LY_STEP]: no button holds it; STEP locks the layer (ly_lock) */

static struct {
    uint8_t page;                       /* the window: steps 16 page + 1 .. 16 page + 16 */
    uint8_t follow;                     /* the window follows the playhead while playing */
    uint8_t play;                       /* SEQ tapped on STEP: the keys play, the screen stays */
    uint8_t lock_pg;                    /* the held step's p-lock page: a PAGES row (a page button, or SELECT), LOCK_NONE else */
    uint8_t hp;                         /* the held step's card page: 0 the step, 1 its extras, then the p-lock pages */
    uint16_t held;                      /* the step keys down (white key w: step 16 page + w) */
    uint16_t fdown;                     /* the physical step keys down (a bit per white key: its finger) */
    uint8_t fabs[16];                   /* the step each finger holds (absolute: a held step moved by PRESETS leaves its key) */
    uint8_t nsel;                       /* a held step of several notes: the note SELECT picked (ALGORITHM moves that one) */
    uint16_t pend;                      /* the step keys down that toggle their step when let go: a short press, nothing edited */
    uint32_t sess;                      /* the undo session of the step key pressed last */
    uint32_t t_down;                    /* fm1_ms when the first of the keys held went down (step_held_long) */
    uint32_t t_key[16];                 /* fm1_ms when each step key went down (a tap: let go before op_hold_ms) */
    uint8_t ph_drawn;                   /* the playhead strip as drawn (op_stepdraw.c) */
    uint8_t pk;                         /* the note picker: the step picked (KNOB 1, the cards down), PK_NONE none */
    uint8_t pk_trk;                     /* ... on this track */
    uint8_t latch;                      /* SAVE on a picked note: its step held as by a finger (its cards); HOME lets go */
} st = {.follow = 1, .lock_pg = 0xFF, .ph_drawn = 0xFF, .pk = 0xFF};
#define LOCK_NONE 0xFFu
#define PK_NONE 0xFFu
static void hp_sync(void);
static void hp_save(void);
static void hp_reopen(void);
static void pk_unlatch(void);
static int pk_ok(void);

static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};   /* the levels, softest first */
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }
static int step_on(const step_t *s) { return s->time == ST_NOTE && s->n; }
static void step_clear(step_t *s)
{
    memset(s, 0, sizeof *s);
    s->time = ST_REST;
}
static uint32_t step_pages(const track_t *t) { return (trk_len(t) + 15u) / 16u; }

/* the drum track's lane pick on STEP: none since 2026-10-16 (the user: the black keys select the lane, the white keys are
 * always steps; was SHIFT (OCT-) + a white key) */
static int step_pick(uint32_t held)
{
    (void)held;
    return 0;
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
    if (st.latch && (ui.scr != SCR_STEP || !pk_ok()))
        pk_unlatch();                                   /* (the picked note's cards: another track, lane or screen lets go) */
    if (st.follow && song.playing && !st.held)
        st.page = (uint8_t)((t->seq_idx % trk_len(t)) / 16u);
    if (st.page >= n)
        st.page = (uint8_t)(n - 1u);
}
static void step_scroll(int32_t d)                      /* SHIFT + OCT- / OCT+ */
{
    st.page = (uint8_t)clamp((int32_t)st.page + d, 0, (int32_t)step_pages(TSEL) - 1);
    st.follow = 0;
    ui.cards_t = 0;                                     /* (the grid is what is looked at: the cards go, the header names the window) */
}
static void step_reset(void)                            /* STEP entered or left: nothing held, the keys are steps */
{
    if (st.held)
        hp_save();                                      /* (left with a step held: its page is the one to reopen) */
    st.held = st.pend = st.fdown = 0;
    st.play = 0;
    st.lock_pg = LOCK_NONE, st.hp = 0;
    st.pk = PK_NONE, st.latch = 0;                      /* (the picker starts with nothing picked) */
}

/* ---- the pick: what a tapped step takes (drums the selected lane, synths the core's pen: the notes played last),
 * moved by the octaves OCT- / OCT+ turned since it was played (the user, 2026-10-11: step entry follows the keyboard
 * octave, as live playing does) */
static uint32_t pick_note(uint32_t i)                   /* the pick's note i (0..3) */
{
    int32_t n = pen_n ? pen_note[i & 3u] : last_note;
    return (uint32_t)clamp(n + 12 * ((int32_t)song.octave - pen_oct), 0, 127);
}
static uint32_t pick_base(void)                         /* the pick's note 0 with no octave applied (the roll's view adds song.octave itself) */
{
    int32_t n = pen_n ? pen_note[0] : last_note;
    return (uint32_t)clamp(n + 12 * (0 - pen_oct), 0, 127);
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
    if (is_drum(t)) {                                   /* (SHIFT held: a ghost hit, SHIFT2 a hard one: the shifts' less / more) */
        uint32_t lv = fm1_in.buttons & (1u << panel.btn[B_OCTDN]) ? LV_GHOST : fm1_in.buttons & (1u << panel.btn[B_OCTUP]) ? LV_HARD : LV_NORM;
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
#if FELUCCA_AUTO
            (void)step_mute_set(t, idx, lane_selected(), 0);   /* (and its mute) */
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
        if ((e->place & AUTO_STEP) == idx && (e->place & AUTO_ONLY) && !AUTO_PSEUDO(e->param) && !AUTO_IS_MUTE(e->param))
            n++;                                        /* (a muted note stays muted: the note's, not a lock) */
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
/* a step key down / up (seq.c lk_q, LY_STEP): white key w is step 16 page + w, on STEP and, on the drum track, on every
 * page (the user, 2026-10-16: "while in drum mode they are always the step seq"; seq.c sends them here); the drum
 * track's black keys on STEP select their lane (stopped: it is heard; seq.c plays them off STEP) */
static void lane_pick_from(uint32_t l, int key);        /* op_input.c: the lane selected (its preview) */
static void step_key(uint32_t k, uint32_t down, uint32_t held)
{
    track_t *t = TSEL;
    int32_t w = punch_key(k);
    uint32_t idx, bit;
    if (w < 0 && down && is_drum(t) && lane_key(k)) {
        lane_pick_from(lane_of(k), 1);
        if (lane_of(k) != DRUM_ACC && !song.playing && !transport_req)
            audition_lane(lane_of(k));                  /* (the ACCENT lane has no sound to preview) */
        op_repaint();
        return;
    }
    if (w < 0 || w > 15)
        return;
    if (down && ui.scr != SCR_STEP)
        step_tick();                                    /* (off STEP: the window FOLLOW keeps, inside LEN) */
    bit = 1u << w;
    idx = st.page * 16u + (uint32_t)w;
    if (!down) {
        if (!((st.fdown >> w) & 1u))
            return;
        st.fdown &= (uint16_t)~bit;
        idx = st.fabs[w];                               /* (the step under this finger: PRESETS may have moved it) */
        bit = idx / 16u == st.page ? 1u << (idx % 16u) : 0u;
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
    if ((ui.scr != SCR_STEP && !is_drum(t)) || op_armed() || idx >= trk_len(t))
        return;                                         /* (a question waits: the keys do nothing) */
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (st.latch)
        pk_unlatch();                                   /* (a step key: the picked note's cards let go, the fingers' own) */
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
    if (!st.held) {
        st.t_down = fm1_ms;                             /* (the press that may become a hold) */
        st.nsel = 0;
        hp_reopen();                                    /* (its cards open on the page last used on this track) */
    }
    st.held |= (uint16_t)bit;
    st.fdown |= (uint16_t)bit;
    st.fabs[w] = (uint8_t)idx;
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

/* ---- a held step moved (the user, 2026-10-16): PRESETS moves it left / right in time, ALGORITHM up / down (a synth step's
 * note a semitone, a drum hit to the lane above / below as drawn). The events of the step (its locks, motion, nudge,
 * chance, condition, fill) go with it; the neighbour it passes takes its place (a swap), nothing is lost. A held step that
 * would pass the pattern's first or last step, or a tied note's tie, stays and says so; the held keys follow their steps
 * (st.fabs), the window pages when they cross its edge. Several held: they move together, in one undo level. */
static int held_note_sel(uint32_t idx)                  /* the held synth step idx of several notes: the note SELECT picked, else -1 */
{
    const step_t *s;
    if (is_drum(TSEL) || ui.scr != SCR_STEP || idx / 16u != st.page || !((st.held >> (idx % 16u)) & 1u))
        return -1;
    s = &TSEL->step[idx % NSTEP];
    return step_on(s) && s->n > 1u ? (int)(st.nsel < s->n ? st.nsel : s->n - 1u) : -1;
}
static int32_t held_note(void)                          /* the note being edited on the first step held (synth), -1 none */
{
    int32_t i = held_first();
    const step_t *s;
    if (i < 0 || is_drum(TSEL))
        return -1;
    s = &TSEL->step[i];
    return step_on(s) ? s->note[s->n > 1u ? (st.nsel < s->n ? st.nsel : s->n - 1u) : 0u] : -1;
}
static int32_t roll_fold_step(const track_t *t, int32_t note, int32_t s);   /* op_stepdraw.c: FOLD's next row */
static void held_pitch_one(track_t *t, uint32_t idx, int32_t s)
{
    step_t *x = &t->step[idx];
    uint32_t k;
    if (!step_on(x))
        return;
    k = x->n > 1u ? (st.nsel < x->n ? st.nsel : x->n - 1u) : 0u;
    x->note[k] = (uint8_t)clamp(roll_fold_step(t, x->note[k], s), 0, 127);   /* (folded: row to row) */
}
static void held_pitch(int32_t s)                       /* ALGORITHM on a held synth step: the selected note (the only one) up / down */
{
    if (held_first() < 0)
        return;
    st.hp = 0;
    st.lock_pg = LOCK_NONE;
    held_each(held_pitch_one, s);
    step_cards_show();
    ui.hot = 0;
    ui.hot_lit = 1;
}
#if FELUCCA_AUTO
static void ev_block_shift(track_t *t, uint32_t a, uint32_t n, int32_t d)   /* the events of steps a .. a+n-1 move d (+-1), the step passed takes the vacated one */
{
    auto_list_t *l = AL(t);
    uint32_t i;
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        uint32_t s = l->ev[i].place & AUTO_STEP, ns = s;
        if (s >= a && s < a + n)
            ns = (uint32_t)((int32_t)s + d);
        else if (d > 0 && s == a + n)
            ns = a;
        else if (d < 0 && s + 1u == a)
            ns = a + n - 1u;
        l->ev[i].place = (uint8_t)((l->ev[i].place & ~AUTO_STEP) | ns);
    }
    auto_touch(trk_index(t));
}
#endif
static void step_rotate(track_t *t, uint32_t a, uint32_t n, int32_t d)   /* the note a (n steps with its ties) one step on, the neighbour into its place */
{
    step_t tmp;
    if (d > 0) {
        tmp = t->step[a + n];
        memmove(&t->step[a + 1u], &t->step[a], n * sizeof(step_t));
        t->step[a] = tmp;
    } else {
        tmp = t->step[a - 1u];
        memmove(&t->step[a - 1u], &t->step[a], n * sizeof(step_t));
        t->step[a + n - 1u] = tmp;
    }
}
static void dlane_swap(track_t *t, uint32_t a, uint32_t b, uint32_t l)   /* lane l's hit (level, ratchet, its locks and motion) of steps a and b exchanged */
{
    dstep_t *x = &t->dstep[a], *y = &t->dstep[b];
    uint32_t hx = (uint32_t)dstep_has(x, l), lx = dstep_lvl(x, l), rx = dstep_rat(x, l);
    uint32_t hy = (uint32_t)dstep_has(y, l), ly = dstep_lvl(y, l), ry = dstep_rat(y, l);
    if (hy)
        dstep_set(x, l, ly, ry);
    else
        dstep_clr(x, l);
    if (hx)
        dstep_set(y, l, lx, rx);
    else
        dstep_clr(y, l);
#if DLA_ON
    if (l < DRUM_LANES) {
        auto_list_t *al = AL(t);
        uint32_t i;
        for (i = 0; i < al->n && i < AUTO_MAX; i++) {
            uint32_t s = al->ev[i].place & AUTO_STEP;
            if (AUTO_IS_LANE(al->ev[i].param) && AUTO_LANE_L(al->ev[i].param) == l && (s == a || s == b))
                al->ev[i].place = (uint8_t)((al->ev[i].place & ~AUTO_STEP) | (s == a ? b : a));
        }
        auto_touch(trk_index(t));
    }
#endif
#if FELUCCA_AUTO
    if (l < DRUM_LANES) {                               /* (a muted hit stays muted where it goes) */
        int mx = step_muted(t, a, l), my = step_muted(t, b, l);
        if (mx != my)
            (void)step_mute_set(t, a, l, (uint32_t)my), (void)step_mute_set(t, b, l, (uint32_t)mx);
    }
#endif
}
/* the steps the fingers hold, in the order they move (d > 0: the last first, so a held neighbour is not passed twice) */
static uint32_t held_order(uint32_t len, int32_t d, uint8_t *w)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < 16u; i++)
        if (((st.fdown >> i) & 1u) && st.fabs[i] < len)
            w[k++] = (uint8_t)i;
    for (i = 1; i < k; i++) {
        uint8_t x = w[i];
        for (j = i; j > 0 && (d > 0 ? st.fabs[w[j - 1u]] < st.fabs[x] : st.fabs[w[j - 1u]] > st.fabs[x]); j--)
            w[j] = w[j - 1u];
        w[j] = x;
    }
    return k;
}
static uint32_t held_block(const track_t *t, uint32_t a)    /* the steps a held step moves with: a note and its ties, a hit 1; 0 a tie (stays) */
{
    if (is_drum(t))
        return 1u;
    if (t->step[a].time == ST_TIE)
        return 0u;
    return step_on(&t->step[a]) ? step_note_len(t, a) : 1u;
}
static const char *held_move_refused(const track_t *t, int32_t d, const uint8_t *w, uint32_t k)   /* why not (the toast), 0 fine */
{
    uint32_t len = trk_len(t), i;
    int32_t lo = 1 << 20, hi = -1;
    for (i = 0; i < k; i++) {
        uint32_t a = st.fabs[w[i]], bl = held_block(t, a), tg;
        if (!bl)
            return "TIED NOTE";
        if (d > 0 ? a + bl >= len : a == 0u)
            return d > 0 ? "LAST STEP" : "FIRST STEP";
        tg = d > 0 ? a + bl : a - 1u;
        if (!is_drum(t) && (t->step[tg].time == ST_TIE || (tg + 1u < len && t->step[tg + 1u].time == ST_TIE && d > 0)))
            return "TIED NOTE";
        lo = (int32_t)a + d < lo ? (int32_t)a + d : lo;
        hi = (int32_t)a + d > hi ? (int32_t)a + d : hi;
    }
    return lo / 16 != hi / 16 ? "PAGE EDGE" : 0;
}
static int held_move(int32_t d)                         /* every step held one step left (-1) / right (+1); 1 moved */
{
    track_t *t = TSEL;
    uint8_t w[16];
    uint32_t k = held_order(trk_len(t), d, w), i, lo = 255u;
    const char *why;
    if (!k)
        return 0;
    why = held_move_refused(t, d, w, k);
    if (why) {
        ui_message(why);
        return 0;
    }
    undo_mark(t, st.sess);
    fm1_irq_off();
    for (i = 0; i < k; i++) {
        uint32_t a = st.fabs[w[i]], bl = held_block(t, a);
        if (is_drum(t)) {
            dlane_swap(t, a, (uint32_t)((int32_t)a + d), lane_selected());
        } else {
            step_rotate(t, a, bl, d);
#if FELUCCA_AUTO
            ev_block_shift(t, a, bl, d);
#endif
        }
    }
    fm1_irq_on();
    st.pend = 0;
    st.held = 0;
    for (i = 0; i < k; i++) {
        st.fabs[w[i]] = (uint8_t)((int32_t)st.fabs[w[i]] + d);
        lo = st.fabs[w[i]] < lo ? st.fabs[w[i]] : lo;
    }
    if (lo / 16u != st.page)
        st.page = (uint8_t)(lo / 16u), st.follow = 0;
    for (i = 0; i < k; i++)
        st.held |= (uint16_t)(1u << (st.fabs[w[i]] % 16u));
    sync_reload = 1;
    step_cards_show();
    return 1;
}
static void held_moves(int32_t s)                       /* PRESETS: s detents of one step */
{
    int32_t d = s > 0 ? 1 : -1, n = s > 0 ? s : -s;
    while (n-- > 0 && held_move(d))
        ;
}
#if DLA_ON
static void lane_events_move(track_t *t, uint32_t idx, uint32_t l, uint32_t l2)   /* the hit's locks and motion follow it to lane l2 (none for the ACCENT lane) */
{
    auto_list_t *al = AL(t);
    uint32_t i, v;
    if (l2 < DRUM_LANES) {
        (void)lane_step_drop(t, idx, l2);
        for (v = 0; v < DE_N; v++)
            (void)lane_hold_drop(t, idx, l2, v);
    }
    for (i = 0; i < al->n && i < AUTO_MAX; i++) {
        uint8_t p = al->ev[i].param;
        if ((al->ev[i].place & AUTO_STEP) == idx && l < DRUM_LANES && AUTO_IS_LANE(p) && AUTO_LANE_L(p) == l && l2 < DRUM_LANES)
            al->ev[i].param = (uint8_t)AUTO_LANE(l2, AUTO_LANE_V(p));
    }
    if (l < DRUM_LANES && l2 >= DRUM_LANES) {
        (void)lane_step_drop(t, idx, l);
        for (v = 0; v < DE_N; v++)
            (void)lane_hold_drop(t, idx, l, v);
    }
    auto_touch(trk_index(t));
}
#endif
static int held_lane_one(track_t *t, uint32_t idx, uint32_t l, uint32_t l2)   /* 1 moved */
{
    dstep_t *x = &t->dstep[idx];
    uint32_t lv, rt;
    if (!dstep_has(x, l) || dstep_has(x, l2))
        return 0;
    lv = dstep_lvl(x, l), rt = dstep_rat(x, l);
    dstep_clr(x, l);
    dstep_set(x, l2, lv, rt);
#if FELUCCA_AUTO
    if (step_muted(t, idx, l))                          /* (a muted hit: muted on its new lane; the ACCENT lane has no mute) */
        (void)step_mute_set(t, idx, l, 0), (void)step_mute_set(t, idx, l2, 1);
#endif
#if DLA_ON
    lane_events_move(t, idx, l, l2);
#endif
#if FELUCCA_PLOCK
    if (l == DRUM_ACC)
        lock_drop(t, idx, P_DACC);
#endif
    return 1;
}
static int held_lane(int32_t d)                         /* ALGORITHM on a held drum step: its hit one lane up (+1) / down (-1) as drawn; 1 moved */
{
    track_t *t = TSEL;
    uint32_t l = lane_selected(), l2, i, n = 0;
    uint8_t w[16];
    uint32_t k = held_order(trk_len(t), 1, w);
    if (!k)
        return 0;
    if (d > 0 ? l == 0u : l + 1u >= DRUM_UILANES) {
        ui_message(d > 0 ? "TOP LANE" : "LAST LANE");
        return 0;
    }
    l2 = d > 0 ? l - 1u : l + 1u;
    undo_mark(t, st.sess);
    st.pend &= (uint16_t)~st.held;
    fm1_irq_off();
    for (i = 0; i < k; i++)
        n += (uint32_t)held_lane_one(t, st.fabs[w[i]], l, l2);
    fm1_irq_on();
    if (!n) {
        ui_message("LANE TAKEN");                       /* (no hit to move, or the lane above / below hits there already) */
        return 0;
    }
    lane_select(l2);
    sync_reload = 1;
    step_cards_show();
    return 1;
}
static void held_algo(int32_t s)                        /* ALGORITHM with a step held: pitch (synths), lane (drums) */
{
    int32_t d = s > 0 ? 1 : -1, n = s > 0 ? s : -s;
    if (!is_drum(TSEL)) {
        held_pitch(clamp(s, -12, 12));
        return;
    }
    while (n-- > 0 && held_lane(d))
        ;
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
/* ---- the note picker (the user, 2026-10-17): STEP with its cards down, nothing held. KNOB 1 picks a note (the drum
 * track: a hit of the selected lane), KNOB 2 moves it a step (SHIFT held: its nudge), KNOB 3 its pitch (the drum track:
 * its lane), KNOB 4 its length (the drum track: its level); SAVE its cards as if its step were held (the step, its
 * extras, its p-locks; HOME lets go), SAVE held muted / not (seq/auto.h AUTO_MUTE), HOME held deleted. The edits are the
 * held step's (held_*), run on the picked step; it is drawn framed (op_stepdraw.c) */
static int pk_note(const track_t *t, uint32_t idx)       /* a note starts at idx (what KNOB 1 walks) */
{
    if (idx >= trk_len(t))
        return 0;
    return is_drum(t) ? dstep_has(&t->dstep[idx], lane_selected()) : step_on(&t->step[idx]);
}
static int pk_ok(void) { return st.pk != PK_NONE && st.pk_trk == song.sel && pk_note(TSEL, st.pk); }
static void pk_hold(uint32_t *sv)                       /* the picked step held as by a finger (sv: what was) */
{
    uint32_t w = st.pk % 16u;
    sv[0] = st.held, sv[1] = st.fdown, sv[2] = st.pend, sv[3] = st.fabs[w];
    st.page = (uint8_t)(st.pk / 16u);
    st.held = st.fdown = (uint16_t)(1u << w);
    st.pend = 0;
    st.fabs[w] = st.pk;
}
static void pk_free(const uint32_t *sv)                 /* ... let go: the pick follows the step (a move), the window it */
{
    uint32_t w = st.pk % 16u;
    st.pk = st.fabs[w];
    st.held = (uint16_t)sv[0], st.fdown = (uint16_t)sv[1], st.pend = (uint16_t)sv[2], st.fabs[w] = (uint8_t)sv[3];
    st.page = (uint8_t)(st.pk / 16u);
    st.follow = 0;
    ui.cards_t = 0;                                     /* (the held edits show the cards: the picker's are the grid) */
    op_repaint();
}
static void pk_walk(int32_t s)                          /* KNOB 1: s notes on, stopping at the first / last */
{
    const track_t *t = TSEL;
    int32_t len = (int32_t)trk_len(t), d = s > 0 ? 1 : -1, n = s > 0 ? s : -s, i, j;
    if (st.pk != PK_NONE && st.pk_trk == song.sel && st.pk < (uint32_t)len)
        i = st.pk;                                      /* (from the pick, picked or deleted) */
    else
        i = d > 0 ? (int32_t)st.page * 16 - 1 : (int32_t)st.page * 16 + 16;   /* (none: from the window's edge) */
    while (n-- > 0) {
        for (j = i + d; j >= 0 && j < len && !pk_note(t, (uint32_t)j); j += d)
            ;
        if (j < 0 || j >= len)
            break;
        i = j;
    }
    if (i < 0 || i >= len || !pk_note(t, (uint32_t)i)) {
        ui_message(is_drum(t) ? "NO HITS ON THIS LANE" : "NO NOTES");
        return;
    }
    if ((uint32_t)i != st.pk || st.pk_trk != song.sel)
        st.sess = (undo_sess += 4u) | 3u;               /* (a note picked: its edits one undo level) */
    st.pk = (uint8_t)i, st.pk_trk = song.sel, st.nsel = 0;
    st.page = (uint8_t)(i / 16), st.follow = 0;
    op_repaint();
}
static void pk_knob(uint32_t k, int32_t s, int shift)   /* a knob turned on STEP with its cards down */
{
    uint32_t sv[4];
    int32_t d = s > 0 ? 1 : -1, n = s > 0 ? s : -s;
    if (k == 0u || !pk_ok()) {
        if (k == 0u)
            pk_walk(s);
        else
            ui_message("PICK A NOTE: KNOB 1");
        return;
    }
    pk_hold(sv);
    if (k == 1u && shift) {
#if FELUCCA_MICRO
        held_each(edit_nudge, clamp(s, -16, 16));       /* (SHIFT: the nudge, finer than a step) */
#else
        ui_message("NO NUDGE IN THIS BUILD");
#endif
    } else if (k == 1u) {
        while (n-- > 0 && held_move(d))
            ;
    } else if (k == 2u && is_drum(TSEL)) {
        while (n-- > 0 && held_lane(-d))                /* (right: the lane under it, as drawn) */
            ;
    } else if (k == 2u) {
        held_each(held_pitch_one, clamp(s, -12, 12));
    } else {
        held_edit(is_drum(TSEL) ? 0u : 3u, s);          /* (drums: LEVEL; synths: LENGTH) */
    }
    pk_free(sv);
}
static int pk_save(void)                                /* SAVE tapped: the picked note's cards; 1 done */
{
    if (ui.scr != SCR_STEP || ui.cards_t || st.held || op_armed() || !pk_ok())
        return 0;                                       /* (the cards up: SAVE is theirs, the tools' verb) */
    st.page = (uint8_t)(st.pk / 16u);
    st.held = st.fdown = (uint16_t)(1u << (st.pk % 16u));
    st.fabs[st.pk % 16u] = st.pk;
    st.pend = 0;
    st.latch = 1;
    st.t_down = fm1_ms - op_hold_ms();                  /* (held long: its cards up, as a held step's) */
    st.nsel = 0;
    hp_reopen();
    step_cards_show();
    return 1;
}
static void pk_unlatch(void)                            /* HOME tapped, a step key, STEP left: the picked note's cards go */
{
    uint32_t w;
    if (!st.latch)
        return;
    for (w = 0; w < 16u; w++)
        if ((st.held >> w) & 1u)
            st.pk = st.fabs[w];                         /* (moved while held: the pick follows it) */
    st.latch = 0;
    hp_save();
    st.held = st.fdown = st.pend = 0;
    st.lock_pg = LOCK_NONE, st.hp = 0;
    ui.cards_t = 0;
    op_repaint();
}
static int pk_mute(void)                                /* SAVE held alone: the picked note muted / not; 1 done */
{
    track_t *t = TSEL;
    uint32_t l = lane_selected();
    if (ui.scr != SCR_STEP || !pk_ok())
        return 0;
#if FELUCCA_AUTO
    if (is_drum(t) && l >= DRUM_LANES) {
        ui_message("NO MUTE ON ACCENT");
        return 1;
    }
    {
        uint32_t on = !step_muted(t, st.pk, l);
        int ok;
        undo_mark(t, st.sess);
        fm1_irq_off();
        ok = step_mute_set(t, st.pk, l, on);
        fm1_irq_on();
        ui_message(!ok ? "PATTERN FULL" : on ? "NOTE MUTED" : "NOTE ON");
    }
#else
    (void)t, (void)l;
    ui_message("NO MUTE IN THIS BUILD");
#endif
    op_repaint();
    return 1;
}
static int pk_delete(void)                              /* HOME held alone: the picked note deleted; 1 done */
{
    track_t *t = TSEL;
    if (ui.scr != SCR_STEP || !pk_ok())
        return 0;
    if (st.latch)
        pk_unlatch();
    undo_mark(t, st.sess);
    fm1_irq_off();
    step_wipe(t, st.pk, 0);                             /* (the drum track: the lane's hit; a synth: the note and its ties) */
    fm1_irq_on();
    sync_reload = 1;
    ui_message("NOTE DELETED");                         /* (the pick stays there: KNOB 1 goes on from it) */
    op_repaint();
    return 1;
}
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
 * OP_RESET (SHIFT + the knob) drops the step's events of the cell, both kinds */
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
    uint32_t nl = st.hp >= HP_BASE ? hp_locks(ix) : 0u, k = st.hp >= HP_BASE ? st.hp - HP_BASE : 0u;
    /* (st.hp past the pages the step has now, its locks gone meanwhile: ix[k] was never written, a wild PAGES index:
     * tests/audio_hang_stress.c under ASan) */
    const page_t *pg = st.hp >= HP_BASE ? &PAGES[k < nl ? ix[k] : 0] : 0;
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
    if (kd == 3)                                        /* (- 1:2 .. 4:4, stopping at both ends; SHIFT + the knob: -) */
        held_each(edit_cond, s == OP_RESET ? 0 : clamp((int32_t)step_cond(TSEL, (uint32_t)i) + s, 0, (int32_t)AUTO_NCOND));
#endif
}

/* ---- the screen: rows, cells, turns, YES */
/* STEP's pages (the user: "the first SEQ menu should have the length of the pattern"): PATTERN (LEN DIV SWING GATE) on
 * every track, then TOOLS, GEN and GEN OUT. No ARP rows (ARP has its own button and page) and no drum LANE page (LEVEL TUNE
 * DECAY REV: sound values, not the sequencer's; the user, 2026-10-14: "why do we have an arpeggio section in the SEQ",
 * "on the sequencer pages i have items to edit the engine sound"); the sound pages stay on a held step as p-locks. SELECT pages the four knobs only and
 * never changes the lane; the lane is ALGORITHM's (DR, then the lanes) or SHIFT + a key's (the user, 2026-10-12: "in step
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
static uint32_t step_rows0(void) { return 1u; }   /* the pages before TOOLS, GEN, GEN OUT: PATTERN */
#if FELUCCA_SEQTOOLS
#define STP_TL 3u                                       /* the last three pages: TOOLS, GEN, GEN OUT (op_tools.c) */
#else
#define STP_TL 0u
#endif
static uint32_t step_rows(void) { return step_rows0() + STP_TL; }
static int stp_tl(uint32_t r) { return STP_TL && r >= step_rows0() ? (int)(r - step_rows0()) : -1; }   /* 0 TOOLS, 1 GEN, 2 GEN OUT */
static void step_name(uint32_t r, char *b)
{
    if (stp_tl(r) >= 0)
        str_cpy(b, stp_tl(r) == 2 ? "GEN OUT" : stp_tl(r) ? "GEN" : "TOOLS", 12);
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
    if ((s = panel_enc(EN_SELECT)) != 0) {
        turned = 1;
        int32_t n = held_note_sel((uint32_t)held_first()) >= 0 ? (int32_t)TSEL->step[held_first()].n : 0;
        int32_t v = (int32_t)(st.nsel < n ? st.nsel : n - 1) + s;
        if (n && st.hp == 0u && v < n) {                /* a step of several notes: SELECT picks the note ALGORITHM moves; past */
            st.nsel = (uint8_t)clamp(v, 0, n - 1);      /* the last one it pages the cards, back from them the last note again */
            st.pend &= (uint16_t)~st.held;
            step_cards_show();
            ui.force = 1;
        } else {                                        /* SELECT pages the held step's cards */
            hp_set((uint32_t)clamp((int32_t)st.hp + s, 0, (int32_t)hp_count() - 1));
        }
    }
    if ((s = panel_enc(EN_ALGO)) != 0) {                /* (no other track while a step is held: it moves the held step up / down) */
        turned = 1;
        if (!op_armed() && !rh.ring)
            held_algo(s);
    }
    if ((s = panel_enc(EN_PRESET)) != 0) {              /* (the window pages with no step held: it moves the held step in time) */
        turned = 1;
        if (!op_armed() && !rh.ring)
            held_moves(s);
    }
    return turned;
}
static void op_row_pick(uint32_t r);                    /* op_input.c */
/* SEQ let go with nothing else done: STEP (on PATTERN); on STEP, tapped again its next page, past the last the first (section 2's
 * paging rule); held alone past HOLD (long), the keys between steps and playing */
/* STEP with its cards down (no step held): the knobs are the grid's, a SEQ tap or a SELECT turn only brings the cards back */
static int step_cards_down(void) { return ui.scr == SCR_STEP && !ui.cards_t && !st.held; }
static void step_seq_tap(uint32_t lng)
{
    if (ui.scr != SCR_STEP) {
        op_enter(SCR_STEP);
        return;
    }
    if (!lng && step_cards_down()) {
        step_cards_show();                              /* (the cards back, the page kept) */
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
    st.held = st.pend = st.fdown = 0;
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
