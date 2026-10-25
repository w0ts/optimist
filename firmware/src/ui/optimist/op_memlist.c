/* SPDX-License-Identifier: GPL-3.0-only */
/* The load's memory list (docs/SD-MODE-DESIGN.md 6b; the user, 2026-10-25: "the list page as designed"): a project's
 * load that does not fit the arena (an arena placed by need, FELUCCA_ARENA_BYTES) makes every part it could not place a
 * silent placeholder with the project's sound (engines.c part_mem_refused.lmask), and once every part's switch is done
 * and a part that finds room then is on its engine (model.c mem_load_settle), this page lists:
 *   the cards' band  the room the drops leave against what the refused parts not left silent want (KB), a gauge; FITS
 *                    (green) when the arena's own trial says each of them is placed (model.c mem_load_fits: what SAVE
 *                    then does)
 *   the panel        every row on a white key (its number at the left): the refused parts (each with its engine and
 *                    the KB it wants, the amount amber), a rule, then what can be dropped: the parts that hold memory
 *                    (their engine, KB), then the effects' lines placed (REV DLY CHO, FELUCCA_FX_ARENA); the parts first
 *   white key n      a toggle: a refused part left silent (dim, NONE; the user's ruling of the night of 2026-10-25: so
 *                    that SAVE can place the others when not all fit), what can be dropped dropped (red, DROP)
 *   SAVE             with FITS: the drops done (a part a placeholder, an effect bypassed until there is room again),
 *                    the refused parts not left silent placed once that memory is free; else NO ROOM
 *   HOME             the refused parts stay placeholders (an engine picked ends one); the rest of the project as loaded
 *   PLAY             as anywhere (the page stays)
 * The keys play nothing (seq.c ly_lock LY_STEP, as NAME's); the knobs do nothing. An edit's change keeps its one
 * confirm (op_input.c mem_tick); it waits while a load's list is up. Only with an arena placed by need (the fixed slots,
 * the default, never refuse) */
#if !ARN_FIXED
#define ML_MAX (NPART + 3u)                            /* the parts (refused or droppable), then REV DLY CHO */
static struct {
    uint8_t on;
    uint8_t seq;                                       /* the load it is for (part_mem_refused.lseq) */
    uint8_t n, it[ML_MAX];                             /* the items: the refused parts, what can be dropped (model.c mem_load_items) */
    uint8_t fits;                                      /* mem_load_fits(sel) */
    uint32_t sel;                                      /* the items to drop: a bit by item (part k; NPART + FXM_*) */
} ml;
static int ml_on(void) { return ml.on; }
static void ml_close(void)
{
    ml.on = 0;
    ui.force = 1;
}
static void ml_count(void)                             /* the items, the selection still among them, the trial */
{
    uint32_t i, keep = 0;
    ml.n = (uint8_t)mem_load_items(ml.it);
    for (i = 0; i < ml.n; i++)
        keep |= 1u << ml.it[i];
    ml.sel &= keep;
    ml.fits = (uint8_t)mem_load_fits(ml.sel);
}
static void ml_open(void)
{
    uint32_t l;
    ml.on = 1;
    ml.seq = part_mem_refused.lseq;
    ml.sel = 0;
    ml_count();
    for (l = LY_FX; l < LY_COUNT; l++)                 /* (no layer takes the keys: they pick) */
        if (l != LY_STEP)
            ly_bit[l] = 0;
    op_disarm();
    ui.toast_t = 0;
    ui.force = 1;
}
/* each frame (op_input.c mem_tick): 1 while a load's refusals are dealt with (the page, or the parts still switching),
 * the edit's confirm waits */
