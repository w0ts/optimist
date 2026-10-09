/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP's panel (docs/UI-OPTIMIST-DESIGN.md section 4.2): the window's 16 steps as columns under the keys.
 *   the drum track   the 16 lanes x the 16 steps: a hit in its lane's colour, its level as the shade (ghost dim ..
 *                    hard full), its ratchet as notches; the selected lane's row lit, its colour strip at the left
 *   a synth track    a mini piano roll: the notes stacked (a chord: several in a column), the ties as lines after
 *                    their note, the level as the shade, the ratchet as notches; the Cs as faint lines
 * Both: the beats' columns on a lighter ground, the steps beyond LEN empty, the steps held framed white, and under
 * the grid the playhead strip (a canvas of its own, so a playing grid is drawn again only when a step changes;
 * when the playhead is outside the window, a mark at the side it is on). One 240 x 123 canvas: gfx.c's band. */
#define SG_X 16                         /* the first column's x; a column is SG_CW wide, its cell SG_CW - 2 */
#define SG_CW 14
#define SG_TOP 1
#define SG_LH (cards_2x2() ? 5 : 7)   /* a lane's row (its cell SG_LH - 1; CARDS 2x2: the panel is shorter) */
#define SG_H (16 * SG_LH)               /* 16 lanes; the roll's height */
#define SG_PH_Y (SG_TOP + SG_H + 2)     /* the playhead strip, in the panel */
#define SG_INFO_DY (cards_2x2() ? 13 : 18)   /* the two lines' pitch */
#define SG_INFO_Y (SG_PH_Y + (cards_2x2() ? 6 : 7))        /* a held step's nudge, chance and fill (no footer: the user, 2026-10-08) */

