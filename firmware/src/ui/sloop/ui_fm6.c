/* SPDX-License-Identifier: GPL-3.0-only */
/* FM6 operator editor, on the FM-1's printed black-key labels (was the DX7 engine's; the FM6 engine is
 * Kerem Kilic's, Melodee: eng_fm6.c). On an FM6 track ENV is a layer (LY_OPS, seq.c): hold ENV and press
 * a black key to pick what to edit; the white keys keep playing notes (audition while you edit):
 *     OP1 .. OP6    that operator                  PIT          the pitch envelope
 *     GLO           algorithm, LFO, portamento
 *     MONO / POLY   the voice mode
 * The FM6 page stays up when ENV is let go (the black keys play notes again); KNOB 1..4 edit the four
 * values shown. Pages, as on every page family: ENV tapped turns to the next one; with ENV held, OCT-
 * goes back one, OCT+ goes on one. An operator has FREQ, LEVEL, EG RATE, EG LEVEL, KEY SCALE, CURVES; PIT
 * has RATE and LEVEL; GLO has ALGO, LFO, LFO 2 and PORTA.
 * The screen: the algorithm, the operator picked in white, carriers in the engine's colour, switched-off
 * operators dim; the voice name and the black keys' map, as on the keyboard; the four values. (VIEW ALL's rows
 * and the algorithm full screen, FM6_ALL and FM6_ALGO, and the STORE / SEND / INIT page were dropped 2026-10-18.)
 * Edits go into the part's voice (eng_fm6.c fm6_ed, as Melodee's FM6 pages and a DX7 parameter change do);
 * sounding notes follow them. Projects keep them; VOICE (EDIT 1 KNOB 1) loads a voice over them. */
#ifndef FELUCCA_FM6_KEYS
#define FELUCCA_FM6_KEYS 1       /* 0: no operator editor (to measure its flash; edits then come by SysEx only) */
#endif
#if FELUCCA_FM6_KEYS
enum { FMT_PIT = 6, FMT_GLO = 7, FMT_MONO = 20, FMT_POLY = 22 };
enum { FK_NUM, FK_ENUM, FK_BREAK, FK_COARSE, FK_ON, FK_NONE };
typedef struct {
    const char *lab;
    uint8_t off;                 /* in the operator (operator pages), else in the voice (fm6_ed) */
    uint8_t max;
    int8_t show;                 /* value shown = value + show */
    uint8_t kind;
    const char *const *names;    /* FK_ENUM */
} fm6k_pd_t;
typedef struct {
    const char *name;
    fm6k_pd_t p[4];
} fm6k_page_t;

