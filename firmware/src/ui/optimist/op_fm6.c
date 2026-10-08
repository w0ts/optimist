/* SPDX-License-Identifier: GPL-3.0-only */
/* FM6's operator editor in this UI (docs/UI-OPTIMIST-DESIGN.md sections 2.1, 4.3, 4.9; section 11.4), the function of
 * ui/sloop/ui_fm6.c in this UI's grammar and look (its page tables and the algorithm's drawing maths copied: nothing
 * in ui/sloop is called). On an FM6 track (the FM6 engine is Kerem Kilic's, Melodee: eng_fm6.c):
 *   ENV held     the layer (seq.c LY_OPS): a black key picks what to edit, by the FM-1's printed labels: OP1 .. OP6,
 *                PIT (the pitch envelope), GLO (algorithm, LFO, portamento, store), MONO / POLY the voice mode; the
 *                white keys keep playing (audition). The cards are the page's four values, the panel the algorithm
 *                (the operator picked white, carriers in the engine's colour, an operator switched off dim) over the
 *                black keys' map; OCT- / OCT+ the page back / on. Let go after using it: SOUND's FM6 rows on that
 *                page. HOME + ENV locks it (section 2.1)
 *   ENV tapped   SOUND's FM6 family: OPERATOR (OP, ALG, FDBK, VOICE), the six pages of the operator picked (FREQ,
 *                LEVEL, RATES, LEVELS, KEY SCALE, CURVES), PIT's two, then ALGO, LFO, LFO 2, PORTA, STORE (SLOT, and
 *                STORE SEND INIT as actions); the algorithm drawn over the rows on every one of them. ENV again: the
 *                next row, round. On another engine ENV stays the envelopes' family
 * Edits go into the part's voice (eng_fm6.c fm6_ed), as SLOOP's editor and a DX7 parameter change do. */
