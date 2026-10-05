/* SPDX-License-Identifier: GPL-3.0-only */
/* DX7 operator editor, on the FM-1's printed black-key labels. On a DX7 track ENV is a layer (LY_OPS,
 * seq.c): hold ENV and press a black key to pick what to edit; the white keys keep playing notes
 * (audition while you edit):
 *     OP1 .. OP6    that operator                  PIT          the pitch envelope
 *     GLO           algorithm, feedback, LFO, OP 7/8 routing
 *     MONO / POLY   the voice mode                 the last (unlabelled) black key: OP7
 *     OCT+ held + the last black key: OP8 (OCT+ is the modifier, as on the drum track where OCT+ held
 *     makes a key's hard hit: ENV, then OCT+, then the key)
 * The DX7 page stays up when ENV is let go (the black keys play notes again); KNOB 1..4 edit the four
 * values shown. Pages, as on every page family: ENV tapped turns to the next one; with ENV held, OCT-
 * goes back one, OCT+ tapped alone (no key while it was down) goes on one. An operator has FREQ, LEVEL,
 * EG RATE, EG LEVEL, KEY SCALE; PIT has RATE and LEVEL; GLO has ALGO, LFO, LFO 2 and OP 7/8.
 * The screen: the algorithm (OP7 / OP8 as EXT routes them), the operator picked in white, carriers in
 * yellow, OP7 / OP8 in orange; the voice name (* = edited) and the black keys' map, as on the keyboard.
 * Edits go into the part's voice (eng_dx7.c dx7_part[].v); sounding notes follow them. They live in RAM:
 * VOICE (EDIT 1 KNOB 1) loads a voice over them, and a project does not keep them (yet). */
enum { DX7T_OP7 = 6, DX7T_OP8 = 7, DX7T_PIT = 8, DX7T_GLO = 9, DX7T_MONO = 20, DX7T_POLY = 22 };
enum { DK_NUM, DK_ENUM, DK_CURVE, DK_BREAK, DK_COARSE, DK_NONE };
typedef struct {
    const char *lab;
    uint8_t off;                 /* in the operator's 21 bytes (operator pages), else in the voice */
    uint8_t max;
    int8_t show;                 /* value shown = byte + show */
    uint8_t kind;
    const char *const *names;    /* DK_ENUM */
} dx7_pd_t;
typedef struct {
    const char *name;
    dx7_pd_t p[4];
} dx7_page_t;