static const char *const FMKN_MODE[] = {"ratio", "fixed"};
static const char *const FMKN_ONOFF[] = {"off", "on"};
static const char *const FMKN_WAVE[] = {"tri", "saw-", "saw+", "sqr", "sin", "s&h"};
static const char *const FMKN_CURVE[] = {"-lin", "-exp", "+exp", "+lin"};
static const char *const FMKN_FIXED[] = {"1hz", "10hz", "100hz", "1khz"};
static const char *const FMKN_PMODE[] = {"pedal", "on"};
#define FMK_NOP {"", 0, 0, 0, FK_NONE, 0}
static const fm6k_page_t FMK_OP_PAGES[] = {
    {"freq", {{"mode", FO_MODE, 1, 0, FK_ENUM, FMKN_MODE}, {"coars", FO_CRS, 31, 0, FK_COARSE, 0},
              {"fine", FO_FINE, 99, 0, FK_NUM, 0}, {"dtune", FO_DET, 14, -7, FK_NUM, 0}}},
    {"level", {{"level", FO_OL, 99, 0, FK_NUM, 0}, {"vel", FO_KVS, 7, 0, FK_NUM, 0},
               {"ams", FO_AMS, 3, 0, FK_NUM, 0}, {"on", 0, 1, 0, FK_ON, FMKN_ONOFF}}},
    {"eg rate", {{"r1", FO_R1, 99, 0, FK_NUM, 0}, {"r2", FO_R2, 99, 0, FK_NUM, 0},
                 {"r3", FO_R3, 99, 0, FK_NUM, 0}, {"r4", FO_R4, 99, 0, FK_NUM, 0}}},
    {"eg level", {{"l1", FO_L1, 99, 0, FK_NUM, 0}, {"l2", FO_L2, 99, 0, FK_NUM, 0},
                  {"l3", FO_L3, 99, 0, FK_NUM, 0}, {"l4", FO_L4, 99, 0, FK_NUM, 0}}},
    {"key scale", {{"break", FO_BP, 99, 0, FK_BREAK, 0}, {"l dep", FO_LD, 99, 0, FK_NUM, 0},
                   {"r dep", FO_RD, 99, 0, FK_NUM, 0}, {"rt sc", FO_RS, 7, 0, FK_NUM, 0}}},
    {"curves", {{"l crv", FO_LC, 3, 0, FK_ENUM, FMKN_CURVE}, {"r crv", FO_RC, 3, 0, FK_ENUM, FMKN_CURVE},
                FMK_NOP, FMK_NOP}},
};
static const fm6k_page_t FMK_PIT_PAGES[] = {
    {"pitch rate", {{"r1", FV_PR, 99, 0, FK_NUM, 0}, {"r2", FV_PR + 1, 99, 0, FK_NUM, 0},
                    {"r3", FV_PR + 2, 99, 0, FK_NUM, 0}, {"r4", FV_PR + 3, 99, 0, FK_NUM, 0}}},
    {"pitch level", {{"l1", FV_PL, 99, -50, FK_NUM, 0}, {"l2", FV_PL + 1, 99, -50, FK_NUM, 0},
                     {"l3", FV_PL + 2, 99, -50, FK_NUM, 0}, {"l4", FV_PL + 3, 99, -50, FK_NUM, 0}}},
};
static const fm6k_page_t FMK_GLO_PAGES[] = {
    {"algo", {{"alg", FV_ALG, 31, 1, FK_NUM, 0}, {"fdbk", FV_FB, 7, 0, FK_NUM, 0},
              {"osync", FV_OKS, 1, 0, FK_ENUM, FMKN_ONOFF}, {"trnsp", FV_TRNSP, 48, -24, FK_NUM, 0}}},
    {"lfo", {{"speed", FV_LFS, 99, 0, FK_NUM, 0}, {"delay", FV_LFD, 99, 0, FK_NUM, 0},
             {"pmd", FV_LPMD, 99, 0, FK_NUM, 0}, {"amd", FV_LAMD, 99, 0, FK_NUM, 0}}},
    {"lfo 2", {{"wave", FV_LFW, 5, 0, FK_ENUM, FMKN_WAVE}, {"ksync", FV_LFKS, 1, 0, FK_ENUM, FMKN_ONOFF},
               {"pms", FV_LPMS, 7, 0, FK_NUM, 0}, FMK_NOP}},
    {"porta", {{"porta", FN_PMODE, 1, 0, FK_ENUM, FMKN_PMODE}, {"time", FN_PTIME, 127, 0, FK_NUM, 0},
               {"gliss", FN_GLISS, 1, 0, FK_ENUM, FMKN_ONOFF}, {"dxvel", FN_VNORM, 1, 0, FK_ENUM, FMKN_ONOFF}}},
};
#define FMK_NPG(a) (sizeof(a) / sizeof(a[0]))

static struct {
    uint8_t target;              /* 0..5 = OP1..OP6, FMT_PIT, FMT_GLO */
    uint8_t sub[3];              /* the page of each kind: operator, PIT, GLO */
    uint8_t shown;               /* the screen holds the FM6 page */
    uint8_t opened;              /* this ENV press opened the page: its tap does not turn it too */
    uint32_t oct_prev;           /* OCT- / OCT+ down last frame (ENV held) */
    uint32_t sig[3];             /* drawn-state caches: header, graph, info */
    uint32_t dial;
} fm6ui;

