/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP's panel (docs/UI-OPTIMIST-DESIGN.md 4.2 STEP): the window's 16 steps as columns under the keys.
 *   the drum track   the 16 lanes (and the ACCENT lane, 17) x the 16 steps: a hit in its lane's colour, its level as the shade (ghost dim ..
 *                    hard full), its ratchet as notches; the selected lane's row lit; at the left each lane's colour
 *                    chip and two-letter code (LANE_CODE, as the mixer), the selected lane's white
 *   a synth track    a mini piano roll: the notes stacked (a chord: several in a column), the ties as lines after
 *                    their note, the level as the shade, the ratchet as notches; the Cs as faint lines
 * A step with automation (a lock or motion event, op_step_auto) is drawn in the automation colour (C_AUTO) instead of its lane's or the track's.
 * Both: black ground, the beats ruled by thin lines, the steps beyond LEN empty, the steps held framed white; the drums'
 * empty steps a small dot each, a beat's first a little bigger (op_graph.c step_dot, as Melodee); under the
 * grid the events of the automation store (seq/auto.h; FELUCCA_AUTO: a dot under each step with one, in the lane's or
 * the roll's colour, a hold event with a tail to the next hold event of its parameter, or to LEN), then the playhead
 * strip (a canvas of its own, so a playing grid is drawn again only when a step changes; when the playhead is outside
 * the window, a mark at the side it is on). One 240 x 123 canvas: gfx.c's band. */
#define SG_X 26                         /* the first column's x (the gutter left of it: the lanes' codes, the note names);
                                         * a column is SG_CW wide, its cell SG_CW - 2 (the roll's note SG_CW - 1) */
#define SG_CW 13
#define SG_TOP 1
#define SP_Y 25                         /* the panel: from under the header to the help band (OP_FOOT), no cards (they overlay it) */
#define SP_H (OP_FOOT_Y - SP_Y)
#define SG_H (SP_H - 11)                /* the 17 lanes' rows (12 px; 10 or 11 over the help band), the roll's height: the panel less the marks and the playhead */
#define SG_LY0(l) (SG_TOP + (int32_t)(l) * SG_H / (int32_t)DRUM_UILANES)   /* lane l's row: 12 or 13 px (17 lanes with the ACCENT lane: 12), the remainder spread out */
#define SG_LY(l) (sg_dy + SG_LY0(l))    /* ... as drawn: shifted while a held step's cards cover the selected lane (sg_shift) */
static int32_t sg_dy;
#define SG_LH (SG_H / (int32_t)DRUM_UILANES)   /* (the least a row is) */
#define SG_MK_Y (SG_TOP + SG_H + 2)     /* the events' marks: a row of 3 px */
#define SG_PH_Y (SG_MK_Y + 4)           /* the playhead strip, in the panel's last 4 rows */
#define SG_INFO_H 38                    /* a held step's two lines: part of the overlay, over the grid's foot */
#define SG_INFO_DY 18                   /* the two lines' pitch */
#define SG_INFO_Y (SG_MK_Y - 2 - SG_INFO_H)

/* a note generator on (seq/gen.c): the steps as they play, drawn instead of the pattern's; a generated hit or note
 * outlined, a pattern's hit the generator silences a dim stub (stored and played: solid, as ever) */
#if GEN_ON
/* rendered into the generator's WRITE buffer (seq/gen.c gen_wbuf, 896 B): WRITE's two halves run in one key handler
 * (op_tools.c, ed_tools.c: prep, then commit with the interrupts off), no frame is drawn between them, and a draw
 * renders its own steps first (sg_prep): one buffer, not two (2026-10-24) */
#define sg_gst gen_wbuf
static const step_t *sg_st;             /* the steps drawn (0: the pattern's) */
static void sg_prep(const track_t *t)
{
    sg_st = (gen_trk >> trk_index(t)) & 1u ? sg_gst : 0;
    if (sg_st)
        gen_render(t, sg_gst, ~0u);
}
#define SGS(t) (sg_st ? sg_st : (t)->step)
#define SGD(t) (sg_st ? (const dstep_t *)(const void *)sg_st : (t)->dstep)
#else
#define sg_prep(t) ((void)0)
#define sg_st ((const step_t *)0)
#define SGS(t) ((t)->step)
#define SGD(t) ((t)->dstep)
#endif
static void sg_notches(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rat)   /* ratchet x2..x4: 1..3 cuts in the cell */
{
    uint32_t i;
    for (i = 1; i <= rat; i++)
        cv_rect(x + (int32_t)i * w / (int32_t)(rat + 1u), y, 1, h, C_BLACK);
}
#if STEP_FILLS
/* the fill view (SEQ held, op_step.c step_fillview): a cell by its fill condition. FILL ONLY: full colour, hatched, framed white; NO FILL: hollow,
 * struck through (dropped in a fill); normal: dim. No text on a fill. ox: the canvas's x origin (the roll's column canvas) */
static void fv_cell(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t fc, int32_t ox)
{
    int32_t i;
    x -= ox;
    if (fc == FC_FILL) {
        cv_rect(x, y, w, h, c);
        for (i = 2; h >= 3 && i < w - 1; i += 3)
            cv_rect(x + i, y + 1, 1, h - 2, C_BLACK);
        cv_frame(x, y, w, h, C_WHITE);
    } else if (fc == FC_NOFILL) {
        cv_frame(x, y, w, h, c);
        cv_rect(x + 1, y + h / 2, w - 2, 1, c);
    } else {
        cv_rect(x, y, w, h, col_shade(c, 2u));
    }
}
#define FVIEW() step_fillview()
#else
#define FVIEW() 0
#endif
#define FILL_HID(t, idx, l) (!FVIEW() && fill_only((t), (idx), (l)))   /* (a FILL ONLY note or hit: drawn in the fill view only) */
static int32_t sg_ph = -1;              /* the playhead's column as painted, roll or drums (-1 none) */
/* a step with automation: a dot inside its cell or note bar, a white 6 x 6 ring with a black 4 x 4 centre (the ring shows on dark cells, the centre on
 * light and white ones, the grey muted too); the largest that fits (w x h) when the cell is smaller, under 3 a white and a black pixel. Drawn last,
 * over the playhead's white and the selected note's outline (the user, 2026-11-01: "put it inside the drum step ... in a visible colour") */
static void au_dot(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t s = w < h ? w : h;
    s = s > 6 ? 6 : s;
    if (s < 1)
        return;
    x += w > s + 1 ? 1 : 0;
    y += (h - s) / 2;
    cv_rect(x, y, s, s, C_WHITE);
    cv_rect(x + (s >= 3 ? 1 : 0), y + (s >= 3 ? 1 : 0), s >= 3 ? s - 2 : 1, s >= 3 ? s - 2 : 1, C_BLACK);
}
/* a drum lane's cell has automation (a lane's own event, or the track's on a cell with a hit) */
static int au_cell(const track_t *t, uint32_t idx, uint32_t l, int hit)
{
    return hit ? op_step_auto(t, idx, l) : op_step_auto(t, idx, l) && !op_step_auto(t, idx, 0xFEu);
}
static void sg_ground(const track_t *t)                 /* the beats, the steps past LEN, the columns held */
{
    uint32_t w, len = trk_len(t);
    for (w = 0; w < 16u; w++) {
        int32_t x = SG_X + (int32_t)w * SG_CW;
        if (st.page * 16u + w >= len)
            continue;
        if (!(w & 3u))                                  /* black ground, lines only: a beat's first column ruled at */
            cv_rect(x - 1, SG_TOP, 1, SG_H, C_LINE);    /* its left (each empty step: a dot in its lanes, sg_drums) */
        if (((st.held >> w) & 1u) || (!st.held && pk_ok() && st.pk == st.page * 16u + w))   /* (a held step white, the
                                                         * note picker's in the pick's colour) */
            cv_frame(x - 1, SG_TOP - 1, SG_CW, SG_H + 2, st.held ? C_WHITE : C_NOTE);
    }
}
/* (noinline: a call from sg_paint, which is smaller without it inlined) */
static __attribute__((noinline)) void sg_drums(const track_t *t)
{
    uint32_t l, w, sel = lane_selected(), len = trk_len(t);
    sg_prep(t);
    if (sg_ph >= 0)                                     /* the playing column: a faint white ground under the cells (its hits framed white below) */
        cv_rect(SG_X + sg_ph * SG_CW - 1, SG_TOP, SG_CW, SG_H, col_shade(C_WHITE, 2u));
    cv_hline(SG_LY(sel) - 1, C_DIM);          /* the selected lane's row: lined above and below */
    cv_hline(SG_LY(sel + 1u) - 1, C_DIM);
    sg_ground(t);
    for (l = 0; l < DRUM_UILANES; l++) {                /* (the 16 sounds, then the ACCENT lane's row) */
        int32_t y = SG_LY(l), lh = SG_LY(l + 1u) - y;
        uint16_t c = op_lane_col(l);
        if (y < SG_TOP || y + lh > SG_TOP + SG_H)
            continue;                                   /* (shifted out of the grid: sg_shift) */
        cv_rect(2, y, 3, lh - 1, l == sel ? C_WHITE : c);   /* the lane: its colour chip and its code, as the mixer's */
        cv_texts(6, y + (lh - 11) / 2 - 2, lane_code(l), l == sel ? C_WHITE : c);   /* (the user, 2026-10-11; its 10 rows of ink in the row) */
        for (w = 0; w < 16u; w++) {
            uint32_t idx = st.page * 16u + w;
            const dstep_t *d = &SGD(t)[idx % NSTEP];
            int32_t x = SG_X + (int32_t)w * SG_CW, own = dstep_has(&t->dstep[idx % NSTEP], l);
            if (idx >= len)
                continue;
            if (own && FILL_HID(t, idx, l)) {           /* (a FILL ONLY hit, the fill view down: not there) */
                step_dot(x, y, SG_CW - 2, lh - 1, !(w & 3u));
                continue;
            }
            if ((int32_t)w == sg_ph && dstep_has(d, l))    /* (the playhead's hit: framed white, its level shade or grey kept inside) */
                cv_frame(x - 1, y - 1, SG_CW, lh + 1, C_WHITE);
            if (!dstep_has(d, l)) {
                if (own)                                /* (silenced by a generator: a dim stub) */
                    cv_rect(x, y + lh - 3, SG_CW - 2, 2, col_shade(c, 2u));
                else                                    /* (empty: a small dot, a beat's bigger) */
                    step_dot(x, y, SG_CW - 2, lh - 1, !(w & 3u));
                continue;
            }
            if (!own) {                                 /* (a generator's hit: outlined) */
                cv_frame(x, y, SG_CW - 2, lh - 1, lvl_col(c, dstep_lvl(d, l)));
                continue;
            }
#if FELUCCA_AUTO
            if (step_muted(t, idx, l)) {                /* (a muted hit: kept, grey) */
                cv_rect(x, y, SG_CW - 2, lh - 1, mute_col());    /* (grey, whatever the lane's colour or level) */
                continue;
            }
#endif
#if STEP_FILLS
            if (FVIEW()) {                              /* (the fill view: the selected lane's hits by their condition, the other lanes dim) */
                fv_cell(x, y, SG_CW - 2, lh - 1, c, l == sel ? step_lane_fc(t, idx, l) : FC_NORM, 0);
                continue;
            }
#endif
            cv_rect(x, y, SG_CW - 2, lh - 1,
                    op_step_auto(t, idx, l) ? lvl_acol(dstep_lvl(d, l)) : lvl_col(c, dstep_lvl(d, l)));   /* (automated: its own colour) */
            sg_notches(x, y, SG_CW - 2, lh - 1, dstep_rat(d, l));
        }
        for (w = 0; w < 16u; w++) {                     /* the automation dots, over the cells (the playhead's, the muted's, a stub's too) */
            uint32_t idx = st.page * 16u + w;
            int own = dstep_has(&t->dstep[idx % NSTEP], l) && !FILL_HID(t, idx, l);
            if (idx < len && au_cell(t, idx, l, own || dstep_has(&SGD(t)[idx % NSTEP], l)))
                au_dot(SG_X + (int32_t)w * SG_CW, y, SG_CW - 2, lh - 1);
        }
    }
}
/* ---- the synth roll (the user, 2026-10-11: "looks a bit sad compared to sloop. could we have the note names on the
 * left, and more details ?"): the note names in a gutter on the left (the Cs with their octave, C4; every row when
 * the rows are tall enough, else the Cs and the rows with a note), the black keys' rows a faint tint (C_LINE at 2/8,
 * from the gutter on, as a keyboard's), the octaves ruled under each C, the bar's
 * start and the end of LEN in C_DIM, the beats in C_LINE, each note a bar as long as its ties, its level the shade,
 * an accent a white top edge, the ratchet as notches, the playhead's column white (its own canvas: a column is
 * painted again, not the band) */
#define RL_X SG_X                       /* the roll's columns: the drum grid's */
#define RL_CW SG_CW
static int32_t sg_ox;                   /* the x the canvas starts at: 0 the band, a column's for its own canvas */
#define RR(x, y, w, h, c) cv_rect((x)-sg_ox, y, w, h, c)
static int is_black(int32_t n) { return (0x54Au >> (n % 12)) & 1; }   /* C# D# F# G# A# */
static struct {
    int32_t lo, hi, rh, n;              /* the lowest and highest note shown, a row's height, the rows */
    int8_t row[128];                    /* each note's row from the bottom, -1 not shown (folded away or out of view) */
} rg;
static uint8_t roll_fold;               /* FOLD (both OCT tapped on a synth track's STEP, op_input.c): only the rows of the
                                         * pattern's notes, or the scale's when one is set (the user, 2026-10-16) */
static int roll_rv(int32_t nt) { return nt >= 0 && nt <= 127 && rg.row[nt & 127] >= 0; }   /* note nt has a row */
#define RV(nt) roll_rv(nt)
#define FOLD_MAX 51                     /* (4 px a row at least, as the unfolded roll's 51 semitones) */
static int roll_scale_has(int32_t n)    /* the song's scale (trk[0]'s, op_layers.c) holds note n */
{
    uint32_t root = (uint32_t)ui_clamp(trk[0].p[P_ROOT], 0, 11);
    return (SCALE_MASK[ui_clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)] >> (((uint32_t)n + 12u - root) % 12u)) & 1u;
}
/* FOLD's rows: every note the pattern plays (all its steps), with a scale set the scale's notes from the pattern's
 * lowest to its highest (an octave at least) too, and the edited note; an empty pattern the pick's; at most FOLD_MAX,
 * the lowest kept */
static void roll_fold_rows(const track_t *t, int32_t a, int32_t b)
{
    uint32_t i, j, len = trk_len(t), on[4] = {0, 0, 0, 0};
    int32_t n, k = 0, h = held_note();
    for (i = 0; i < len && i < NSTEP; i++) {
        const step_t *s = &SGS(t)[i];
        for (j = 0; step_on(s) && j < s->n && j < 4u; j++)
            on[s->note[j] >> 5] |= 1u << (s->note[j] & 31u);
    }
    for (n = a; trk[0].p[P_SCALE] && n <= b; n++)       /* (a scale set: its notes over the pattern's span) */
        if (roll_scale_has(n))
            on[n >> 5] |= 1u << (n & 31);
    if (h >= 0)
        on[h >> 5] |= 1u << (h & 31);
    for (i = 0; !(on[0] | on[1] | on[2] | on[3]) && i < (pen_n ? (uint32_t)pen_n : 1u) && i < 4u; i++)
        on[(pick_note(i) >> 5) & 3u] |= 1u << (pick_note(i) & 31u);   /* (an empty pattern: the pick's rows) */
    for (n = 0; n < 128; n++)
        rg.row[n] = -1;
    rg.lo = -1;
    for (n = 0; n < 128 && k < FOLD_MAX; n++)
        if ((on[n >> 5] >> (n & 31)) & 1u) {
            rg.row[n] = (int8_t)k++;
            rg.lo = rg.lo < 0 ? n : rg.lo;
            rg.hi = n;
        }
    rg.n = k ? k : 1;
    rg.lo = rg.lo < 0 ? 0 : rg.lo;
    rg.hi = k ? rg.hi : 0;
    rg.rh = SG_H / rg.n;
    rg.rh = rg.rh > 16 ? 16 : rg.rh;
}
/* the rows: the pattern's notes (every page alike), 13 semitones at least, the pick's around an empty one; the rows as
 * tall as fit (16 px at most), then as many more as the height holds, half above, half below */
static void roll_range(const track_t *t)
{
    uint32_t i, j, len = trk_len(t);
    int32_t a = 127, b = 0, rows, ex;
    for (i = 0; i < len && i < NSTEP; i++) {
        const step_t *s = &SGS(t)[i];
        if (!step_on(s))
            continue;
        for (j = 0; j < s->n && j < 4u; j++)
            a = s->note[j] < a ? s->note[j] : a, b = s->note[j] > b ? s->note[j] : b;
    }
    if (a > b)                                          /* (an empty pattern: around the pick) */
        a = b = (int32_t)pick_base();           /* (the view adds the octave below: the pick's own octave shift would count twice) */
    if (roll_fold) {                                    /* FOLD: the pattern's (and the scale's) rows only, no OCT view */
        for (b = b - a < 11 ? a + 11 : b; b > 127; a--, b--)
            ;
        roll_fold_rows(t, a < 0 ? 0 : a, b);
        return;
    }
    while (b - a < 12)
        a > 0 ? a-- : 0, b < 127 ? b++ : 0;
    if (b - a > 50)                                     /* (4 px a row at least: the lowest notes kept) */
        b = a + 50;
    rg.rh = SG_H / (b - a + 1);
    rg.rh = rg.rh > 16 ? 16 : rg.rh;
    rows = SG_H / rg.rh;                                /* (rh >= 4: no divide by 0) */
    ex = rows - (b - a + 1);
    a -= ex / 2;
    b += ex - ex / 2;
    if (a < 0)
        b -= a, a = 0;
    if (b > 127)
        a -= b - 127, b = 127;
    ex = song.octave * 12;                              /* OCT: the keyboard octave and the view are one value (the user, 2026-10-13),
                                                         * the view moved from the fit, kept inside 0..127 */
    ex = a + ex < 0 ? -a : b + ex > 127 ? 127 - b : ex;
    a += ex, b += ex;
    rg.lo = a < 0 ? 0 : a;
    rg.hi = b;
    ex = held_note();                                   /* the note being edited (a step held, or picked): always inside the rows, whatever OCT and the pattern say */
    if (ex < 0 && !st.held && pk_ok() && step_on(&t->step[st.pk]))
        ex = t->step[st.pk].note[sel_one(t, st.pk)];
    if (ex >= 0 && ex < rg.lo)
        rg.hi -= rg.lo - ex, rg.lo = ex;
    else if (ex > rg.hi)
        rg.lo += ex - rg.hi, rg.hi = ex;
    rg.n = rg.hi - rg.lo + 1;
    for (ex = 0; ex < 128; ex++)
        rg.row[ex] = (int8_t)(ex >= rg.lo && ex <= rg.hi ? ex - rg.lo : -1);
}
/* ALGORITHM on a held synth step (op_step.c held_pitch_one): note moved s detents. Folded, a detent is the next folded
 * row up / down (the user, 2026-10-16), one semitone past the top or bottom row; unfolded, a semitone */
static int32_t roll_fold_step(const track_t *t, int32_t note, int32_t s)
{
    int32_t d = s > 0 ? 1 : -1, n;
    if (!roll_fold)
        return note + s;
    sg_prep(t);
    roll_range(t);
    for (; s; s -= d) {
        for (n = note + d; n >= 0 && n <= 127 && !RV(n); n += d)
            ;
        note = n >= 0 && n <= 127 ? n : note + d;
    }
    return note;
}
/* the synth note selected (the user, 2026-10-17: "in seq for drum we can see the selected note super well, for synth tracks
 * not at all"): a held step's edited note, else the note picker's (nothing held): its step, pitch and the steps it sounds
 * (over its ties and carry steps); 0 none. Drawn in C_NOTE its whole length, its row lined, its name in C_NOTE */
static int roll_sel(const track_t *t, uint32_t *at, int32_t *nt, uint32_t *ext)
{
    int32_t i = held_first();
    const step_t *s;
    uint32_t k;
    if (i < 0 && !st.held && pk_ok())
        i = st.pk;
    if (i < 0 || is_drum(t))
        return 0;
    s = &t->step[i];
    if (!step_on(s))
        return 0;
    k = sel_one(t, (uint32_t)i);                        /* (a chord all selected: its lowest here, every one drawn: roll_selx) */
    *at = (uint32_t)i, *nt = s->note[k], *ext = (note_span(t, (uint32_t)i, k) + 127u) / 128u;
    return 1;
}
static int roll_selx(const track_t *t, uint32_t at, int32_t nt, uint32_t idx)   /* note nt drawn at step idx: one of the selected */
{
    const step_t *s = &t->step[at % NSTEP];
    uint32_t m = sel_mask(t, at), k;
    for (k = 0; k < s->n && k < 4u; k++)
        if (((m >> k) & 1u) && s->note[k] == nt && idx >= at && idx < at + (note_span(t, at, k) + 127u) / 128u)
            return 1;
    return 0;
}
static int32_t roll_ny(int32_t nt) { return SG_TOP + sg_dy + (rg.n - 1 - rg.row[nt & 127]) * rg.rh; }   /* note nt's row's y (sg_dy: the
                                                         * roll moved so the edited note clears the cards) */
#define NY(nt) roll_ny(nt)
static uint32_t roll_org(const track_t *t, uint32_t idx)   /* the note a step sounds: itself, or the one it ties */
{
    uint32_t k, len = trk_len(t);
    for (k = 0; k < len && SGS(t)[idx % NSTEP].time == ST_TIE; k++)
        idx = (idx + len - 1u) % len;
    return idx;
}
/* note slot i of step s, in the column of step idx: carried on from the step before (a carry step names a note that
 * sounds already: drawn on, no head) */
static int roll_carried(const track_t *t, const step_t *s, uint32_t idx, uint32_t i)
{
    uint32_t len = trk_len(t), p = (idx + len - 1u) % len, o, k;
    const step_t *q;
    if (!(s->flags & SF_CARRY) || s->rat)
        return 0;
    o = roll_org(t, p);
    q = &SGS(t)[o % NSTEP];
    for (k = 0; k < q->n && k < 4u && q->note[k] != s->note[i]; k++)
        ;
    return step_on(q) && k < q->n && k < 4u && !ngate_of(q, k);
}
/* the step where note slot i of step idx started: a note carried through (or tied to) idx takes its level and mute from the step that struck it,
 * never from the carry step (the user, 2026-11-01: a cycle on a step must not recolour the notes only held through it); *oi: its slot there */
static uint32_t roll_head(const track_t *t, uint32_t idx, uint32_t i, uint32_t *oi)
{
    uint32_t len = trk_len(t), n, k;
    idx = roll_org(t, idx);
    for (n = 0; n < len; n++) {
        const step_t *s = &SGS(t)[idx % NSTEP], *q;
        uint32_t o = roll_org(t, (idx + len - 1u) % len);
        if (!roll_carried(t, s, idx, i))
            break;
        q = &SGS(t)[o % NSTEP];
        for (k = 0; k < q->n && k < 4u && q->note[k] != s->note[i]; k++)
            ;
        idx = o, i = k;
    }
    *oi = i;
    return idx;
}
static void roll_label(int32_t n, uint16_t c)           /* a row's name, "C4" for a C, in the gutter */
{
    char b[6];
    if (n % 12 == 0)
        note_name(b, (uint32_t)n);
    else
        str_cpy(b, N_NOTE[n % 12], 3);
    cv_texts(2 - sg_ox, NY(n) + (rg.rh - 16) / 2, b, c);
}
/* the gutter: every row named when the rows are tall enough (11 px), else the Cs, then the rows with a note in the
 * window where they do not touch a name above */
static void roll_names(uint64_t used)
{
    int32_t n, last = -100;
    if (rg.rh >= 11) {
        for (n = rg.lo; n <= rg.hi; n++)
            if (RV(n))
                roll_label(n, n % 12 == 0 ? C_HI : ((used >> rg.row[n]) & 1u) ? C_AMB : is_black(n) ? C_DIM : C_GRAY);
        return;
    }
    for (n = rg.hi; n >= rg.lo; n--) {
        int32_t y, c = n % 12 == 0, near = 0, m;
        if (!RV(n))
            continue;
        y = NY(n);
        if (!c && !((used >> rg.row[n]) & 1u))
            continue;
        for (m = n - 1; !c && m >= rg.lo && m > n - 12; m--)   /* (a C just under it keeps its place) */
            near |= RV(m) && m % 12 == 0 && NY(m) - y < 11;
        if (y - last < 11 || near)
            continue;
        roll_label(n, c ? C_HI : C_AMB);
        last = y;
    }
}
static void roll_ground(const track_t *t, uint32_t len)  /* the rows, the octaves, the beats; the gutter's keys */
{
    int32_t n, w, ye = SG_TOP + sg_dy + rg.n * rg.rh, xe, below;
    uint32_t end = len > st.page * 16u ? len - st.page * 16u : 0u;
    (void)t;
    end = end > 16u ? 16u : end;
    xe = RL_X + (int32_t)end * RL_CW - 1;               /* (the gap right of the last step within LEN) */
    for (n = rg.lo, below = -1; n <= rg.hi; n++) {
        int32_t y;
        if (!RV(n))
            continue;
        y = NY(n);
        if (is_black(n))
            RR(0, y, xe, rg.rh, col_shade(C_LINE, 2u));  /* (a tint, the gutter's name on it: the line colour at 2/8) */
        if (n % 12 == 0 || (below >= 0 && below / 12 != n / 12))
            RR(0, y + rg.rh - 1, xe, 1, C_LINE);           /* the octave: ruled under its C (folded: under its lowest row) */
        below = n;
    }
    for (w = 0; w < (int32_t)end; w++) {
        int32_t x = RL_X + w * RL_CW;
        if (!(w & 3))
            RR(x - 1, SG_TOP, 1, ye - SG_TOP, w ? C_LINE : C_DIM);   /* the bar's start, the beats */
        RR(x, SG_TOP + SG_H - 1, RL_CW - 2, 1, C_LINE);  /* every step: a foot tick */
    }
    if (end)
        RR(xe, SG_TOP, 1, ye - SG_TOP, end < 16u || st.page * 16u + 16u >= len ? C_DIM : C_LINE);   /* LEN's end */
}
/* the selected note's frame over its bar in this column: C_NOTE top and bottom, a cap at its head and at its last column (the fill, its level shade
 * or grey, shows between; a bar under 3 px high: caps only) */
static void roll_outline(const track_t *t, uint32_t at, int32_t nt, uint32_t idx, int32_t x, int32_t y, int32_t w, int32_t h)
{
    int head = idx == at || !roll_selx(t, at, nt, idx - 1u), last = !roll_selx(t, at, nt, idx + 1u);
    if (h >= 3) {
        RR(x, y, w, 1, C_NOTE);
        RR(x, y + h - 1, w, 1, C_NOTE);
    }
    if (head)
        RR(x, y, 1, h, C_NOTE);
    if (last)
        RR(x + w - 1, y, 1, h, C_NOTE);
}
static void sg_roll(const track_t *t)
{
    uint32_t w, i, len = trk_len(t), gn, sat = 0, sext = 0, sel;
    uint64_t used = 0;
    uint16_t c = trk_col(song.sel);
    int32_t m, h, ye, snt = -1;
    sg_prep(t);
    roll_range(t);
    m = rg.rh >= 6 ? 1 : 0;
    h = rg.rh >= 6 ? rg.rh - 2 : rg.rh - 1;
    ye = SG_TOP + sg_dy + rg.n * rg.rh;
    roll_ground(t, len);
    sel = (uint32_t)roll_sel(t, &sat, &snt, &sext) && RV(snt);
    if (sel) {                                          /* the selected note's row: lined above and below, as the drums' lane */
        RR(0, NY(snt) - 1, 240, 1, col_shade(C_NOTE, 5u));
        RR(0, NY(snt) + rg.rh - 1, 240, 1, col_shade(C_NOTE, 5u));
    }
    for (w = 0; w < 16u; w++) {
        uint32_t idx = st.page * 16u + w, tie;
        const step_t *s;
        int32_t x = RL_X + (int32_t)w * RL_CW;
        if (idx >= len)
            break;
        tie = SGS(t)[idx % NSTEP].time == ST_TIE;
        s = &SGS(t)[roll_org(t, idx) % NSTEP];
        gn = sg_st && !tie && !(step_on(&t->step[idx % NSTEP]) && t->step[idx % NSTEP].note[0] == s->note[0]);
        if (sg_st && !step_on(s) && !tie && step_on(&t->step[idx % NSTEP])) {   /* (silenced by a generator: a dim stub) */
            const step_t *q = &t->step[idx % NSTEP];
            for (i = 0; i < q->n && i < 4u; i++)
                if (RV(q->note[i]))
                    RR(x, NY(q->note[i]) + rg.rh / 2, RL_CW - 1, 1, col_shade(c, 2u));
            continue;
        }
        if (!step_on(s))
            continue;
        for (i = 0; i < s->n && i < 4u; i++) {
            int32_t nt = s->note[i], y = NY(nt), g = (int32_t)ngate_of(s, i), cw = RL_CW - 1;
            uint32_t oi, hd = roll_head(t, idx, i, &oi), lv = (SGS(t)[hd % NSTEP].lvl >> (2u * oi)) & 3u;
            uint16_t nc = (int32_t)w == sg_ph ? C_WHITE : op_step_auto(t, idx, 16u) ? lvl_acol(lv) : lvl_col(c, lv);
            int mut = 0, sx;
#if STEP_FILLS
            int fvw = FVIEW();                          /* (the fill view: the notes by the step's condition) */
            uint32_t fc = fvw ? step_fill(t, hd) : FC_NORM;
            if (fvw)
                nc = col_shade(c, fc == FC_FILL ? 8u : 2u);
#endif
#if FELUCCA_AUTO
            if (!gn && step_muted(t, hd, 0))
                nc = mute_col(), mut = 1;               /* (a muted note: kept, grey; its ties too; picked it stays grey, its row's lines show the pick) */
#endif
            sx = sel && roll_selx(t, sat, nt, idx) && (int32_t)w != sg_ph;   /* (the selected note: its level shade stays, an outline in C_NOTE) */
            if (!RV(nt) || FILL_HID(t, hd, 0))
                continue;
            if (g) {                                    /* (its own end: in this step, or one before a TIE here) */
                if (tie)
                    continue;
                cw = (RL_CW - 1) * g / 128;
                cw = cw < 2 ? 2 : cw;
            }
            used |= (uint64_t)1u << rg.row[nt];
            if (tie || roll_carried(t, s, idx, i)) {    /* (a tie, a note carried on: the bar on, over the gap) */
                RR(x - 1, y + m, cw + 1, h, nc);
                if (sx)
                    roll_outline(t, sat, nt, idx, x - 1, y + m, cw + 1, h);
                continue;
            }
            if (gn) {                                   /* (a generator's note: outlined) */
                cv_frame(x - sg_ox, y + m, cw, h, nc);
                continue;
            }
#if STEP_FILLS
            if (fvw && !mut) {
                fv_cell(x, y + m, cw, h, c, fc, sg_ox);
                continue;
            }
#endif
            RR(x, y + m, cw, h, nc);                    /* (a note: its head, a gap from the note before) */
            if (sx)
                roll_outline(t, sat, nt, idx, x, y + m, cw, h);
            if ((s->flags & SF_ACCENT) && h > 2)
                RR(x, y + m, RL_CW - 1, 1, C_WHITE);
            if ((s->rat >> (2u * i)) & 3u)
                sg_notches(x - sg_ox, y + m, RL_CW - 1, h, (s->rat >> (2u * i)) & 3u);
        }
    }
    roll_names(used);
    if (sel && rg.rh >= 11)
        roll_label(snt, C_NOTE);                        /* (its name in the gutter, over the one drawn) */
    if (sel && !st.held && sat / 16u == st.page) {      /* the picked note's step: framed in C_NOTE (a held one: white, below) */
        int32_t x = RL_X + (int32_t)(sat % 16u) * RL_CW;
        cv_frame(x - 1 - sg_ox, SG_TOP - 1, RL_CW, ye - SG_TOP + 2, C_NOTE);
    }
    for (i = 0; i < (pen_n ? (uint32_t)pen_n : 1u) && i < 4u; i++) {   /* the pick (what a tapped step takes): a white
                                                         * mark at the gutter's edge on its rows (the audit's #17: no
                                                         * longer in the header) */
        int32_t nt = (int32_t)pick_note(i);
        if (RV(nt))
            RR(0, NY(nt) + m, 2, h, C_WHITE);
    }
    for (w = 0; w < 16u; w++)                           /* the steps held: framed white */
        if (((st.held >> w) & 1u) && st.page * 16u + w < len)
            cv_frame(RL_X + (int32_t)w * RL_CW - 1 - sg_ox, SG_TOP - 1, RL_CW, ye - SG_TOP + 2, C_WHITE);
    for (w = 0; w < 16u; w++) {                         /* the steps with automation: a dot in the bar of the picked note, else the lowest; a step
                                                         * with no note (or only a FILL ONLY one): on the roll's bottom row */
        uint32_t idx = st.page * 16u + w, oi = roll_org(t, idx) % NSTEP;
        const step_t *s = &SGS(t)[oi];
        int32_t y = ye - rg.rh + m, bw = RL_CW - 1, pn = pk_ok() ? (int32_t)pick_note(0) : -2, lo = -1;
        if (idx >= len)
            break;
        if (!op_step_auto(t, idx, 16u))
            continue;
        if (step_on(s) && !FILL_HID(t, oi, 0))
            for (i = 0; i < s->n && i < 4u; i++) {
                int32_t nt = s->note[i];
                if (RV(nt) && lo != pn && (nt == pn || lo < 0 || nt < lo)) {
                    lo = nt;
                    bw = ngate_of(s, i) ? (RL_CW - 1) * (int32_t)ngate_of(s, i) / 128 : RL_CW - 1;
                    bw = bw < 2 ? 2 : bw;
                }
            }
        if (lo >= 0)
            y = NY(lo) + m;
        au_dot(RL_X + (int32_t)w * RL_CW - sg_ox, y, bw, h);
    }
}
#undef NY
static void roll_col(int32_t w)                         /* column w of the roll again, on a canvas of its own */
{
    sg_ox = RL_X + w * RL_CW - 1;
    cv_begin(RL_CW, SG_TOP + SG_H, C_BLACK);
    sg_roll(TSEL);
    cv_blit((uint32_t)sg_ox, SP_Y);
    sg_ox = 0;
}
#define GX SG_X                                         /* the panel's columns (the drum grid's and the roll's) */
#define GCW SG_CW
#if FELUCCA_AUTO
/* the events of the window's steps: a dot each, a hold event's tail to the next hold event of its parameter (or LEN) */
static void sg_marks(const track_t *t)
{
    const auto_list_t *l = AL(t);
    uint32_t i, j, len = trk_len(t), lo = st.page * 16u, hi = lo + 16u;
    uint16_t c0 = is_drum(t) ? op_lane_col(lane_selected()) : trk_col(song.sel);
    for (i = 0; i < l->n; i++) {
        const auto_ev_t *e = &l->ev[i];
        uint32_t s = e->place & AUTO_STEP, nx = len, a, b;
        uint16_t c = AUTO_IS_LANE(e->param) ? lane_col(AUTO_LANE_L(e->param)) : c0;   /* (a lane's event: its colour, D9) */
        if (s >= len || s >= hi || AUTO_IS_LFX(e))
            continue;                                   /* (a lane's FX: no step's dot; past the window: neither its dot nor its tail is in it; no search for
                                                         * its next event, a pass over every event) */
        if (!(e->place & AUTO_ONLY)) {
            for (j = 0; j < l->n; j++) {
                uint32_t s2 = l->ev[j].place & AUTO_STEP;
                if (!(l->ev[j].place & AUTO_ONLY) && l->ev[j].param == e->param && s2 > s && s2 < nx)
                    nx = s2;
            }
            a = s > lo ? s : lo;                        /* (the tail inside the window) */
            b = nx < hi ? nx : hi;
            if (a < b)
                cv_rect(GX + (int32_t)(a - lo) * GCW + (s >= lo ? (GCW - 2) / 2 : -2), SG_MK_Y + 1,
                        (int32_t)(b - a) * GCW - (s >= lo ? (GCW - 2) / 2 : -2) - 2, 1, col_shade(c, 5u));
        }
        (void)c;                                        /* (no dot here: a step's automation is the dot inside its cell or note bar, au_dot) */
    }
}
#endif
/* all the panel shows but the playhead and the steps held's white frames: what the held steps change besides (the roll's
 * selected note, its rows, the picked step's frame) is in; their frames alone are a column each (draw_step_panel) */
static uint32_t step_sig(void)
{
    const track_t *t = TSEL;
    uint32_t h = hu(hu(hu(hu(hu(13u, trk_len(t)), st.page), (uint32_t)(!st.held && pk_ok())), lane_sel * 2u + is_drum(t) + (uint32_t)st.nsel * 64u), trk_col(song.sel)), i;
    if (!is_drum(t)) {                                  /* (the roll: the held step its selected note when it has one, roll_sel;
                                                         * the rows around the edited note, roll_range) */
        int32_t f = held_first();
        h = hu(hu(h, f >= 0 && step_on(&t->step[f]) ? (uint32_t)f + 1u : 0u), (uint32_t)(held_note() + 1));
    }
    h = hu(h, (uint32_t)FVIEW());                       /* (SEQ held: the fill view) */
    h = hbytes(h, is_drum(t) ? (const void *)t->dstep : (const void *)t->step, NSTEP * sizeof(step_t));
#if FELUCCA_AUTO
    h = hbytes(h, AL(t), 1u + 3u * AL(t)->n);           /* (the events' marks) */
#endif
#if GEN_ON
    h = hbytes(h, gen, sizeof gen);                     /* (the generators: what plays) */
    if ((gen_trk >> trk_index(t)) & 1u) {               /* a generator on: what it plays now, as the grid draws it (sg_prep).
                                                         * Not only its settings: the kit GRIDS' values are the drum track's,
                                                         * its OUT, CHAOS's pass, the kit's LFOs move them (the user, 2026-10-23:
                                                         * "when i move the knobs of grids ... and when lfo moves them too") */
        gen_render(t, sg_gst, ~0u);
        h = hbytes(h, sg_gst, sizeof sg_gst);
    }
#endif
    if (is_drum(t))
        for (i = 0; i < DRUM_UILANES; i++)
            h = hu(h, op_lane_col(i));
    else
        h = hu(hu(hu(hu(hu(h, pick_note(0)), pick_note(1)), pick_note(2) * 128u + pick_note(3)), pen_n),
               (uint32_t)(song.octave + 16) + song.sel * 64u + roll_fold * 4096u);   /* (the roll's view: OCT, FOLD; the pick's marks) */
    h = hu(h, (uint32_t)st.pk + (pk_ok() ? 256u : 0u));   /* (the note picker's frame) */
    return hu(hu(h, settings.palette), (uint32_t)(sg_dy + 512));
}
/* a held step's two lines, over the grid's foot while the overlay is up, "Step 6  Fill only" and "Nudge +4  Chance 85%" (op_step.c step_foot's,
 * the footer's before there was none); empty when no step is held */
static char sg_info[2][32];
static void step_info(void)
{
    sg_info[0][0] = sg_info[1][0] = 0;
    if (held_first() >= 0)
        step_foot(sg_info[1], sg_info[0], sizeof sg_info[0]);
}
static void sg_paint(void)
{
    if (is_drum(TSEL))
        sg_drums(TSEL);
    else
        sg_roll(TSEL);
#if FELUCCA_AUTO
    sg_marks(TSEL);
#endif
}
static void step_strip(uint32_t key)                    /* the playhead's strip, the window's page in the gutter */
{
    uint32_t i, n = step_pages(TSEL), gw = (uint32_t)GX - 4u, sw;
    cv_begin(240, 4, C_BLACK);
    if (key < 16u)
        cv_rect(GX + (int32_t)key * GCW, 0, GCW - 2, 4, C_WHITE);   /* the playhead's column */
    else if (key != 0xFEu)
        cv_rect(key == 16u ? 0 : 236, 0, 4, 4, C_DIM);  /* outside the window (FOLLOW off): which side */
    sw = n > 1u ? gw / n : 0u;                          /* (n >= 1: LEN >= 1) */
    for (i = 0; sw > 1u && i < n && key != 16u; i++)    /* the pages, the window's lit (1-16, 17-32, ...) */
        cv_rect(1 + (int32_t)(i * sw), 1, (int32_t)sw - 1, 2, i == st.page ? C_HI : C_LINE);
    cv_blit(0, SP_Y + SG_PH_Y);
}
/* only the columns w0..w1 of the grid sent (its rows; the marks and the playhead strip under it stay): the whole grid painted as
 * ever, a pass at a time, each pass's columns packed to the canvas's front. A step key pressed or let go with nothing else
 * changed: its white frame, not the whole panel (the user, 2026-10-22: the frame after a press was the slow one) */
static __attribute__((noinline)) void sg_cols(uint32_t w0, uint32_t w1)
{
    uint32_t r0, n, r, i, h = SG_TOP + SG_H + 1u, x0 = SG_X + w0 * SG_CW - 1u, w = (w1 - w0 + 1u) * SG_CW;
    for (r0 = 0; r0 < h; r0 += n) {
        n = h - r0 < 120u ? h - r0 : 120u;
        cv_begin(240, n, C_BLACK);
        cv_oy = -(int32_t)r0;
        sg_paint();
        cv_oy = 0;
        for (r = 0; r < n; r++)                         /* (to the front: never over a pixel still to move) */
            for (i = 0; i < w; i++)
                cv_px[r * w + i] = cv_px[r * 240u + x0 + i];
        cv_w = w;                                       /* (the canvas those columns now; the next cv_begin sets it again) */
        cv_blit(x0, SP_Y + r0);
    }
}
static int draw_step_panel(void)                       /* 1: the whole grid painted (what lies over it goes again) */
{
    static uint16_t held_drawn;                         /* (the steps framed white on the screen) */
    const track_t *t = TSEL;
    uint32_t sig, len = trk_len(t), at = t->seq_idx % len, key, d = st.held ^ held_drawn, w0, w1;
    int32_t col;
    int full = 0;
    key = !song.playing ? 0xFEu : at / 16u == st.page ? at % 16u : at / 16u < st.page ? 16u : 17u;
    col = key < 16u ? (int32_t)key : -1;
    sig = step_sig();
    if (sig != ui.sig[2] || (d && (ui.cards_t || ui.overlay))) {
        if (sig == ui.sig[2])
            ui.sig[4] = 0;                              /* (the grid again for a step held under the cards or a toast: the toast
                                                         * again over it, as a changed grid brings it, op_draw.c) */
        ui.sig[2] = sig;
        sg_ph = col;                                    /* (the roll's playing column, white) */
        cv_tall(SP_Y, SP_H, C_BLACK, sg_paint);  /* (the grid, its playhead strip, the held step's line) */
        st.ph_drawn = 0xFF;
        full = 1;
    } else if (d) {                                     /* only steps held or let go, the cards down: their columns */
        for (w0 = 0; !((d >> w0) & 1u); w0++)
            ;
        for (w1 = 15u; !((d >> w1) & 1u); w1--)
            ;
        sg_cols(w0, w1);
    }
    held_drawn = st.held;
    if (key == st.ph_drawn || ui.overlay == 2u)         /* (still under a toast) */
        return full;
    st.ph_drawn = (uint8_t)key;
    step_strip(key);
    if (col != sg_ph && !ui.cards_t) {                  /* the columns the playhead left and reached (the roll's own canvases, the drums'
                                                         * one column each, sg_cols); not under the cards: the band again when they go */
        int32_t old = sg_ph;
        sg_ph = col;
        if (is_drum(t)) {
            if (old >= 0)
                sg_cols((uint32_t)old, (uint32_t)old);
            if (col >= 0)
                sg_cols((uint32_t)col, (uint32_t)col);
        } else {
            if (old >= 0)
                roll_col(old);
            if (col >= 0)
                roll_col(col);
        }
    }
    return full;
}
/* STEP's frame: the grid on the whole panel, the four cards over its top and a held step's two lines over its foot
 * while ui.cards_t runs (a page change, a value edited, a step held: step_cards_show); when they go the grid is drawn
 * again whole, and a grid drawn again under them brings them back. A step let go: the cards and the lines go on
 * showing the page that was held until the timer ends (cards_snap_use, op_draw.c draw_cards) */
/* the drum grid's shift while the cards are up (the audit's #15): the selected lane's row whole between the cards and
 * the held step's lines, else the grid moves so that row is the first under the cards (the rows above it go under
 * them, the rows past the grid's foot are not drawn); 0 with the cards down, on a synth track, or the row in view */
static int32_t sg_shift(uint32_t want, uint32_t inf)
{
    int32_t top = OP_PY - SP_Y, lim = inf ? SG_INFO_Y : SG_TOP + SG_H, y, lh;
    uint32_t sel = lane_selected();
    if (want && !is_drum(TSEL) && held_note() >= 0) {   /* the roll: the edited note a row clear of the cards and of the step's lines */
        int32_t nt = held_note();
        roll_range(TSEL);
        y = SG_TOP + (rg.n - 1 - rg.row[nt & 127]) * rg.rh;
        if (y < top + rg.rh)
            return top + rg.rh - y;
        return y + 2 * rg.rh > lim ? lim - 2 * rg.rh - y : 0;
    }
    if (!want || !is_drum(TSEL))
        return 0;
    if (held_first() >= 0 && !dstep_has(&TSEL->dstep[held_first()], sel)) {   /* (the held step's hit, not the selected lane's */
        uint32_t l;                                                         /* empty row: its first lane with one, the user 2026-10-16) */
        for (l = 0; l < DRUM_UILANES; l++)
            if (dstep_has(&TSEL->dstep[held_first()], l)) {
                sel = l;
                break;
            }
    }
    y = SG_LY0(sel);
    lh = SG_LY0(sel + 1u) - y;
    return y >= top && y + lh <= lim ? 0 : top - y;
}
/* (noinline: a call from op_frame_draw, which is smaller without it inlined) */
static __attribute__((noinline)) void step_frame(void)
{
    static uint8_t on, info, was_held;
    static uint32_t isig;
    static char snap[2][32];
    uint32_t want = ui.cards_t != 0u, inf;
    int full;
    if (was_held && !st.held && want)
        cards_snap_use = 1;
    else if (!want)
        cards_snap_use = 0;                             /* (the cards gone: no snapshot left over for later) */
    was_held = (uint8_t)(st.held != 0u);
    step_info();
    if (sg_info[0][0])
        memcpy(snap, sg_info, sizeof snap);
    else if (cards_snap_use)
        memcpy(sg_info, snap, sizeof sg_info);
    inf = want && sg_info[0][0];
    sg_dy = sg_shift(want, inf);                        /* (in step_sig: the grid again when it changes) */
    if ((!want && on) || (!inf && info))
        ui.sig[2] = 0;
    full = draw_step_panel();
    if (want && (!on || full)) {
        ui.sig[1] = 0;
        cv_begin(240, OY_CARD - SP_Y, C_BLACK);         /* the rows between the header and the cards: no grid fragments */
        cv_blit(0, SP_Y);                               /* behind the overlay (the audit's #16) */
    }
    if (full || !inf)
        isig = 0;
    on = (uint8_t)want;
    info = (uint8_t)inf;
    if (want)
        draw_cards();
    if (inf && hs(hs(1u, sg_info[0]), sg_info[1]) != isig) {
        isig = hs(hs(1u, sg_info[0]), sg_info[1]);
        cv_begin(240, SG_INFO_H, C_BLACK);
        cv_texts(4, 2, sg_info[0], C_HI);
        cv_texts(4, 2 + SG_INFO_DY, sg_info[1], C_GRAY);
        cv_blit(0, SP_Y + SG_INFO_Y);
        if (ui.toast_t)
            ui.sig[4] = 0;                              /* (a toast over it is drawn again) */
    }
}