static const char *const DX7N_MODE[] = {"ratio", "fixed"};
static const char *const DX7N_ONOFF[] = {"off", "on"};
static const char *const DX7N_WAVE[] = {"tri", "saw-", "saw+", "sqr", "sin", "s&h"};
static const char *const DX7N_EXT[] = {"off", "8>7", "7+8", "8>7>op", "7+8>op"};
static const char *const DX7N_CURVE[] = {"-l", "-e", "+e", "+l"};
static const char *const DX7N_FIXED[] = {"1hz", "10hz", "100hz", "1khz"};
_Static_assert(sizeof DX7N_EXT / sizeof DX7N_EXT[0] == DX7_EXT_COUNT, "a name per EXT routing");
#define DX7_NOP {"", 0, 0, 0, DK_NONE, 0}
static const dx7_page_t DX7_OP_PAGES[] = {
    {"freq", {{"mode", DX7_MODE, 1, 0, DK_ENUM, DX7N_MODE}, {"coarse", DX7_COARSE, 31, 0, DK_COARSE, 0},
              {"fine", DX7_FINE, 99, 0, DK_NUM, 0}, {"detune", DX7_DET, 14, -7, DK_NUM, 0}}},
    {"level", {{"level", DX7_OL, 99, 0, DK_NUM, 0}, {"vel", DX7_KVS, 7, 0, DK_NUM, 0},
               {"ams", DX7_AMS, 3, 0, DK_NUM, 0}, {"rate sc", DX7_RS, 7, 0, DK_NUM, 0}}},
    {"eg rate", {{"r1", DX7_R1, 99, 0, DK_NUM, 0}, {"r2", DX7_R1 + 1, 99, 0, DK_NUM, 0},
                 {"r3", DX7_R1 + 2, 99, 0, DK_NUM, 0}, {"r4", DX7_R1 + 3, 99, 0, DK_NUM, 0}}},
    {"eg level", {{"l1", DX7_L1, 99, 0, DK_NUM, 0}, {"l2", DX7_L1 + 1, 99, 0, DK_NUM, 0},
                  {"l3", DX7_L1 + 2, 99, 0, DK_NUM, 0}, {"l4", DX7_L1 + 3, 99, 0, DK_NUM, 0}}},
    {"key scale", {{"break", DX7_BP, 99, 0, DK_BREAK, 0}, {"l depth", DX7_LD, 99, 0, DK_NUM, 0},
                   {"r depth", DX7_RD, 99, 0, DK_NUM, 0}, {"curves", DX7_LC, 15, 0, DK_CURVE, 0}}},
};
static const dx7_page_t DX7_PIT_PAGES[] = {
    {"pitch rate", {{"r1", DX7_PR, 99, 0, DK_NUM, 0}, {"r2", DX7_PR + 1, 99, 0, DK_NUM, 0},
                    {"r3", DX7_PR + 2, 99, 0, DK_NUM, 0}, {"r4", DX7_PR + 3, 99, 0, DK_NUM, 0}}},
    {"pitch level", {{"l1", DX7_PL, 99, -50, DK_NUM, 0}, {"l2", DX7_PL + 1, 99, -50, DK_NUM, 0},
                     {"l3", DX7_PL + 2, 99, -50, DK_NUM, 0}, {"l4", DX7_PL + 3, 99, -50, DK_NUM, 0}}},
};
static const dx7_page_t DX7_GLO_PAGES[] = {
    {"algo", {{"alg", DX7_ALG, 31, 1, DK_NUM, 0}, {"fdbk", DX7_FB, 7, 0, DK_NUM, 0},
              {"osc sync", DX7_OKS, 1, 0, DK_ENUM, DX7N_ONOFF}, {"transp", DX7_TRNSP, 48, -24, DK_NUM, 0}}},
    {"lfo", {{"speed", DX7_LFS, 99, 0, DK_NUM, 0}, {"delay", DX7_LFD, 99, 0, DK_NUM, 0},
             {"pmd", DX7_LPMD, 99, 0, DK_NUM, 0}, {"amd", DX7_LAMD, 99, 0, DK_NUM, 0}}},
    {"lfo 2", {{"wave", DX7_LFW, 5, 0, DK_ENUM, DX7N_WAVE}, {"key sync", DX7_LFKS, 1, 0, DK_ENUM, DX7N_ONOFF},
               {"pms", DX7_LPMS, 7, 0, DK_NUM, 0}, DX7_NOP}},
    {"op 7/8", {{"route", DX7_EXT, DX7_EXT_COUNT - 1, 0, DK_ENUM, DX7N_EXT}, {"target", DX7_EXTT, 5, 1, DK_NUM, 0},
                {"fb 8", DX7_FB8, 7, 0, DK_NUM, 0}, DX7_NOP}},
};
#define DX7_NPG(a) (sizeof(a) / sizeof(a[0]))

static struct {
    uint8_t target;              /* 0..7 = OP1..OP8, DX7T_PIT, DX7T_GLO */
    uint8_t sub[3];              /* the page of each kind: operator, PIT, GLO */
    uint8_t shown;               /* the screen holds the DX7 page */
    uint8_t opened;              /* this ENV press opened the page: its tap does not turn it too */
    uint8_t oct_pend;            /* OCT+ went down with ENV held, no key since: let go, it turns the page */
    uint32_t oct_prev;           /* OCT- / OCT+ down last frame (ENV held) */
    uint32_t sig[3];             /* drawn-state caches: header, graph, info */
    uint32_t dial;
} dx7ui;