static int fm6k_sel(void) { return !is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], FM6); }
static int on_fm6k_page(void) { return cur_page()->scope == SC_FM6K; }
static uint32_t fm6k_kind(void) { return fm6ui.target < 6u ? 0u : fm6ui.target == FMT_PIT ? 1u : 2u; }
static int16_t *fm6k_ed(void) { return fm6_ed[song.sel % NPART]; }
static const fm6k_page_t *fm6k_group(uint32_t *n_out)   /* the pages of what is edited, *n_out of them */
{
    static const fm6k_page_t *const PG[3] = {FMK_OP_PAGES, FMK_PIT_PAGES, FMK_GLO_PAGES};
    static const uint8_t NPG[3] = {FMK_NPG(FMK_OP_PAGES), FMK_NPG(FMK_PIT_PAGES), FMK_NPG(FMK_GLO_PAGES)};
    uint32_t k = fm6k_kind();
    *n_out = NPG[k];
    return PG[k];
}
static const fm6k_page_t *fm6k_cur_page(uint32_t *n_out)
{
    uint32_t n;
    const fm6k_page_t *g = fm6k_group(&n);
    if (n_out)
        *n_out = n;
    return &g[fm6ui.sub[fm6k_kind()] % n];
}

/* where parameter d lives in the voice buffer (operator pages: in the operator picked) */
static uint32_t fm6k_off(const fm6k_pd_t *d)
{
    if (d->kind == FK_ON)
        return FV_ON + fm6ui.target;                    /* (FV_ON + n - 1: OP n's switch) */
    return fm6k_kind() == 0u ? FM6_OPB(fm6ui.target + 1u) + d->off : d->off;
}

static int32_t fm6k_value(const fm6k_pd_t *d)
{
    if (d->kind == FK_NONE)
        return 0;
    return fm6k_ed()[fm6k_off(d)];
}

static void fm6k_format(const fm6k_pd_t *d, int32_t v, char *b)
{
    b[0] = 0;
    switch (d->kind) {
    case FK_ENUM:
    case FK_ON:
        str_cpy(b, d->names[clamp(v, 0, d->max)], 8);
        break;
    case FK_BREAK:                                       /* 0 = A-1 .. 99 = C8 (Yamaha), SLOOP's octaves */
        note_name(b, (uint32_t)(v + 21));
        te_lower(b, b, 8);
        break;
    case FK_COARSE:                                      /* ratio: 0 is a half; fixed: 1 Hz .. 1 kHz */
        if (fm6k_ed()[fm6k_off(d) - FO_CRS + FO_MODE])
            str_cpy(b, FMKN_FIXED[v & 3], 8);
        else if (v == 0)
            str_cpy(b, "0.5", 4);
        else
            fmt_int(b, v);
        break;
    case FK_NONE:
        break;
    default:
        fmt_int(b, v + d->show);
        break;
    }
}

/* KNOB k turned s detents (the FM6 page, or ENV held) */
static void fm6k_knob(uint32_t k, int32_t s)
{
    const fm6k_pd_t *d;
    uint32_t o;
    if (!fm6k_sel())
        return;
    d = &fm6k_cur_page(0)->p[k & 3u];
    if (d->kind == FK_NONE)
        return;                                          /* (an empty column) */
    ui.hot_col = (uint8_t)k;
    ui.hot_t = PH_HOT;
    PH_SET(PH_FM6, fm6k_cur_page(0)->name, d->lab);     /* its help line (param_help.c) */
    o = fm6k_off(d);
    fm6_set(fm6k_ed(), o, clamp(fm6k_value(d) + accel(EN_K1 + k, s, d->kind == FK_ENUM ? 0 : d->max), 0, d->max));
}

static void fm6k_page_entered(void)
{
    ui.arm = 0;
}

static void fm6k_next_page(int32_t d)
{
    uint32_t n, k = fm6k_kind();
    fm6k_cur_page(&n);
    fm6ui.sub[k] = (uint8_t)((fm6ui.sub[k] + (d > 0 ? 1u : n - 1u)) % n);
    fm6k_page_entered();
}