static uint16_t lvl_col(uint16_t c, uint32_t lv)        /* a hit's colour by its level: ghost 3/8 .. hard full */
{
    static const uint8_t SH[4] = {3, 5, 7, 8};
    return col_shade(c, SH[lvl_rank(lv)]);
}
static void sg_notches(int32_t x, int32_t y, int32_t h, uint32_t rat)   /* ratchet x2..x4: 1..3 cuts in the cell */
{
    uint32_t i;
    for (i = 1; i <= rat; i++)
        cv_rect(x + (int32_t)i * (SG_CW - 2) / (int32_t)(rat + 1u), y, 1, h, C_BLACK);
}
static void sg_ground(const track_t *t)                 /* the beats, the steps past LEN, the columns held */
{
    uint32_t w, len = trk_len(t);
    for (w = 0; w < 16u; w++) {
        int32_t x = SG_X + (int32_t)w * SG_CW;
        if (st.page * 16u + w >= len)
            continue;
        cv_rect(x, SG_TOP, SG_CW - 2, SG_H, (w / 4u) & 1u ? C_BLACK : OP_SURF);
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
    cv_rect(0, SG_TOP + (int32_t)sel * SG_LH - 1, 240, SG_LH + 1, C_LINE);   /* the selected lane's row */
    sg_ground(t);
    for (l = 0; l < DRUM_LANES; l++) {
        int32_t y = SG_TOP + (int32_t)l * SG_LH;
        uint16_t c = lane_col(l);
        cv_rect(2, y, l == sel ? 10 : 6, SG_LH - 1, l == sel ? C_WHITE : c);
        for (w = 0; w < 16u; w++) {
            uint32_t idx = st.page * 16u + w;
            const dstep_t *d = &t->dstep[idx % NSTEP];
            int32_t x = SG_X + (int32_t)w * SG_CW;
            if (idx >= len || !dstep_has(d, l))
                continue;
            cv_rect(x, y, SG_CW - 2, SG_LH - 1, lvl_col(c, dstep_lvl(d, l)));
            sg_notches(x, y, SG_LH - 1, dstep_rat(d, l));
        }
    }
}
/* the roll's note range: the window's notes (and the tie origins), at least an octave and a bit, the pick inside */
static void roll_range(const track_t *t, int32_t *lo, int32_t *hi)
{
    uint32_t w, i, len = trk_len(t);
    int32_t a = 127, b = 0;
    for (w = 0; w < 16u; w++) {
        const step_t *s = &t->step[(st.page * 16u + w) % NSTEP];
        if (st.page * 16u + w >= len || !step_on(s))
            continue;
        for (i = 0; i < s->n && i < 4u; i++)
            a = s->note[i] < a ? s->note[i] : a, b = s->note[i] > b ? s->note[i] : b;
    }
    if (a > b)                                          /* (an empty window: around the pick) */
        a = b = pen_n ? pen_note[0] : last_note;
    while (b - a < 12)
        a > 0 ? a-- : 0, b < 127 ? b++ : 0;
    if (b - a > 55)                                     /* (2 px a semitone at least: the lowest notes kept) */
        b = a + 55;
    *lo = a;
    *hi = b;
}
static void sg_roll(const track_t *t)
{
    uint32_t w, i, len = trk_len(t);
    int32_t lo, hi, rh, n;
    uint16_t c = trk_col(song.sel);
    roll_range(t, &lo, &hi);
    rh = SG_H / (hi - lo + 1);
    rh = rh > 8 ? 8 : rh;
    sg_ground(t);
#define NY(nt) (SG_TOP + (hi - (nt)) * rh)
    for (n = lo; n <= hi; n++)                          /* the Cs, faint */
        if (n % 12 == 0)
            cv_rect(SG_X - 4, NY(n) + rh - 1, 240 - SG_X + 4, 1, C_LINE);
    for (w = 0; w < 16u; w++) {
        uint32_t idx = st.page * 16u + w, org = idx;
        const step_t *s = &t->step[idx % NSTEP];
        int32_t x = SG_X + (int32_t)w * SG_CW;
        if (idx >= len)
            continue;
        if (s->time == ST_TIE) {                        /* a tie: its note's line, at the note's height */
            uint32_t k;
            for (k = 0; k < len && t->step[org % NSTEP].time == ST_TIE; k++)
                org = (org + len - 1u) % len;
            s = &t->step[org % NSTEP];
            if (!step_on(s))
                continue;
            for (i = 0; i < s->n && i < 4u; i++)
                if (s->note[i] >= lo && s->note[i] <= hi)
                    cv_rect(x - 2, NY(s->note[i]) + (rh - 2) / 2, SG_CW, 2, col_shade(c, 5u));
            continue;
        }
        if (!step_on(s))
            continue;
        for (i = 0; i < s->n && i < 4u; i++) {
            int32_t nt = s->note[i], y = NY(nt), h = rh > 2 ? rh - 1 : rh;
            if (nt < lo || nt > hi)
                continue;
            cv_rect(x, y, SG_CW - 2, h, lvl_col(c, (s->lvl >> (2u * i)) & 3u));
            sg_notches(x, y, h, (s->rat >> (2u * i)) & 3u);
        }
    }
#undef NY
}
static uint32_t step_sig(void)                          /* all the panel shows but the playhead */
{
    const track_t *t = TSEL;
    const uint8_t *p = (const uint8_t *)(is_drum(t) ? (const void *)t->dstep : (const void *)t->step);
    uint32_t h = hu(hu(hu(hu(hu(13u, trk_len(t)), st.page), st.held), lane_sel * 2u + is_drum(t)), trk_col(song.sel)), i;
    for (i = 0; i < NSTEP * sizeof(step_t); i++)
        h = (h ^ p[i]) * 16777619u;
    if (is_drum(t))
        for (i = 0; i < DRUM_LANES; i++)
            h = hu(h, lane_col(i));
    else
        h = hu(h, pen_n ? pen_note[0] : last_note);
    return hu(h, settings.palette);
}
/* a held step's two lines under the grid, "Step 6  Fill only" and "Nudge +4  Chance 85%" (op_step.c step_foot's,
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
    cv_text(4, SG_INFO_Y, &FONT_S, sg_info[0], C_HI);
    cv_text(4, SG_INFO_Y + SG_INFO_DY, &FONT_S, sg_info[1], C_GRAY);
}
static void draw_step_panel(void)
{
    const track_t *t = TSEL;
    uint32_t sig, len = trk_len(t), at = t->seq_idx % len, key;
    step_info();
    sig = hs(hs(step_sig(), sg_info[0]), sg_info[1]);
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        cv_tall(OP_PY, OH_BODY, C_BLACK, sg_paint);  /* (the grid, its playhead strip, the held step's line) */
        st.ph_drawn = 0xFF;
    }
    key = !song.playing ? 0xFEu : at / 16u == st.page ? at % 16u : at / 16u < st.page ? 16u : 17u;
    if (key == st.ph_drawn || ui.overlay == 2u)         /* (still under a toast) */
        return;
    st.ph_drawn = (uint8_t)key;
    cv_begin(240, 4, C_BLACK);
    if (key < 16u)
        cv_rect(SG_X + (int32_t)key * SG_CW, 0, SG_CW - 2, 4, C_WHITE);   /* the playhead's column */
    else if (key != 0xFEu)
        cv_rect(key == 16u ? 0 : 236, 0, 4, 4, C_DIM);  /* outside the window (FOLLOW off): which side */
    cv_blit(0, OP_PY + SG_PH_Y);
}