static int dx7_sel(void) { return !is_drum(TSEL) && ENGINES[TSEL->eng_req % NENGINES] == &ENG_DX7; }
static int on_dx7_page(void) { return !ui.home && cur_page()->scope == SC_DX7; }
static uint32_t dx7_kind(void) { return dx7ui.target < 8u ? 0u : dx7ui.target == DX7T_PIT ? 1u : 2u; }
static const dx7_page_t *dx7_cur_page(uint32_t *n_out)
{
    static const dx7_page_t *const PG[3] = {DX7_OP_PAGES, DX7_PIT_PAGES, DX7_GLO_PAGES};
    static const uint8_t NPG[3] = {DX7_NPG(DX7_OP_PAGES), DX7_NPG(DX7_PIT_PAGES), DX7_NPG(DX7_GLO_PAGES)};
    uint32_t k = dx7_kind();
    if (n_out)
        *n_out = NPG[k];
    return &PG[k][dx7ui.sub[k] % NPG[k]];
}
/* the voice data index of OPn (n = 0..7: OP1..OP8): the DX7 stores OP6 first */
static uint32_t dx7_op_base(uint32_t n) { return (uint32_t)DX7_OP(n < 6u ? 5u - n : n); }
/* where parameter d lives in the voice (operator pages: in the operator picked) */
static uint32_t dx7_off(const dx7_pd_t *d)
{
    return dx7_kind() == 0u ? dx7_op_base(dx7ui.target) + d->off : d->off;
}

static int32_t dx7_value(const dx7_part_t *dp, const dx7_pd_t *d)
{
    uint32_t o = dx7_off(d);
    if (d->kind == DK_NONE)
        return 0;
    if (d->kind == DK_CURVE)                             /* left and right curve: one value 0..15 */
        return dp->v[o] * 4 + dp->v[o + 1u];
    return dp->v[o];
}

static void dx7_format(const dx7_part_t *dp, const dx7_pd_t *d, int32_t v, char *b)
{
    b[0] = 0;
    switch (d->kind) {
    case DK_ENUM:
        str_cpy(b, d->names[clamp(v, 0, d->max)], 8);
        break;
    case DK_CURVE:
        str_cpy(b, DX7N_CURVE[(v >> 2) & 3], 4);
        b[2] = ' ';
        str_cpy(b + 3, DX7N_CURVE[v & 3], 4);
        break;
    case DK_BREAK:                                       /* 0 = A-1 .. 99 = C8 (Yamaha), SLOOP's octaves */
        note_name(b, (uint32_t)(v + 21));
        te_lower(b, b, 8);
        break;
    case DK_COARSE:                                      /* ratio: 0 is a half; fixed: 1 Hz .. 1 kHz */
        if (dp->v[dx7_off(d) - DX7_COARSE + DX7_MODE])
            str_cpy(b, DX7N_FIXED[v & 3], 8);
        else if (v == 0)
            str_cpy(b, "0.5", 4);
        else
            fmt_int(b, v);
        break;
    case DK_NONE:
        break;
    default:
        fmt_int(b, v + d->show);
        break;
    }
}

/* KNOB k turned s detents (the DX7 page, or ENV held) */
static void dx7_knob(uint32_t k, int32_t s)
{
    dx7_part_t *dp;
    const dx7_pd_t *d;
    uint32_t o;
    int32_t v;
    if (!dx7_sel())
        return;
    dp = dx7_of(TSEL);
    d = &dx7_cur_page(0)->p[k & 3u];
    if (d->kind == DK_NONE || !dp->loaded1)
        return;                                          /* (an empty column; no voice loaded yet) */
    ui.hot_col = (uint8_t)k;
    ui.hot_t = 40;
    v = clamp(dx7_value(dp, d) + accel(EN_K1 + k, s, d->max), 0, d->max);
    o = dx7_off(d);
    fm1_irq_off();                                       /* (the ISR rebuilds what notes play from it) */
    if (d->kind == DK_CURVE) {
        dp->v[o] = (uint8_t)(v >> 2);
        dp->v[o + 1u] = (uint8_t)(v & 3);
    } else {
        dp->v[o] = (uint8_t)v;
    }
    dp->edited = 1;
    dp->gen++;
    fm1_irq_on();
}

static void dx7_next_page(int32_t d)
{
    uint32_t n, k = dx7_kind();
    dx7_cur_page(&n);
    dx7ui.sub[k] = (uint8_t)((dx7ui.sub[k] + (d > 0 ? 1u : n - 1u)) % n);
}