static void fm6k_open(void)
{
    studio_open(SC_FM6K);
    ui.hot_t = 0;
    fm6k_page_entered();
}

/* a black key with ENV held (seq.c lk_q): what to edit, or the voice mode */
static void fm6k_layer_key(uint32_t k)
{
    static const int8_t BK[27] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, FMT_PIT, -1, FMT_GLO,
                                  -1, -1, FMT_MONO, -1, FMT_POLY, -1, -1, -1, -1};
    int32_t t = k < 27u ? BK[k] : -1;
    if (t < 0 || !fm6k_sel())
        return;
    if (t == FMT_MONO || t == FMT_POLY) {
        TSEL->p[P_VOICE] = t == FMT_MONO ? V_MONO : V_POLY;
        ui_message(t == FMT_MONO ? "MONO" : "POLY");
        return;
    }
    fm6ui.target = (uint8_t)t;
    if (!on_fm6k_page())
        fm6k_open();
    else
        fm6k_page_entered();
}

/* OCT- / OCT+ with ENV held: the page back / on. Returns 1 when either moved (the layer was used: ENV
 * let go is no tap) */
static int fm6k_layer_oct(void)
{
    uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
    uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~fm6ui.oct_prev;
    fm6ui.oct_prev = b;
    if (press & ob)
        fm6k_next_page(-1);
    if (press & pb)
        fm6k_next_page(1);
    return press != 0u;
}

/* once a frame after the layers: ENV held shows the FM6 page; let go, OCT starts afresh next time */
static void fm6k_follow_layer(void)
{
    if (ui.layer == LY_OPS) {
        if (!on_fm6k_page()) {
            fm6k_open();
            fm6ui.opened = 1;
        }
        return;
    }
    fm6ui.opened = 0;
    fm6ui.oct_prev = fm1_in.buttons & (1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP]);
}

static void fm6k_tap(void)                               /* ENV tapped on an FM6 track */
{
    if (fm6ui.opened)
        fm6ui.opened = 0;                                /* (the press showed the page: that was the tap) */
    else if (on_fm6k_page())
        fm6k_next_page(1);
    else
        fm6k_open();
}

/* the keys' lights with ENV held: the black key of what is edited, MONO / POLY */
static uint32_t fm6k_keys_lit(void)
{
    static const uint8_t KEY[8] = {1, 3, 5, 8, 10, 13, 15, 17};
    uint32_t m = 1u << KEY[fm6ui.target % 8u];
    if (TSEL->p[P_VOICE] == V_MONO || TSEL->p[P_VOICE] == V_LEGATO)
        m |= 1u << FMT_MONO;
    else if (TSEL->p[P_VOICE] == V_POLY)
        m |= 1u << FMT_POLY;
    return m;
}

/* --------------------------------------------------------------- the algorithm diagram --- */
/* the modulators of each operator (bit m: OP m+1 modulates it), the carriers, the feedback loop; index
 * 0..5 = OP1..OP6. As eng_fm6.c renders them (FM6_ALG): OP6 .. OP1, through two buses */
static void fm6k_graph(const int16_t *v, uint8_t *mods, uint8_t *car, uint8_t *fb)
{
    const uint8_t *fl = FM6_ALG[v[FV_ALG] & 31];
    uint32_t i, b1 = 0, b2 = 0;
    *car = *fb = 0;
    for (i = 0; i < 6u; i++)
        mods[i] = 0;
    for (i = 0; i < 6u; i++) {
        uint32_t f = fl[i], op = 5u - i, inb = (f >> 4) & 3u, outb = f & 3u, bit = 1u << op;
        mods[op] |= (uint8_t)(inb == 1u ? b1 : inb == 2u ? b2 : 0u);
        if (outb == 0u)
            *car |= (uint8_t)bit;
        else if (outb == 1u)
            b1 = (f & 4u) ? b1 | bit : bit;
        else
            b2 = (f & 4u) ? b2 | bit : bit;
        if ((f & 0xC0u) == 0xC0u)                        /* an operator's own loop (MARK I: algorithms 4 and 6 */
            *fb |= (uint8_t)bit;                         /* loop through OP6 .. OP5 (OP4): drawn on OP6) */
    }
}