static int ml_tick(void)
{
    if (!part_mem_refused.lmask) {                     /* (nothing refused, or a new load that fits) */
        if (ml.on)
            ml_close();
        return 0;
    }
    if (ml.on && ml.seq == part_mem_refused.lseq) {
        ml_count();                                    /* (an engine changed from the editor meanwhile: counted again) */
        return 1;
    }
    if (ml.on)
        ml_close();                                    /* (another load: settled afresh) */
    if (!name_on() && mem_load_settle() < 0)
        ml_open();
    return 1;
}
static void ml_save(void)
{
    if (!ml.fits) {
        ui_message("NO ROOM");
        return;
    }
    mem_load_commit(ml.sel);                           /* (the refused parts: placed by the settle, once their room is
                                                        * free; the page opens again only if that fails) */
    ml_close();
}
/* one UI frame of the list (op_input.c ui_input): the keys (seq.c lk_q, as STEP's), SAVE, HOME, PLAY */
static void ml_frame(uint32_t pressed)
{
    uint32_t k;
    while (lk_r != lk_w) {
        uint32_t e = lk_q[lk_r % LKQ], kk = e & 0x7Fu;
        int32_t w;
        lk_r++;
        if (!((e >> 7) & 1u) || (e >> 8) != LY_STEP)
            continue;                                  /* (a key let go, or another layer's) */
        w = punch_key(kk);
        if (w >= 0 && (uint32_t)w < ml.n)
            ml.sel ^= 1u << ml.it[w];                  /* white key n: item n picked or not (dropped; a refused part left silent) */
    }
    for (k = 0; k < 4u; k++)
        (void)panel_enc(EN_K1 + k);                    /* (the knobs do nothing here: no stray detents left) */
    (void)panel_enc(EN_PRESET);
    (void)panel_enc(EN_SELECT);
    (void)panel_enc(EN_ALGO);
    ml_count();
    if (pressed & (1u << panel.btn[B_PLAY]))
        op_play();
    if (pressed & (1u << panel.btn[B_HOME])) {
        mem_load_cancel();                             /* (the refused parts: placeholders, NONE) */
        ml_close();
        return;
    }
    if (pressed & (1u << panel.btn[B_SAVE]))
        ml_save();
}
/* the key LEDs (op_keys.c): an item's key dim, a dropped one's full */
static uint32_t ml_leds(uint32_t *d)
{
    uint32_t k, m = 0;
    *d = 0;
    for (k = 0; k < 27u; k++) {
        int32_t w = punch_key(k);
        if (w < 0 || (uint32_t)w >= ml.n)
            continue;
        *d |= 1u << k;
        m |= (uint32_t)(ml.sel >> ml.it[w] & 1u) << k;
    }
    return m;
}

/* ---- drawing: the header's badge MEMORY, the title Load; the cards' band the gauge, the panel the rows, the help
 * line's place SAVE / HOME as the modal's foot (dim SAVE while it does not fit). No text on a fill */
static uint32_t ml_sel_bytes(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ml.n; i++)
        if ((ml.sel & ~(uint32_t)part_mem_refused.lmask) >> ml.it[i] & 1u)
            n += mem_item_bytes(ml.it[i]);              /* (what is dropped; a refused part left silent frees nothing) */
    return n;
}
static uint32_t ml_room(void) { return mem_load_free() + ml_sel_bytes(); }   /* (the room once the drops are done) */
static void ml_kb(char *b, uint32_t bytes)             /* "10 KB" (rounded up: what it takes) */
{
    fmt_int(b, (int32_t)((bytes + 1023u) / 1024u));
    str_app(b, " KB", 12);
}
static void ml_item_name(char *b, uint32_t it, uint16_t *c)   /* "T1 FM6" in its engine's colour; "REV" */
{
#if FXM_NEED
    static const char *const FXN[3] = {"CHO", "DLY", "REV"};
    if (it >= NPART) {
        str_cpy(b, FXN[(it - NPART) % 3u], 12);
        *c = C_HI;
        return;
    }
#endif
    str_cpy(b, trk_tag(it), 12);
    str_app(b, " ", 12);
    str_app(b, ENGINES[trk[it % NPART].engine % NENGINES]->name, 12);
    *c = ENG_COL[trk[it % NPART].engine % NENGINES];
}
static uint32_t ml_sig(void)
{
    uint32_t h = 2166136261u, i;
    const uint32_t st[7] = {ml.n, ml.sel, ml.fits, part_mem_refused.lmask, ml_room(), mem_load_need(ml.sel), settings.palette};
    for (i = 0; i < 7u; i++)
        h = (h ^ st[i]) * 16777619u;
    for (i = 0; i < ml.n; i++)
        h = (h ^ ml.it[i]) * 16777619u;
    for (i = 0; i < NPART; i++)
        h = (h ^ part_mem_refused.leng[i] ^ (uint32_t)trk[i].engine << 8) * 16777619u;
    return h;
}
static void ml_draw_gauge(void)                        /* the room against the need, FITS */
{
    char b[24], t[12];
    uint32_t need = mem_load_need(ml.sel), room = ml_room();
    int32_t w = need ? (int32_t)((room >= need ? need : room) * 230u / need) : 230;   /* (every refused part left
                                                         * silent: nothing needed, a full bar) */
    uint16_t c = ml.fits ? C_OK : C_WARN;
    cv_begin(240, OH_CARD, C_BLACK);
    ml_kb(t, room);
    str_cpy(b, t, sizeof b);
    str_app(b, " / ", sizeof b);
    ml_kb(t, need);
    str_app(b, t, sizeof b);
    cv_texts(4, 2, b, C_HI);
    cv_texts(236 - text_ws(ml.fits ? "FITS" : "SHORT"), 2, ml.fits ? "FITS" : "SHORT", c);
    cv_frame(4, 24, 232, 14, C_LINE);
    cv_rect(5, 25, w, 12, c);
    cv_blit(0, OY_CARD);
}
#define ML_RH 19                                       /* a row */
/* row i (white key i + 1) at y: a refused part (its engine in its colour, the KB it wants amber; picked: left silent,
 * dim with NONE), else what can be dropped (picked: red with DROP) */