static void dx7_open(void)
{
    studio_open(SC_DX7);
    ui.hot_t = 0;
}

/* a black key with ENV held (seq.c lk_q): what to edit, or the voice mode */
static void dx7_layer_key(uint32_t k)
{
    static const int8_t BK[27] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, DX7T_PIT, -1, DX7T_GLO,
                                  -1, -1, DX7T_MONO, -1, DX7T_POLY, -1, -1, DX7T_OP7, -1};
    int32_t t = k < 27u ? BK[k] : -1;
    if (t < 0 || !dx7_sel())
        return;
    dx7ui.oct_pend = 0;                                  /* (OCT+ held meanwhile: a modifier, not a page turn) */
    if (t == DX7T_MONO || t == DX7T_POLY) {
        TSEL->p[P_VOICE] = t == DX7T_MONO ? V_MONO : V_POLY;
        ui_message(t == DX7T_MONO ? "MONO" : "POLY");
        return;
    }
    if (t == DX7T_OP7 && (fm1_in.buttons & 1u << panel.btn[B_OCTUP]))
        t = DX7T_OP8;
    dx7ui.target = (uint8_t)t;
    if (!on_dx7_page())
        dx7_open();
}

/* OCT- / OCT+ with ENV held: the page back / on (OCT+ on its release, if no key used it as OP8's modifier).
 * Returns 1 when either moved (the layer was used: ENV let go is no tap) */
static int dx7_layer_oct(void)
{
    uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
    uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~dx7ui.oct_prev, rel = dx7ui.oct_prev & ~b;
    dx7ui.oct_prev = b;
    if (press & ob)
        dx7_next_page(-1);
    if (press & pb)
        dx7ui.oct_pend = 1;
    if ((rel & pb) && dx7ui.oct_pend) {
        dx7ui.oct_pend = 0;
        dx7_next_page(1);
    }
    return (press | rel) != 0u;
}

/* once a frame after the layers: ENV held shows the DX7 page; let go, OCT starts afresh next time */
static void dx7_follow_layer(void)
{
    if (ui.layer == LY_OPS) {
        if (!on_dx7_page()) {
            dx7_open();
            dx7ui.opened = 1;
        }
        return;
    }
    dx7ui.opened = 0;
    dx7ui.oct_pend = 0;
    dx7ui.oct_prev = fm1_in.buttons & (1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP]);
}

static void dx7_tap(void)                                /* ENV tapped on a DX7 track */
{
    if (dx7ui.opened)
        dx7ui.opened = 0;                                /* (the press showed the page: that was the tap) */
    else if (on_dx7_page())
        dx7_next_page(1);
    else
        dx7_open();
}

/* the keys' lights with ENV held: the black key of what is edited (OP8: the OP7 key blinks), MONO / POLY */
static uint32_t dx7_keys_lit(void)
{
    static const uint8_t KEY[10] = {1, 3, 5, 8, 10, 13, 25, 25, 15, 17};
    uint32_t m = 1u << KEY[dx7ui.target % 10u];
    if (dx7ui.target == DX7T_OP8 && ((fm1_ms / 250u) & 1u))
        m = 0;
    if (TSEL->p[P_VOICE] == V_MONO || TSEL->p[P_VOICE] == V_LEGATO)
        m |= 1u << DX7T_MONO;
    else if (TSEL->p[P_VOICE] == V_POLY)
        m |= 1u << DX7T_POLY;
    return m;
}

/* --------------------------------------------------------------- the algorithm diagram --- */
/* the modulators of each operator (bit m: OP m+1 modulates it), the carriers, the feedback loops, the
 * operators in use; index 0..7 = OP1..OP8. As fm_core renders them: OP6 .. OP1, through two buses */