#ifndef FELUCCA_FM6_KEYS
#define FELUCCA_FM6_KEYS 1                              /* (registry.py FM6_KEYS: the operator editor) */
#endif
#define OP_FM6 (FELUCCA_ENG_FM6 && FELUCCA_FM6_KEYS)
#define SND_FM6 0xFEu                                   /* op_screens.c snd_fam: SOUND's FM6 rows */
#if OP_FM6
static void fm6_store(uint32_t k);                       /* engines/fm6/fm6_store.c (after the UI) */
static void fm6_send(void);
static void fm6_init_voice(void);
static void note_name(char *b, uint32_t n);             /* op_step.c */
static void val_turn(const param_desc_t *d, int16_t *vp, uint32_t k, int32_t s, int fine);   /* op_screens.c */
enum { FMT_PIT = 6, FMT_GLO = 7, FMT_MONO = 20, FMT_POLY = 22 };
enum { FK_NUM, FK_ENUM, FK_BREAK, FK_COARSE, FK_ON, FK_SLOT, FK_GO, FK_NONE };
typedef struct {
    const char *lab;
    uint8_t off;                                        /* in the operator (operator pages), else in the voice */
    uint8_t max;
    int8_t show;                                        /* value shown = value + show */
    uint8_t kind;
    const char *const *names;                           /* FK_ENUM */
} fm6k_pd_t;
typedef struct {
    const char *name;                                   /* the row's name (this UI's, 9 characters at most) */
    fm6k_pd_t p[4];
} fm6k_page_t;
/* ui/sloop/ui_fm6.c's tables, copied; the labels in capitals (this UI cases them as it draws) */
static const char *const FMKN_MODE[] = {"RATIO", "FIXED"};
static const char *const FMKN_ONOFF[] = {"OFF", "ON"};
static const char *const FMKN_WAVE[] = {"TRI", "SAW-", "SAW+", "SQR", "SIN", "S&H"};
static const char *const FMKN_CURVE[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char *const FMKN_FIXED[] = {"1HZ", "10HZ", "100HZ", "1KHZ"};
static const char *const FMKN_PMODE[] = {"PEDAL", "ON"};
#define FMK_NOP {"", 0, 0, 0, FK_NONE, 0}
static const fm6k_page_t FMK_PAGES[] = {
    {"FREQ", {{"MODE", FO_MODE, 1, 0, FK_ENUM, FMKN_MODE}, {"COARS", FO_CRS, 31, 0, FK_COARSE, 0},
              {"FINE", FO_FINE, 99, 0, FK_NUM, 0}, {"DTUNE", FO_DET, 14, -7, FK_NUM, 0}}},
    {"LEVEL", {{"LEVEL", FO_OL, 99, 0, FK_NUM, 0}, {"VEL", FO_KVS, 7, 0, FK_NUM, 0},
               {"AMS", FO_AMS, 3, 0, FK_NUM, 0}, {"ON", 0, 1, 0, FK_ON, FMKN_ONOFF}}},
    {"RATES", {{"R1", FO_R1, 99, 0, FK_NUM, 0}, {"R2", FO_R2, 99, 0, FK_NUM, 0},
               {"R3", FO_R3, 99, 0, FK_NUM, 0}, {"R4", FO_R4, 99, 0, FK_NUM, 0}}},
    {"LEVELS", {{"L1", FO_L1, 99, 0, FK_NUM, 0}, {"L2", FO_L2, 99, 0, FK_NUM, 0},
                {"L3", FO_L3, 99, 0, FK_NUM, 0}, {"L4", FO_L4, 99, 0, FK_NUM, 0}}},
    {"KEY SCALE", {{"BREAK", FO_BP, 99, 0, FK_BREAK, 0}, {"L DEP", FO_LD, 99, 0, FK_NUM, 0},
                   {"R DEP", FO_RD, 99, 0, FK_NUM, 0}, {"RT SC", FO_RS, 7, 0, FK_NUM, 0}}},
    {"CURVES", {{"L CRV", FO_LC, 3, 0, FK_ENUM, FMKN_CURVE}, {"R CRV", FO_RC, 3, 0, FK_ENUM, FMKN_CURVE},
                FMK_NOP, FMK_NOP}},
    {"PIT RATES", {{"R1", FV_PR, 99, 0, FK_NUM, 0}, {"R2", FV_PR + 1, 99, 0, FK_NUM, 0},
                   {"R3", FV_PR + 2, 99, 0, FK_NUM, 0}, {"R4", FV_PR + 3, 99, 0, FK_NUM, 0}}},
    {"PIT LVLS", {{"L1", FV_PL, 99, -50, FK_NUM, 0}, {"L2", FV_PL + 1, 99, -50, FK_NUM, 0},
                  {"L3", FV_PL + 2, 99, -50, FK_NUM, 0}, {"L4", FV_PL + 3, 99, -50, FK_NUM, 0}}},
    {"ALGO", {{"ALG", FV_ALG, 31, 1, FK_NUM, 0}, {"FDBK", FV_FB, 7, 0, FK_NUM, 0},
              {"OSYNC", FV_OKS, 1, 0, FK_ENUM, FMKN_ONOFF}, {"TRNSP", FV_TRNSP, 48, -24, FK_NUM, 0}}},
    {"LFO", {{"SPEED", FV_LFS, 99, 0, FK_NUM, 0}, {"DELAY", FV_LFD, 99, 0, FK_NUM, 0},
             {"PMD", FV_LPMD, 99, 0, FK_NUM, 0}, {"AMD", FV_LAMD, 99, 0, FK_NUM, 0}}},
    {"LFO 2", {{"WAVE", FV_LFW, 5, 0, FK_ENUM, FMKN_WAVE}, {"KSYNC", FV_LFKS, 1, 0, FK_ENUM, FMKN_ONOFF},
               {"PMS", FV_LPMS, 7, 0, FK_NUM, 0}, FMK_NOP}},
    {"PORTA", {{"PORTA", FN_PMODE, 1, 0, FK_ENUM, FMKN_PMODE}, {"TIME", FN_PTIME, 127, 0, FK_NUM, 0},
               {"GLISS", FN_GLISS, 1, 0, FK_ENUM, FMKN_ONOFF}, {"DXVEL", FN_VNORM, 1, 0, FK_ENUM, FMKN_ONOFF}}},
    {"STORE", {{"SLOT", 0, FM6_NUSER - 1u, 1, FK_SLOT, 0}, {"STORE", 0, 1, 0, FK_GO, 0},
               {"SEND", 1, 1, 0, FK_GO, 0}, {"INIT", 2, 1, 0, FK_GO, 0}}},
};
#define F6_NPG (sizeof FMK_PAGES / sizeof FMK_PAGES[0])
enum { F6_OP0 = 1, F6_PIT0 = 7, F6_GLO0 = 9, F6_STORE = 13, F6_ROWS = 1 + (uint32_t)F6_NPG };   /* the rows */
_Static_assert(F6_ROWS == 14u && F6_STORE == F6_ROWS - 1u, "FM6: OPERATOR, 6 operator pages, PIT 2, GLO 5");

static struct {
    uint8_t target;                                     /* 0..5 = OP1..OP6 (the operator pages edit it) */
    uint8_t row;                                        /* the layer's page: a row of the family (1..13) */
    uint8_t sub[3];                                     /* each kind's page last shown: operator, PIT, GLO */
    uint8_t slot;                                       /* STORE: the user slot */
    uint8_t picked;                                     /* the layer was used: let go, SOUND's FM6 rows */
} f6 = {.row = F6_OP0, .sub = {F6_OP0, F6_PIT0, F6_GLO0}};

static int fm6_sel(void) { return !is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], FM6); }
static int16_t *f6_ed(void) { return fm6_ed[song.sel % NPART]; }
static uint32_t f6_kind(uint32_t r) { return r < F6_PIT0 ? 0u : r < F6_GLO0 ? 1u : 2u; }
static const fm6k_page_t *f6_page(uint32_t r) { return r ? &FMK_PAGES[(r - 1u) % F6_NPG] : 0; }
static uint32_t f6_off(uint32_t r, const fm6k_pd_t *d)  /* where value d of row r lives in the voice */
{
    if (d->kind == FK_ON)
        return FV_ON + f6.target;                       /* (FV_ON + n - 1: OP n's switch) */
    return f6_kind(r) == 0u ? FM6_OPB(f6.target + 1u) + d->off : d->off;
}
static int32_t f6_value(uint32_t r, const fm6k_pd_t *d)
{
    if (d->kind == FK_NONE || d->kind == FK_GO)
        return 0;
    return d->kind == FK_SLOT ? f6.slot : f6_ed()[f6_off(r, d)];
}
static void f6_row_set(uint32_t r)                      /* row r is the page now (the layer's, the family's) */
{
    r = r % F6_ROWS;
    if (!r)
        return;
    f6.row = (uint8_t)r;
    f6.sub[f6_kind(r)] = (uint8_t)r;
    if (r == F6_STORE)                                  /* the voice's own slot, or a free one (Melodee) */
        f6.slot = (uint8_t)fm6_store_slot(song.sel % NPART, f6.slot);
}