/* a tree per carrier, its modulators above it; a modulator of several operators sits over the first */
typedef struct { int16_t x2, row, w; } fm6k_node_t;      /* x2: centre in half columns */
static uint32_t fm6k_parent(const uint8_t *mods, uint32_t m)   /* the first operator m modulates, 6 = none */
{
    uint32_t op;
    for (op = 0; op < 6u; op++)
        if (op != m && ((mods[op] >> m) & 1u))
            return op;
    return 6;
}
static int16_t fm6k_width(const uint8_t *mods, fm6k_node_t *nd, uint32_t op, uint32_t depth)
{
    uint32_t m;
    int16_t w = 0;
    for (m = 0; m < 6u && depth < 6u; m++)
        if (m != op && fm6k_parent(mods, m) == op)
            w = (int16_t)(w + fm6k_width(mods, nd, m, depth + 1u));
    nd[op].w = w ? w : 1;
    return nd[op].w;
}
static int16_t fm6k_place(const uint8_t *mods, fm6k_node_t *nd, uint32_t op, int16_t x0, int16_t row, uint32_t depth)
{
    uint32_t m;
    int16_t x = x0, top = row;
    nd[op].x2 = (int16_t)(2 * x0 + nd[op].w);
    nd[op].row = row;
    for (m = 0; m < 6u && depth < 6u; m++)
        if (m != op && fm6k_parent(mods, m) == op) {
            int16_t t = fm6k_place(mods, nd, m, x, (int16_t)(row + 1), depth + 1u);
            top = t > top ? t : top;
            x = (int16_t)(x + nd[m].w);
        }
    return top;
}

/* the feedback loop: right of the box (half sizes bw x bh), back into its top */
static void fm6k_feedback_loop(int32_t x, int32_t y, int32_t bw, int32_t bh, uint16_t c)
{
    cv_line(x + bw, y, x + bw + 4, y, c);
    cv_line(x + bw + 4, y, x + bw + 4, y - bh - 3, c);
    cv_line(x + bw + 4, y - bh - 3, x, y - bh - 3, c);
    cv_line(x, y - bh - 3, x, y - bh, c);
}