static void dx7_graph(const uint8_t *v, uint8_t *mods, uint8_t *car, uint8_t *fb, uint8_t *used)
{
    const uint8_t *fl = DX7_ALGS[v[DX7_ALG] & 31u];
    uint32_t i, b1 = 0, b2 = 0, ext = v[DX7_EXT], t = v[DX7_EXTT] % 6u;
    *car = *fb = 0;
    for (i = 0; i < 8u; i++)
        mods[i] = 0;
    for (i = 0; i < 6u; i++) {
        uint32_t f = fl[i], op = 5u - i, inb = (f >> 4) & 3u, outb = f & 3u, bit = 1u << op;
        mods[op] |= (uint8_t)(inb == 1u ? b1 : inb == 2u ? b2 : 0u);
        if (outb == 0u)
            *car |= (uint8_t)bit;
        else if (outb == 1u)
            b1 = (f & DX7_ADD) ? b1 | bit : bit;
        else
            b2 = (f & DX7_ADD) ? b2 | bit : bit;
        if ((f & 0xC0u) == 0xC0u)                        /* (msfa: only an operator's own loop sounds) */
            *fb |= (uint8_t)bit;
    }
    *used = 0x3F;
    if (ext == DX7_EXT_OFF || ext >= DX7_EXT_COUNT)
        return;
    *used = 0xFF;
    if (ext == DX7_EXT_STACK || ext == DX7_EXT_STACKMOD)
        mods[6] |= 1u << 7;
    if (ext == DX7_EXT_STACK)
        *car |= 1u << 6;
    if (ext == DX7_EXT_PAIR)
        *car |= 3u << 6;
    if (ext == DX7_EXT_STACKMOD)
        mods[t] |= 1u << 6;
    if (ext == DX7_EXT_PAIRMOD)
        mods[t] |= 3u << 6;
    if (v[DX7_FB8])
        *fb |= 1u << 7;
}

/* a tree per carrier, its modulators above it; a modulator of several operators sits over the first */
typedef struct { int16_t x2, row, w; } dx7_node_t;      /* x2: centre in half columns */
static uint32_t dx7_parent(const uint8_t *mods, uint32_t m)  /* the first operator m modulates, 8 = none */
{
    uint32_t op;
    for (op = 0; op < 8u; op++)
        if (op != m && ((mods[op] >> m) & 1u))
            return op;
    return 8;
}
static int16_t dx7_width(const uint8_t *mods, dx7_node_t *nd, uint32_t op, uint32_t depth)
{
    uint32_t m;
    int16_t w = 0;
    for (m = 0; m < 8u && depth < 8u; m++)
        if (m != op && dx7_parent(mods, m) == op)
            w = (int16_t)(w + dx7_width(mods, nd, m, depth + 1u));
    nd[op].w = w ? w : 1;
    return nd[op].w;
}
static int16_t dx7_place(const uint8_t *mods, dx7_node_t *nd, uint32_t op, int16_t x0, int16_t row, uint32_t depth)
{
    uint32_t m;
    int16_t x = x0, top = row;
    nd[op].x2 = (int16_t)(2 * x0 + nd[op].w);
    nd[op].row = row;
    for (m = 0; m < 8u && depth < 8u; m++)
        if (m != op && dx7_parent(mods, m) == op) {
            int16_t t = dx7_place(mods, nd, m, x, (int16_t)(row + 1), depth + 1u);
            top = t > top ? t : top;
            x = (int16_t)(x + nd[m].w);
        }
    return top;
}

static void dx7_feedback_loop(int32_t x, int32_t y, uint16_t c)   /* right of the box, back into its top */
{
    cv_line(x + 11, y, x + 15, y, c);
    cv_line(x + 15, y, x + 15, y - 10, c);
    cv_line(x + 15, y - 10, x, y - 10, c);
    cv_line(x, y - 10, x, y - 7, c);
}

