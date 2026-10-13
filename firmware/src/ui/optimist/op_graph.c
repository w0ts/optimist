/* SPDX-License-Identifier: GPL-3.0-only */
/* The forms values are drawn with, the SOUND rows' graphs and the modal (docs/UI-OPTIMIST-DESIGN.md section 3).
 *   draw_gauge   one primitive set for every value, chosen from its range (op_cells.c cell_gauge): a bar from the
 *                left, a bar from the centre (bipolar), a row of dots (a short list), a tick (a long list), a pill
 *   sound graph  the cursor row's picture on SOUND where the core has one: the envelope (ENV, ENV2), the LFO's wave,
 *                the FX sends, the scale on a keyboard, the pattern's steps, the drum lane (its ENV row: the lane's
 *                envelope); the maths written here
 *                afresh (SLOOP's ui_draw.c is not called)
 *   the modal    a question over the panel: the verb big, its target big in its colour, "SAVE yes" / "HOME no";
 *                a red frame when it destroys or replaces the work, amber otherwise. The toast: a result, small */
#define OY_HEAD 0                       /* the bands (op_draw.c): the header, the cards, the panel (no footer) */
#define OH_HEAD 25
#define OY_CARD 28
#define OH_CARD 45
#define OY_PANEL 76
#define OH_PANEL 123
#define OH_CARD_2X2 93                  /* the cards 2x2: the big values' band, two rows of BIG_H + 1 */
#define OY_PANEL_2X2 123                /* ... and the panel under it, 117 rows to the screen's foot */
#define OP_CH (cards_2x2() ? OH_CARD_2X2 : OH_CARD)   /* the cards' band and the panel: 2x2, the mixer 1x4 */
#define OP_PY (cards_2x2() ? OY_PANEL_2X2 : OY_PANEL)
#define OP_PH (240 - OP_PY)
#define MODAL_H (OP_PH < OH_PANEL ? OP_PH : OH_PANEL)   /* the modal's box over the panel */
#define CARD_X(k) (3 + 59 * (int32_t)(k))
#define CARD_W 57
#define OP_SURF C_BLACK                 /* the cards' and tiles' surface: black, outlined (gfx.c cv_box; no grey fills) */

static uint32_t trk_on_step(uint32_t c, uint32_t i)    /* step i of track c has something to play */
{
    const track_t *t = &trk[c % NTRK];
    return is_drum(t) ? dstep_mask(&t->dstep[i % NSTEP]) != 0u : t->step[i % NSTEP].time == ST_NOTE && t->step[i % NSTEP].n;
}

/* the value of cell c as a form in x, y, w, h: on its colour; the empty part (off) a 1 px line along the foot, the
 * trough as a line on black, never a grey fill */
