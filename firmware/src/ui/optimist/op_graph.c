/* SPDX-License-Identifier: GPL-3.0-only */
/* The forms values are drawn with, the SOUND rows' graphs and the modal (docs/UI-OPTIMIST-DESIGN.md section 3).
 *   draw_gauge   one primitive set for every value, chosen from its range (op_cells.c cell_gauge): a bar from the
 *                left, a bar from the centre (bipolar), a row of dots (a short list), a tick (a long list), a pill
 *   sound graph  the cursor row's picture on SOUND where the core has one: the envelope (ENV, ENV2), the LFO's wave,
 *                the FX sends, the scale on a keyboard, the pattern's steps, the drum lane; the maths written here
 *                afresh (SLOOP's ui_draw.c is not called)
 *   the modal    a question over the panel: the verb big, its target big in its colour, "SAVE yes" / "HOME no";
 *                a red frame when it destroys or replaces the work, amber otherwise. The toast: a result, small */
#define OY_HEAD 0                       /* the bands (op_draw.c): the header, the cards, the panel, the footer */
#define OH_HEAD 25
#define OY_CARD 28
#define OH_CARD 45
#define OY_PANEL 76
#define OH_PANEL 123
#define OY_FOOT 202
#define OH_FOOT 38
#define CARD_X(k) (3 + 59 * (int32_t)(k))
#define CARD_W 57
#define OP_SURF RGB(26, 26, 30)         /* the cards' and columns' surface (Felucca's SURF; its tokens: phase 5) */

static uint32_t trk_on_step(uint32_t c, uint32_t i)    /* step i of track c has something to play */
{
    const track_t *t = &trk[c % NTRK];
    return is_drum(t) ? dstep_mask(&t->dstep[i % NSTEP]) != 0u : t->step[i % NSTEP].time == ST_NOTE && t->step[i % NSTEP].n;
}

/* the value of cell c as a form in x, y, w, h: on its colour, off the empty part */
static void draw_gauge(int32_t x, int32_t y, int32_t w, int32_t h, const cell_t *c, uint16_t on, uint16_t off)
{
    int32_t span = c->gmax - c->gmin, v = c->gv - c->gmin, i, n, p, mid;
    switch (c->gk) {
    case GK_BAR:
        cv_rect(x, y, w, h, off);
        cv_rect(x, y, v * w / span, h, on);
        break;
    case GK_BIP:
        mid = x + (int32_t)(-c->gmin) * w / span;
        p = x + v * w / span;
        cv_rect(x, y, w, h, off);
        cv_rect(p < mid ? p : mid, y, p < mid ? mid - p : p - mid, h, on);
        cv_rect(mid, y, 1, h, on);                      /* (the centre always marked) */
        break;
    case GK_DOTS:
        n = span + 1;
        for (i = 0; i < n; i++) {
            int32_t x0 = x + i * w / n, x1 = x + (i + 1) * w / n;
            cv_rect(x0, y, x1 - x0 - 1, h, i == v ? on : off);
        }
        break;
    case GK_POS:
        cv_rect(x, y + h / 2, w, 1, off);
        cv_rect(x + v * (w - 3) / span, y, 3, h, on);
        break;
    case GK_PILL:
        w = w > 16 ? 16 : w;
        cv_rect(x, y, w, h, on);
        if (!v && w > 2 && h > 2)
            cv_rect(x + 1, y + 1, w - 2, h - 2, off);   /* off: an empty pill */
        break;
    default:
        break;
    }
}

/* ---- SOUND: the cursor row's graph, in the panel's top GRAPH_H rows */
#define GRAPH_H 58
static int has_graph(const page_t *pg)
{
    return pg->graph == GR_ADSR || pg->graph == GR_ENV2 || pg->graph == GR_LFO || pg->graph == GR_FX ||
           pg->graph == GR_SCALE || pg->graph == GR_STEPS || pg->graph == GR_DSND;
}
/* the graph shown on SOUND's row r: its own page's, else the nearest row of its family above (then below) that has
 * one, so a family shows its shape on every one of its rows (the user, 2026-10-08: "LFO and ENV should always display
 * their shape"): LFO DEST the LFO's wave, ENV DEST and ENV2 DEST their envelopes. 0: none */