static void ml_draw_row(uint32_t i, int32_t y)
{
    char b[16], kb[12], n[4];
    uint32_t it = ml.it[i], on = ml.sel >> it & 1u, ref = it < NPART && (part_mem_refused.lmask >> it & 1u);
    uint16_t c, kc = on ? (ref ? C_HI : C_ERR) : C_GRAY;
    const char *tag = on ? (ref ? "NONE" : "DROP") : "";
    fmt_int(n, (int32_t)i + 1);
    cv_frame(4, y, 16, 16, kc);
    cv_texts(12 - text_ws(n) / 2, y + 1, n, kc);
    if (ref) {
        str_cpy(b, trk_tag(it), sizeof b);
        str_app(b, " ", sizeof b);
        str_app(b, ENGINES[part_mem_refused.leng[it] % NENGINES]->name, sizeof b);
        c = ENG_COL[part_mem_refused.leng[it] % NENGINES];
    } else {
        ml_item_name(b, it, &c);
    }
    cv_texts(24, y, b, on ? (ref ? C_DIM : C_ERR) : c);
    ml_kb(kb, mem_item_bytes(it));
    cv_texts(on ? 190 - text_ws(kb) : 236 - text_ws(kb), y, kb, on ? (ref ? C_DIM : C_ERR) : ref ? C_WARN : C_HI);
    if (on)
        cv_texts(236 - text_ws(tag), y, tag, ref ? C_GRAY : C_ERR);
}
static void ml_draw_rows(void)
{
    uint32_t i;
    int32_t y = 2;
    cv_begin(240, OH_PANEL, C_BLACK);
    for (i = 0; i < ml.n && y + ML_RH <= OH_PANEL; i++, y += ML_RH) {
        uint32_t ref = ml.it[i] < NPART && (part_mem_refused.lmask >> ml.it[i] & 1u);
        if (!ref && i && ml.it[i - 1u] < NPART && (part_mem_refused.lmask >> ml.it[i - 1u] & 1u)) {
            cv_rect(4, y, 232, 1, C_LINE);              /* (the refused parts, a rule, what can be dropped) */
            y += 4;
        }
        ml_draw_row(i, y);
    }
    cv_blit(0, OY_PANEL);
}
#define ML_FOOT_Y (240 - 18)
static void ml_draw_foot(void)                         /* the modal's foot: HOME left, SAVE right (dim: no room yet) */
{
    cv_begin(240, 18, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    cv_texts(24, 2, "Home no", C_GRAY);
    cv_texts(216 - text_ws("Save yes"), 2, "Save yes", ml.fits ? C_WHITE : C_DIM);
    cv_blit(0, ML_FOOT_Y);
}
static void ml_draw(void)                              /* (op_draw.c op_frame_draw) */
{
    uint32_t sig = ml_sig();
    if (sig == ui.sig[1] && sig == ui.sig[2])
        return;
    ui.sig[1] = ui.sig[2] = sig;
    ml_draw_gauge();
    ml_draw_rows();
    ml_draw_foot();
}
#else
#define ml_on() 0
#define ml_tick() 0
#define ml_frame(p) ((void)(p))
#define ml_draw() ((void)0)
#define ml_leds(d) (*(d) = 0, 0u)
#endif
/* a page that owns the keys, the knobs and the buttons (NAME, the load's memory list): no layer, no walk, no help line */
static int page_owned(void) { return name_on() || ml_on(); }