static void draw_gauge(int32_t x, int32_t y, int32_t w, int32_t h, const cell_t *c, uint16_t on, uint16_t off)
{
    int32_t span = c->gmax - c->gmin, v = c->gv - c->gmin, i, n, p, mid;
    switch (c->gk) {
    case GK_BAR:
        cv_rect(x, y + h - 1, w, 1, off);
        cv_rect(x, y, v * w / span, h, on);
        break;
    case GK_BIP:
        mid = x + (int32_t)(-c->gmin) * w / span;
        p = x + v * w / span;
        cv_rect(x, y + h - 1, w, 1, off);
        cv_rect(p < mid ? p : mid, y, p < mid ? mid - p : p - mid, h, on);
        cv_rect(mid, y, 1, h, on);                      /* (the centre always marked) */
        break;
    case GK_DOTS:
        n = span + 1;
        for (i = 0; i < n; i++) {
            int32_t x0 = x + i * w / n, x1 = x + (i + 1) * w / n;
            if (i == v)
                cv_rect(x0, y, x1 - x0 - 1, h, on);
            else
                cv_rect(x0, y + h - 1, x1 - x0 - 1, 1, off);
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
            cv_rect(x + 1, y + 1, w - 2, h - 2, off == C_LINE ? C_BLACK : off);   /* off: an empty pill (on black) */
        break;
    default:
        break;
    }
}

/* ---- SOUND: the cursor row's graph, in the panel's top GRAPH_H rows: all the height the page's few rows leave
 * (op_draw.c draw_list sets gr_h each time it paints; GRAPH_MIN is the least, kept while a long list scrolls) */
#define GRAPH_MIN (cards_2x2() ? 40 : 58)   /* (2x2: the panel shrinks, the graph keeps 40 rows) */
static int32_t gr_h = 58;
#define GRAPH_H gr_h
static int has_graph(const page_t *pg)
{
    return pg->graph == GR_ADSR || pg->graph == GR_ENV2 || pg->graph == GR_LFO || pg->graph == GR_FX || pg->graph == GR_INS || pg->graph == GR_BUS ||
           pg->graph == GR_SCALE || pg->graph == GR_STEPS || pg->graph == GR_DSND || pg->graph == GR_ARP;
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
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25, w = (uint32_t)t->p[P_LWAVE];
#if LANE_LFO_UI
    const dlfo_t *dc = is_drum(t) ? dlfo_get(lane_selected()) : 0;   /* (a lane's LFO page: its own wave, lane_play.c) */
    if (dc)
        ph = (uint32_t)dc->phase << 25, w = dc->wave;
#endif
    for (x = 0; x < 240; x++) {
        int32_t y = mid - lfo_wave(t, ph + (uint32_t)x * (0xFFFFFFFFu / 120u)) * amp / 32768;
#if LANE_LFO_UI
        if (dc)
            y = mid - lfo_shape(w, ph + (uint32_t)x * (0xFFFFFFFFu / 120u), 0) * amp / 32768;
#endif
        if (w == 4)
            y = mid - ((int32_t)((x / 20 * 2654435761u) >> 16) - 32768) * amp / 32768;
        if (x)
            cv_line(x - 1, py, x, y, col);
        py = y;
    }
    cv_line(0, mid, 239, mid, C_LINE);
}
/* SENDS, INSERTS, BUS SENDS: the amounts as needles, one under each card (SLOOP's graph_fx, copied; params.c
 * fx_needle: an INSERTS TYPE card, an empty slot: none); grey when bypassed */
static void graph_fx(uint32_t g, uint16_t c)
{
    const track_t *t = TSEL;
    uint32_t i;
    int32_t top = 4, bot = GRAPH_H - 4;
    if (!fx_on(t))
        c = C_DIM;
    for (i = 0; i < 4u; i++) {
        int32_t v = fx_needle(g, i), h = (v < 0 ? 0 : v) * (bot - top) / 127, x = CARD_X(i) + CARD_W / 2;
        if (v < 0)
            continue;
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
/* ARP: one bar of the arpeggiator as seq.c arp_next plays it (SLOOP's graph_arp, ui_overview.c, its maths copied: this
 * UI does not call into ui/sloop), always a picture (the user, 2026-10-12: "arp is the same as lfo/env. no list"):
 *   the notes   the ones held (the latch's too), else a reference chord, C4 E4 G4 (E C G with ORD PLAY), over OCT
 *               octaves, a guide line at each pitch, named on the top line with MODE and RATE
 *   the marks   a step of RATE, GATE long, every other step SWG late, up / down / up-down / random by MODE, a step PROB
 *               would skip dim; MODE OFF: the pattern UP would play, in the shade, "OFF" on the top line
 * It fills GRAPH_H: the pitches spread over the height under the top line, the marks as tall as they fit */
#define ARP_LINE 18                                     /* the top line: MODE RATE, the notes */
static uint32_t arp_ref(const track_t *t, uint8_t *nt)  /* the notes the picture plays (16 at most), how many */
{
    static const uint8_t SORTED[3] = {60, 64, 67}, PLAYED[3] = {64, 60, 67};
    uint32_t n = 0, i, j, h = (uint32_t)t->nheld;
    for (i = 0; i < h && i < 16u; i++) {
        for (j = 0; j < n && nt[j] != t->held[i]; j++)
            ;
        if (j == n)
            nt[n++] = t->held[i];
    }
    if (!n) {
        memcpy(nt, t->p[P_AORDER] ? PLAYED : SORTED, 3);
        n = 3u;
    }
    if (!t->p[P_AORDER])                                /* (NOTE: low to high; PLAY: as they went down) */
        for (i = 1; i < n; i++)
            for (j = i; j > 0u && nt[j - 1u] > nt[j]; j--) {
                uint8_t x = nt[j];
                nt[j] = nt[j - 1u];
                nt[j - 1u] = x;
            }
    return n;
}
static void arp_top_line(const track_t *t, const uint8_t *nt, uint32_t cnt, uint16_t c)
{
    char b[32], nm[6];
    uint32_t i, k, mode = (uint32_t)t->p[P_AMODE] % 6u;
    str_cpy(b, N_AMODE[mode], sizeof b);
    str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
    str_cpy(b + str_len(b), N_DIV[(uint32_t)t->p[P_ARATE] % 6u], sizeof b - str_len(b));
    cv_text(4, GR_TOP, &FONT_S, b, mode ? c : C_GRAY);
    str_cpy(b, t->nheld ? "HELD" : "", sizeof b);       /* (the reference chord: its notes alone) */
    for (i = 0; i < cnt && i < 4u; i++) {
        note_name(nm, nt[i]);
        k = str_len(b);
        if (k)
            b[k++] = ' ', b[k] = 0;
        str_cpy(b + k, nm, sizeof b - k);
    }
    if (cnt > 4u)
        str_cpy(b + str_len(b), "+", sizeof b - str_len(b));
    cv_text(236 - text_w(&FONT_S, b), GR_TOP, &FONT_S, b, t->nheld ? C_WHITE : C_GRAY);
}
static void graph_arp(uint16_t c)
{
    const track_t *t = TSEL;
    uint8_t nt[16];
    uint32_t cnt = arp_ref(t, nt), mode = (uint32_t)t->p[P_AMODE] % 6u, oct = (uint32_t)clamp(t->p[P_AOCT], 1, 4);
    uint32_t len = cnt * oct, steps = 4u * DIV_DEN[(uint32_t)t->p[P_ARATE] % 6u], i, b, lo = 127, hi = 0;
    int32_t top = GR_TOP + ARP_LINE, bot = GR_BOT, span, sw = 236 / (int32_t)steps, mh;
    int32_t gate = sw * t->p[P_AGATE] / 128, late = (int32_t)clamp(t->p[P_ASWING], 0, 100) * sw / 200;
    uint16_t mc = mode ? c : col_shade(c, 3u);          /* (OFF: the pattern UP would play, shaded) */
    for (i = 0; i < cnt; i++) {
        lo = nt[i] < lo ? nt[i] : lo;
        hi = nt[i] > hi ? nt[i] : hi;
    }
    hi += 12u * (oct - 1u);
    span = hi > lo ? (int32_t)(hi - lo) : 1;
    mh = (bot - top - 4) / (span + 1);
    mh = mh < 3 ? 3 : mh > 12 ? 12 : mh;
#define ARPY(n) (bot - 2 - (int32_t)((n) - lo) * (bot - top - 4) / span)
    arp_top_line(t, nt, cnt, c);
    for (i = 0; i < len; i++)                           /* the guide lines: every pitch it plays */
        cv_rect(2, ARPY(nt[i % cnt] + 12u * (i / cnt)), 234, 1, C_LINE);
    for (b = 0; b < 4u; b++)                            /* the beats */
        cv_rect(2 + (int32_t)b * 59, bot + 1, 1, 3, C_LINE);
    cv_line(0, bot + 1, 239, bot + 1, C_LINE);
    for (i = 0; i < steps; i++) {
        uint32_t k = i + 1u, j, cyc = len > 1u ? 2u * len - 2u : 1u, h = k * 2654435761u;
        int32_t x = 2 + (int32_t)i * 236 / (int32_t)steps + ((i & 1u) ? late : 0);
        j = mode == 2u ? len - 1u - k % len : mode == 3u ? (k % cyc < len ? k % cyc : cyc - k % cyc) :
            mode == 4u ? (h >> 16) % len : k % len;
        cv_rect(x, ARPY(nt[j % cnt] + 12u * (j / cnt)) - mh / 2, gate > 1 ? gate : 1, mh,
                (int32_t)((h >> 8) % 127u) >= t->p[P_APROB] ? C_DIM : mc);
    }
#undef ARPY
}
static void graph_steps(uint16_t col)                    /* the pattern: every step over its length */
{
    const track_t *t = TSEL;
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i;
    for (i = 0; i < len; i++) {
        int32_t rows = len > 32u ? 2 : 1, rh = (GRAPH_H - 16 - 6 * (rows - 1)) / rows, x = 4 + (int32_t)(i % 32u) * 7,
                y = i < 32u ? 8 : 14 + rh;
        rh = rh < 12 ? 12 : rh > 64 ? 64 : rh;
        cv_box(x, y, 5, rh, trk_on_step(song.sel, i) ? col : C_BLACK);
    }
}
/* the drum lane: its name big in its source's colour, its sound under it (the lane's pages: they edit it); the SOUND
 * row on DR itself (kit): the kit big in its kind's colour, its style (what PRESETS steps there) */
static void graph_lane_of(int kit)
{
    uint32_t l = lane_selected();
    int32_t ny = GRAPH_H >= 58 ? 6 + (GRAPH_H - 58) / 2 : 6;     /* (a taller picture: the name and kit in the middle) */
    const char *sub = kit ? drum_kit_style() : lane_sound_name(l);
    cv_text(4, ny, &FONT_L, kit ? drum_kit_name() : LANE_NAME[l], kit ? kit_col() : lane_col(l));
    if (GRAPH_H >= 58)
        cv_text(4, 40 + ny - 6, &FONT_S, sub, C_GRAY);
    else                                                /* (2x2: beside the name, at the right) */
        cv_text(236 - text_w(&FONT_S, sub), 22, &FONT_S, sub, C_GRAY);
}
static void graph_lane(void) { graph_lane_of(0); }
/* ---- the drum track's ENV: the picked lane's envelope as the drums run it (SLOOP's ui_drums.c graph_dsnd, its maths
 * copied: this UI does not call into ui/sloop). The level over the sound's length in the lane's colour (synthesised:
 * the body and the noise as they hold and decay, CLICK a tick at the start; a sample: its peaks with DECAY; an X0X
 * model: a sketch of its decay as set), the pitch drop (BEND) in white, the length at the right. A LEVEL-only sound
 * (the 909's rim shot) says so */
#define DENV_W 232
static void denv_head(uint32_t l, const char *right)
{
    cv_text(4, GR_TOP, &FONT_S, LANE_NAME[l], lane_col(l));
    cv_text(236 - text_w(&FONT_S, right), GR_TOP, &FONT_S, right, C_GRAY);
}
static void denv_time(uint32_t ms)
{
    char b[12];
    fmt_int(b, (int32_t)ms);
    str_cpy(b + str_len(b), "ms", 4);
    cv_text(236 - text_w(&FONT_S, b), GR_BOT - 14, &FONT_S, b, C_DIM);
}
#if DRUM_X0X
static void denv_x0x(uint32_t l, uint32_t kit, const int8_t *o, int32_t y0, int32_t h, uint16_t c)
{
    int32_t dk = (o ? o[DE_DECAY] : 0) + (int32_t)x0x_var_decay(kit, l), w = 6 + (64 + dk) * 3 / 4, x, y;
    for (x = 0; x < DENV_W; x++) {
        y = h * 1024 / (1024 + x * 1024 / (w > 1 ? w : 1));   /* (1 / (1 + t / w): no exp here) */
        cv_line(x + 4, y0 + h, x + 4, y0 + h - y * y / h, c);
    }
}
#endif
static void denv_sample(uint32_t l, uint32_t usr, const int8_t *o, int32_t y0, int32_t h, uint16_t c)
{
    const smp_zone_t *z = 0;
    uint32_t pos = 0, end = 0, k = o && o[DE_DECAY] < 0 ? DECAY_K[clamp(127 + 2 * o[DE_DECAY], 0, 127)] : 0u;
    uint32_t i, n, per, rate, x;
    int32_t e = 32767, pk[DENV_W], mx = 1;
    voice_t v;
    int32_t (*next)(const smp_zone_t *, voice_t *, int) = FAR(sample_next);
#if FELUCCA_DRUM_USR
    if (usr)
        z = dl_usr_zone(l, usr, &pos, &end);
#endif
    if (!usr && drum_set() >= 0) {
        const smp_set_t *set = &SMP_SETS[drum_set()];
        for (i = 0; i < set->nz; i++)
            if (LANE_NOTE[l] >= SMP_ZONES[set->z0 + i].lo && LANE_NOTE[l] <= SMP_ZONES[set->z0 + i].hi)
                z = &SMP_ZONES[set->z0 + i];
        end = z ? z->n : 0u;
    }
    if (!z || end <= pos)
        return;
    memset(&v, 0, sizeof v);
    for (i = 0; i < pos; i++)                           /* (the start: decoded as dl_tick does) */
        next(z, &v, 0);
    n = end - pos;
    per = (n + DENV_W - 1u) / DENV_W;
    for (x = 0; x < DENV_W; x++) {
        int32_t m = 0;
        for (i = 0; i < per && pos < end; i++, pos++) {
            int32_t s = next(z, &v, 0);
            s = mulq15(s < 0 ? -s : s, e);
            m = s > m ? s : m;
            if (k)
                e = (int32_t)(((uint32_t)e * k) >> 16);
        }
        pk[x] = m;
        mx = m > mx ? m : mx;
    }
    for (x = 0; x < DENV_W; x++)
        if (pk[x])
            cv_line((int32_t)x + 4, y0 + h, (int32_t)x + 4, y0 + h - pk[x] * h / mx, c);
    rate = ((z->rate ? z->rate : 65536u) >> 4) * FS >> 12;   /* the length played: n samples at its rate, TUNE */
    denv_time(n * 1000u / (rate ? rate : 1u) * 65536u / pow2_q16(dl_tune(l) * 16));
}
static void denv_synth(uint32_t l, uint32_t kit, const int8_t *o, int32_t y0, int32_t h, uint16_t c)
{
    int32_t semi;
    const dsnd_t *d = DS_SND(&DS_KITS[kit - DRUM_SAMPLED], ds_lane(LANE_NOTE[l], &semi));
    uint32_t ka, kn, kp, j, blocks, per, hold, nhold, bend, x;
    int32_t a0, n0, pk, a, n, pe = 32767, px = 4, py = y0 + h, bot = y0 + h;
#if FELUCCA_DRUM_EDIT
    dsnd_t ed;                                          /* (on the stack: no RAM kept for a picture) */
    if (o) {
        de_synth(&ed, d, o);
        d = &ed;
    }
#else
    (void)o;
#endif
    ka = ds_k32(d->decay), kn = ds_k32(d->ndec), kp = ds_k32(d->btime);
    a0 = d->wave ? (int32_t)d->tlev * 258 : 0, n0 = (d->src & 15u) ? (int32_t)d->nlev * 258 : 0;
    pk = a0 > n0 ? a0 : n0;
    pk = pk ? pk : 1;
    bend = d->bend > 36u ? 36u : d->bend;
    a = a0, n = n0, hold = ds_blocks(d->hold), nhold = ds_blocks(d->nhold);
    for (blocks = 0; blocks < FS * 4u / CTL && (hold || nhold || a > pk / 64 || n > pk / 64); blocks++) {
        if (hold) hold--; else a = (int32_t)(((uint32_t)a * ka) >> 16);
        if (nhold) nhold--; else n = (int32_t)(((uint32_t)n * kn) >> 16);
    }
    per = blocks / DENV_W + 1u;
    a = a0, n = n0, hold = ds_blocks(d->hold), nhold = ds_blocks(d->nhold);
    for (x = 0; x < DENV_W; x++) {
        int32_t lv = a > n ? a : n, yp = bot - pe * h / 32767 * (int32_t)bend / 36;
        cv_line((int32_t)x + 4, bot, (int32_t)x + 4, bot - lv * h / pk, c);
        if (bend && x)
            cv_line(px, py, (int32_t)x + 4, yp, C_WHITE);
        px = (int32_t)x + 4;
        py = yp;
        for (j = 0; j < per; j++) {
            if (hold) hold--; else a = (int32_t)(((uint32_t)a * ka) >> 16);
            if (nhold) nhold--; else n = (int32_t)(((uint32_t)n * kn) >> 16);
            pe = (int32_t)(((uint32_t)pe * kp) >> 16);
        }
    }
    if (d->click)                                       /* CLICK: the attack's tick, as tall as it is loud */
        cv_rect(4, bot - (int32_t)d->click * h / 127, 2, (int32_t)d->click * h / 127, C_WHITE);
    denv_time(per * DENV_W * CTL * 1000u / FS);
}
static void graph_denv(void)
{
    uint32_t l = lane_selected(), usr = dl_usr_of(l), kit = dl_kit_of(l, drum_kit());
    const int8_t *o = dl_ofs(l);
    const char *tg = usr ? "SMP" : kit < DRUM_SAMPLED ? "SMP" : kit < DRUM_SYNTH_END ? "SYN" : "X0X";
    int32_t y0 = GR_TOP + 16, h = GR_BOT - y0;
    uint16_t c = lane_col(l);
    cv_line(0, GR_BOT + 1, 239, GR_BOT + 1, C_LINE);
    denv_head(l, tg);
    if (!dsnd_has_env(l)) {
        cv_text(4, y0 + (h > 14 ? (h - 14) / 2 : 0), &FONT_S, "No envelope: level only", C_GRAY);
        return;
    }
    if (h < 8)
        return;
#if DRUM_X0X
    if (!usr && x0x_show(kit, l)) {
        denv_x0x(l, kit, o, y0, h, c);
        return;
    }
    if (!usr && kit >= DRUM_UID_X909)
        kit = x0x_standin(kit);                         /* (a note the machine lacks: the stand-in's sound) */
#endif
    if (usr || kit < DRUM_SAMPLED || !FELUCCA_DRUM_SYNTH)
        denv_sample(l, usr, o, y0, h, c);
    else
        denv_synth(l, kit, o, y0, h, c);
}

/* what the graphs read beyond the cursor row's cells (those are in the list's signature already): the scale, the
 * FX bypass and the slots' sends, the LFO's wave and phase, the lane, the pattern's steps */
static uint32_t graph_sig(void)
{
    const track_t *t = TSEL;
    uint32_t h = (uint32_t)lane_sel ^ (uint32_t)t->p[P_ROOT] << 4 ^ (uint32_t)t->p[P_SCALE] << 8 ^ trk_len(t) << 16, i;
    h = h * 31u + (uint32_t)t->p[P_AMODE] + 8u * (uint32_t)t->p[P_ARATE] + 64u * (uint32_t)t->p[P_AOCT] +
        512u * (uint32_t)t->p[P_AGATE] + 65536u * (uint32_t)t->p[P_ASWING] + 8388608u * (uint32_t)t->p[P_AORDER] +
        (uint32_t)t->p[P_APROB] * 977u;
    for (i = 0; i < t->nheld && i < 16u; i++)           /* (ARP: the notes held) */
        h = h * 31u + t->held[i];
    h = h * 31u + t->nheld;
    h ^= (uint32_t)t->p[P_FXOFF] << 24 ^ (uint32_t)t->p[P_LWAVE] << 26 ^ (uint32_t)t->p[P_LPHASE] << 12;
    for (i = 0; i < 4u; i++)
        h = h * 31u + (uint32_t)(fx_needle(GR_FX, i) + 1) * 7u + (uint32_t)(fx_needle(GR_INS, i) + 1) * 131u + ins_slot[song.sel % NTRK][i & 1u] * 977u;
    for (i = 0; i < NSTEP; i++)
        h = h * 3u + trk_on_step(song.sel, i);
    h = h * 31u + drum_kit() + smp_user_gen * 131u + dl.src[lane_selected()];   /* (the lane's envelope: graph_denv) */
    for (i = 0; i < DE_N; i++)
        h = h * 31u + (uint8_t)dl.ofs[lane_selected()][i];
    h = h * 31u + dlfo_get(lane_selected())->wave * 128u + dlfo_get(lane_selected())->phase;   /* (a lane's LFO wave) */
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
    case GR_INS:
    case GR_BUS:
        graph_fx(pg->graph, col);
        break;
    case GR_SCALE:
        graph_scale(col);
        break;
    case GR_STEPS:
        graph_steps(col);
        break;
    case GR_ARP:
        graph_arp(col);
        break;
    default:
        if (pg->scope == SC_DSND && pg->fam == FAM_ENV)
            graph_denv();                               /* (the drum track's ENV: the lane's envelope) */
        else
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
static void modal_words(char *v, char *a, char *q)      /* the verb, its target and the one-line question as drawn */
{
    op_case(v, ui.arm_verb, sizeof ui.arm_verb);
    if (ui.arm_raw) {                                   /* a device's name: as advertised, only the verb is cased */
        uint32_t n;
        str_cpy(a, ui.arm_arg, sizeof ui.arm_arg);
        str_cpy(q, v, sizeof ui.arm_q);
        n = str_len(q);
        q[n - 1u] = ' ';                                /* ("Forget? " + name + "?") */
        str_cpy(q + n, a, sizeof ui.arm_q - n);
        str_cpy(q + str_len(q), "?", sizeof ui.arm_q - str_len(q));
    } else {
        op_case(a, ui.arm_arg, sizeof ui.arm_arg);
        op_case(q, ui.arm_q, sizeof ui.arm_q);
    }
}
static void draw_modal(void)
{
    char v[sizeof ui.arm_verb], a[sizeof ui.arm_arg], q[sizeof ui.arm_q];
    const felucca_font_t *fl = font_big(), *ft;
    uint16_t fr = ui.arm_danger ? C_ERR : C_WARN, tc = target_col(ui.arm_arg);
    modal_words(v, a, q);
    ft = text_w(fl, a) <= MODAL_W ? fl : &FONT_S;
    cv_begin(240, MODAL_H, C_BLACK);
    cv_rect(8, 1, 224, MODAL_H - 2, fr);               /* the frame: red destroys, amber the rest */
    cv_rect(11, 4, 218, MODAL_H - 8, OP_SURF);
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
    cv_blit(0, OP_PY);
}
static void draw_toast(void)                             /* the result of a confirmed action: small, in the middle */
{
    char m[sizeof ui.msg];
    int32_t w = text_w(&FONT_S, op_case(m, ui.msg, sizeof m)) + 24;   /* "T1 cleared" */
    uint32_t st = ui.msg_st ? ui.msg_st : msg_status(ui.msg);   /* (the whole message: "DR CLEARED" is green) */
    w = w > 236 ? 236 : w;
    cv_begin((uint32_t)w, 36, C_BLACK);
    cv_rect(0, 0, w, 36, ui.toast_col ? ui.toast_col : st ? C_STATUS[st & 3u] : C_HI);   /* (a preset: its engine's) */
    cv_rect(2, 2, w - 4, 32, OP_SURF);
    cv_text(12, 10, &FONT_S, m, C_WHITE);
    cv_blit((uint32_t)(120 - w / 2), OP_PY + 44u);
}