static const page_t *snd_graph_page(uint32_t r)
{
    const page_t *p = snd_page(r), *q;
    uint32_t i, n = snd_rows();
    if (!p || has_graph(p))
        return p;
    for (i = r; i-- > 0u && (q = snd_page(i)) != 0 && q->fam == p->fam;)
        if (has_graph(q))
            return q;
    for (i = r + 1u; i < n && (q = snd_page(i)) != 0 && q->fam == p->fam; i++)
        if (has_graph(q))
            return q;
    return 0;
}
static int32_t page_frac(const page_t *pg, uint32_t k, int32_t full)   /* value k of the page as 0..full */
{
    cell_t c;
    page_cell(pg, k, &c);
    return c.gk == GK_NONE || c.gmax <= c.gmin ? 0 : (c.gv - c.gmin) * full / (c.gmax - c.gmin);
}
/* The envelope, SLOOP's maths (ui/sloop/ui_draw.c graph_adsr4, copied: this UI does not call into ui/sloop), as
 * voice.c runs it: the attack linear, decay and release exponential (env += (target - env) * k each tick, ~99 %
 * after the set time); the time axis is the value (the times themselves are exponential). The four values 0..127
 * come from the page's own descriptors (page_frac), so ENV and ANALOG 2's ENV2 share it */
#define GR_TOP 4
#define GR_BOT (GRAPH_H - 5)
static void graph_adsr4(int32_t atk, int32_t dec, int32_t sus127, int32_t rel, uint16_t c)
{
    int32_t a = 4 + atk * 50 / 127, d = 6 + dec * 50 / 127, r = 6 + rel * 60 / 127;
    int32_t top = GR_TOP, bot = GR_BOT, sus = sus127 * 1000 / 127;     /* 0..1000 */
    int32_t x0 = 6, x1 = x0 + a, x3 = 232 - r, i, px, py;
    int32_t e = 32768;                                                  /* exp(-4.6 u), Q15 */
#define EGY(lvl) (bot - (lvl) * (bot - top) / 1000)
    cv_line(x0, bot, x1, top, c);                                       /* attack: linear */
    px = x1;
    py = top;
    for (i = 1; i <= d; i++) {                                          /* decay: exponential to SUS */
        int32_t lvl;
        e = (e * (32768 - 150733 / d)) >> 15;                           /* k^d = exp(-4.6) */
        lvl = sus + ((1000 - sus) * e >> 15);
        cv_line(px, py, x1 + i, EGY(lvl), c);
        px = x1 + i;
        py = EGY(lvl);
    }
    cv_line(px, py, x3, EGY(sus), c);                                   /* sustain */
    px = x3;
    py = EGY(sus);
    e = 32768;
    for (i = 1; i <= r; i++) {                                          /* release: exponential to 0 */
        e = (e * (32768 - 150733 / r)) >> 15;
        cv_line(px, py, x3 + i, EGY(sus * e >> 15), c);
        px = x3 + i;
        py = EGY(sus * e >> 15);
    }
    cv_line(0, bot + 1, 239, bot + 1, C_LINE);
#undef EGY
}
static void graph_adsr(const page_t *pg, uint16_t col)
{
    int32_t rel = page_frac(pg, 3, 127);
    if (pg->graph == GR_ENV2 && !rel)                   /* (ANALOG 2's ENV2: REL2 0 releases at DEC2's time) */
        rel = page_frac(pg, 1, 127);
    graph_adsr4(page_frac(pg, 0, 127), page_frac(pg, 1, 127), page_frac(pg, 2, 127), rel, col);
}
/* the LFO as it runs: two cycles over the width from the core's own wave (core/voice.c lfo_wave), its phase as set;
 * S&H drawn as random steps (SLOOP's graph_lfo, copied) */