/* ---- the rows of SOUND's FM6 family (op_screens.c snd_*) */
static uint32_t fm6_rows(void) { return F6_ROWS; }
static void fm6_row_name(uint32_t r, char *b)
{
    if (r && f6_kind(r) == 0u) {                        /* an operator's page: "OP3 RATES" */
        str_cpy(b, "OP1 ", 12);
        b[2] = (char)('1' + f6.target);
        str_cpy(b + 4, f6_page(r)->name, 8);
        return;
    }
    str_cpy(b, r ? f6_page(r)->name : "OPERATOR", 12);
}
static void f6_format(uint32_t r, const fm6k_pd_t *d, int32_t v, char *b)
{
    b[0] = 0;
    switch (d->kind) {
    case FK_ENUM:
    case FK_ON:
        str_cpy(b, d->names[clamp(v, 0, d->max)], 8);
        break;
    case FK_BREAK:                                      /* 0 = A-1 .. 99 = C8 (Yamaha) */
        note_name(b, (uint32_t)(v + 21));
        break;
    case FK_COARSE:                                     /* ratio: 0 is a half; fixed: 1 Hz .. 1 kHz */
        if (f6_ed()[f6_off(r, d) - FO_CRS + FO_MODE])
            str_cpy(b, FMKN_FIXED[v & 3], 8);
        else if (v == 0)
            str_cpy(b, "0.5", 4);
        else
            fmt_int(b, v);
        break;
    case FK_SLOT:                                       /* U01 .. U32 */
        b[0] = 'U';
        b[1] = (char)('0' + (v + 1) / 10);
        b[2] = (char)('0' + (v + 1) % 10);
        b[3] = 0;
        break;
    default:
        fmt_int(b, v + d->show);
        break;
    }
}
static void fm6_cell(uint32_t r, uint32_t k, cell_t *c)
{
    const fm6k_pd_t *d;
    int32_t v;
    cell_clear(c);
    if (!fm6_sel())
        return;
    if (!r) {                                           /* OPERATOR: OP, ALG, FDBK, VOICE */
        static const char *const L[3] = {"OP", "ALG", "FDBK"};
        if (k == 3u) {
            cell_param(c, &TP[P_VOICE], &TSEL->p[P_VOICE]);
            return;
        }
        c->label = L[k];
        c->kind = CK_VAL;
        v = k == 0u ? (int32_t)f6.target + 1 : k == 1u ? f6_ed()[FV_ALG] + 1 : f6_ed()[FV_FB];
        fmt_int(c->val, v);
        cell_gauge(c, k == 0u, k == 2u ? 0 : 1, k == 0u ? 6 : k == 1u ? 32 : 7, v);
        return;
    }
    d = &f6_page(r)->p[k & 3u];
    if (d->kind == FK_NONE)
        return;
    c->label = d->lab;
    if (d->kind == FK_GO) {
        c->kind = CK_ACT;
        return;
    }
    c->kind = CK_VAL;
    v = f6_value(r, d);
    f6_format(r, d, v, c->val);
    cell_gauge(c, d->kind != FK_NUM && d->kind != FK_COARSE && d->kind != FK_BREAK, d->show, d->max + d->show,
               v + d->show);
}
static void fm6_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    const fm6k_pd_t *d;
    if (!fm6_sel() || s == OP_RESET)
        return;                                         /* (a DX7 value has no default to go back to) */
    f6_row_set(r);
    if (!r) {
        if (k == 0u)
            f6.target = (uint8_t)clamp((int32_t)f6.target + (s > 0 ? 1 : -1), 0, 5);
        else if (k == 3u)
            val_turn(&TP[P_VOICE], &TSEL->p[P_VOICE], k, s, fine);
        else
            fm6_set(f6_ed(), k == 1u ? FV_ALG : FV_FB, f6_ed()[k == 1u ? FV_ALG : FV_FB] + (s > 0 ? 1 : -1));
        return;
    }
    d = &f6_page(r)->p[k & 3u];
    if (d->kind == FK_NONE || d->kind == FK_GO)
        return;
    if (d->kind == FK_SLOT) {
        f6.slot = (uint8_t)clamp((int32_t)f6.slot + (s > 0 ? 1 : -1), 0, d->max);
        return;
    }
    fm6_set(f6_ed(), f6_off(r, d), clamp(f6_value(r, d) + (fine ? s : accel(EN_K1 + k, s, d->kind == FK_ENUM ? 0 : d->max)),
                                         0, d->max));
}
/* YES: a toggle (two-value lists), or STORE / SEND / INIT (STORE and INIT asked: they replace a voice) */
static int fm6_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    static const char *const GO[3] = {"STORE", "SEND", "INIT"};
    const fm6k_pd_t *d;
    char b[8];
    if (!fm6_sel() || !r)
        return 0;
    d = &f6_page(r)->p[k & 3u];
    if (d->kind == FK_ENUM && d->max == 1u) {
        fm6_set(f6_ed(), f6_off(r, d), !f6_value(r, d));
        return 1;
    }
    if (d->kind != FK_GO)
        return 0;
    if (!ok && d->off != 1u) {
        f6_format(r, &FMK_PAGES[F6_STORE - 1u].p[0], f6.slot, b);
        op_arm(SCR_SOUND, r, k, GO[d->off], d->off ? trk_tag(song.sel) : b, 1);   /* "Store U05?", "Init T2?" */
        return 1;
    }
    ui.toast_next = ok;
    if (d->off == 0u)
        fm6_store(f6.slot);
    else if (d->off == 1u)
        fm6_send();
    else
        fm6_init_voice();
    ui.toast_next = 0;
    return 1;
}