#define FMK_GY 40                                        /* the diagram strip: y 40 .. 141 */
#define FMK_GH 102
/* the algorithm in a strip of h rows at y0 */
static void fm6k_draw_graph(const int16_t *v, uint32_t y0, int32_t h)
{
    uint8_t mods[6], car, fb;
    fm6k_node_t nd[6];
    int16_t cols = 0, rows = 0, cw, rh, px[6], py[6];
    int32_t bw = 11, bh = 7, foot = 14, rmax = 24, cmax = 40;
    uint32_t op, m, sig;
    char b[8];
    fm6k_graph(v, mods, &car, &fb);
    sig = (uint32_t)v[FV_ALG] * 977u + fm6ui.target * 131u + (uint32_t)(v[FV_FB] != 0) * 3u;
    for (op = 0; op < 6u; op++)                          /* (an operator at level 0 or off is drawn dim) */
        sig = sig * 31u + (v[FM6_OPB(op + 1u) + FO_OL] != 0 && v[FV_ON + op]);
    if (!ui.force && sig == fm6ui.sig[1])
        return;
    fm6ui.sig[1] = sig;
    for (op = 0; op < 6u; op++)
        nd[op].w = nd[op].x2 = nd[op].row = -1;
    for (op = 0; op < 6u; op++)                          /* the carriers, OP1 first, then their trees */
        if ((car >> op) & 1u) {
            int16_t top;
            fm6k_width(mods, nd, op, 0);
            top = fm6k_place(mods, nd, op, cols, 0, 0);
            cols = (int16_t)(cols + nd[op].w);
            rows = top + 1 > rows ? (int16_t)(top + 1) : rows;
        }
    cw = (int16_t)(cols ? (236 / cols > cmax ? cmax : 236 / cols) : cmax);
    rh = (int16_t)(rows ? ((h - foot) / rows > rmax ? rmax : (h - foot) / rows) : rmax);
    for (op = 0; op < 6u; op++) {
        px[op] = (int16_t)((240 - cols * cw) / 2 + nd[op].x2 * cw / 2);
        py[op] = (int16_t)(h - foot - nd[op].row * rh - rh / 2);
    }
    {
        cv_begin(240, (uint32_t)h, C_BLACK);
        for (op = 0; op < 6u; op++) {                    /* the wires, under the boxes */
            if (nd[op].row < 0)
                continue;
            for (m = 0; m < 6u; m++)
                if ((mods[op] >> m) & 1u && nd[m].row >= 0)
                    cv_line(px[m], py[m] + bh, px[op], py[op] - bh, TE_G3);
            if ((car >> op) & 1u)
                cv_line(px[op], py[op] + bh, px[op], h - foot + 8, TE_G3);
            if ((fb >> op) & 1u)                         /* lit while its feedback is above 0 */
                fm6k_feedback_loop(px[op], py[op], bw, bh, v[FV_FB] ? TE_G4 : TE_G2);
        }
        if (cols)                                        /* the output */
            cv_rect((240 - cols * cw) / 2 + cw / 4, h - foot + 8, cols * cw - cw / 2, 1, TE_G3);
        for (op = 0; op < 6u; op++) {
            char n[2] = {(char)('1' + op), 0};
            int sel = fm6ui.target == op, mute = !v[FM6_OPB(op + 1u) + FO_OL] || !v[FV_ON + op];
            uint16_t fg = sel ? C_BLACK : (car >> op) & 1u ? COL_ENG_FM6 : TE_G4;
            if (nd[op].row < 0)
                continue;
            if (sel)
                cv_rect(px[op] - bw, py[op] - bh, 2 * bw, 2 * bh, C_WHITE);
            else                                        /* outlined on black, brighter when it sounds */
                cv_frame(px[op] - bw, py[op] - bh, 2 * bw, 2 * bh, mute ? C_LINE : TE_G3);
            cv_text(px[op] - 4, py[op] - 8, &FONT_S, n, mute && !sel ? TE_G3 : fg);
        }
        str_cpy(b, "alg ", sizeof b);
        fmt_int(b + 4, v[FV_ALG] + 1);
        cv_text(4, 2, &FONT_S, b, TE_G3);
        cv_blit(0, y0);
    }
}

/* the band at y (42 rows; keys_only: the map alone, 20): the voice name, the page, the black keys' map (grouped
 * as on the keyboard:
 * OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY) */
static void fm6k_draw_info(const int16_t *v, uint32_t y, int keys_only)
{
    static const char *const KEYS[10] = {"1", "2", "3", "4", "5", "6", "pi", "gl", "mo", "po"};
    static const uint8_t KT[10] = {0, 1, 2, 3, 4, 5, FMT_PIT, FMT_GLO, FMT_MONO, FMT_POLY};
    static const uint8_t GROUP[10] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3};
    char nm[16], pg[24], *p;
    uint32_t n, i, sig, voice = (uint32_t)TSEL->p[P_VOICE];
    const fm6k_page_t *page = fm6k_cur_page(&n);
    fm6_name(nm, v);
    te_lower(nm, nm, sizeof nm);
    str_cpy(pg, page->name, sizeof pg);                 /* "eg rate 3/6" */
    p = pg + str_len(pg);
    p[0] = ' ';
    p[1] = (char)('1' + fm6ui.sub[fm6k_kind()] % n);
    p[2] = '/';
    p[3] = (char)('0' + n);
    p[4] = 0;
    sig = studio_hash(studio_hash(fm6ui.target * 7u + voice * 3u + (ui.layer == LY_OPS) * 1001u + y * 37u + (uint32_t)keys_only, nm), pg);
    if (!ui.force && sig == fm6ui.sig[2])
        return;
    fm6ui.sig[2] = sig;
    cv_begin(240, keys_only ? 20u : 42u, C_BLACK);       /* (keys_only: the map alone, 20 rows) */
    cv_oy = keys_only ? -21 : 0;
    cv_text(4, 2, &FONT_S, nm, C_WHITE);
    cv_text(236 - text_w(&FONT_S, pg), 2, &FONT_S, pg, TE_G4);
    for (i = 0; i < 10u; i++) {
        int on = KT[i] == fm6ui.target || (KT[i] == FMT_MONO && (voice == V_MONO || voice == V_LEGATO)) ||
                 (KT[i] == FMT_POLY && voice == V_POLY);
        int32_t x = 12 + (int32_t)i * 21 + (int32_t)GROUP[i] * 5;
        if (on)
            cv_rect(x, 22, 19, 18, C_WHITE);
        else
            cv_frame(x, 22, 19, 18, ui.layer == LY_OPS ? TE_G3 : C_LINE);
        te_text_c(x + 10, 23, KEYS[i], on ? C_BLACK : TE_G4);
    }
    cv_oy = 0;
    cv_blit(0, y);
}