static void graph_lfo(uint16_t col)
{
    track_t *t = TSEL;
    int32_t x, py = GRAPH_H / 2, mid = GRAPH_H / 2, amp = GRAPH_H / 2 - 6;
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25;
    for (x = 0; x < 240; x++) {
        int32_t y = mid - lfo_wave(t, ph + (uint32_t)x * (0xFFFFFFFFu / 120u)) * amp / 32768;
        if (t->p[P_LWAVE] == 4)
            y = mid - ((int32_t)((x / 20 * 2654435761u) >> 16) - 32768) * amp / 32768;
        if (x)
            cv_line(x - 1, py, x, y, col);
        py = y;
    }
    cv_line(0, mid, 239, mid, C_LINE);
}
/* FX: the four slots' sends as needles, one under each card (SLOOP's graph_fx, copied); grey when bypassed */
static void graph_fx(uint16_t c)
{
    const track_t *t = TSEL;
    uint32_t i;
    int32_t top = 4, bot = GRAPH_H - 4;
    if (!fx_on(t))
        c = C_DIM;
    for (i = 0; i < 4u; i++) {
        uint32_t id = fxs_amt(i);                       /* (the slot's amount: fx_slots.c; empty: none) */
        int32_t v = id == 0xFFu ? 0 : t->p[id], h = (v < 0 ? -2 * v : v) * (bot - top) / 127, x = CARD_X(i) + CARD_W / 2;
        h = h > bot - top ? bot - top : h;
        cv_rect(x, top, 1, bot - top, C_LINE);
        cv_rect(x, bot - h, 1, h, c);
        cv_rect(x - 3, bot - h, 7, 1, c);
    }
}
/* the scale on an octave of keys: the black keys high, the white low, the scale's notes lit, the root white
 * (SLOOP's graph_scale, copied, with seq.c's scale_mask) */
static void graph_scale(uint16_t c)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    const track_t *t = TSEL;
    uint32_t i, mask = scale_mask(t);
    int32_t span = GR_BOT - GR_TOP, low = span * 30 / 82, len = span * 40 / 82;
    for (i = 0; i < 12u; i++) {
        uint32_t deg = (i + 12u - (uint32_t)t->p[P_ROOT]) % 12u;
        int32_t x = 6 + (int32_t)i * 19, y = GR_TOP + 2 + (BLACK[i] ? 0 : low);
        uint16_t col = (mask >> deg) & 1u ? (deg == 0u ? C_WHITE : c) : C_DIM;
        cv_rect(x, y, 16, 1, col);
        cv_rect(x + 7, y, 1, len, col);
    }
}
static void graph_steps(uint16_t col)                    /* the pattern: every step over its length */
{
    const track_t *t = TSEL;
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i;
    for (i = 0; i < len; i++) {
        int32_t x = 4 + (int32_t)(i % 32u) * 7, y = i < 32u ? 8 : 32;
        cv_rect(x, y, 5, 18, trk_on_step(song.sel, i) ? col : C_LINE);
    }
}
static void graph_lane(void)                             /* the drum lane: its name big, its source's colour */
{
    uint32_t l = lane_selected();
    cv_text(4, 6, &FONT_L, LANE_NAME[l], lane_col(l));
    cv_text(4, 40, &FONT_S, drum_kit_name(), C_GRAY);
}
/* what the graphs read beyond the cursor row's cells (those are in the list's signature already): the scale, the
 * FX bypass and the slots' sends, the LFO's wave and phase, the lane, the pattern's steps */
static uint32_t graph_sig(void)
{
    const track_t *t = TSEL;
    uint32_t h = (uint32_t)lane_sel ^ (uint32_t)t->p[P_ROOT] << 4 ^ (uint32_t)t->p[P_SCALE] << 8 ^ trk_len(t) << 16, i;
    h ^= (uint32_t)t->p[P_FXOFF] << 24 ^ (uint32_t)t->p[P_LWAVE] << 26 ^ (uint32_t)t->p[P_LPHASE] << 12;
    for (i = 0; i < 4u; i++)
        h = h * 31u + (fxs_amt(i) == 0xFFu ? 0u : (uint16_t)t->p[fxs_amt(i)]);
    for (i = 0; i < NSTEP; i++)
        h = h * 3u + trk_on_step(song.sel, i);
    return h;
}
static void draw_sound_graph(const page_t *pg)
{
    uint16_t col = trk_col(song.sel);
    switch (pg->graph) {
    case GR_ADSR:
    case GR_ENV2:
        graph_adsr(pg, col);
        break;
    case GR_LFO:
        graph_lfo(col);
        break;
    case GR_FX:
        graph_fx(col);
        break;
    case GR_SCALE:
        graph_scale(col);
        break;
    case GR_STEPS:
        graph_steps(col);
        break;
    default:
        graph_lane();
        break;
    }
}