#define DX7_GY 40                                        /* the diagram strip: y 40 .. 141 */
#define DX7_GH 102
static void dx7_draw_graph(const uint8_t *v)
{
    uint8_t mods[8], car, fb, used;
    dx7_node_t nd[8];
    int16_t cols = 0, rows = 0, cw, rh, px[8], py[8];
    uint32_t op, m, sig;
    char b[8];
    dx7_graph(v, mods, &car, &fb, &used);
    sig = (uint32_t)v[DX7_ALG] * 977u + v[DX7_EXT] * 31u + v[DX7_EXTT] * 7u + dx7ui.target * 131u +
          (uint32_t)(v[DX7_FB8] != 0) + (uint32_t)(v[DX7_FB] != 0) * 3u;
    for (op = 0; op < 8u; op++)                          /* (an operator at level 0 is drawn dim) */
        sig = sig * 31u + (v[dx7_op_base(op) + DX7_OL] != 0);
    if (!ui.force && sig == dx7ui.sig[1])
        return;
    dx7ui.sig[1] = sig;
    for (op = 0; op < 8u; op++)
        nd[op].w = nd[op].x2 = nd[op].row = -1;
    for (op = 0; op < 8u; op++)                          /* the carriers, OP1 first, then their trees */
        if ((used >> op) & 1u && (car >> op) & 1u) {
            int16_t top;
            dx7_width(mods, nd, op, 0);
            top = dx7_place(mods, nd, op, cols, 0, 0);
            cols = (int16_t)(cols + nd[op].w);
            rows = top + 1 > rows ? (int16_t)(top + 1) : rows;
        }
    cw = (int16_t)(cols ? (236 / cols > 40 ? 40 : 236 / cols) : 40);
    rh = (int16_t)(rows ? ((DX7_GH - 14) / rows > 24 ? 24 : (DX7_GH - 14) / rows) : 24);
    cv_begin(240, DX7_GH, C_BLACK);
    for (op = 0; op < 8u; op++) {
        px[op] = (int16_t)((240 - cols * cw) / 2 + nd[op].x2 * cw / 2);
        py[op] = (int16_t)(DX7_GH - 14 - nd[op].row * rh - rh / 2);
    }
    for (op = 0; op < 8u; op++) {                        /* the wires, under the boxes */
        if (nd[op].row < 0)
            continue;
        for (m = 0; m < 8u; m++)
            if ((mods[op] >> m) & 1u && nd[m].row >= 0)
                cv_line(px[m], py[m] + 7, px[op], py[op] - 7, m >= 6u ? TE_MID[3] : TE_G3);
        if ((car >> op) & 1u)
            cv_line(px[op], py[op] + 7, px[op], DX7_GH - 6, TE_G3);
        if ((fb >> op) & 1u)                             /* lit while its feedback is above 0 */
            dx7_feedback_loop(px[op], py[op], (op == 7u ? v[DX7_FB8] : v[DX7_FB]) ? TE_COL[3] : TE_G2);
    }
    if (cols)                                            /* the output */
        cv_rect((240 - cols * cw) / 2 + cw / 4, DX7_GH - 6, cols * cw - cw / 2, 1, TE_G3);
    for (op = 0; op < 8u; op++) {
        char n[2] = {(char)('1' + op), 0};
        int sel = dx7ui.target == op, mute = !v[dx7_op_base(op) + DX7_OL];
        uint16_t fg = sel ? C_BLACK : (car >> op) & 1u ? TE_COL[2] : op >= 6u ? TE_COL[3] : TE_G4;
        if (nd[op].row < 0)
            continue;
        cv_rect(px[op] - 11, py[op] - 7, 22, 14, sel ? C_WHITE : mute ? TE_G1 : TE_G2);
        cv_text(px[op] - 4, py[op] - 8, &FONT_S, n, mute && !sel ? TE_G3 : fg);
    }
    if (used == 0x3F)                                    /* OP7 / OP8 not routed (GLO > OP 7/8: route) */
        cv_text(204, 2, &FONT_S, "7 8", dx7ui.target == DX7T_OP7 || dx7ui.target == DX7T_OP8 ? C_WHITE : TE_G2);
    str_cpy(b, "alg ", sizeof b);
    fmt_int(b + 4, v[DX7_ALG] + 1);
    cv_text(4, 2, &FONT_S, b, TE_G3);
    cv_blit(0, DX7_GY);
}

/* the band under the diagram: the voice name, the page, the black keys' map (grouped as on the keyboard:
 * OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY | OP7) */