/* ---- the layer (ENV held on an FM6 track: op_layers.c, LY_OPS) */
static void fm6_lay_key(uint32_t k)                     /* a black key: what to edit, or the voice mode */
{
    static const int8_t BK[27] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, FMT_PIT, -1, FMT_GLO,
                                  -1, -1, FMT_MONO, -1, FMT_POLY, -1, -1, -1, -1};
    int32_t t = k < 27u ? BK[k] : -1;
    if (t < 0 || !fm6_sel())
        return;
    if (t == FMT_MONO || t == FMT_POLY) {
        TSEL->p[P_VOICE] = t == FMT_MONO ? V_MONO : V_POLY;
        ui_message(t == FMT_MONO ? "MONO" : "POLY");
        return;
    }
    if (t < 6)
        f6.target = (uint8_t)t;
    f6_row_set(f6.sub[t < 6 ? 0u : t == FMT_PIT ? 1u : 2u]);
    f6.picked = 1;
}
static void fm6_lay_page(int32_t d)                     /* OCT- / OCT+ with ENV held: the page back / on, its kind's */
{
    static const uint8_t A[3] = {F6_OP0, F6_PIT0, F6_GLO0}, N[3] = {6, 2, 5};
    uint32_t kd = f6_kind(f6.row), i = f6.row - A[kd];
    f6_row_set(A[kd] + (i + (d > 0 ? 1u : N[kd] - 1u)) % N[kd]);
    f6.picked = 1;
}
static void fm6_lay_cell(uint32_t k, cell_t *c) { fm6_cell(f6.row, k, c); }   /* the layer's cards: its page */
static void fm6_lay_turn(uint32_t k, int32_t s, int fine)   /* a knob in the layer: the page's value k */
{
    fm6_turn(f6.row, k, s, fine);
    f6.picked = 1;
}
static uint32_t fm6_keys_lit(void)                      /* the keys' lights with ENV held: what is edited, the mode */
{
    static const uint8_t KEY[8] = {1, 3, 5, 8, 10, 13, 15, 17};
    uint32_t kd = f6_kind(f6.row), m = 1u << KEY[kd == 0u ? f6.target % 6u : kd == 1u ? 6u : 7u];
    uint32_t v = (uint32_t)TSEL->p[P_VOICE];
    m |= v == V_POLY ? 1u << FMT_POLY : v == V_MONO || v == V_LEGATO ? 1u << FMT_MONO : 0u;
    return m;
}
#else
static int fm6_sel(void) { return 0; }
static uint32_t fm6_rows(void) { return 0; }
static void fm6_row_name(uint32_t r, char *b) { (void)r; b[0] = 0; }
static void fm6_cell(uint32_t r, uint32_t k, cell_t *c) { (void)r; (void)k; cell_clear(c); }
static void fm6_turn(uint32_t r, uint32_t k, int32_t s, int fine) { (void)r; (void)k; (void)s; (void)fine; }
static int fm6_yes(uint32_t r, uint32_t k, uint32_t ok) { (void)r; (void)k; (void)ok; return 0; }
static void fm6_lay_key(uint32_t k) { (void)k; }
static void fm6_lay_page(int32_t d) { (void)d; }
static void fm6_lay_turn(uint32_t k, int32_t s, int fine) { (void)k; (void)s; (void)fine; }
static void fm6_lay_cell(uint32_t k, cell_t *c) { (void)k; cell_clear(c); }
static uint32_t fm6_keys_lit(void) { return 0; }
#endif