/* ---- the modal and the toast, over the panel */
static uint16_t target_col(const char *s)               /* a track named: its colour (the engine's, the kit's) */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        if (str_eq(s, trk_tag(i)))
            return trk_col(i);
    return C_HI;
}
#define MODAL_W 208                     /* the text's room inside the box (224 px framed, 8 px margins) */
/* the large face with lower case: FONT_L is FONT_S's glyphs at 2x but stops at '_' (gfx.c folds a..z to capitals),
 * so sentence case needs FONT_S's range; made here, not in the generated font that SLOOP's UI draws with */
static const felucca_font_t *font_big(void)
{
    static felucca_font_t f;
    if (!f.scale) {
        f = FONT_L;
        f.last = FONT_S.last;
    }
    return &f;
}
/* the question in the large font: one line, "Clear DR?", its target in its colour, when it fits; else the verb
 * ("Save?") over its target ("Project 1", in the small font when even that is too wide); sentence case as drawn */
static void draw_modal(void)
{
    char v[sizeof ui.arm_verb], a[sizeof ui.arm_arg], q[sizeof ui.arm_q];
    const felucca_font_t *fl = font_big(), *ft;
    uint16_t fr = ui.arm_danger ? C_ERR : C_WARN, tc = target_col(ui.arm_arg);
    op_case(v, ui.arm_verb, sizeof v);
    op_case(a, ui.arm_arg, sizeof a);
    op_case(q, ui.arm_q, sizeof q);
    ft = text_w(fl, a) <= MODAL_W ? fl : &FONT_S;
    cv_begin(240, OH_PANEL, C_BLACK);
    cv_rect(8, 1, 224, OH_PANEL - 2, fr);               /* the frame: red destroys, amber the rest */
    cv_rect(11, 4, 218, OH_PANEL - 8, OP_SURF);
    if (a[0] && text_w(fl, q) <= MODAL_W) {
        int32_t x;
        v[str_len(v) - 1u] = ' ';                       /* "Clear? " -> "Clear " (the "?" goes after the target) */
        x = cv_text(120 - text_w(fl, q) / 2, 30, fl, v, C_WHITE);
        x = cv_text(x, 30, fl, a, tc);
        cv_text(x, 30, fl, "?", C_WHITE);
    } else {
        cv_text(120 - text_w(fl, v) / 2, 12, fl, v, C_WHITE);
        cv_text(120 - text_w(ft, a) / 2, 50, ft, a, tc);
    }
    cv_text(24, 96, &FONT_S, "Home no", C_GRAY);       /* (as the panel: HOME left of SAVE) */
    cv_text(216 - text_w(&FONT_S, "Save yes"), 96, &FONT_S, "Save yes", C_WHITE);
    cv_blit(0, OY_PANEL);
}
static void draw_toast(void)                             /* the result of a confirmed action: small, in the middle */
{
    char m[sizeof ui.msg];
    int32_t w = text_w(&FONT_S, op_case(m, ui.msg, sizeof m)) + 24;   /* "T1 cleared" */
    uint32_t st = ui.msg_st ? ui.msg_st : msg_status(ui.msg);   /* (the whole message: "DR CLEARED" is green) */
    w = w > 236 ? 236 : w;
    cv_begin((uint32_t)w, 36, C_BLACK);
    cv_rect(0, 0, w, 36, st ? C_STATUS[st & 3u] : C_HI);
    cv_rect(2, 2, w - 4, 32, OP_SURF);
    cv_text(12, 10, &FONT_S, m, C_WHITE);
    cv_blit((uint32_t)(120 - w / 2), OY_PANEL + 44u);
}