static void dx7_draw_info(const dx7_part_t *dp)
{
    static const char *const KEYS[11] = {"1", "2", "3", "4", "5", "6", "pi", "gl", "mo", "po", "7"};
    static const uint8_t KT[11] = {0, 1, 2, 3, 4, 5, DX7T_PIT, DX7T_GLO, DX7T_MONO, DX7T_POLY, DX7T_OP7};
    static const uint8_t GROUP[11] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4};
    char nm[16], pg[24], *p;
    uint32_t n, i, sig, voice = (uint32_t)TSEL->p[P_VOICE];
    const dx7_page_t *page = dx7_cur_page(&n);
    dx7_name_of(nm, dp->v);
    te_lower(nm, nm, sizeof nm);
    if (dp->edited)
        str_cpy(nm + str_len(nm), "*", 2);
    str_cpy(pg, page->name, sizeof pg);                 /* "eg rate 3/5" */
    p = pg + str_len(pg);
    p[0] = ' ';
    p[1] = (char)('1' + dx7ui.sub[dx7_kind()] % n);
    p[2] = '/';
    p[3] = (char)('0' + n);
    p[4] = 0;
    sig = studio_hash(studio_hash(dx7ui.target * 7u + voice * 3u + (ui.layer == LY_OPS) * 1001u, nm), pg);
    if (!ui.force && sig == dx7ui.sig[2])
        return;
    dx7ui.sig[2] = sig;
    cv_begin(240, 42, C_BLACK);
    cv_text(4, 2, &FONT_S, nm, C_WHITE);
    cv_text(236 - text_w(&FONT_S, pg), 2, &FONT_S, pg, TE_G4);
    for (i = 0; i < 11u; i++) {
        int on = KT[i] == dx7ui.target || (KT[i] == DX7T_OP7 && dx7ui.target == DX7T_OP8) ||
                 (KT[i] == DX7T_MONO && (voice == V_MONO || voice == V_LEGATO)) || (KT[i] == DX7T_POLY && voice == V_POLY);
        int32_t x = 6 + (int32_t)i * 19 + (int32_t)GROUP[i] * 5;
        const char *lab = KT[i] == DX7T_OP7 && dx7ui.target == DX7T_OP8 ? "8" : KEYS[i];
        cv_rect(x, 22, 17, 18, on ? C_WHITE : ui.layer == LY_OPS ? TE_G2 : TE_G1);
        te_text_c(x + 9, 23, lab, on ? C_BLACK : TE_G4);
    }
    cv_blit(0, DX7_GY + DX7_GH);
}

static void dx7_screen_draw(void)
{
    static char v[4][10];
    const char *lab[4], *val[4] = {v[0], v[1], v[2], v[3]};
    char title[12];
    int32_t ratio[4];
    uint32_t k, sig = 0;
    const dx7_page_t *p;
    dx7_part_t *dp;
    if (!dx7_sel()) {                                    /* the track or its engine changed: its ENV pages */
        ui.page = (uint8_t)page_first(FAM_ENV);          /* (ui_draw clears the screen: dx7ui.shown) */
        page_entered();
        return;
    }
    dp = dx7_of(TSEL);
    if (!dx7ui.shown) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        dx7ui.shown = 1;
    }
    str_cpy(title, "dx7 ", sizeof title);
    if (dx7ui.target < 8u) {
        str_cpy(title + 4, "op", 4);
        title[6] = (char)('1' + dx7ui.target);
        title[7] = 0;
    } else {
        str_cpy(title + 4, dx7ui.target == DX7T_PIT ? "pit" : "glo", 8);   /* (as the keys: clear of "loop") */
    }
    te_header(title, ENG_DX7.color, &dx7ui.sig[0]);
    dx7_draw_graph(dp->v);
    dx7_draw_info(dp);
    p = dx7_cur_page(0);
    for (k = 0; k < 4u; k++) {
        const dx7_pd_t *d = &p->p[k];
        int32_t x = dx7_value(dp, d);
        lab[k] = d->lab;
        dx7_format(dp, d, x, v[k]);
        ratio[k] = d->kind == DK_NONE ? -1 : d->max ? x * 1000 / d->max : 0;
        sig = sig * 31u + (uint32_t)x;
    }
    te_dials(DX7_GY + DX7_GH + 42, lab, val, ratio, sig + dx7ui.target * 7u, &dx7ui.dial);
}
