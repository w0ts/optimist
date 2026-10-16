/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP's panel (docs/UI-OPTIMIST-DESIGN.md section 4.2): the window's 16 steps as columns under the keys.
 *   the drum track   the 16 lanes (and the ACCENT lane, 17) x the 16 steps: a hit in its lane's colour, its level as the shade (ghost dim ..
 *                    hard full), its ratchet as notches; the selected lane's row lit; at the left each lane's colour
 *                    chip and two-letter code (LANE_CODE, as the mixer), the selected lane's white
 *   a synth track    a mini piano roll: the notes stacked (a chord: several in a column), the ties as lines after
 *                    their note, the level as the shade, the ratchet as notches; the Cs as faint lines
 * A step with automation (a lock or motion event, op_step_auto) is drawn in the automation colour (C_AUTO) instead of its lane's or the track's.
 * Both: black ground, the beats ruled by thin lines, the steps beyond LEN empty, the steps held framed white; under the
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
static step_t sg_gst[NSTEP];
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
static void sg_outline(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a generated hit: its frame */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}
static uint16_t lvl_col(uint16_t c, uint32_t lv)        /* a hit's colour by its level: ghost 3/8 .. hard full */
{
    static const uint8_t SH[4] = {3, 5, 7, 8};
    return col_shade(c, SH[lvl_rank(lv)]);
}
static uint16_t lvl_acol(uint32_t lv)                   /* ... a step with automation: the automation colour, its level 5/8 .. full */
{
    static const uint8_t SH[4] = {5, 6, 7, 8};
    return col_shade(C_AUTO, SH[lvl_rank(lv)]);
}
static void sg_notches(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rat)   /* ratchet x2..x4: 1..3 cuts in the cell */
{
    uint32_t i;
    for (i = 1; i <= rat; i++)
        cv_rect(x + (int32_t)i * w / (int32_t)(rat + 1u), y, 1, h, C_BLACK);
}
static void sg_ground(const track_t *t)                 /* the beats, the steps past LEN, the columns held */
{
    uint32_t w, len = trk_len(t);
    for (w = 0; w < 16u; w++) {
        int32_t x = SG_X + (int32_t)w * SG_CW;
        if (st.page * 16u + w >= len)
            continue;
        if (!(w & 3u))                                  /* black ground, lines only: a beat's first column ruled at */
            cv_rect(x - 1, SG_TOP, 1, SG_H, C_LINE);    /* its left, every step within LEN a foot line */
        cv_rect(x, SG_TOP + SG_H - 1, SG_CW - 2, 1, C_LINE);
        if ((st.held >> w) & 1u) {
            cv_rect(x - 1, SG_TOP - 1 < 0 ? 0 : SG_TOP - 1, SG_CW, 1, C_WHITE);
            cv_rect(x - 1, SG_TOP + SG_H, SG_CW, 1, C_WHITE);
            cv_rect(x - 1, SG_TOP, 1, SG_H, C_WHITE);
            cv_rect(x + SG_CW - 2, SG_TOP, 1, SG_H, C_WHITE);
        }
    }
}
static void sg_drums(const track_t *t)
{
    uint32_t l, w, sel = lane_selected(), len = trk_len(t);
    sg_prep(t);
    cv_rect(0, SG_LY(sel) - 1, 240, 1, C_DIM);          /* the selected lane's row: lined above and below */
    cv_rect(0, SG_LY(sel + 1u) - 1, 240, 1, C_DIM);
    sg_ground(t);
    for (l = 0; l < DRUM_UILANES; l++) {                /* (the 16 sounds, then the ACCENT lane's row) */
        int32_t y = SG_LY(l), lh = SG_LY(l + 1u) - y;
        uint16_t c = op_lane_col(l);
        if (y < SG_TOP || y + lh > SG_TOP + SG_H)
            continue;                                   /* (shifted out of the grid: sg_shift) */
        cv_rect(2, y, 3, lh - 1, l == sel ? C_WHITE : c);   /* the lane: its colour chip and its code, as the mixer's */
        cv_text(6, y + (lh - 11) / 2 - 2, &FONT_S, lane_code(l), l == sel ? C_WHITE : c);   /* (the user, 2026-10-11; its 10 rows of ink in the row) */
        for (w = 0; w < 16u; w++) {
            uint32_t idx = st.page * 16u + w;
            const dstep_t *d = &SGD(t)[idx % NSTEP];
            int32_t x = SG_X + (int32_t)w * SG_CW, own = dstep_has(&t->dstep[idx % NSTEP], l);
            if (idx >= len)
                continue;
            if (!dstep_has(d, l)) {
                if (own)                                /* (silenced by a generator: a dim stub) */
                    cv_rect(x, y + lh - 3, SG_CW - 2, 2, col_shade(c, 2u));
                continue;
            }
            if (!own) {                                 /* (a generator's hit: outlined) */
                sg_outline(x, y, SG_CW - 2, lh - 1, lvl_col(c, dstep_lvl(d, l)));
                continue;
            }
            cv_rect(x, y, SG_CW - 2, lh - 1,
                    op_step_auto(t, idx, l) ? lvl_acol(dstep_lvl(d, l)) : lvl_col(c, dstep_lvl(d, l)));   /* (automated: its own colour) */
            sg_notches(x, y, SG_CW - 2, lh - 1, dstep_rat(d, l));
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
static int32_t sg_ph = -1;              /* the roll's playhead column as painted (-1 none) */
#define RR(x, y, w, h, c) cv_rect((x)-sg_ox, y, w, h, c)
static int is_black(int32_t n) { return (0x54Au >> (n % 12)) & 1; }   /* C# D# F# G# A# */
static struct {
    int32_t lo, hi, rh;
} rg;
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
    ex = held_note();                                   /* the note being edited (a step held): always inside the rows, whatever OCT and the pattern say */
    if (ex >= 0 && ex < rg.lo)
        rg.hi -= rg.lo - ex, rg.lo = ex;
    else if (ex > rg.hi)
        rg.lo += ex - rg.hi, rg.hi = ex;
}
#define NY(nt) (SG_TOP + sg_dy + (rg.hi - (nt)) * rg.rh)   /* (sg_dy: the roll moved so the edited note clears the cards) */
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
static void roll_label(int32_t n, uint16_t c)           /* a row's name, "C4" for a C, in the gutter */
{
    char b[6];
    if (n % 12 == 0)
        note_name(b, (uint32_t)n);
    else
        str_cpy(b, N_NOTE[n % 12], 3);
    cv_text(2 - sg_ox, NY(n) + (rg.rh - 16) / 2, &FONT_S, b, c);
}
/* the gutter: every row named when the rows are tall enough (11 px), else the Cs, then the rows with a note in the
 * window where they do not touch a name above */
static void roll_names(uint64_t used)
{
    int32_t n, last = -100;
    if (rg.rh >= 11) {
        for (n = rg.lo; n <= rg.hi; n++)
            roll_label(n, n % 12 == 0 ? C_HI : ((used >> (n - rg.lo)) & 1u) ? C_AMB : is_black(n) ? C_DIM : C_GRAY);
        return;
    }
    for (n = rg.hi; n >= rg.lo; n--) {
        int32_t y = NY(n), c = n % 12 == 0, near = 0, m;
        if (!c && !((used >> (n - rg.lo)) & 1u))
            continue;
        for (m = n - 1; !c && m >= rg.lo && m > n - 12; m--)   /* (a C just under it keeps its place) */
            near |= m % 12 == 0 && NY(m) - y < 11;
        if (y - last < 11 || near)
            continue;
        roll_label(n, c ? C_HI : C_AMB);
        last = y;
    }
}
static void roll_ground(const track_t *t, uint32_t len)  /* the rows, the octaves, the beats; the gutter's keys */
{
    int32_t n, w, ye = SG_TOP + sg_dy + (rg.hi - rg.lo + 1) * rg.rh, xe;
    uint32_t end = len > st.page * 16u ? len - st.page * 16u : 0u;
    (void)t;
    end = end > 16u ? 16u : end;
    xe = RL_X + (int32_t)end * RL_CW - 1;               /* (the gap right of the last step within LEN) */
    for (n = rg.lo; n <= rg.hi; n++) {
        int32_t y = NY(n);
        if (is_black(n))
            RR(0, y, xe, rg.rh, col_shade(C_LINE, 2u));  /* (a tint, the gutter's name on it: the line colour at 2/8) */
        if (n % 12 == 0)
            RR(0, y + rg.rh - 1, xe, 1, C_LINE);           /* the octave: ruled under its C */
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
static void sg_roll(const track_t *t)
{
    uint32_t w, i, len = trk_len(t), gn;
    uint64_t used = 0;
    uint16_t c = trk_col(song.sel);
    int32_t m, h, ye;
    sg_prep(t);
    roll_range(t);
    m = rg.rh >= 6 ? 1 : 0;
    h = rg.rh >= 6 ? rg.rh - 2 : rg.rh - 1;
    ye = SG_TOP + sg_dy + (rg.hi - rg.lo + 1) * rg.rh;
    roll_ground(t, len);
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
                if (q->note[i] >= rg.lo && q->note[i] <= rg.hi)
                    RR(x, NY(q->note[i]) + rg.rh / 2, RL_CW - 1, 1, col_shade(c, 2u));
            continue;
        }
        if (!step_on(s))
            continue;
        for (i = 0; i < s->n && i < 4u; i++) {
            int32_t nt = s->note[i], y = NY(nt), g = (int32_t)ngate_of(s, i), cw = RL_CW - 1;
            uint32_t lv = (s->lvl >> (2u * i)) & 3u;
            uint16_t nc = (int32_t)w == sg_ph ? C_WHITE : op_step_auto(t, idx, 16u) ? lvl_acol(lv) : lvl_col(c, lv);
            if (!tie && held_note_sel(idx) == (int)i)
                nc = C_NOTE;                            /* (the note SELECT picked on a held step of several: its own colour) */
            if (nt < rg.lo || nt > rg.hi)
                continue;
            if (g) {                                    /* (its own end: in this step, or one before a TIE here) */
                if (tie)
                    continue;
                cw = (RL_CW - 1) * g / 128;
                cw = cw < 2 ? 2 : cw;
            }
            used |= (uint64_t)1u << (nt - rg.lo);
            if (tie || roll_carried(t, s, idx, i)) {    /* (a tie, a note carried on: the bar on, over the gap) */
                RR(x - 1, y + m, cw + 1, h, nc);
                continue;
            }
            if (gn) {                                   /* (a generator's note: outlined) */
                sg_outline(x - sg_ox, y + m, cw, h, nc);
                continue;
            }
            RR(x, y + m, cw, h, nc);                    /* (a note: its head, a gap from the note before) */
            if ((s->flags & SF_ACCENT) && h > 2)
                RR(x, y + m, RL_CW - 1, 1, C_WHITE);
            if ((s->rat >> (2u * i)) & 3u)
                sg_notches(x - sg_ox, y + m, RL_CW - 1, h, (s->rat >> (2u * i)) & 3u);
        }
    }
    roll_names(used);
    for (i = 0; i < (pen_n ? (uint32_t)pen_n : 1u) && i < 4u; i++) {   /* the pick (what a tapped step takes): a white
                                                         * mark at the gutter's edge on its rows (the audit's #17: no
                                                         * longer in the header) */
        int32_t nt = (int32_t)pick_note(i);
        if (nt >= rg.lo && nt <= rg.hi)
            RR(0, NY(nt) + m, 2, h, C_WHITE);
    }
    for (w = 0; w < 16u; w++) {                         /* the steps held: framed white */
        int32_t x = RL_X + (int32_t)w * RL_CW;
        if (!((st.held >> w) & 1u) || st.page * 16u + w >= len)
            continue;
        RR(x - 1, SG_TOP - 1 < 0 ? 0 : SG_TOP - 1, RL_CW, 1, C_WHITE);
        RR(x - 1, ye, RL_CW, 1, C_WHITE);
        RR(x - 1, SG_TOP, 1, ye - SG_TOP, C_WHITE);
        RR(x + RL_CW - 2, SG_TOP, 1, ye - SG_TOP, C_WHITE);
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
        if (s >= len)
            continue;
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
        if (s >= lo && s < hi)
            cv_rect(GX + (int32_t)(s - lo) * GCW + (GCW - 2) / 2 - 1, SG_MK_Y, 3, 3, c);
    }
}
#endif
static uint32_t step_sig(void)                          /* all the panel shows but the playhead */
{
    const track_t *t = TSEL;
    const uint8_t *p = (const uint8_t *)(is_drum(t) ? (const void *)t->dstep : (const void *)t->step);
    uint32_t h = hu(hu(hu(hu(hu(13u, trk_len(t)), st.page), st.held), lane_sel * 2u + is_drum(t) + (uint32_t)st.nsel * 64u), trk_col(song.sel)), i;
    for (i = 0; i < NSTEP * sizeof(step_t); i++)
        h = (h ^ p[i]) * 16777619u;
#if FELUCCA_AUTO
    p = (const uint8_t *)AL(t);                         /* (the events' marks) */
    for (i = 0; i < 1u + 3u * AL(t)->n; i++)
        h = (h ^ p[i]) * 16777619u;
#endif
#if GEN_ON
    p = (const uint8_t *)gen;                           /* (the generators: what plays) */
    for (i = 0; i < sizeof gen; i++)
        h = (h ^ p[i]) * 16777619u;
#endif
    if (is_drum(t))
        for (i = 0; i < DRUM_UILANES; i++)
            h = hu(h, op_lane_col(i));
    else
        h = hu(hu(hu(hu(hu(h, pick_note(0)), pick_note(1)), pick_note(2) * 128u + pick_note(3)), pen_n),
               (uint32_t)(song.octave + 16) + song.sel * 64u);   /* (the roll's view: OCT; the pick's marks) */
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
static void draw_step_panel(void)
{
    const track_t *t = TSEL;
    uint32_t sig, len = trk_len(t), at = t->seq_idx % len, key;
    int32_t col;
    key = !song.playing ? 0xFEu : at / 16u == st.page ? at % 16u : at / 16u < st.page ? 16u : 17u;
    col = key < 16u ? (int32_t)key : -1;
    sig = step_sig();
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        sg_ph = col;                                    /* (the roll's playing column, white) */
        cv_tall(SP_Y, SP_H, C_BLACK, sg_paint);  /* (the grid, its playhead strip, the held step's line) */
        st.ph_drawn = 0xFF;
    }
    if (key == st.ph_drawn || ui.overlay == 2u)         /* (still under a toast) */
        return;
    st.ph_drawn = (uint8_t)key;
    step_strip(key);
    if (!is_drum(t) && col != sg_ph && !ui.cards_t) {   /* the roll: the columns the playhead left and reached */
        int32_t old = sg_ph;                            /* (not under the cards: the band again when they go) */
        sg_ph = col;
        if (old >= 0)
            roll_col(old);
        if (col >= 0)
            roll_col(col);
    }
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
        y = SG_TOP + (rg.hi - nt) * rg.rh;
        if (y < top + rg.rh)
            return top + rg.rh - y;
        return y + 2 * rg.rh > lim ? lim - 2 * rg.rh - y : 0;
    }
    if (!want || !is_drum(TSEL))
        return 0;
    y = SG_LY0(sel);
    lh = SG_LY0(sel + 1u) - y;
    return y >= top && y + lh <= lim ? 0 : top - y;
}
static void step_frame(void)
{
    static uint8_t on, info, was_held;
    static uint32_t isig;
    static char snap[2][32];
    uint32_t want = ui.cards_t != 0u, under, inf;
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
    under = ui.sig[2];
    draw_step_panel();
    if (want && (!on || ui.sig[2] != under)) {
        ui.sig[1] = 0;
        cv_begin(240, OY_CARD - SP_Y, C_BLACK);         /* the rows between the header and the cards: no grid fragments */
        cv_blit(0, SP_Y);                               /* behind the overlay (the audit's #16) */
    }
    if (ui.sig[2] != under || !inf)
        isig = 0;
    on = (uint8_t)want;
    info = (uint8_t)inf;
    if (want)
        draw_cards();
    if (inf && hs(hs(1u, sg_info[0]), sg_info[1]) != isig) {
        isig = hs(hs(1u, sg_info[0]), sg_info[1]);
        cv_begin(240, SG_INFO_H, C_BLACK);
        cv_text(4, 2, &FONT_S, sg_info[0], C_HI);
        cv_text(4, 2 + SG_INFO_DY, &FONT_S, sg_info[1], C_GRAY);
        cv_blit(0, SP_Y + SG_INFO_Y);
        if (ui.toast_t)
            ui.sig[4] = 0;                              /* (a toast over it is drawn again) */
    }
}
