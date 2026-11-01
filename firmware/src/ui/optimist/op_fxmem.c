/* SPDX-License-Identifier: GPL-3.0-only */
/* PROJECT > FX MEM (2026-11-01, the user: "on-device settings for memory for the big fishes: delay, verb etc."; ruled per
 * FX, per project; fx/fx_memsz.h): the cards DLY MEM (FULL, 1/2, 1/4, 1/8), REV MEM (FULL, SMALL), XDLY MEM (FULL, HALF)
 * and FREE (the arena's room, a read-out); a change applies at once (fx_arena.c fxz_poll: the line given back, its effect
 * silent a few blocks, placed again at the new size). A size this build does not offer shows its one value (FULL).
 * The panel under the cards: the arena as a bar (the effects' lines bright, the rest of what is used grey, the room
 * outlined), its KB, then every big line with its length and its KB: placed white, not placed (not used now) dim,
 * refused amber (MEM FULL); the fixed ones (CHO, ECHO, JUNO, FLNG: no size to choose) grey.
 * Also here, the unified "make room" question's plan (op_input.c mem_tick, op_preset.c's FX list): fxz_plan, the least
 * change of these sizes that frees what a refused change needs. */
static void cv_tall(uint32_t y, uint32_t h, uint16_t bg, void (*fn)(void));   /* op_draw.c */
static uint32_t hs(uint32_t h, const char *s);
static uint32_t hu(uint32_t h, uint32_t v);
#if FXZ_ANY
static const page_t FXZ_PAGE = {"FX MEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_FXZ_DLY, G_FXZ_REV, G_FXZ_XDL, 0xFF}};
static uint32_t fxz_kb(uint32_t b) { return (b + 1023u) >> 10; }
/* send line c's block at size step w, bytes (as the arena rounds it) */
static uint32_t fxz_bytes_at(uint32_t c, uint32_t w)
{
    if (c == FXM_DLY)
        return arn_round((DLY_LEN >> w) * 2u);
#if FXZ_REV_N > 1u
    if (c == FXM_REV)
        return arn_round((w ? (REV_LINE_OWN > 8192u ? REV_LINE_OWN : 8192u) : REV_LINE_N) * 2u + FXM_REV_AP);
#endif
    return arn_round(fxm_bytes(c));
}
static uint32_t fxz_xdly_n(void)                        /* the XDLY instances holding a line now */
{
    uint32_t i, n = 0;
#if FXZ_XDL_N > 1u
    for (i = 0; i < FXI_TN[4]; i++)
        n += fxi_own[4][i] != FXI_NONE;
#endif
    (void)i;
    return n;
}
/* the least change of the project's FX MEMORY that frees need bytes of the arena now (only what is placed frees
 * anything): REV SMALL, then XDLY HALF, then the delay a step at a time; the sizes in w[3] (DLY REV XDLY), the bytes it
 * frees in *got; 1 when it frees need */
static int fxz_plan(uint32_t need, uint8_t *w, uint32_t *got)
{
    uint32_t g = 0;
    w[0] = (uint8_t)fxz_want(G_FXZ_DLY, FXZ_DLY_N), w[1] = (uint8_t)fxz_want(G_FXZ_REV, FXZ_REV_N);
    w[2] = (uint8_t)fxz_want(G_FXZ_XDL, FXZ_XDL_N);
#if FXZ_REV_N > 1u
    if (g < need && !w[1] && fxm_st[FXM_REV] == FXM_ON)
        g += fxz_bytes_at(FXM_REV, 0) - fxz_bytes_at(FXM_REV, 1), w[1] = 1;
#endif
#if FXZ_XDL_N > 1u
    if (g < need && !w[2] && fxz_xdly_n())
        g += fxz_xdly_n() * (FXA_Z4 >> 1) * FXA_U * 2u, w[2] = 1;
#endif
    while (g < need && w[0] + 1u < FXZ_DLY_N && fxm_st[FXM_DLY] == FXM_ON) {
        g += fxz_bytes_at(FXM_DLY, w[0]) - fxz_bytes_at(FXM_DLY, w[0] + 1u);
        w[0]++;
    }
    *got = g;
    return g >= need && g > 0u;
}
static void fxz_plan_text(const uint8_t *w, char *b, uint32_t n)   /* "REV SMALL DLY 1/4": what the plan changes */
{
    b[0] = 0;
    if (w[1] != fxz_want(G_FXZ_REV, FXZ_REV_N))
        str_app(b, "REV SMALL ", n);
    if (w[2] != fxz_want(G_FXZ_XDL, FXZ_XDL_N))
        str_app(b, "XDLY HALF ", n);
    if (w[0] != fxz_want(G_FXZ_DLY, FXZ_DLY_N)) {
        str_app(b, "DLY ", n);
        str_app(b, N_FXZD[w[0] % FXZ_DLY_N], n);
    }
    if (b[0] && b[str_len(b) - 1u] == ' ')
        b[str_len(b) - 1u] = 0;
}
static void fxz_apply(const uint8_t *w)                 /* the plan taken: the project's sizes (fxz_poll does the rest) */
{
    song.g[G_FXZ_DLY] = w[0], song.g[G_FXZ_REV] = w[1], song.g[G_FXZ_XDL] = w[2];
    ui.force = 1;
}

/* ---- the row */
static void fxz_cell(uint32_t k, cell_t *c)
{
    if (k < 3u) {
        page_cell(&FXZ_PAGE, k, c);
        return;
    }
    cell_clear(c);
    c->label = "FREE";                                  /* the arena's room, KB (the arena's own count, every gap) */
    c->kind = CK_RO;
    fmt_int(c->val, (int32_t)(arn_room() >> 10));
    c->unit = "KB";
    cell_gauge(c, 0, 0, (int32_t)(ARN_BYTES >> 10), (int32_t)(arn_room() >> 10));
}
static void fxz_turn(uint32_t k, int32_t s, int fine)
{
    if (k < 3u)
        page_turn(&FXZ_PAGE, k, s, fine);
}

/* ---- the panel */
#define FXZ_NL 7u
static struct {
    uint32_t used, fx, size;                            /* the arena: used, the effects' lines' share, its size (bytes) */
    char t[FXZ_NL][18];
    uint16_t col[FXZ_NL];
} fzp;
static void fxz_secs(char *b, uint32_t samples)         /* " 0.74S" */
{
    uint32_t cs = (samples * 100u + FS / 2u) / FS;
    str_app(b, cs < 100u ? " 0" : " ", 18);
    fmt_fix(b + str_len(b), (int32_t)cs, 2);
    str_app(b, "s", 18);
}
static void fxz_kb_app(char *b, uint32_t bytes)
{
    str_app(b, " ", 18);
    fmt_app(b, (int32_t)fxz_kb(bytes));
    str_app(b, "K", 18);
}
static uint16_t fxz_st_col(uint32_t c)                  /* a send line's state: placed, refused, not used now */
{
    return fxm_st[c] == FXM_ON ? C_WHITE : (fxm_full >> c & 1u) ? C_WARN : C_DIM;
}
static void fxz_line_text(uint32_t i)                   /* panel line i */
{
    char *b = fzp.t[i];
    uint32_t w;
    b[0] = 0;
    fzp.col[i] = C_GRAY;
    switch (i) {
    case 0:
        w = fxz_want(G_FXZ_DLY, FXZ_DLY_N);
        str_cpy(b, "DLY", 18);
        fxz_secs(b, DLY_LEN >> w);
        fxz_kb_app(b, fxz_bytes_at(FXM_DLY, w));
        fzp.col[i] = FELUCCA_FX_DELAY ? fxz_st_col(FXM_DLY) : C_DIM;
        break;
    case 1:
        str_cpy(b, fxz_want(G_FXZ_REV, FXZ_REV_N) ? "REV SMALL" : "REV FULL", 18);
        fxz_kb_app(b, fxz_bytes_at(FXM_REV, fxz_want(G_FXZ_REV, FXZ_REV_N)));
        fzp.col[i] = FELUCCA_FX_REVERB ? fxz_st_col(FXM_REV) : C_DIM;
        break;
    case 2:
        w = fxz_want(G_FXZ_XDL, FXZ_XDL_N);
        str_cpy(b, "XDLY", 18);
        fxz_secs(b, (uint32_t)FELUCCA_XDLY_LEN >> w);
        fxz_kb_app(b, (FXA_Z4 >> w) * FXA_U * 2u);
        if (fxz_xdly_n()) {                             /* (the instances holding a line: each that much) */
            str_app(b, "x", 18);
            fmt_app(b, (int32_t)fxz_xdly_n());
        }
        fzp.col[i] = FELUCCA_INS_XDLY ? (fxz_xdly_n() ? C_WHITE : C_DIM) : C_DIM;
        break;
    case 3:
        str_cpy(b, "CHO", 18);                          /* (fixed: DEPTH reaches 1,163 samples back) */
        fxz_kb_app(b, fxm_bytes(FXM_CHO));
        break;
    case 4:
        str_cpy(b, "ECHO", 18);                         /* (fixed: its 75 ms repeat; each instance) */
        fxz_kb_app(b, FXA_Z1 * FXA_U * 2u);
        break;
    case 5:
        str_cpy(b, "JUNO", 18);
        fxz_kb_app(b, FXA_Z2 * FXA_U * 2u);
        break;
    default:
        str_cpy(b, "FLNG", 18);
        fxz_kb_app(b, FXA_Z3 * FXA_U * 2u);
        break;
    }
}
static void fxz_paint(void)                             /* (cv_tall: the panel's own coordinates) */
{
    char b[32];
    uint32_t i, s = fzp.size ? fzp.size : 1u;
    int32_t wu = (int32_t)(238u * (fzp.used > s ? s : fzp.used) / s), wf = (int32_t)(238u * (fzp.fx > s ? s : fzp.fx) / s);
    cv_frame(0, 0, 240, 12, C_LINE);                    /* the arena: used (the lines bright from the left), the room outlined */
    cv_rect(1, 1, wu, 10, C_GRAY);
    cv_rect(1, 1, wf < wu ? wf : wu, 10, C_HI);
    str_cpy(b, "ARENA ", sizeof b);
    fmt_app(b, (int32_t)fxz_kb(fzp.used));
    str_app(b, "/", sizeof b);
    fmt_app(b, (int32_t)(fzp.size >> 10));
    str_app(b, "K  FX ", sizeof b);
    fmt_app(b, (int32_t)fxz_kb(fzp.fx));
    str_app(b, "K", sizeof b);
    cv_texts(0, 18, op_case(b, b, sizeof b), C_GRAY);
    for (i = 0; i < FXZ_NL; i++)                        /* two columns: the sizes chosen left, the fixed right */
        cv_texts(i < 3u ? 0 : 144, (int32_t)(34u + (i < 3u ? i : i - 3u) * 16u), op_case(b, fzp.t[i], sizeof b), fzp.col[i]);
}
static void fxz_draw(uint32_t y, uint32_t h)            /* op_prjflow.c prj_draw: lazy, redrawn when what it shows changes */
{
    uint32_t i, k, sig = 0xF2u;
    fzp.size = ARN_BYTES;
    fzp.used = ARN_BYTES > arn_room() ? ARN_BYTES - arn_room() : 0u;
    fzp.fx = 0;
    for (k = NPART; k < ARN_PARTS; k++)                 /* (the effects' lines and copies: every client after the parts but */
        if (arn_ptr(k) && (k < NPART + FXM_NC || k >= FXM_CP_K0))   /* the other buffers) */
            fzp.fx += arn_cur[k].len;
    for (i = 0; i < FXZ_NL; i++) {
        fxz_line_text(i);
        sig = hu(hs(sig, fzp.t[i]), fzp.col[i]);
    }
    sig = hu(hu(hu(hu(sig, fzp.used), fzp.fx), settings.palette), h) | 1u;
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(y, h, C_BLACK, fxz_paint);
}
#endif

/* ---- the send's TIME halved to fit a short DLY MEM (fx.c dly_run): said once a change of TIME, tempo or size */
static void fxz_tick(void)
{
#if FXZ_DLY_N > 1u
    static uint32_t said;
    uint32_t key;
    char b[24];
    if (!fxsz_dly_cut || op_armed())
        return;
    fxsz_dly_cut = 0;
    key = (uint32_t)song.g[G_DTIME] | (uint32_t)song.g[G_BPM] << 8 | (uint32_t)fxsz_dly << 20 | 1u << 24;
    if (key == said)
        return;
    said = key;
    str_cpy(b, "DLY LINE", sizeof b);
    fxz_secs(b, DLY_CUR);
    str_app(b, ": TIME /2", sizeof b);
    ui_message(b);
#endif
}