/* the header: "fm6 op1" / "fm6 pit" / "fm6 glo" */
static void fm6k_draw_header(void)
{
    char title[12];
    str_cpy(title, "fm6 ", sizeof title);
    if (fm6ui.target < 6u) {
        str_cpy(title + 4, "op", 4);
        title[6] = (char)('1' + fm6ui.target);
        title[7] = 0;
    } else {
        str_cpy(title + 4, fm6ui.target == FMT_PIT ? "pit" : "glo", 8);
    }
    te_header(title, COL_ENG_FM6, &fm6ui.sig[0]);
}

/* VIEW PAGE: the algorithm, the band, the four values of the page */
static void fm6k_page_draw(const int16_t *ed)
{
    static char v[4][10];
    const char *lab[4], *val[4] = {v[0], v[1], v[2], v[3]};
    int32_t ratio[4];
    uint32_t k, sig = 0;
    const fm6k_page_t *p = fm6k_cur_page(0);
    fm6k_draw_graph(ed, FMK_GY, FMK_GH);
    fm6k_draw_info(ed, FMK_GY + FMK_GH, 0);
    for (k = 0; k < 4u; k++) {
        const fm6k_pd_t *d = &p->p[k];
        int32_t x = fm6k_value(d);
        lab[k] = d->lab;
        fm6k_format(d, x, v[k]);
        ratio[k] = d->kind == FK_NONE ? -1 : d->max ? x * 1000 / d->max : 0;
        sig = sig * 31u + (uint32_t)x;
    }
    te_dials(FMK_GY + FMK_GH + 42, lab, val, ratio, sig + fm6ui.target * 7u, &fm6ui.dial, COL_ENG_FM6, 0xFu);
}

static void fm6k_screen_draw(void)
{
    const int16_t *ed;
    if (!fm6k_sel()) {                                   /* the track or its engine changed: its ENV pages */
        ui.page = (uint8_t)page_first(FAM_ENV);          /* (ui_draw clears the screen: fm6ui.shown) */
        page_entered();
        return;
    }
    ed = fm6k_ed();
    if (!fm6ui.shown) {                                  /* in: the screen anew */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        fm6ui.shown = 1;
    }
    fm6k_draw_header();
    fm6k_page_draw(ed);
}
#else
static int fm6k_sel(void) { return 0; }
static int on_fm6k_page(void) { return 0; }
static void fm6k_knob(uint32_t k, int32_t s) { (void)k; (void)s; }
static void fm6k_layer_key(uint32_t k) { (void)k; }
static int fm6k_layer_oct(void) { return 0; }
static void fm6k_follow_layer(void) {}
static void fm6k_tap(void) {}
static uint32_t fm6k_keys_lit(void) { return 0; }
static void fm6k_screen_draw(void) {}
static struct { uint8_t shown; } fm6ui;
#endif
